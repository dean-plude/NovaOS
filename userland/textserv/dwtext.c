/*
 * dwtext.c — the windowless Rich Edit's text in DirectWrite's fonts, for a
 * host that gives its fonts as DirectWrite font faces (IProvideFontInfo, by
 * QueryInterface on its ITextHost: XAML's text boxes do): the text is
 * measured with the face's glyph advances and TxDrawD2D draws it as glyph
 * runs, with the selection's highlight a filled rectangle, as Windows' Rich
 * Edit draws on Direct2D.  XAML's render target (HWRenderTarget) takes
 * glyph runs, rectangles and solid brushes, which it turns into its own
 * text drawing; it has no bitmaps, the GDI path textserv.c draws for any
 * other host.  The calls go through the interfaces' slots (DirectWrite's
 * IDWriteFontFace, Direct2D's ID2D1RenderTarget), as tsint.h's are.
 */
#include "tsint.h"

/* IDWriteFontFace's slots and structures */
typedef struct {
    UINT16 designUnitsPerEm, ascent, descent;
    INT16 lineGap;
    UINT16 capHeight, xHeight;
    INT16 underlinePosition;
    UINT16 underlineThickness;
    INT16 strikethroughPosition;
    UINT16 strikethroughThickness;
} DWFONTMETRICS_;
typedef struct { INT32 lsb; UINT32 advance; INT32 rsb, tsb; UINT32 advanceh; INT32 bsb, originy; } DWGLYPHMETRICS_;
typedef struct { float advanceOffset, ascenderOffset; } DWGLYPHOFFSET_;
typedef struct {
    IUnknown *fontFace;
    float fontEmSize;
    UINT32 glyphCount;
    const UINT16 *glyphIndices;
    const float *glyphAdvances;
    const DWGLYPHOFFSET_ *glyphOffsets;
    BOOL isSideways;
    UINT32 bidiLevel;
} DWGLYPHRUN_;
typedef void (STDMETHODCALLTYPE *DWGETMETRICS_)(IUnknown *, DWFONTMETRICS_ *);
typedef HRESULT (STDMETHODCALLTYPE *DWGLYPHMETRICSFN_)(IUnknown *, const UINT16 *, UINT32, DWGLYPHMETRICS_ *, BOOL);
typedef HRESULT (STDMETHODCALLTYPE *DWGLYPHINDICES_)(IUnknown *, const UINT32 *, UINT32, UINT16 *);
#define DW_GETMETRICS           8
#define DW_GETDESIGNGLYPHMETRICS 10
#define DW_GETGLYPHINDICES      11

/* ID2D1RenderTarget's */
typedef struct { float r, g, b, a; } D2DCOLORF_;
typedef struct { float left, top, right, bottom; } D2DRECT_;
typedef struct { float x, y; } D2DPOINT_;
typedef HRESULT (STDMETHODCALLTYPE *D2DSOLIDBRUSH_)(void *, const D2DCOLORF_ *, const void *, IUnknown **);
typedef void (STDMETHODCALLTYPE *D2DFILLRECT_)(void *, const D2DRECT_ *, IUnknown *);
typedef void (STDMETHODCALLTYPE *D2DGLYPHRUN_)(void *, D2DPOINT_, const DWGLYPHRUN_ *, IUnknown *, UINT32);
typedef void (STDMETHODCALLTYPE *D2DPUSHCLIP_)(void *, const D2DRECT_ *, UINT32);
typedef void (STDMETHODCALLTYPE *D2DPOPCLIP_)(void *);
#define D2D_CREATESOLIDCOLORBRUSH 8
#define D2D_FILLRECTANGLE       17
#define D2D_DRAWGLYPHRUN        29
#define D2D_PUSHAXISALIGNEDCLIP 45
#define D2D_POPAXISALIGNEDCLIP  46

/* the host's IProvideFontInfo (its methods have C++'s member calling
 * convention, as ITextHost's: TXAPI) */
