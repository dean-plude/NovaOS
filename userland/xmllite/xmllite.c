/*
 * xmllite.dll — XmlLite for NovaOS: IXmlReader, the pull parser Windows
 * programs (and XAML's parser, dxaml/xcp/components/legacy/WinReader.cpp)
 * read XML with, over libxml2's xmlTextReader (third_party/libxml2, MIT),
 * which is modelled on the same .NET XmlReader.  IXmlWriter is not here.
 *
 * Each Read copies the node it lands on, with all of an element's
 * attributes (namespace declarations among them, as XmlLite shows them:
 * prefix "xmlns"), into UTF-16 strings that stay valid until the next Read,
 * so a caller may hold an attribute's strings while it moves on to the next
 * attribute, as XmlLite allows.  Differences from XmlLite:
 * - no XmlDeclaration node (libxml2's reader does not report one);
 * - line numbers are the node's own, positions the parser's column;
 * - external entities and DTDs are never loaded (DtdProcessing_Parse reads
 *   an internal subset only).
 */
#include <windows.h>
#include <objbase.h>
#include <string.h>
#include <libxml/xmlreader.h>
#include <libxml/parser.h>

int xmlDllMain(void *inst, unsigned long reason, void *reserved);  /* libxml2's per-thread state */
int _fltused = 0x9875;                  /* floating point in use (libxml2's XPath numbers, linked in by its init) */

typedef enum { NT_None = 0, NT_Element = 1, NT_Attribute = 2, NT_Text = 3, NT_CDATA = 4,
               NT_ProcessingInstruction = 7, NT_Comment = 8, NT_DocumentType = 10, NT_Whitespace = 13,
               NT_EndElement = 15, NT_XmlDeclaration = 17 } XmlNodeType;
enum { P_MultiLanguage, P_ConformanceLevel, P_RandomAccess, P_XmlResolver, P_DtdProcessing, P_ReadState,
       P_MaxElementDepth, P_MaxEntityExpansion };
enum { RS_Initial, RS_Interactive, RS_Error, RS_EndOfFile, RS_Closed };

#define WC_E_SYNTAX          ((HRESULT)0xC00CEE2DL)
#define WC_E_DTDPROHIBITED   ((HRESULT)0xC00CEE4FL)
#define MX_E_INPUTEND        ((HRESULT)0xC00CEE01L)
#define MX_E_ENCODING        ((HRESULT)0xC00CEE02L)
#define XML_E_INVALID_ENC    MX_E_ENCODING

static const GUID IID_IXmlReader = { 0x7279fc81, 0x709d, 0x4095, { 0xb6, 0x3d, 0x69, 0xfe, 0x4b, 0x0d, 0x90, 0x30 } };
/* NovaOS's own: what CreateXmlReaderInputWith* return, recognised by SetInput */
static const GUID IID_NovaXmlInput = { 0x6e6f7661, 0x0020, 0x4358, { 0x8d, 0x4c, 0x78, 0x6d, 0x6c, 0x6c, 0x69, 0x74 } };

typedef struct { const WCHAR *p; UINT n; } Str;
typedef struct { Str prefix, local, ns, value, qname; } Attr;

/* the strings of the current node: blocks freed at the next Read */
typedef struct Block { struct Block *next; size_t used, cap; WCHAR text[]; } Block;

typedef struct Reader {
    const struct ReaderVtbl *lpVtbl;
    LONG refs;
    xmlTextReaderPtr x;
    char *data;                   /* the whole input, for libxml2 */
    int state, dtd, eof;
    HRESULT error;                /* the first error libxml2 reported for this input */
    XmlNodeType type;
    BOOL empty;
    UINT line, col, depth;
    Attr node;                    /* the node's own names and value */
    Attr *attrs;
    UINT nattrs, cattrs;
    int cur;                      /* the attribute the reader is on, -1 = the node */
    UINT chunk;                   /* ReadValueChunk's position in the value */
    Block *blocks;
} Reader;

