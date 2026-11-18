/*
 * tomfmt.c — the Text Object Model's character and paragraph formatting
 * objects (ITextFont2 and ITextPara2, which a range's GetFont/GetFont2 and
 * GetPara/GetPara2 give) for plain text in one format: XAML's TextBox asks
 * a caret's empty paragraph for its direction (ITextPara2's GetEffects,
 * PFE_RTLPARA) and sets it, and keeps per-character cookies in the font
 * (GetCookie, SetCookie) while it composes input.  The paragraph is always
 * left to right; the cookie is kept per object.  The other properties
 * answer E_NOTIMPL, each method by its own number of arguments (the
 * interfaces are stdcall in 32-bit code, so a method pops what it is
 * given).  Written from the Text Object Model documentation.
 */
#include "tsint.h"

typedef struct FMT {
    const void *const *lpVtbl;
    LONG ref;
    BOOL font;                                    /* ITextFont2, else ITextPara2 */
    LONG cookie;
} FMT;

static const IID iid_unknown_ = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID iid_dispatch_ = NOVA_IID_IDispatch;
static const IID iid_font = {0x8cc497c3, 0xa1df, 0x11ce, {0x80, 0x98, 0x00, 0xaa, 0x00, 0x47, 0xbe, 0x5d}};
static const IID iid_para = {0x8cc497c4, 0xa1df, 0x11ce, {0x80, 0x98, 0x00, 0xaa, 0x00, 0x47, 0xbe, 0x5d}};
static const IID iid_font2 = {0xc241f5e3, 0x7206, 0x11d8, {0xa2, 0xc7, 0x00, 0xa0, 0xd1, 0xd6, 0xc6, 0xb3}};
static const IID iid_para2 = {0xc241f5e4, 0x7206, 0x11d8, {0xa2, 0xc7, 0x00, 0xa0, 0xd1, 0xd6, 0xc6, 0xb3}};