typedef struct IProvideFontInfo_ IProvideFontInfo_;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IProvideFontInfo_ *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IProvideFontInfo_ *);
    ULONG (STDMETHODCALLTYPE *Release)(IProvideFontInfo_ *);
    BSTR (TXAPI *GetDefaultFont)(IProvideFontInfo_ *);
    BSTR (TXAPI *GetSerializableFontName)(IProvideFontInfo_ *, DWORD id);
    IUnknown *(TXAPI *GetFontFace)(IProvideFontInfo_ *, DWORD id);
    DWORD (TXAPI *GetRunFontFaceId)(IProvideFontInfo_ *, const WCHAR *name, UINT32 weight, UINT32 stretch, UINT32 style,
                                    LCID lcid, const WCHAR *text, UINT32 count, DWORD current, UINT32 *runs);
} IProvideFontInfo_Vtbl;
struct IProvideFontInfo_ { const IProvideFontInfo_Vtbl *lpVtbl; };

void dw_free(TS *t)
{
    int i;
    if (t->face) t->face->lpVtbl->Release(t->face);
    t->face = 0;
    for (i = 0; i < MAXFONTS; i++) {
        if (t->faces[i]) t->faces[i]->lpVtbl->Release(t->faces[i]);
        t->faces[i] = 0;
    }
}

/* the host's face of a font in a weight and style: @face's (a format's
 * own), or the default font's (null) */
static IUnknown *host_face(TS *t, const WCHAR *face, UINT32 weight, UINT32 style)
{
    IProvideFontInfo_ *fi = t->fontinfo;
    BSTR name = 0;
    DWORD id;
    UINT32 runs = 0;
    if (!fi) return 0;
    if (!face) name = fi->lpVtbl->GetDefaultFont(fi);
    id = fi->lpVtbl->GetRunFontFaceId(fi, face ? face : name ? name : t->cf.cf.szFaceName, weight, 5, style, 0, L" ", 1, 0, &runs);
    if (name) SysFreeString(name);
    return id ? fi->lpVtbl->GetFontFace(fi, id) : 0;
}

/* the face of rich text's font @key (fmt_key): the default's own, or the
 * host's of the key's face in its weight and style, asked once (the
 * default font's name when the key's face is the default's) */
IUnknown *dw_face(TS *t, int key)
{
    const FONTKEY *k;
    if (!t->face) return 0;
    if (key <= 0 || key >= t->nfkeys) return t->face;
    k = &t->fkeys[key];
    if (!t->faces[key]) {
        const WCHAR *own = k->face, *d = t->cf.cf.szFaceName;
        int i;
        for (i = 0; i < 32 && own[i] == d[i] && own[i]; i++) ;
        t->faces[key] = host_face(t, (i < 32 && own[i] == d[i]) ? 0 : own, (k->bi & 1) ? 700 : 400, (k->bi & 2) ? 2 : 0);
    }
    return t->faces[key] ? t->faces[key] : t->face;
}

/* a font key's size in pixels (its twips at 96 dpi) */
float dw_emsize(TS *t, int key)
{
    if (key <= 0 || key >= t->nfkeys) return t->emsize;
    return t->fkeys[key].size / 15.0f;
}

/* a font key's height and ascent from its face's metrics, when the host
 * gives faces */
BOOL dw_metrics(TS *t, int key, LONG *h, LONG *asc)
{
    DWFONTMETRICS_ m;
    IUnknown *face = dw_face(t, key);
    float du;
    if (!face) return FALSE;
    memset(&m, 0, sizeof(m));
    ((DWGETMETRICS_)(*(void ***)face)[DW_GETMETRICS])(face, &m);
    if (!m.designUnitsPerEm) return FALSE;
    du = dw_emsize(t, key) / m.designUnitsPerEm;
    *asc = (LONG)(m.ascent * du + 0.99f);
    *h = (LONG)((m.ascent + m.descent + m.lineGap) * du + 0.99f);
    if (*h <= 0) *h = 1;
    return TRUE;
}

/* After the character format changes: the face the host gives its default
 * font (in the format's weight and style) and its metrics, or none (GDI) */
