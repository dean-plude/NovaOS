/*
 * winrtdoc.c — the Windows Runtime face of the windowless Rich Edit's text
 * (Microsoft.UI.Text's ITextDocument, ITextRange and ITextSelection, the
 * RichEditTextDocument a XAML RichEditBox's Document property hands out):
 * QueryInterface on the text services gives the document, as WinUIEdit's
 * does (XAML's CRichEditBox::GetDocument).  Each object is a thin wrapper
 * over the Text Object Model (tom.c): the document over ITextDocument2, a
 * range or the selection over its ITextRange2 / ITextSelection2, so the
 * Windows Runtime calls behave as TOM's do.  The Windows Runtime units,
 * text options and selection types are TOM's values renumbered (units) or
 * the same bits (TextGetOptions, TextSetOptions, SelectionType).
 *
 * What is here: the document's text (GetText, SetText; FormatRtf reads and
 * writes plain text), its selection and ranges (GetRange,
 * GetRangeFromPoint), undo and redo, the clipboard queries, the caret type,
 * tab stop and undo limit; a range's text, character, positions, units
 * (Move, MoveStart, MoveEnd, Expand, StartOf, EndOf, GetIndex, SetIndex,
 * Delete), FindText, ChangeCase, Cut, Copy, Paste, CanPaste, comparisons;
 * the selection's key moves (MoveLeft/Right/Up/Down, HomeKey, EndKey) and
 * TypeText; a range's character and paragraph formats (winrtfmt.c; the
 * paragraph's alignment).  The default formats, links, images, streams, points and rectangles and math answer
 * E_NOTIMPL.  Written from the
 * Windows App SDK's Microsoft.UI.Text documentation.
 */
#include "tsint.h"
#include <winstring.h>

typedef struct { float X, Y; } WPOINT;
typedef struct { float X, Y, Width, Height; } WRECT;
typedef HRESULT (STDMETHODCALLTYPE *VFN)(void);

/* (IInspectable, IAgileObject, and the three interfaces) */
static const IID iid_unk = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID iid_insp = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const IID iid_agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const IID iid_wdoc = {0x1149d57d, 0x86a6, 0x59dd, {0x88, 0xd9, 0x19, 0x6f, 0x27, 0xbc, 0x5c, 0x85}};
static const IID iid_wrange = {0x06d4abcf, 0x0c06, 0x5d12, {0xa7, 0x43, 0x85, 0x53, 0x7e, 0xfd, 0x09, 0xea}};
static const IID iid_wsel = {0x8f5e6cb1, 0x2b04, 0x589f, {0xbd, 0x24, 0x54, 0xe5, 0xcd, 0x8d, 0xd3, 0x99}};

/* combase's strings (ole32.dll on NovaOS), found when first used, so the
 * plain Rich Edit DLLs that share this code need no Windows Runtime */
static HRESULT (WINAPI *p_create)(const WCHAR *, UINT32, HSTRING *);
static const WCHAR *(WINAPI *p_buffer)(HSTRING, UINT32 *);

static BOOL strings(void)
{
    if (!p_create) {
        HMODULE m = LoadLibraryW(L"combase.dll");
        if (!m) m = LoadLibraryW(L"ole32.dll");
        if (!m) return FALSE;
        p_buffer = (const WCHAR *(WINAPI *)(HSTRING, UINT32 *))(void *)GetProcAddress(m, "WindowsGetStringRawBuffer");
        p_create = (HRESULT (WINAPI *)(const WCHAR *, UINT32, HSTRING *))(void *)GetProcAddress(m, "WindowsCreateString");
    }
    return p_create && p_buffer;
}

HRESULT make_hstring(const WCHAR *s, UINT32 n, HSTRING *out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!strings()) return E_NOTIMPL;
    return n ? p_create(s, n, out) : S_OK;          /* (the empty string is null) */
}

/* an HSTRING as a BSTR, for TOM */
static BSTR to_bstr(HSTRING h)
{
    UINT32 n = 0;
    const WCHAR *s = (h && strings()) ? p_buffer(h, &n) : L"";
    return SysAllocStringLen(s, n);
}
BSTR hstring_bstr(HSTRING h) { return to_bstr(h); }

/* a BSTR as an HSTRING, freeing the BSTR; (UseLf: paragraph marks as \n) */
static HRESULT bstr_out(BSTR b, BOOL lf, HSTRING *out)
{
    HRESULT hr;
    UINT n = b ? SysStringLen(b) : 0, i;
    if (lf) for (i = 0; i < n; i++) if (b[i] == L'\r') b[i] = L'\n';
    hr = make_hstring(b ? b : L"", n, out);
    SysFreeString(b);
    return hr;
}