typedef struct Input {
    const IUnknownVtbl *lpVtbl;
    LONG refs;
    IUnknown *stream;
    char encoding[32];
} Input;

static void *mem(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1); }
static void mem_free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static const WCHAR empty_w[1];

static Str wstr(Reader *r, const xmlChar *s)
{
    Str out = { empty_w, 0 };
    if (!s || !*s) return out;
    int len = (int)strlen((const char *)s);
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char *)s, len, 0, 0);
    if (n <= 0) return out;
    if (!r->blocks || r->blocks->cap - r->blocks->used < (size_t)n + 1) {
        size_t cap = n + 1 > 2048 ? (size_t)n + 1 : 2048;
        Block *b = mem(sizeof *b + cap * sizeof(WCHAR));
        if (!b) return out;
        b->cap = cap;
        b->next = r->blocks;
        r->blocks = b;
    }
    WCHAR *p = r->blocks->text + r->blocks->used;
    MultiByteToWideChar(CP_UTF8, 0, (const char *)s, len, p, n);
    p[n] = 0;
    r->blocks->used += n + 1;
    out.p = p;
    out.n = n;
    return out;
}

static void clear_node(Reader *r)
{
    while (r->blocks) {
        Block *b = r->blocks;
        r->blocks = b->next;
        mem_free(b);
    }
    memset(&r->node, 0, sizeof r->node);
    r->nattrs = 0;
    r->cur = -1;
    r->chunk = 0;
    r->empty = FALSE;
    r->depth = 0;
}

static void close_input(Reader *r)
{
    clear_node(r);
    if (r->x) xmlFreeTextReader(r->x);
    r->x = 0;
    mem_free(r->data);
    r->data = 0;
    r->type = NT_None;
    r->eof = 0;
    r->error = S_OK;
}

static Attr names(Reader *r)
{
    Attr a;
    a.prefix = wstr(r, xmlTextReaderConstPrefix(r->x));
    a.local = wstr(r, xmlTextReaderConstLocalName(r->x));
    a.ns = wstr(r, xmlTextReaderConstNamespaceUri(r->x));
    a.qname = wstr(r, xmlTextReaderConstName(r->x));
    a.value = wstr(r, xmlTextReaderConstValue(r->x));
    return a;
}

/* libxml2's errors, kept rather than printed */
static void on_error(void *arg, const xmlError *e)
{
    Reader *r = arg;
    if (e && e->level >= XML_ERR_ERROR && r->error == S_OK)
        r->error = e->code == XML_ERR_UNSUPPORTED_ENCODING || e->code == XML_ERR_INVALID_ENCODING ? MX_E_ENCODING :
                   e->code == XML_ERR_DOCUMENT_END || e->code == XML_ERR_DOCUMENT_EMPTY ? MX_E_INPUTEND : WC_E_SYNTAX;
}

static XmlNodeType map_type(int t)
{
    switch (t) {
    case XML_READER_TYPE_ELEMENT: return NT_Element;
    case XML_READER_TYPE_TEXT: return NT_Text;
    case XML_READER_TYPE_CDATA: return NT_CDATA;
    case XML_READER_TYPE_PROCESSING_INSTRUCTION: return NT_ProcessingInstruction;
    case XML_READER_TYPE_COMMENT: return NT_Comment;
    case XML_READER_TYPE_DOCUMENT_TYPE: return NT_DocumentType;
    case XML_READER_TYPE_WHITESPACE:
    case XML_READER_TYPE_SIGNIFICANT_WHITESPACE: return NT_Whitespace;
    case XML_READER_TYPE_END_ELEMENT: return NT_EndElement;
    }
    return NT_None;                 /* (entity references, entities, notations: not XmlLite nodes) */
}

/* ---- IXmlReader ---- */
#define R(This) ((Reader *)(This))