void dw_refresh(TS *t)
{
    static const IID iid_fontinfo = NOVA_IID_IProvideFontInfo;
    DWFONTMETRICS_ m;
    float du;
    dw_free(t);
    if (t->host && !t->fontinfo_asked) {
        void *p = 0;
        t->fontinfo_asked = TRUE;
        /* (held without a reference, as the host is: until ShutdownTextServices) */
        if (HOST(t)->QueryInterface(t->host, &iid_fontinfo, &p) == S_OK && p) {
            t->fontinfo = p;
            ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
        }
    }
    if (!t->fontinfo) return;
    if (!(t->face = host_face(t, 0, (t->cf.cf.dwEffects & CFE_BOLD) ? 700 : t->cf.wWeight ? t->cf.wWeight : 400,
                              (t->cf.cf.dwEffects & CFE_ITALIC) ? 2 : 0))) return;
    memset(&m, 0, sizeof(m));
    ((DWGETMETRICS_)(*(void ***)t->face)[DW_GETMETRICS])(t->face, &m);
    if (!m.designUnitsPerEm) { dw_free(t); return; }
    t->emsize = t->cf.cf.yHeight / 15.0f;         /* (twips at 96 dpi) */
    du = t->emsize / m.designUnitsPerEm;
    t->ascent = (LONG)(m.ascent * du + 0.99f);
    t->lineh = (LONG)((m.ascent + m.descent + m.lineGap) * du + 0.99f);
    if (t->lineh <= 0) t->lineh = 1;
}

/* the glyphs of @n characters @s and their advances (in pixels) */
static void glyphs(float emsize, IUnknown *face, const WCHAR *s, LONG n, UINT16 *ids, float *adv)
{
    UINT32 stackcp[256], *cp = n > 256 ? alloc(n * sizeof(UINT32)) : stackcp;
    DWGLYPHMETRICS_ stackgm[256], *gm = n > 256 ? alloc(n * sizeof(DWGLYPHMETRICS_)) : stackgm;
    DWFONTMETRICS_ m;
    void **vt = *(void ***)face;
    LONG i;
    float du;
    memset(&m, 0, sizeof(m));
    ((DWGETMETRICS_)vt[DW_GETMETRICS])(face, &m);
    du = m.designUnitsPerEm ? emsize / m.designUnitsPerEm : 0;
    for (i = 0; i < n; i++) cp[i] = s[i];
    memset(gm, 0, n * sizeof(*gm));
    if (FAILED(((DWGLYPHINDICES_)vt[DW_GETGLYPHINDICES])(face, cp, (UINT32)n, ids))) memset(ids, 0, n * sizeof(UINT16));
    ((DWGLYPHMETRICSFN_)vt[DW_GETDESIGNGLYPHMETRICS])(face, ids, (UINT32)n, gm, FALSE);
    for (i = 0; i < n; i++) adv[i] = gm[i].advance * du;
    if (cp != stackcp) release(cp);
    if (gm != stackgm) release(gm);
}

/* measure()'s widths with the face: the advances added up, each total
 * rounded to the pixel (so the caret and the drawn glyphs agree) */
BOOL dw_measure(TS *t, int key, const WCHAR *s, LONG n, int *dx, SIZE *sz)
{
    UINT16 stackid[256], *ids;
    float stackadv[256], *adv, x = 0;
    LONG i;
    if (!t->face) return FALSE;
    ids = n > 256 ? alloc(n * sizeof(UINT16)) : stackid;
    adv = n > 256 ? alloc(n * sizeof(float)) : stackadv;
    glyphs(dw_emsize(t, key), dw_face(t, key), s, n, ids, adv);
    for (i = 0; i < n; i++) {
        x += adv[i];
        if (dx) dx[i] = (int)(x + 0.5f);
    }
    sz->cx = (LONG)(x + 0.5f);
    {
        LONG h, asc;
        fmt_metrics(t, 0, key, &h, &asc);
        sz->cy = h;
    }
    if (ids != stackid) release(ids);
    if (adv != stackadv) release(adv);
    return TRUE;
}

static D2DCOLORF_ colour(COLORREF c)
{
    D2DCOLORF_ f = { GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, 1.0f };
    return f;
}

static IUnknown *brush(void *rt, COLORREF c)
{
    D2DCOLORF_ f = colour(c);
    IUnknown *b = 0;
    if (FAILED(((D2DSOLIDBRUSH_)(*(void ***)rt)[D2D_CREATESOLIDCOLORBRUSH])(rt, &f, 0, &b))) b = 0;
    return b;
}