#define OPT_USELF    0x01000000                     /* TextGetOptions.UseLf */
#define OPT_RTF      0x2000                         /* TextGetOptions/TextSetOptions.FormatRtf */

/* TextRangeUnit -> TOM's unit */
static LONG unit_of(INT32 u)
{
    static const LONG map[] = { 1 /* Character */, 2 /* Word */, 3 /* Sentence */, 4 /* Paragraph */,
                                5 /* Line */, 6 /* Story */, 7 /* Screen */, 8 /* Section */, 11 /* Window */,
                                13 /* CharacterFormat */, 14 /* ParagraphFormat */ };
    return u >= 0 && u < (INT32)(sizeof(map) / sizeof(map[0])) ? map[u] : 0x7fff;
}

static LONG ext(BYTE extend) { return extend ? tomExtend : 0; }

static HRESULT STDMETHODCALLTYPE insp_iids(void *me, ULONG *n, IID **iids) { (void)me; if (n) *n = 0; if (iids) *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE insp_trust(void *me, INT32 *level) { (void)me; if (level) *level = 0; return S_OK; }

static HRESULT class_name(const WCHAR *s, HSTRING *out)
{
    UINT32 n = 0;
    while (s[n]) n++;
    return make_hstring(s, n, out);
}

/* ---- the document: a field of the text services (TS.wdoc) ---- */
#define WDOC(p) ((TS *)((char *)(p) - __builtin_offsetof(TS, wdoc)))
static ITextDocument2 *tomdoc(void *me) { return &WDOC(me)->doc; }

static HRESULT STDMETHODCALLTYPE wd_qi(void *me, REFIID iid, void **out)
{ TS *t = WDOC(me); return t->outer->lpVtbl->QueryInterface(t->outer, iid, out); }
static ULONG STDMETHODCALLTYPE wd_addref(void *me) { TS *t = WDOC(me); return t->outer->lpVtbl->AddRef(t->outer); }
static ULONG STDMETHODCALLTYPE wd_release(void *me) { TS *t = WDOC(me); return t->outer->lpVtbl->Release(t->outer); }
static HRESULT STDMETHODCALLTYPE wd_name(void *me, HSTRING *out) { (void)me; return class_name(L"Microsoft.UI.Text.RichEditTextDocument", out); }

/* ---- a range, or the selection (both its ITextRange and its ITextSelection) ---- */
typedef struct WRANGE {
    const VFN *vtbl;                                /* ITextRange */
    const VFN *selvtbl;                             /* ITextSelection (the selection's only) */
    LONG ref;
    ITextRange2 *tom;                               /* TOM's range or selection */
    DWORD magic;
} WRANGE;
#define WRANGE_MAGIC 0x6e67724e                     /* (a range of ours, for InRange and the others) */
#define WR(p) ((WRANGE *)(p))
#define WS(p) ((WRANGE *)((char *)(p) - __builtin_offsetof(WRANGE, selvtbl)))

static HRESULT wrap(ITextRange2 *tom, BOOL sel, void **out);
static HRESULT STDMETHODCALLTYPE wr_GetClone(void *me, void **result);
static HRESULT STDMETHODCALLTYPE wr_GetText(void *me, INT32 options, HSTRING *value);
static HRESULT STDMETHODCALLTYPE wr_SetText(void *me, INT32 options, HSTRING value);

/* TOM's range of a Windows Runtime range given back to us */
static ITextRange2 *tom_of(void *p)
{
    WRANGE *r = WR(p);
    return (p && r->magic == WRANGE_MAGIC) ? r->tom : 0;
}

static HRESULT range_qi(WRANGE *r, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (IsEqualGUID(iid, &iid_unk) || IsEqualGUID(iid, &iid_insp) || IsEqualGUID(iid, &iid_agile) ||
        IsEqualGUID(iid, &iid_wrange)) *out = &r->vtbl;
    else if (r->selvtbl && IsEqualGUID(iid, &iid_wsel)) *out = &r->selvtbl;
    else return E_NOINTERFACE;
    InterlockedIncrement(&r->ref);
    return S_OK;
}

static ULONG range_release(WRANGE *r)
{
    LONG n = InterlockedDecrement(&r->ref);
    if (!n) {
        r->tom->lpVtbl->Release(r->tom);
        r->magic = 0;
        release(r);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE wr_qi(void *me, REFIID iid, void **out) { return range_qi(WR(me), iid, out); }
static ULONG STDMETHODCALLTYPE wr_addref(void *me) { return (ULONG)InterlockedIncrement(&WR(me)->ref); }
static ULONG STDMETHODCALLTYPE wr_release(void *me) { return range_release(WR(me)); }
static HRESULT STDMETHODCALLTYPE wr_name(void *me, HSTRING *out) { (void)me; return class_name(L"Microsoft.UI.Text.RichEditTextRange", out); }
static HRESULT STDMETHODCALLTYPE ws_qi(void *me, REFIID iid, void **out) { return range_qi(WS(me), iid, out); }
static ULONG STDMETHODCALLTYPE ws_addref(void *me) { return (ULONG)InterlockedIncrement(&WS(me)->ref); }
static ULONG STDMETHODCALLTYPE ws_release(void *me) { return range_release(WS(me)); }
static HRESULT STDMETHODCALLTYPE ws_name(void *me, HSTRING *out) { (void)me; return class_name(L"Microsoft.UI.Text.RichEditTextRange", out); }

#define TR(me) (WR(me)->tom)
#define TS_(me) (WS(me)->tom)

static HRESULT get_bool(HRESULT hr, LONG v, BYTE *out) { if (out) *out = SUCCEEDED(hr) && v == tomTrue; return hr; }

/* ---- Microsoft.UI.Text.ITextDocument {1149d57d-86a6-59dd-88d9-196f27bc5c85} ---- */
static HRESULT STDMETHODCALLTYPE wd_get_CaretType(void *me, INT32 *value)
{
    LONG v = 0;
    HRESULT hr = tomdoc(me)->lpVtbl->GetCaretType(tomdoc(me), &v);
    if (value) *value = v ? 1 : 0;                  /* (CaretType: Normal 0, Null 1) */
    return hr;
}
static HRESULT STDMETHODCALLTYPE wd_put_CaretType(void *me, INT32 value)
{
    return tomdoc(me)->lpVtbl->SetCaretType(tomdoc(me), value ? 1 : 0);
}
static HRESULT STDMETHODCALLTYPE wd_get_DefaultTabStop(void *me, float *value)
{
    return tomdoc(me)->lpVtbl->GetDefaultTabStop(tomdoc(me), value);
}
static HRESULT STDMETHODCALLTYPE wd_put_DefaultTabStop(void *me, float value)
{
    return tomdoc(me)->lpVtbl->SetDefaultTabStop(tomdoc(me), value);
}
static HRESULT STDMETHODCALLTYPE wd_get_Selection(void *me, void **value)
{
    ITextSelection2 *s = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = 0;
    if (FAILED(hr = tomdoc(me)->lpVtbl->GetSelection2(tomdoc(me), &s))) return hr;
    return wrap((ITextRange2 *)s, TRUE, value);
}
static HRESULT STDMETHODCALLTYPE wd_get_UndoLimit(void *me, UINT32 *value)
{
    if (!value) return E_POINTER;
    *value = (UINT32)WDOC(me)->undolimit;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_put_UndoLimit(void *me, UINT32 value)
{
    LRESULT r = 0;
    TS *t = WDOC(me);
    return t->iface.lpVtbl->TxSendMessage(&t->iface, EM_SETUNDOLIMIT, value, 0, &r);
}
static HRESULT STDMETHODCALLTYPE wd_CanCopy(void *me, BYTE *result)
{
    TS *t = WDOC(me);
    if (!result) return E_POINTER;
    *result = selmin(t) != selmax(t) && !(t->bits & TXTBIT_USEPASSWORD);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_CanPaste(void *me, BYTE *result)
{
    TS *t = WDOC(me);
    if (!result) return E_POINTER;
    *result = !(t->bits & TXTBIT_READONLY) && can_paste(t);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_CanRedo(void *me, BYTE *result)
{
    if (!result) return E_POINTER;
    *result = WDOC(me)->nredo > 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_CanUndo(void *me, BYTE *result)
{
    if (!result) return E_POINTER;
    *result = WDOC(me)->nundo > 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_ApplyDisplayUpdates(void *me, INT32 *result)
{
    LONG n = 0;
    HRESULT hr = tomdoc(me)->lpVtbl->Unfreeze(tomdoc(me), &n);
    if (result) *result = n;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wd_BatchDisplayUpdates(void *me, INT32 *result)
{
    LONG n = 0;
    HRESULT hr = tomdoc(me)->lpVtbl->Freeze(tomdoc(me), &n);
    if (result) *result = n;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wd_BeginUndoGroup(void *me)
{
    (void)me;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_EndUndoGroup(void *me)
{
    (void)me;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_GetDefaultCharacterFormat(void *me, void **result)
{ (void)me; (void)result; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wd_GetDefaultParagraphFormat(void *me, void **result)
{ (void)me; (void)result; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wd_GetRange(void *me, INT32 startPosition, INT32 endPosition, void **result)
{
    ITextRange2 *r = 0;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = 0;
    if (FAILED(hr = tomdoc(me)->lpVtbl->Range2(tomdoc(me), endPosition, startPosition, &r))) return hr;
    return wrap(r, FALSE, result);
}
static HRESULT STDMETHODCALLTYPE wd_GetRangeFromPoint(void *me, WPOINT point, INT32 options, void **result)
{
    ITextRange2 *r = 0;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = 0;
    if (FAILED(hr = tomdoc(me)->lpVtbl->RangeFromPoint2(tomdoc(me), (LONG)point.X, (LONG)point.Y, options, &r))) return hr;
    return wrap(r, FALSE, result);
}
static HRESULT STDMETHODCALLTYPE wd_GetText(void *me, INT32 options, HSTRING *value)
{
    ITextRange2 *r = 0;
    BSTR b = 0;
    LONG n = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = 0;
    if (FAILED(hr = tomdoc(me)->lpVtbl->Range2(tomdoc(me), 0, 0, &r))) return hr;
    r->lpVtbl->GetStoryLength(r, &n);
    r->lpVtbl->SetEnd(r, n);
    hr = r->lpVtbl->GetText2(r, options & ~(OPT_USELF | OPT_RTF), &b);
    r->lpVtbl->Release(r);
    return FAILED(hr) ? hr : bstr_out(b, (options & OPT_USELF) != 0, value);
}
static HRESULT STDMETHODCALLTYPE wd_LoadFromStream(void *me, INT32 options, void *value)
{ (void)me; (void)options; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wd_Redo(void *me)
{
    LONG done = 0;
    return tomdoc(me)->lpVtbl->Redo(tomdoc(me), 1, &done) == E_FAIL ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_SaveToStream(void *me, INT32 options, void *value)
{ (void)me; (void)options; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wd_SetDefaultCharacterFormat(void *me, void *value)
{ (void)me; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wd_SetDefaultParagraphFormat(void *me, void *value)
{ (void)me; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wd_SetText(void *me, INT32 options, HSTRING value)
{
    ITextRange2 *r = 0;
    BSTR b;
    LONG n = 0;
    HRESULT hr;
    if (FAILED(hr = tomdoc(me)->lpVtbl->Range2(tomdoc(me), 0, 0, &r))) return hr;
    r->lpVtbl->GetStoryLength(r, &n);
    r->lpVtbl->SetEnd(r, n);
    if (!(b = to_bstr(value))) { r->lpVtbl->Release(r); return E_OUTOFMEMORY; }
    hr = r->lpVtbl->SetText2(r, options & ~OPT_RTF, b);
    SysFreeString(b);
    r->lpVtbl->Release(r);
    return hr;
}
static HRESULT STDMETHODCALLTYPE wd_Undo(void *me)
{
    LONG done = 0;
    return tomdoc(me)->lpVtbl->Undo(tomdoc(me), 1, &done) == E_FAIL ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_get_AlignmentIncludesTrailingWhitespace(void *me, BYTE *value)
{
    (void)me;
    if (value) *value = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_put_AlignmentIncludesTrailingWhitespace(void *me, BYTE value)
{
    (void)me; (void)value;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_get_IgnoreTrailingCharacterSpacing(void *me, BYTE *value)
{
    (void)me;
    if (value) *value = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_put_IgnoreTrailingCharacterSpacing(void *me, BYTE value)
{
    (void)me; (void)value;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wd_ClearUndoRedoHistory(void *me)
{
    clear_undo(WDOC(me));
    return S_OK;
}
static const VFN wd_vtbl[] = {
    (VFN)wd_qi, (VFN)wd_addref, (VFN)wd_release, (VFN)insp_iids, (VFN)wd_name, (VFN)insp_trust,
    (VFN)wd_get_CaretType, (VFN)wd_put_CaretType, (VFN)wd_get_DefaultTabStop, (VFN)wd_put_DefaultTabStop,
    (VFN)wd_get_Selection, (VFN)wd_get_UndoLimit, (VFN)wd_put_UndoLimit, (VFN)wd_CanCopy,
    (VFN)wd_CanPaste, (VFN)wd_CanRedo, (VFN)wd_CanUndo, (VFN)wd_ApplyDisplayUpdates,
    (VFN)wd_BatchDisplayUpdates, (VFN)wd_BeginUndoGroup, (VFN)wd_EndUndoGroup, (VFN)wd_GetDefaultCharacterFormat,
    (VFN)wd_GetDefaultParagraphFormat, (VFN)wd_GetRange, (VFN)wd_GetRangeFromPoint, (VFN)wd_GetText,
    (VFN)wd_LoadFromStream, (VFN)wd_Redo, (VFN)wd_SaveToStream, (VFN)wd_SetDefaultCharacterFormat,
    (VFN)wd_SetDefaultParagraphFormat, (VFN)wd_SetText, (VFN)wd_Undo, (VFN)wd_get_AlignmentIncludesTrailingWhitespace,
    (VFN)wd_put_AlignmentIncludesTrailingWhitespace, (VFN)wd_get_IgnoreTrailingCharacterSpacing, (VFN)wd_put_IgnoreTrailingCharacterSpacing, (VFN)wd_ClearUndoRedoHistory,
};

/* ---- Microsoft.UI.Text.ITextRange {06d4abcf-0c06-5d12-a743-85537efd09ea} ---- */
static HRESULT STDMETHODCALLTYPE wr_get_Character(void *me, WCHAR *value)
{
    LONG c = 0;
    HRESULT hr = TR(me)->lpVtbl->GetChar(TR(me), &c);
    if (value) *value = (WCHAR)c;
    return hr;
}
static HRESULT STDMETHODCALLTYPE wr_put_Character(void *me, WCHAR value)
{
    return TR(me)->lpVtbl->SetChar(TR(me), value);
}
static HRESULT STDMETHODCALLTYPE wr_get_CharacterFormat(void *me, void **value)
{
    void *font = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = 0;
    if (FAILED(hr = TR(me)->lpVtbl->GetFont2(TR(me), &font))) return hr;
    hr = wfmt_wrap(font, value);                    /* (live, as the TOM font: winrtfmt.c) */
    ((IUnknown *)font)->lpVtbl->Release((IUnknown *)font);
    return hr;
}
static HRESULT STDMETHODCALLTYPE wr_put_CharacterFormat(void *me, void *value)
{
    void *font = wfmt_tom(value);
    if (!font) return E_INVALIDARG;
    return TR(me)->lpVtbl->SetFont2(TR(me), font);
}
static HRESULT STDMETHODCALLTYPE wr_get_FormattedText(void *me, void **value)
{
    return wr_GetClone(me, value);
}
static HRESULT STDMETHODCALLTYPE wr_put_FormattedText(void *me, void *value)
{ (void)me; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wr_get_EndPosition(void *me, INT32 *value)
{
    LONG v = 0;
    HRESULT hr = TR(me)->lpVtbl->GetEnd(TR(me), &v);
    if (value) *value = v;
    return hr;
}
static HRESULT STDMETHODCALLTYPE wr_put_EndPosition(void *me, INT32 value)
{
    return TR(me)->lpVtbl->SetEnd(TR(me), value);
}
static HRESULT STDMETHODCALLTYPE wr_get_Gravity(void *me, INT32 *value)
{
    (void)me;
    if (value) *value = 0;                          /* (RangeGravity.UIBehavior) */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wr_put_Gravity(void *me, INT32 value)
{
    (void)me; (void)value;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wr_get_Length(void *me, INT32 *value)
{
    LONG s = 0, e = 0;
    HRESULT hr = TR(me)->lpVtbl->GetStart(TR(me), &s);
    if (SUCCEEDED(hr)) hr = TR(me)->lpVtbl->GetEnd(TR(me), &e);
    if (value) *value = e - s;
    return hr;
}
static HRESULT STDMETHODCALLTYPE wr_get_Link(void *me, HSTRING *value)
{
    (void)me;
    if (value) *value = 0;                          /* (no links: the empty string) */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE wr_put_Link(void *me, HSTRING value)
{ (void)me; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wr_get_ParagraphFormat(void *me, void **value)
{
    void *para = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = 0;
    if (FAILED(hr = TR(me)->lpVtbl->GetPara2(TR(me), &para))) return hr;
    hr = wpara_wrap(para, value);                   /* (live, as the TOM paragraph: winrtfmt.c) */
    ((IUnknown *)para)->lpVtbl->Release((IUnknown *)para);
    return hr;
}
static HRESULT STDMETHODCALLTYPE wr_put_ParagraphFormat(void *me, void *value)
{
    void *para = wpara_tom(value);
    if (!para) return E_INVALIDARG;
    return TR(me)->lpVtbl->SetPara2(TR(me), para);
}
static HRESULT STDMETHODCALLTYPE wr_get_StartPosition(void *me, INT32 *value)
{
    LONG v = 0;
    HRESULT hr = TR(me)->lpVtbl->GetStart(TR(me), &v);
    if (value) *value = v;
    return hr;
}
static HRESULT STDMETHODCALLTYPE wr_put_StartPosition(void *me, INT32 value)
{
    return TR(me)->lpVtbl->SetStart(TR(me), value);
}
static HRESULT STDMETHODCALLTYPE wr_get_StoryLength(void *me, INT32 *value)
{
    LONG v = 0;
    HRESULT hr = TR(me)->lpVtbl->GetStoryLength(TR(me), &v);
    if (value) *value = v;
    return hr;
}
static HRESULT STDMETHODCALLTYPE wr_get_Text(void *me, HSTRING *value)
{
    return wr_GetText(me, 0, value);
}
static HRESULT STDMETHODCALLTYPE wr_put_Text(void *me, HSTRING value)
{
    return wr_SetText(me, 0, value);
}
static HRESULT STDMETHODCALLTYPE wr_CanPaste(void *me, INT32 format, BYTE *result)
{
    LONG v = 0;
    return get_bool(TR(me)->lpVtbl->CanPaste(TR(me), 0, format, &v), v, result);
}
static HRESULT STDMETHODCALLTYPE wr_ChangeCase(void *me, INT32 value)
{
    return TR(me)->lpVtbl->ChangeCase(TR(me), value);
}
static HRESULT STDMETHODCALLTYPE wr_Collapse(void *me, BYTE value)
{
    return TR(me)->lpVtbl->Collapse(TR(me), value ? tomTrue : tomFalse) == E_FAIL ? E_FAIL : S_OK;
}
static HRESULT STDMETHODCALLTYPE wr_Copy(void *me)
{
    return TR(me)->lpVtbl->Copy(TR(me), 0);
}
static HRESULT STDMETHODCALLTYPE wr_Cut(void *me)
{
    return TR(me)->lpVtbl->Cut(TR(me), 0);
}
static HRESULT STDMETHODCALLTYPE wr_Delete(void *me, INT32 unit, INT32 count, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TR(me)->lpVtbl->Delete(TR(me), unit_of(unit), count, &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wr_EndOf(void *me, INT32 unit, BYTE extend, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TR(me)->lpVtbl->EndOf(TR(me), unit_of(unit), ext(extend), &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wr_Expand(void *me, INT32 unit, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TR(me)->lpVtbl->Expand(TR(me), unit_of(unit), &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wr_FindText(void *me, HSTRING value, INT32 scanLength, INT32 options, INT32 *result)
{
    LONG n = 0;
    BSTR b = to_bstr(value);
    HRESULT hr;
    if (!b) return E_OUTOFMEMORY;
    hr = TR(me)->lpVtbl->FindText(TR(me), b, scanLength, options, &n);
    SysFreeString(b);
    if (result) *result = n;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wr_GetCharacterUtf32(void *me, UINT32 *value, INT32 offset)
{
    LONG c = 0;
    HRESULT hr = TR(me)->lpVtbl->GetChar2(TR(me), &c, offset);
    if (value) *value = (UINT32)c;
    return hr;
}
static HRESULT STDMETHODCALLTYPE wr_GetClone(void *me, void **result)
{
    ITextRange2 *d = 0;
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = 0;
    if (FAILED(hr = TR(me)->lpVtbl->GetDuplicate2(TR(me), &d))) return hr;
    return wrap(d, FALSE, result);
}
static HRESULT STDMETHODCALLTYPE wr_GetIndex(void *me, INT32 unit, INT32 *result)
{
    LONG v = 0;
    HRESULT hr = TR(me)->lpVtbl->GetIndex(TR(me), unit_of(unit), &v);
    if (result) *result = v;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wr_GetPoint(void *me, INT32 horizontalAlign, INT32 verticalAlign, INT32 options, WPOINT *point)
{ (void)me; (void)horizontalAlign; (void)verticalAlign; (void)options; (void)point; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wr_GetRect(void *me, INT32 options, WRECT *rect, INT32 *hit)
{ (void)me; (void)options; (void)rect; (void)hit; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wr_GetText(void *me, INT32 options, HSTRING *value)
{
    BSTR b = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = 0;
    if (FAILED(hr = TR(me)->lpVtbl->GetText2(TR(me), options & ~(OPT_USELF | OPT_RTF), &b))) return hr;
    return bstr_out(b, (options & OPT_USELF) != 0, value);
}
static HRESULT STDMETHODCALLTYPE wr_GetTextViaStream(void *me, INT32 options, void *value)
{ (void)me; (void)options; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wr_InRange(void *me, void *range, BYTE *result)
{
    LONG v = 0;
    ITextRange2 *o = tom_of(range);
    if (!o) return E_INVALIDARG;
    return get_bool(TR(me)->lpVtbl->InRange(TR(me), (ITextRange *)o, &v), v, result);
}
static HRESULT STDMETHODCALLTYPE wr_InsertImage(void *me, INT32 width, INT32 height, INT32 ascent, INT32 verticalAlign, HSTRING alternateText, void *value)
{ (void)me; (void)width; (void)height; (void)ascent; (void)verticalAlign; (void)alternateText; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wr_InStory(void *me, void *range, BYTE *result)
{
    LONG v = 0;
    ITextRange2 *o = tom_of(range);
    if (!o) return E_INVALIDARG;
    return get_bool(TR(me)->lpVtbl->InStory(TR(me), (ITextRange *)o, &v), v, result);
}
static HRESULT STDMETHODCALLTYPE wr_IsEqual(void *me, void *range, BYTE *result)
{
    LONG v = 0;
    ITextRange2 *o = tom_of(range);
    if (!o) return E_INVALIDARG;
    return get_bool(TR(me)->lpVtbl->IsEqual(TR(me), (ITextRange *)o, &v), v, result);
}
static HRESULT STDMETHODCALLTYPE wr_Move(void *me, INT32 unit, INT32 count, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TR(me)->lpVtbl->Move(TR(me), unit_of(unit), count, &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wr_MoveEnd(void *me, INT32 unit, INT32 count, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TR(me)->lpVtbl->MoveEnd(TR(me), unit_of(unit), count, &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wr_MoveStart(void *me, INT32 unit, INT32 count, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TR(me)->lpVtbl->MoveStart(TR(me), unit_of(unit), count, &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE wr_Paste(void *me, INT32 format)
{
    return TR(me)->lpVtbl->Paste(TR(me), 0, format);
}
static HRESULT STDMETHODCALLTYPE wr_ScrollIntoView(void *me, INT32 value)
{
    return TR(me)->lpVtbl->ScrollIntoView(TR(me), value);
}
static HRESULT STDMETHODCALLTYPE wr_MatchSelection(void *me)
{
    return TR(me)->lpVtbl->Select(TR(me));
}
static HRESULT STDMETHODCALLTYPE wr_SetIndex(void *me, INT32 unit, INT32 index, BYTE extend)
{
    return TR(me)->lpVtbl->SetIndex(TR(me), unit_of(unit), index, ext(extend));
}
static HRESULT STDMETHODCALLTYPE wr_SetPoint(void *me, WPOINT point, INT32 options, BYTE extend)
{ (void)me; (void)point; (void)options; (void)extend; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wr_SetRange(void *me, INT32 startPosition, INT32 endPosition)
{
    return TR(me)->lpVtbl->SetRange(TR(me), startPosition, endPosition);
}
static HRESULT STDMETHODCALLTYPE wr_SetText(void *me, INT32 options, HSTRING value)
{
    BSTR b = to_bstr(value);
    HRESULT hr;
    if (!b) return E_OUTOFMEMORY;
    hr = TR(me)->lpVtbl->SetText2(TR(me), options & ~OPT_RTF, b);
    SysFreeString(b);
    return hr;
}
static HRESULT STDMETHODCALLTYPE wr_SetTextViaStream(void *me, INT32 options, void *value)
{ (void)me; (void)options; (void)value; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE wr_StartOf(void *me, INT32 unit, BYTE extend, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TR(me)->lpVtbl->StartOf(TR(me), unit_of(unit), ext(extend), &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static const VFN wr_vtbl[] = {
    (VFN)wr_qi, (VFN)wr_addref, (VFN)wr_release, (VFN)insp_iids, (VFN)wr_name, (VFN)insp_trust,
    (VFN)wr_get_Character, (VFN)wr_put_Character, (VFN)wr_get_CharacterFormat, (VFN)wr_put_CharacterFormat,
    (VFN)wr_get_FormattedText, (VFN)wr_put_FormattedText, (VFN)wr_get_EndPosition, (VFN)wr_put_EndPosition,
    (VFN)wr_get_Gravity, (VFN)wr_put_Gravity, (VFN)wr_get_Length, (VFN)wr_get_Link,
    (VFN)wr_put_Link, (VFN)wr_get_ParagraphFormat, (VFN)wr_put_ParagraphFormat, (VFN)wr_get_StartPosition,
    (VFN)wr_put_StartPosition, (VFN)wr_get_StoryLength, (VFN)wr_get_Text, (VFN)wr_put_Text,
    (VFN)wr_CanPaste, (VFN)wr_ChangeCase, (VFN)wr_Collapse, (VFN)wr_Copy,
    (VFN)wr_Cut, (VFN)wr_Delete, (VFN)wr_EndOf, (VFN)wr_Expand,
    (VFN)wr_FindText, (VFN)wr_GetCharacterUtf32, (VFN)wr_GetClone, (VFN)wr_GetIndex,
    (VFN)wr_GetPoint, (VFN)wr_GetRect, (VFN)wr_GetText, (VFN)wr_GetTextViaStream,
    (VFN)wr_InRange, (VFN)wr_InsertImage, (VFN)wr_InStory, (VFN)wr_IsEqual,
    (VFN)wr_Move, (VFN)wr_MoveEnd, (VFN)wr_MoveStart, (VFN)wr_Paste,
    (VFN)wr_ScrollIntoView, (VFN)wr_MatchSelection, (VFN)wr_SetIndex, (VFN)wr_SetPoint,
    (VFN)wr_SetRange, (VFN)wr_SetText, (VFN)wr_SetTextViaStream, (VFN)wr_StartOf,
};

/* ---- Microsoft.UI.Text.ITextSelection {8f5e6cb1-2b04-589f-bd24-54e5cd8dd399} ---- */
static HRESULT STDMETHODCALLTYPE ws_get_Options(void *me, INT32 *value)
{
    LONG v = 0;
    HRESULT hr = TS_(me)->lpVtbl->GetFlags(TS_(me), &v);
    if (value) *value = v;
    return hr;
}
static HRESULT STDMETHODCALLTYPE ws_put_Options(void *me, INT32 value)
{
    return TS_(me)->lpVtbl->SetFlags(TS_(me), value);
}
static HRESULT STDMETHODCALLTYPE ws_get_Type(void *me, INT32 *value)
{
    LONG v = 0;
    HRESULT hr = TS_(me)->lpVtbl->GetType(TS_(me), &v);
    if (value) *value = v;                          /* (SelectionType: TOM's values) */
    return hr;
}
static HRESULT STDMETHODCALLTYPE ws_EndKey(void *me, INT32 unit, BYTE extend, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TS_(me)->lpVtbl->EndKey(TS_(me), unit_of(unit), ext(extend), &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE ws_HomeKey(void *me, INT32 unit, BYTE extend, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TS_(me)->lpVtbl->HomeKey(TS_(me), unit_of(unit), ext(extend), &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE ws_MoveDown(void *me, INT32 unit, INT32 count, BYTE extend, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TS_(me)->lpVtbl->MoveDown(TS_(me), unit_of(unit), count, ext(extend), &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE ws_MoveLeft(void *me, INT32 unit, INT32 count, BYTE extend, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TS_(me)->lpVtbl->MoveLeft(TS_(me), unit_of(unit), count, ext(extend), &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE ws_MoveRight(void *me, INT32 unit, INT32 count, BYTE extend, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TS_(me)->lpVtbl->MoveRight(TS_(me), unit_of(unit), count, ext(extend), &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE ws_MoveUp(void *me, INT32 unit, INT32 count, BYTE extend, INT32 *result)
{
    LONG d = 0;
    HRESULT hr = TS_(me)->lpVtbl->MoveUp(TS_(me), unit_of(unit), count, ext(extend), &d);
    if (result) *result = d;
    return SUCCEEDED(hr) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE ws_TypeText(void *me, HSTRING value)
{
    BSTR b = to_bstr(value);
    HRESULT hr;
    if (!b) return E_OUTOFMEMORY;
    hr = TS_(me)->lpVtbl->TypeText(TS_(me), b);
    SysFreeString(b);
    return hr;
}
static const VFN ws_vtbl[] = {
    (VFN)ws_qi, (VFN)ws_addref, (VFN)ws_release, (VFN)insp_iids, (VFN)ws_name, (VFN)insp_trust,
    (VFN)ws_get_Options, (VFN)ws_put_Options, (VFN)ws_get_Type, (VFN)ws_EndKey,
    (VFN)ws_HomeKey, (VFN)ws_MoveDown, (VFN)ws_MoveLeft, (VFN)ws_MoveRight,
    (VFN)ws_MoveUp, (VFN)ws_TypeText,
};

static HRESULT wrap(ITextRange2 *tom, BOOL sel, void **out)
{
    WRANGE *r = alloc(sizeof(*r));
    if (!r) { tom->lpVtbl->Release(tom); return E_OUTOFMEMORY; }
    r->vtbl = wr_vtbl;
    r->selvtbl = sel ? ws_vtbl : 0;
    r->ref = 1;
    r->tom = tom;
    r->magic = WRANGE_MAGIC;
    *out = sel ? (void *)&r->selvtbl : (void *)&r->vtbl;
    return S_OK;
}

/* QueryInterface on the text services: the document, for its interface */
void *wdoc_qi(TS *t, REFIID iid)
{
    if (!IsEqualGUID(iid, &iid_wdoc)) return 0;
    t->wdoc = wd_vtbl;
    return &t->wdoc;
}
