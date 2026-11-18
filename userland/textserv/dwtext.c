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
    if (t->face) t->face->lpVtbl->Release(t->face);
    t->face = 0;
}

/* After the character format changes: the face the host gives its default
 * font (in the format's weight and style) and its metrics, or none (GDI) */
void dw_refresh(TS *t)
{
    static const IID iid_fontinfo = NOVA_IID_IProvideFontInfo;
    IProvideFontInfo_ *fi;
    DWFONTMETRICS_ m;
    BSTR name;
    DWORD id;
    UINT32 runs = 0;
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
    if (!(fi = t->fontinfo)) return;
    name = fi->lpVtbl->GetDefaultFont(fi);
    id = fi->lpVtbl->GetRunFontFaceId(fi, name ? name : t->cf.cf.szFaceName,
                                      (t->cf.cf.dwEffects & CFE_BOLD) ? 700 : t->cf.wWeight ? t->cf.wWeight : 400,
                                      5, (t->cf.cf.dwEffects & CFE_ITALIC) ? 2 : 0, 0, L" ", 1, 0, &runs);
    if (name) SysFreeString(name);
    if (!id || !(t->face = fi->lpVtbl->GetFontFace(fi, id))) return;
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
static void glyphs(TS *t, const WCHAR *s, LONG n, UINT16 *ids, float *adv)
{
    UINT32 stackcp[256], *cp = n > 256 ? alloc(n * sizeof(UINT32)) : stackcp;
    DWGLYPHMETRICS_ stackgm[256], *gm = n > 256 ? alloc(n * sizeof(DWGLYPHMETRICS_)) : stackgm;
    DWFONTMETRICS_ m;
    void **vt = *(void ***)t->face;
    LONG i;
    float du;
    memset(&m, 0, sizeof(m));
    ((DWGETMETRICS_)vt[DW_GETMETRICS])(t->face, &m);
    du = m.designUnitsPerEm ? t->emsize / m.designUnitsPerEm : 0;
    for (i = 0; i < n; i++) cp[i] = s[i];
    memset(gm, 0, n * sizeof(*gm));
    if (FAILED(((DWGLYPHINDICES_)vt[DW_GETGLYPHINDICES])(t->face, cp, (UINT32)n, ids))) memset(ids, 0, n * sizeof(UINT16));
    ((DWGLYPHMETRICSFN_)vt[DW_GETDESIGNGLYPHMETRICS])(t->face, ids, (UINT32)n, gm, FALSE);
    for (i = 0; i < n; i++) adv[i] = gm[i].advance * du;
    if (cp != stackcp) release(cp);
    if (gm != stackgm) release(gm);
}

/* measure()'s widths with the face: the advances added up, each total
 * rounded to the pixel (so the caret and the drawn glyphs agree) */
BOOL dw_measure(TS *t, const WCHAR *s, LONG n, int *dx, SIZE *sz)
{
    UINT16 stackid[256], *ids;
    float stackadv[256], *adv, x = 0;
    LONG i;
    if (!t->face) return FALSE;
    ids = n > 256 ? alloc(n * sizeof(UINT16)) : stackid;
    adv = n > 256 ? alloc(n * sizeof(float)) : stackadv;
    glyphs(t, s, n, ids, adv);
    for (i = 0; i < n; i++) {
        x += adv[i];
        if (dx) dx[i] = (int)(x + 0.5f);
    }
    sz->cx = (LONG)(x + 0.5f);
    sz->cy = t->lineh;
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
static void run(TS *t, void *rt, float x, float baseline, const UINT16 *ids, const float *adv, const DWGLYPHOFFSET_ *off,
                LONG a, LONG b, IUnknown *br)
{
    DWGLYPHRUN_ g;
    D2DPOINT_ at = { x, baseline };
    if (b <= a || !br) return;
    memset(&g, 0, sizeof(g));
    g.fontFace = t->face;
    g.fontEmSize = t->emsize;
    g.glyphCount = (UINT32)(b - a);
    g.glyphIndices = ids + a;
    g.glyphAdvances = adv + a;
    g.glyphOffsets = off + a;
    ((D2DGLYPHRUN_)(*(void ***)rt)[D2D_DRAWGLYPHRUN])(rt, at, &g, br, 0);   /* (DWRITE_MEASURING_MODE_NATURAL) */
}

/* TxDrawD2D with the face: the laid-out lines (textserv.c lays them out
 * for @rc's width first), each a glyph run, the selection's part on its
 * highlight in the highlight's text colour */
HRESULT dw_draw(TS *t, void *rt, const RECT *rc, BOOL opaque)
{
    COLORREF fgc = (t->cf.cf.dwEffects & CFE_AUTOCOLOR) || !(t->cf.cf.dwMask & CFM_COLOR)
                   ? (t->host ? HOST(t)->TxGetSysColor(t->host, COLOR_WINDOWTEXT) : GetSysColor(COLOR_WINDOWTEXT))
                   : t->cf.cf.crTextColor;
    COLORREF selbgc = t->host ? HOST(t)->TxGetSysColor(t->host, COLOR_HIGHLIGHT) : GetSysColor(COLOR_HIGHLIGHT);
    COLORREF selfgc = t->host ? HOST(t)->TxGetSysColor(t->host, COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_HIGHLIGHTTEXT);
    BOOL showsel = t->focus || !(t->bits & TXTBIT_HIDESELECTION);
    LONG smin = selmin(t), smax = selmax(t);
    IUnknown *fg = brush(rt, fgc), *selbg = 0, *selfg = 0;
    D2DRECT_ clip = { (float)rc->left, (float)rc->top, (float)rc->right, (float)rc->bottom };
    int i;
    if (!fg) return E_OUTOFMEMORY;
    ((D2DPUSHCLIP_)(*(void ***)rt)[D2D_PUSHAXISALIGNEDCLIP])(rt, &clip, 0);   /* (per primitive) */
    if (opaque) {
        IUnknown *bg = brush(rt, t->host ? HOST(t)->TxGetSysColor(t->host, COLOR_WINDOW) : GetSysColor(COLOR_WINDOW));
        fill(rt, rc->left, rc->top, rc->right, rc->bottom, bg);
        if (bg) bg->lpVtbl->Release(bg);
    }
    for (i = 0; i < t->nlines; i++) {
        LINE *ln = &t->lines[i];
        LONG y = rc->top + i * t->lineh - t->scrolly, n = ln->len, k;
        float x0 = (float)(rc->left - t->scrollx);
        WCHAR *s;
        UINT16 *ids;
        float *adv;
        DWGLYPHOFFSET_ *off;
        if (y + t->lineh <= rc->top || y >= rc->bottom) continue;
        if (!n) {
            if (!(showsel && smin < smax && smax > ln->start && smin <= ln->start && i + 1 < t->nlines)) continue;
        }
        s = alloc((n ? n : 1) * sizeof(WCHAR));
        ids = alloc((n ? n : 1) * sizeof(UINT16));
        adv = alloc((n ? n : 1) * sizeof(float));
        off = alloc((n ? n : 1) * sizeof(DWGLYPHOFFSET_));
        if (!s || !ids || !adv || !off) { release(s); release(ids); release(adv); release(off); break; }
        for (k = 0; k < n; k++) {
            WCHAR c = (t->bits & TXTBIT_USEPASSWORD) ? t->pwch : t->text[ln->start + k];
            s[k] = c == L'\t' ? L' ' : c;
        }
        if (n) {
            /* (the advances as measure() rounds them, so glyphs sit where the caret goes) */
            float x = 0;
            int prev = 0;
            glyphs(t, s, n, ids, adv);
            for (k = 0; k < n; k++) {
                int r;
                x += adv[k];
                r = (int)(x + 0.5f);
                adv[k] = (float)(r - prev);
                prev = r;
            }
            run(t, rt, x0, (float)(y + t->ascent), ids, adv, off, 0, n, fg);
        }
        if (showsel && smin < smax && smax > ln->start && smin <= ln->start + n) {
            LONG a = smin > ln->start ? smin - ln->start : 0, b = smax < ln->start + n ? smax - ln->start : n;
            BOOL eol = smax > ln->start + n && i + 1 < t->nlines;   /* (the paragraph mark) */
            float xa = x0, xb;
            for (k = 0; k < a; k++) xa += adv[k];
            xb = xa;
            for (k = a; k < b; k++) xb += adv[k];
            if (eol) xb += t->lineh / 3;
            if (!selbg) selbg = brush(rt, selbgc);
            if (!selfg) selfg = brush(rt, selfgc);
            fill(rt, (LONG)xa, y, (LONG)xb, y + t->lineh, selbg);
            run(t, rt, xa, (float)(y + t->ascent), ids, adv, off, a, b, selfg);
        }
        release(s);
        release(ids);
        release(adv);
        release(off);
    }
    ((D2DPOPCLIP_)(*(void ***)rt)[D2D_POPAXISALIGNEDCLIP])(rt);
    fg->lpVtbl->Release(fg);
    if (selbg) selbg->lpVtbl->Release(selbg);
    if (selfg) selfg->lpVtbl->Release(selfg);
    return S_OK;
}