static void fill(void *rt, LONG l, LONG tp, LONG r, LONG b, IUnknown *br)
{
    D2DRECT_ rc = { (float)l, (float)tp, (float)r, (float)b };
    if (br) ((D2DFILLRECT_)(*(void ***)rt)[D2D_FILLRECTANGLE])(rt, &rc, br);
}

/* glyphs [a, b) of a line's @ids and @adv, from x */
static void run(float emsize, void *rt, IUnknown *face, float x, float baseline, const UINT16 *ids, const float *adv,
                const DWGLYPHOFFSET_ *off, LONG a, LONG b, IUnknown *br)
{
    DWGLYPHRUN_ g;
    D2DPOINT_ at = { x, baseline };
    if (b <= a || !br) return;
    memset(&g, 0, sizeof(g));
    g.fontFace = face;
    g.fontEmSize = emsize;
    g.glyphCount = (UINT32)(b - a);
    g.glyphIndices = ids + a;
    g.glyphAdvances = adv + a;
    g.glyphOffsets = off + a;
    ((D2DGLYPHRUN_)(*(void ***)rt)[D2D_DRAWGLYPHRUN])(rt, at, &g, br, 0);   /* (DWRITE_MEASURING_MODE_NATURAL) */
}

/* the solid brushes of a drawing, by colour (a few: the formats in view) */
typedef struct { void *rt; int n; COLORREF c[16]; IUnknown *b[16]; } BRUSHES;
static IUnknown *brush_of(BRUSHES *bs, COLORREF c)
{
    int i;
    for (i = 0; i < bs->n; i++) if (bs->c[i] == c) return bs->b[i];
    if (bs->n == 16) {                            /* (full: the last one gives way; each is used at once) */
        if (bs->b[15]) bs->b[15]->lpVtbl->Release(bs->b[15]);
        bs->n = 15;
    }
    bs->c[bs->n] = c;
    return bs->b[bs->n++] = brush(bs->rt, c);
}
typedef struct { void *rt; IUnknown *br; } D2DBAR;
static void d2d_bar(void *ctx, LONG l, LONG tp, LONG r, LONG b) { D2DBAR *d = ctx; fill(d->rt, l, tp, r, b, d->br); }

/* characters [a, b) of a line (@s as shown, @dx measure()'s widths) from
 * x0 on the baseline @base, each run in its format's face, colour and
 * bars; @autoc is the automatic colour, or (@sel, over the highlight) all
 * of them */
static void runs(TS *t, void *rt, BRUSHES *bs, const WCHAR *s, const int *dx, LONG start, LONG a, LONG b, LONG x0, LONG base,
                 COLORREF autoc, BOOL sel)
{
    LONG i, j, k;
    for (i = a; i < b; i = j) {
        int f = fmt_at(t, start + i);
        IUnknown *face;
        int key;
        float em;
        UINT16 stackid[256], *ids;
        float stackadv[256], *adv;
        DWGLYPHOFFSET_ stackoff[256], *off;
        LONG xa = x0 + (i ? dx[i - 1] : 0), n;
        D2DBAR bar;
        for (j = i + 1; j < b && fmt_at(t, start + j) == f; j++) ;
        n = j - i;
        key = fmt_key(t, f);
        face = dw_face(t, key);
        em = dw_emsize(t, key);
        ids = n > 256 ? alloc(n * sizeof(UINT16)) : stackid;
        adv = n > 256 ? alloc(n * sizeof(float)) : stackadv;
        off = n > 256 ? alloc(n * sizeof(DWGLYPHOFFSET_)) : stackoff;
        bar.rt = rt;
        bar.br = brush_of(bs, sel ? autoc : fmt_color(t, f, autoc));
        if (ids && adv && off) {
            glyphs(em, face, s + i, n, ids, adv);
            /* (the advances as measure() rounds them, so glyphs sit where the caret goes) */
            for (k = 0; k < n; k++) adv[k] = (float)(dx[i + k] - (i + k ? dx[i + k - 1] : 0));
            memset(off, 0, n * sizeof(DWGLYPHOFFSET_));
            run(em, rt, face, (float)xa, (float)base, ids, adv, off, 0, n, bar.br);
        }
        fmt_decorate(t, key, fmt_effects(t, f), xa, x0 + dx[j - 1], base, d2d_bar, &bar);
        if (ids != stackid) release(ids);
        if (adv != stackadv) release(adv);
        if (off != stackoff) release(off);
    }
}