static HRESULT STDMETHODCALLTYPE r_qi(void *This, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IXmlReader)) {
        *out = This;
        InterlockedIncrement(&R(This)->refs);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE r_addref(void *This) { return InterlockedIncrement(&R(This)->refs); }
static ULONG STDMETHODCALLTYPE r_release(void *This)
{
    Reader *r = R(This);
    LONG n = InterlockedDecrement(&r->refs);
    if (!n) {
        close_input(r);
        mem_free(r->attrs);
        mem_free(r);
    }
    return n;
}

/* the bytes of @stream, read to its end */
static HRESULT read_all(IUnknown *unk, char **data, size_t *len)
{
    ISequentialStream *s = 0;
    HRESULT hr = unk->lpVtbl->QueryInterface(unk, &IID_ISequentialStream, (void **)&s);
    if (FAILED(hr)) hr = unk->lpVtbl->QueryInterface(unk, &IID_IStream, (void **)&s);
    if (FAILED(hr)) return E_INVALIDARG;
    size_t cap = 4096, n = 0;
    char *buf = mem(cap + 2);
    for (;;) {
        if (!buf) { s->lpVtbl->Release(s); return E_OUTOFMEMORY; }
        if (cap - n < 4096) {
            char *bigger = mem(cap * 2 + 2);
            if (bigger) memcpy(bigger, buf, n);
            mem_free(buf);
            buf = bigger;
            cap *= 2;
            continue;
        }
        ULONG got = 0;
        hr = s->lpVtbl->Read(s, buf + n, (ULONG)(cap - n), &got);
        n += got;
        if (FAILED(hr) || !got) break;
    }
    s->lpVtbl->Release(s);
    if (FAILED(hr)) { mem_free(buf); return hr; }
    *data = buf;
    *len = n;
    return S_OK;
}

/* what XmlLite infers when it is not told: a byte-order mark, or UTF-16
 * text without one ("<" then a zero byte), else what the XML declaration
 * says (libxml2 reads that) */
static const char *detect(const unsigned char *p, size_t n)
{
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) return "UTF-8";
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) return "UTF-16LE";
    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) return "UTF-16BE";
    if (n >= 2 && p[0] && !p[1]) return "UTF-16LE";
    if (n >= 2 && !p[0] && p[1]) return "UTF-16BE";
    return 0;
}