static HRESULT STDMETHODCALLTYPE f_qi(FMT *f, REFIID iid, void **out)
{
    BOOL font = f->font;
    if (!out) return E_POINTER;
    *out = 0;
    if (IsEqualGUID(iid, &iid_unknown_) || IsEqualGUID(iid, &iid_dispatch_) ||
        (font && (IsEqualGUID(iid, &iid_font) || IsEqualGUID(iid, &iid_font2))) ||
        (!font && (IsEqualGUID(iid, &iid_para) || IsEqualGUID(iid, &iid_para2)))) {
        *out = f;
        InterlockedIncrement(&f->ref);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE f_addref(FMT *f) { return (ULONG)InterlockedIncrement(&f->ref); }
static ULONG STDMETHODCALLTYPE f_release(FMT *f)
{
    LONG n = InterlockedDecrement(&f->ref);
    if (!n) release(f);
    return (ULONG)n;
}

/* IDispatch: no type information */
static HRESULT STDMETHODCALLTYPE f_tcount(FMT *f, UINT *n) { (void)f; if (n) *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE f_tinfo(FMT *f, UINT i, LCID l, void **o) { (void)f; (void)i; (void)l; if (o) *o = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE f_ids(FMT *f, REFIID i, LPOLESTR *n, UINT c, LCID l, LONG *d)
{ (void)f; (void)i; (void)n; (void)c; (void)l; (void)d; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE f_invoke(FMT *f, LONG id, REFIID i, LCID l, WORD w, void *p, VARIANT *v, void *e, UINT *a)
{ (void)f; (void)id; (void)i; (void)l; (void)w; (void)p; (void)v; (void)e; (void)a; return E_NOTIMPL; }

/* the properties not kept, by their number of arguments */
static HRESULT STDMETHODCALLTYPE n0(FMT *f) { (void)f; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE n1(FMT *f, void *a) { (void)f; (void)a; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE n2(FMT *f, void *a, void *b) { (void)f; (void)a; (void)b; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE n3(FMT *f, void *a, void *b, void *c) { (void)f; (void)a; (void)b; (void)c; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE n4(FMT *f, void *a, void *b, void *c, void *d) { (void)f; (void)a; (void)b; (void)c; (void)d; return E_NOTIMPL; }

/* ITextPara2's effects: left to right (PFE_RTLPARA clear), which may be set
 * so but not changed */
#define PFE_RTLPARA_ 1
static HRESULT STDMETHODCALLTYPE p_geteffects(FMT *f, LONG *value, LONG *mask)
{
    (void)f;
    if (value) *value = 0;
    if (mask) *mask = PFE_RTLPARA_;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE p_seteffects(FMT *f, LONG value, LONG mask)
{
    (void)f;
    return (value & mask & PFE_RTLPARA_) ? E_NOTIMPL : S_OK;
}

/* ITextFont2's cookie */
static HRESULT STDMETHODCALLTYPE c_get(FMT *f, LONG *v) { if (!v) return E_INVALIDARG; *v = f->cookie; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_set(FMT *f, LONG v) { f->cookie = v; return S_OK; }

#define N1x2 n1, n1
#define N1x4 N1x2, N1x2
#define N1x8 N1x4, N1x4
#define N1x16 N1x8, N1x8

/* ITextPara: 48 methods (GetDuplicate .. GetTab), then ITextPara2's 16 */
static const void *const para_vtbl[] = {
    f_qi, f_addref, f_release, f_tcount, f_tinfo, f_ids, f_invoke,
    n1, n1, n1, n2, n1,                           /* GetDuplicate, SetDuplicate, CanChange, IsEqual, Reset */
    N1x16, N1x8, N1x4, n1, n1,                    /* GetStyle .. SetRightIndent (30) */
    n3, n2,                                       /* SetIndents, SetLineSpacing */
    N1x4, n1, n1, n1,                             /* GetSpaceAfter .. GetTabCount (7) */
    n3, n0, n1, n4,                               /* AddTab, ClearAllTabs, DeleteTab, GetTab */
    /* ITextPara2 */
    N1x8, n1, n1, n1,                             /* GetBorders .. SetTrimPunctuationAtStart (11) */
    p_geteffects, n2, n2, p_seteffects, n2,       /* GetEffects, GetProperty, IsEqual2, SetEffects, SetProperty */
};

/* ITextFont: 55 methods (GetDuplicate .. SetWeight), then ITextFont2's 46 */
static const void *const font_vtbl[] = {
    f_qi, f_addref, f_release, f_tcount, f_tinfo, f_ids, f_invoke,
    n1, n1, n1, n2, n1,                           /* GetDuplicate, SetDuplicate, CanChange, IsEqual, Reset */
    N1x16, N1x16, N1x16, n1, n1,                  /* GetStyle .. SetWeight (50) */
    /* ITextFont2 */
    N1x8, n1, n1, n1, n1, n1,                     /* GetCount .. SetCompressionMode (13) */
    c_get, c_set,                                 /* GetCookie, SetCookie */
    N1x16, N1x4, n1, n1, n1,                      /* GetDoubleStrike .. SetUnderlinePositionMode (23) */
    n2, n2, n2, n3, n2, n2, n2, n2,               /* GetEffects .. SetProperty */
};

_Static_assert(sizeof(para_vtbl) / sizeof(para_vtbl[0]) == 7 + 48 + 16, "ITextPara2's methods");
_Static_assert(sizeof(font_vtbl) / sizeof(font_vtbl[0]) == 7 + 55 + 46, "ITextFont2's methods");

HRESULT tom_format(BOOL font, void **out)
{
    FMT *f;
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!(f = alloc(sizeof(FMT)))) return E_OUTOFMEMORY;
    f->lpVtbl = font ? font_vtbl : para_vtbl;
    f->ref = 1;
    f->font = font;
    *out = f;
    return S_OK;
}