/* TxDrawD2D with the faces: the laid-out lines (textserv.c lays them out
 * for @rc's width first), each run of one format a glyph run, the
 * selection's part on its highlight in the highlight's text colour */
HRESULT dw_draw(TS *t, void *rt, const RECT *rc, BOOL opaque)
{
    COLORREF autoc = t->host ? HOST(t)->TxGetSysColor(t->host, COLOR_WINDOWTEXT) : GetSysColor(COLOR_WINDOWTEXT);
    COLORREF selbgc = t->host ? HOST(t)->TxGetSysColor(t->host, COLOR_HIGHLIGHT) : GetSysColor(COLOR_HIGHLIGHT);
    COLORREF selfgc = t->host ? HOST(t)->TxGetSysColor(t->host, COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_HIGHLIGHTTEXT);
    BOOL showsel = t->focus || !(t->bits & TXTBIT_HIDESELECTION);
    LONG smin = selmin(t), smax = selmax(t);
    D2DRECT_ clip = { (float)rc->left, (float)rc->top, (float)rc->right, (float)rc->bottom };
    BRUSHES bs;
    int i;
    memset(&bs, 0, sizeof(bs));
    bs.rt = rt;
    if (!brush_of(&bs, autoc)) return E_OUTOFMEMORY;
    ((D2DPUSHCLIP_)(*(void ***)rt)[D2D_PUSHAXISALIGNEDCLIP])(rt, &clip, 0);   /* (per primitive) */
    if (opaque)
        fill(rt, rc->left, rc->top, rc->right, rc->bottom,
             brush_of(&bs, t->host ? HOST(t)->TxGetSysColor(t->host, COLOR_WINDOW) : GetSysColor(COLOR_WINDOW)));
    for (i = 0; i < t->nlines; i++) {
        LINE *ln = &t->lines[i];
        LONG y = rc->top + ln->y - t->scrolly, n = ln->len, k, x0 = rc->left - t->scrollx + ln->shift;
        WCHAR *s;
        int *dx;
        if (y + ln->h <= rc->top || y >= rc->bottom) continue;
        if (!n) {
            if (!(showsel && smin < smax && smax > ln->start && smin <= ln->start && i + 1 < t->nlines)) continue;
        }
        s = alloc((n ? n : 1) * sizeof(WCHAR));
        dx = alloc((n ? n : 1) * sizeof(int));
        if (!s || !dx) { release(s); release(dx); break; }
        for (k = 0; k < n; k++) {
            WCHAR c = (t->bits & TXTBIT_USEPASSWORD) ? t->pwch : t->text[ln->start + k];
            s[k] = c == L'\t' ? L' ' : c;
        }
        if (n) {
            measure(t, 0, ln->start, n, dx, 0);
            runs(t, rt, &bs, s, dx, ln->start, 0, n, x0, y + ln->asc, autoc, FALSE);
        }
        if (showsel && smin < smax && smax > ln->start && smin <= ln->start + n) {
            LONG a = smin > ln->start ? smin - ln->start : 0, b = smax < ln->start + n ? smax - ln->start : n;
            BOOL eol = smax > ln->start + n && i + 1 < t->nlines;   /* (the paragraph mark) */
            LONG xa = x0 + (a ? dx[a - 1] : 0), xb = x0 + (b ? dx[b - 1] : 0) + (eol ? ln->h / 3 : 0);
            fill(rt, xa, y, xb, y + ln->h, brush_of(&bs, selbgc));
            runs(t, rt, &bs, s, dx, ln->start, a, b, x0, y + ln->asc, selfgc, TRUE);
        }
        release(s);
        release(dx);
    }
    ((D2DPOPCLIP_)(*(void ***)rt)[D2D_POPAXISALIGNEDCLIP])(rt);
    for (i = 0; i < bs.n; i++) if (bs.b[i]) bs.b[i]->lpVtbl->Release(bs.b[i]);
    return S_OK;
}