static HRESULT STDMETHODCALLTYPE r_setinput(void *This, IUnknown *in)
{
    Reader *r = R(This);
    close_input(r);
    if (!in) { r->state = RS_Closed; return S_OK; }
    Input *input = 0;
    IUnknown *stream = in;
    const char *enc = 0;
    if (SUCCEEDED(in->lpVtbl->QueryInterface(in, &IID_NovaXmlInput, (void **)&input))) {
        stream = input->stream;
        if (input->encoding[0]) enc = input->encoding;
    }
    size_t len = 0;
    HRESULT hr = read_all(stream, &r->data, &len);
    if (SUCCEEDED(hr)) {
        if (!enc) enc = detect((const unsigned char *)r->data, len);
        int opts = XML_PARSE_NONET | XML_PARSE_NOXINCNODE | XML_PARSE_NOERROR | XML_PARSE_NOWARNING;
        r->x = xmlReaderForMemory(r->data, (int)len, 0, enc, opts);
        if (r->x) xmlTextReaderSetStructuredErrorHandler(r->x, on_error, r);
        else hr = enc ? MX_E_ENCODING : E_OUTOFMEMORY;
    }
    if (input) in->lpVtbl->Release(in);
    if (FAILED(hr)) { close_input(r); r->state = RS_Closed; return hr; }
    r->state = RS_Initial;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_getprop(void *This, UINT prop, LONG_PTR *v)
{
    Reader *r = R(This);
    if (!v) return E_INVALIDARG;
    switch (prop) {
    case P_ReadState: *v = r->state; return S_OK;
    case P_DtdProcessing: *v = r->dtd; return S_OK;
    case P_ConformanceLevel: *v = 2; return S_OK;         /* XmlConformanceLevel_Document */
    case P_MaxElementDepth: case P_MaxEntityExpansion: case P_RandomAccess: case P_MultiLanguage:
    case P_XmlResolver: *v = 0; return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE r_setprop(void *This, UINT prop, LONG_PTR v)
{
    Reader *r = R(This);
    switch (prop) {
    case P_DtdProcessing:
        if (v != 0 && v != 1) return E_INVALIDARG;
        r->dtd = (int)v;
        return S_OK;
    case P_ReadState: return E_INVALIDARG;
    case P_MultiLanguage: case P_ConformanceLevel: case P_RandomAccess: case P_XmlResolver:
    case P_MaxElementDepth: case P_MaxEntityExpansion: return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE r_read(void *This, XmlNodeType *type)
{
    Reader *r = R(This);
    if (type) *type = NT_None;
    if (!r->x) return r->state == RS_Error ? WC_E_SYNTAX : E_UNEXPECTED;
    if (r->state == RS_Error) return r->error;
    clear_node(r);
    r->type = NT_None;
    if (r->eof) return S_FALSE;
    for (;;) {
        int got = xmlTextReaderRead(r->x);
        if (got == 0 && r->error == S_OK) {
            r->eof = 1;
            r->state = RS_EndOfFile;
            return S_FALSE;
        }
        if (got <= 0) {
            r->state = RS_Error;
            if (r->error == S_OK) r->error = WC_E_SYNTAX;
            return r->error;
        }
        XmlNodeType t = map_type(xmlTextReaderNodeType(r->x));
        if (t == NT_None) continue;
        if (t == NT_DocumentType && r->dtd == 0) {
            r->state = RS_Error;
            r->error = WC_E_DTDPROHIBITED;
            return r->error;
        }
        r->state = RS_Interactive;
        r->type = t;
        break;
    }
    xmlNodePtr n = xmlTextReaderCurrentNode(r->x);
    long line = n ? xmlGetLineNo(n) : -1;
    r->line = line > 0 ? (UINT)line : (UINT)xmlTextReaderGetParserLineNumber(r->x);
    r->col = (UINT)xmlTextReaderGetParserColumnNumber(r->x);
    r->depth = (UINT)xmlTextReaderDepth(r->x);
    r->node = names(r);
    if (r->type == NT_Element) {
        r->empty = xmlTextReaderIsEmptyElement(r->x) == 1;
        for (int ok = xmlTextReaderMoveToFirstAttribute(r->x); ok == 1; ok = xmlTextReaderMoveToNextAttribute(r->x)) {
            if (r->nattrs == r->cattrs) {
                UINT cap = r->cattrs ? r->cattrs * 2 : 8;
                Attr *a = mem(cap * sizeof *a);
                if (!a) return E_OUTOFMEMORY;
                if (r->attrs) memcpy(a, r->attrs, r->nattrs * sizeof *a);
                mem_free(r->attrs);
                r->attrs = a;
                r->cattrs = cap;
            }
            r->attrs[r->nattrs++] = names(r);
        }
        xmlTextReaderMoveToElement(r->x);
    }
    if (type) *type = r->type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_nodetype(void *This, XmlNodeType *type)
{
    Reader *r = R(This);
    if (!type) return E_INVALIDARG;
    *type = r->cur >= 0 ? NT_Attribute : r->type;
    return r->state == RS_Closed ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE r_firstattr(void *This)
{
    Reader *r = R(This);
    if (!r->x) return E_UNEXPECTED;
    if (!r->nattrs) return S_FALSE;
    r->cur = 0;
    r->chunk = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_nextattr(void *This)
{
    Reader *r = R(This);
    if (!r->x) return E_UNEXPECTED;
    if ((UINT)(r->cur + 1) >= r->nattrs) return S_FALSE;
    r->cur++;
    r->chunk = 0;
    return S_OK;
}

static BOOL same(Str s, const WCHAR *w)
{
    if (!w) w = empty_w;
    UINT n = 0;
    while (w[n]) n++;
    return n == s.n && !memcmp(s.p, w, n * sizeof(WCHAR));
}

static HRESULT STDMETHODCALLTYPE r_attrbyname(void *This, const WCHAR *local, const WCHAR *ns)
{
    Reader *r = R(This);
    if (!local) return E_INVALIDARG;
    for (UINT i = 0; i < r->nattrs; i++)
        if (same(r->attrs[i].local, local) && same(r->attrs[i].ns, ns)) {
            r->cur = (int)i;
            r->chunk = 0;
            return S_OK;
        }
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE r_toelement(void *This)
{
    Reader *r = R(This);
    if (r->cur < 0) return S_FALSE;
    r->cur = -1;
    r->chunk = 0;
    return S_OK;
}

static const Attr *current(Reader *r) { return r->cur >= 0 ? &r->attrs[r->cur] : &r->node; }

static HRESULT give(Str s, const WCHAR **p, UINT *n)
{
    if (!p) return E_INVALIDARG;
    *p = s.p ? s.p : empty_w;
    if (n) *n = s.n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_qname(void *This, const WCHAR **p, UINT *n) { return give(current(R(This))->qname, p, n); }
static HRESULT STDMETHODCALLTYPE r_nsuri(void *This, const WCHAR **p, UINT *n) { return give(current(R(This))->ns, p, n); }
static HRESULT STDMETHODCALLTYPE r_local(void *This, const WCHAR **p, UINT *n) { return give(current(R(This))->local, p, n); }
static HRESULT STDMETHODCALLTYPE r_prefix(void *This, const WCHAR **p, UINT *n) { return give(current(R(This))->prefix, p, n); }
static HRESULT STDMETHODCALLTYPE r_value(void *This, const WCHAR **p, UINT *n) { return give(current(R(This))->value, p, n); }

static HRESULT STDMETHODCALLTYPE r_chunk(void *This, WCHAR *buf, UINT size, UINT *got)
{
    Reader *r = R(This);
    Str v = current(r)->value;
    if (!buf || !got) return E_INVALIDARG;
    UINT n = v.n - r->chunk < size ? v.n - r->chunk : size;
    if (n) memcpy(buf, v.p + r->chunk, n * sizeof(WCHAR));
    r->chunk += n;
    *got = n;
    return n ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE r_baseuri(void *This, const WCHAR **p, UINT *n) { (void)This; return give((Str){ empty_w, 0 }, p, n); }
static BOOL STDMETHODCALLTYPE r_isdefault(void *This) { (void)This; return FALSE; }
static BOOL STDMETHODCALLTYPE r_isempty(void *This) { return R(This)->type == NT_Element && R(This)->empty; }
static HRESULT STDMETHODCALLTYPE r_line(void *This, UINT *v) { if (!v) return E_INVALIDARG; *v = R(This)->line; return S_OK; }
static HRESULT STDMETHODCALLTYPE r_linepos(void *This, UINT *v) { if (!v) return E_INVALIDARG; *v = R(This)->col; return S_OK; }
static HRESULT STDMETHODCALLTYPE r_attrcount(void *This, UINT *v) { if (!v) return E_INVALIDARG; *v = R(This)->nattrs; return S_OK; }
static HRESULT STDMETHODCALLTYPE r_depth(void *This, UINT *v)
{
    if (!v) return E_INVALIDARG;
    *v = R(This)->depth + (R(This)->cur >= 0);
    return S_OK;
}
static BOOL STDMETHODCALLTYPE r_iseof(void *This) { return R(This)->eof; }

/* IXmlReader's methods, in its vtable order */
static const struct ReaderVtbl {
    void *qi, *addref, *release, *setinput, *getprop, *setprop, *read, *nodetype, *firstattr, *nextattr, *attrbyname,
         *toelement, *qname, *nsuri, *local, *prefix, *value, *chunk, *baseuri, *isdefault, *isempty, *line, *linepos,
         *attrcount, *depth, *iseof;
} reader_vtbl = {
    r_qi, r_addref, r_release, r_setinput, r_getprop, r_setprop, r_read, r_nodetype, r_firstattr, r_nextattr,
    r_attrbyname, r_toelement, r_qname, r_nsuri, r_local, r_prefix, r_value, r_chunk, r_baseuri, r_isdefault,
    r_isempty, r_line, r_linepos, r_attrcount, r_depth, r_iseof,
};

/* ---- IXmlReaderInput: a stream with the encoding to read it in ---- */
static HRESULT STDMETHODCALLTYPE in_qi(IUnknown *This, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_NovaXmlInput)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE in_addref(IUnknown *This) { return InterlockedIncrement(&((Input *)This)->refs); }
static ULONG STDMETHODCALLTYPE in_release(IUnknown *This)
{
    Input *in = (Input *)This;
    LONG n = InterlockedDecrement(&in->refs);
    if (!n) {
        in->stream->lpVtbl->Release(in->stream);
        mem_free(in);
    }
    return n;
}
static const IUnknownVtbl input_vtbl = { in_qi, in_addref, in_release };

static HRESULT make_input(IUnknown *stream, const char *enc, IUnknown **out)
{
    if (!stream || !out) return E_INVALIDARG;
    Input *in = mem(sizeof *in);
    if (!in) return E_OUTOFMEMORY;
    in->lpVtbl = &input_vtbl;
    in->refs = 1;
    in->stream = stream;
    stream->lpVtbl->AddRef(stream);
    if (enc) {
        size_t n = strlen(enc) < sizeof in->encoding - 1 ? strlen(enc) : sizeof in->encoding - 1;
        memcpy(in->encoding, enc, n);
    }
    *out = (IUnknown *)in;
    return S_OK;
}

static const struct { UINT cp; const char *name; } codepages[] = {
    { 1200, "UTF-16LE" }, { 1201, "UTF-16BE" }, { 65001, "UTF-8" }, { 28591, "ISO-8859-1" },
    { 20127, "US-ASCII" }, { 12000, "UCS-4LE" }, { 12001, "UCS-4BE" },
};

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK init_once(INIT_ONCE *o, void *p, void **ctx)
{
    (void)o; (void)p; (void)ctx;
    xmlInitParser();
    return TRUE;
}

__declspec(dllexport) HRESULT WINAPI CreateXmlReader(REFIID riid, void **out, IMalloc *malloc)
{
    (void)malloc;
    if (!out) return E_INVALIDARG;
    *out = 0;
    InitOnceExecuteOnce(&g_once, init_once, 0, 0);
    Reader *r = mem(sizeof *r);
    if (!r) return E_OUTOFMEMORY;
    r->lpVtbl = &reader_vtbl;
    r->refs = 1;
    r->cur = -1;
    r->state = RS_Closed;
    HRESULT hr = r_qi(r, riid, out);
    r_release(r);
    return hr;
}

__declspec(dllexport) HRESULT WINAPI CreateXmlReaderInputWithEncodingName(IUnknown *stream, IMalloc *malloc,
    const WCHAR *encoding, BOOL hint, const WCHAR *base_uri, IUnknown **out)
{
    (void)malloc; (void)hint; (void)base_uri;
    char enc[32] = "";
    if (encoding) {
        int i = 0;
        for (; encoding[i] && i < 31; i++) enc[i] = (char)(encoding[i] < 128 ? encoding[i] : '?');
        enc[i] = 0;
        /* Windows' "utf-16" is little-endian unless a byte-order mark says otherwise */
        if (!_stricmp(enc, "utf-16") || !_stricmp(enc, "unicode")) strcpy(enc, "UTF-16LE");
    }
    return make_input(stream, enc[0] ? enc : 0, out);
}

__declspec(dllexport) HRESULT WINAPI CreateXmlReaderInputWithEncodingCodePage(IUnknown *stream, IMalloc *malloc,
    UINT codepage, BOOL hint, const WCHAR *base_uri, IUnknown **out)
{
    (void)malloc; (void)hint; (void)base_uri;
    for (unsigned i = 0; i < sizeof codepages / sizeof *codepages; i++)
        if (codepages[i].cp == codepage) return make_input(stream, codepages[i].name, out);
    return MX_E_ENCODING;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    xmlDllMain(inst, reason, reserved);
    return TRUE;
}
