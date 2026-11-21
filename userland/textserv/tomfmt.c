/*
 * tomfmt.c — the Text Object Model's character and paragraph formatting
 * objects (ITextFont2 and ITextPara2, which a range's GetFont/GetFont2 and
 * GetPara/GetPara2 give).  A range's font is live, as Windows' is: its
 * bold, italic, underline, strikethrough and colour are those of the
 * range's characters now (tomUndefined where they differ), and setting one
 * formats them (runs.c; the whole text when it is plain).  Its name, size
 * and weight are read; GetDuplicate gives a font of its own, whose values
 * SetFont or SetDuplicate apply to a range.  XAML's TextBox also asks a
 * caret's empty paragraph for its direction (ITextPara2's GetEffects,
 * PFE_RTLPARA) and sets it, and keeps per-character cookies in the font
 * (GetCookie, SetCookie) while it composes input.  The paragraph is always
 * left to right; the cookie is kept per object.  A range's paragraph is
 * live too: its alignment is that of the paragraphs the range touches
 * (tomUndefined where they differ, runs.c), and setting it aligns them;
 * GetDuplicate, SetDuplicate (ITextRange::SetPara) and IsEqual work on the
 * alignment (XAML text input, part 7) and the indents (first line, left,
 * right: part 8).  The other properties
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
    ITextRange2 *range;                           /* a live font's or paragraph's range, or null: a duplicate */
    CHARFORMAT2W cf;                              /* a duplicate font's values (dwMask: those defined) */
    LONG align;                                   /* a duplicate paragraph's alignment (PFA_, 0: undefined) */
    LONG ind[3];                                  /* a duplicate paragraph's indents (twips, IND_) */
    DWORD indset;                                 /* those defined (IND_ bits) */
} FMT;

#define tomFalse_      0
#define tomSingle_     1
#define tomAutoColor_  (-9999997)
#define CO_E_RELEASED_ ((HRESULT)0x800401ffL)

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
    if (!n) {
        if (f->range) f->range->lpVtbl->Release(f->range);
        release(f);
    }
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

/* ---- the character properties ------------------------------------------ */
static BOOL ours(const FMT *p) { return p && p->lpVtbl[0] == (const void *)f_qi && p->font; }

/* a live font's range: its text services and its two ends */
static TS *range_of(FMT *f, LONG *a, LONG *b)
{
    TS *t = tom_range_ts(f->range);
    *a = *b = 0;
    if (!t) return 0;
    f->range->lpVtbl->GetStart(f->range, a);
    f->range->lpVtbl->GetEnd(f->range, b);
    return t;
}

/* the font's values now: dwMask says which are defined */
static HRESULT values(FMT *f, CHARFORMAT2W *cf)
{
    LONG a, b;
    TS *t;
    if (!f->range) { *cf = f->cf; return S_OK; }
    if (!(t = range_of(f, &a, &b))) return CO_E_RELEASED_;
    fmt_query(t, a, b, cf);
    return S_OK;
}

/* sets @cf's properties (its dwMask's: the effects, the colour, the size
 * and the face) */
static HRESULT put_cf(FMT *f, const CHARFORMATW *cf)
{
    LONG a, b;
    TS *t;
    DWORD mask = cf->dwMask & FMT_MASK;
    if (!f->range) {
        f->cf.cf.dwMask |= mask;
        f->cf.cf.dwEffects = (f->cf.cf.dwEffects & ~mask) | (cf->dwEffects & mask);
        if (mask & CFM_COLOR) f->cf.cf.crTextColor = cf->crTextColor;
        if (mask & CFM_SIZE) f->cf.cf.yHeight = cf->yHeight;
        if (mask & CFM_FACE) memcpy(f->cf.cf.szFaceName, cf->szFaceName, sizeof(cf->szFaceName));
        return S_OK;
    }
    if (!(t = range_of(f, &a, &b))) return CO_E_RELEASED_;
    if (t->bits & TXTBIT_READONLY) return E_ACCESSDENIED;
    return fmt_apply_cf(t, a, b, cf) ? S_OK : E_INVALIDARG;
}

/* sets @mask's properties to @effects and @color */
static HRESULT put(FMT *f, DWORD mask, DWORD effects, COLORREF color)
{
    CHARFORMATW cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = mask & ~(CFM_SIZE | CFM_FACE);
    cf.dwEffects = effects;
    cf.crTextColor = color;
    return put_cf(f, &cf);
}

/* an on-or-off property (bold, italic, strikethrough, underline) */
static HRESULT get_effect(FMT *f, DWORD e, LONG on, LONG *value)
{
    CHARFORMAT2W cf;
    HRESULT hr;
    if (!value) return E_INVALIDARG;
    if (FAILED(hr = values(f, &cf))) return hr;
    *value = !(cf.cf.dwMask & e) ? tomUndefined : (cf.cf.dwEffects & e) ? on : tomFalse_;
    return S_OK;
}
static HRESULT set_effect(FMT *f, DWORD e, LONG value)
{
    if (value == tomUndefined) return S_OK;
    if (value == tomToggle) {
        CHARFORMAT2W cf;
        HRESULT hr;
        if (FAILED(hr = values(f, &cf))) return hr;
        return put(f, e, ((cf.cf.dwMask & e) && (cf.cf.dwEffects & e)) ? 0 : e, 0);
    }
    return put(f, e, value ? e : 0, 0);
}
static HRESULT STDMETHODCALLTYPE fo_getbold(FMT *f, LONG *v) { return get_effect(f, CFE_BOLD, tomTrue, v); }
static HRESULT STDMETHODCALLTYPE fo_setbold(FMT *f, LONG v) { return set_effect(f, CFE_BOLD, v); }
static HRESULT STDMETHODCALLTYPE fo_getitalic(FMT *f, LONG *v) { return get_effect(f, CFE_ITALIC, tomTrue, v); }
static HRESULT STDMETHODCALLTYPE fo_setitalic(FMT *f, LONG v) { return set_effect(f, CFE_ITALIC, v); }
static HRESULT STDMETHODCALLTYPE fo_getstrike(FMT *f, LONG *v) { return get_effect(f, CFE_STRIKEOUT, tomTrue, v); }
static HRESULT STDMETHODCALLTYPE fo_setstrike(FMT *f, LONG v) { return set_effect(f, CFE_STRIKEOUT, v); }
/* (underline: tomSingle when on; any underline type sets the one kind drawn) */
static HRESULT STDMETHODCALLTYPE fo_getunderline(FMT *f, LONG *v) { return get_effect(f, CFE_UNDERLINE, tomSingle_, v); }
static HRESULT STDMETHODCALLTYPE fo_setunderline(FMT *f, LONG v) { return set_effect(f, CFE_UNDERLINE, v); }

static HRESULT STDMETHODCALLTYPE fo_getforecolor(FMT *f, LONG *v)
{
    CHARFORMAT2W cf;
    HRESULT hr;
    if (!v) return E_INVALIDARG;
    if (FAILED(hr = values(f, &cf))) return hr;
    *v = !(cf.cf.dwMask & CFM_COLOR) ? tomUndefined : (cf.cf.dwEffects & CFE_AUTOCOLOR) ? tomAutoColor_ : (LONG)cf.cf.crTextColor;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fo_setforecolor(FMT *f, LONG v)
{
    if (v == tomUndefined) return S_OK;
    if (v == tomAutoColor_) return put(f, CFM_COLOR, CFE_AUTOCOLOR, 0);
    return put(f, CFM_COLOR, 0, (COLORREF)v & 0xffffff);
}

/* the name and the size (XAML text input, part 9: each character's own,
 * an empty name and tomUndefined when the range's differ); the weight is
 * bold's */
static HRESULT STDMETHODCALLTYPE fo_getname(FMT *f, BSTR *v)
{
    CHARFORMAT2W cf;
    HRESULT hr;
    if (!v) return E_INVALIDARG;
    *v = 0;
    if (FAILED(hr = values(f, &cf))) return hr;
    cf.cf.szFaceName[31] = 0;
    *v = SysAllocString((cf.cf.dwMask & CFM_FACE) ? cf.cf.szFaceName : L"");
    return *v ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE fo_setname(FMT *f, BSTR v)
{
    CHARFORMATW cf;
    UINT i;
    if (!v || !v[0]) return E_INVALIDARG;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE;
    for (i = 0; i < 31 && v[i]; i++) cf.szFaceName[i] = v[i];
    return put_cf(f, &cf);
}
static HRESULT STDMETHODCALLTYPE fo_getsize(FMT *f, float *v)
{
    CHARFORMAT2W cf;
    HRESULT hr;
    if (!v) return E_INVALIDARG;
    if (FAILED(hr = values(f, &cf))) return hr;
    *v = (cf.cf.dwMask & CFM_SIZE) ? cf.cf.yHeight / 20.0f : (float)tomUndefined;   /* (twips: points) */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fo_setsize(FMT *f, float v)
{
    CHARFORMATW cf;
    if (v == (float)tomUndefined) return S_OK;
    if (!(v > 0) || v > 1638.0f) return E_INVALIDARG;   /* (Rich Edit's largest: 1638 points) */
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE;
    cf.yHeight = (LONG)(v * 20.0f + 0.5f);
    return put_cf(f, &cf);
}
static HRESULT STDMETHODCALLTYPE fo_getweight(FMT *f, LONG *v)
{
    CHARFORMAT2W cf;
    HRESULT hr;
    if (!v) return E_INVALIDARG;
    if (FAILED(hr = values(f, &cf))) return hr;
    *v = !(cf.cf.dwMask & CFM_BOLD) ? tomUndefined : (cf.cf.dwEffects & CFE_BOLD) ? FW_BOLD : FW_NORMAL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fo_setweight(FMT *f, LONG v)
{
    if (v == tomUndefined) return S_OK;
    return put(f, CFE_BOLD, v >= 600 ? CFE_BOLD : 0, 0);
}

/* a font of its own with this one's values now */
static HRESULT STDMETHODCALLTYPE fo_getdup(FMT *f, void **out)
{
    FMT *d;
    HRESULT hr;
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (FAILED(hr = tom_format(TRUE, 0, (void **)&d))) return hr;
    if (FAILED(hr = values(f, &d->cf))) { f_release(d); return hr; }
    d->cookie = f->cookie;
    *out = d;
    return S_OK;
}
/* this font takes @from's defined values (one of ours) */
static HRESULT STDMETHODCALLTYPE fo_setdup(FMT *f, FMT *from)
{
    CHARFORMAT2W cf;
    HRESULT hr;
    if (!from || !ours(from)) return E_INVALIDARG;
    if (FAILED(hr = values(from, &cf))) return hr;
    cf.cf.dwMask &= FMT_MASK;
    return put_cf(f, &cf.cf);
}
static HRESULT STDMETHODCALLTYPE fo_canchange(FMT *f, LONG *v)
{
    LONG a, b;
    TS *t = f->range ? range_of(f, &a, &b) : 0;
    if (v) *v = (t && (t->bits & TXTBIT_READONLY)) ? tomFalse_ : tomTrue;
    return (t && (t->bits & TXTBIT_READONLY)) ? S_FALSE : S_OK;
}
static BOOL same_name(const WCHAR *a, const WCHAR *b)
{
    int i;
    for (i = 0; i < 32 && a[i] == b[i]; i++) if (!a[i]) return TRUE;
    return i == 32;
}
static HRESULT STDMETHODCALLTYPE fo_isequal(FMT *f, FMT *other, LONG *v)
{
    CHARFORMAT2W x, y;
    BOOL same;
    if (v) *v = tomFalse_;
    if (!other || !ours(other)) return S_FALSE;
    if (FAILED(values(f, &x)) || FAILED(values(other, &y))) return S_FALSE;
    same = (x.cf.dwMask & FMT_MASK) == (y.cf.dwMask & FMT_MASK)
        && !((x.cf.dwEffects ^ y.cf.dwEffects) & x.cf.dwMask & (CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT | CFE_AUTOCOLOR))
        && (!(x.cf.dwMask & CFM_COLOR) || (x.cf.dwEffects & CFE_AUTOCOLOR) || x.cf.crTextColor == y.cf.crTextColor)
        && (!(x.cf.dwMask & CFM_SIZE) || x.cf.yHeight == y.cf.yHeight)
        && (!(x.cf.dwMask & CFM_FACE) || same_name(x.cf.szFaceName, y.cf.szFaceName));
    if (v) *v = same ? tomTrue : tomFalse_;
    return same ? S_OK : S_FALSE;
}

HRESULT tom_set_font(ITextRange2 *range, void *font)
{
    FMT *live;
    HRESULT hr;
    if (!font || !ours(font)) return E_INVALIDARG;
    if (FAILED(hr = tom_format(TRUE, range, (void **)&live))) return hr;
    hr = fo_setdup(live, font);
    f_release(live);
    return hr;
}

/* ITextFont2's cookie */
static HRESULT STDMETHODCALLTYPE c_get(FMT *f, LONG *v) { if (!v) return E_INVALIDARG; *v = f->cookie; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_set(FMT *f, LONG v) { f->cookie = v; return S_OK; }

/* ---- the paragraph properties: the alignment -------------------------- */
static BOOL ours_para(const FMT *p) { return p && p->lpVtbl[0] == (const void *)f_qi && !p->font; }

/* tomAlignLeft 0, Center 1, Right 2, Justify 3 from and to PFA_LEFT 1,
 * PFA_RIGHT 2, PFA_CENTER 3, PFA_JUSTIFY 4 */
static LONG tom_align(int pfa) { static const LONG m[] = { tomUndefined, 0, 2, 1, 3 }; return pfa >= 1 && pfa <= 4 ? m[pfa] : tomUndefined; }
static int pfa_align(LONG tom) { static const int m[] = { PFA_LEFT, PFA_CENTER, PFA_RIGHT, PFA_JUSTIFY }; return tom >= 0 && tom <= 3 ? m[tom] : 0; }
static LONG para_value(FMT *f)
{
    LONG a, b;
    TS *t;
    if (!f->range) return tom_align(f->align);
    if (!(t = range_of(f, &a, &b))) return tomUndefined;
    return tom_align(para_query(t, a, b));
}
static HRESULT para_put(FMT *f, LONG v)
{
    LONG a, b;
    TS *t;
    if (v == tomUndefined) return S_OK;
    if (v < 0 || v > 3) return E_INVALIDARG;
    if (!f->range) { f->align = pfa_align(v); return S_OK; }
    if (!(t = range_of(f, &a, &b))) return CO_E_RELEASED_;
    if (t->bits & TXTBIT_READONLY) return E_ACCESSDENIED;
    return para_apply(t, a, b, pfa_align(v)) ? S_OK : E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE p_getalign(FMT *f, LONG *v)
{
    if (!v) return E_INVALIDARG;
    if (f->range && !tom_range_ts(f->range)) return CO_E_RELEASED_;
    *v = para_value(f);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE p_setalign(FMT *f, LONG v) { return para_put(f, v); }
/* ---- the paragraph properties: the indents (XAML text input, part 8) ---
 * TOM's are in points: the left indent is the lines' after the first, the
 * first line's is from it, as PARAFORMAT's start indent and offset are the
 * other way round (start = left + first, offset = -first) */
static DWORD ind_value(FMT *f, LONG v[3])
{
    LONG a, b;
    TS *t;
    if (!f->range) { memcpy(v, f->ind, sizeof(f->ind)); return f->indset; }
    if (!(t = range_of(f, &a, &b))) { memset(v, 0, 3 * sizeof(LONG)); return 0; }
    return para_query_ind(t, a, b, v);
}
static HRESULT ind_put(FMT *f, DWORD set, const LONG v[3])
{
    LONG a, b;
    TS *t;
    int i;
    if (!set) return S_OK;
    if (!f->range) {
        for (i = 0; i < 3; i++) if (set & (1u << i)) f->ind[i] = v[i];
        f->indset |= set;
        return S_OK;
    }
    if (!(t = range_of(f, &a, &b))) return CO_E_RELEASED_;
    if (t->bits & TXTBIT_READONLY) return E_ACCESSDENIED;
    return para_apply_ind(t, a, b, set, v) ? S_OK : E_INVALIDARG;
}
static float points(LONG twips) { return (float)twips / 20.0f; }
static LONG twips(float pt) { return (LONG)(pt * 20.0f + (pt < 0 ? -0.5f : 0.5f)); }
#define tomUndefinedF ((float)tomUndefined)
#define IS_(set, i) (((set) >> (i)) & 1)

static HRESULT STDMETHODCALLTYPE p_getfirst(FMT *f, float *v)
{
    LONG x[3];
    DWORD set;
    if (!v) return E_INVALIDARG;
    if (f->range && !tom_range_ts(f->range)) return CO_E_RELEASED_;
    set = ind_value(f, x);
    *v = IS_(set, IND_OFFSET) ? points(-x[IND_OFFSET]) : tomUndefinedF;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE p_getleft(FMT *f, float *v)
{
    LONG x[3];
    DWORD set;
    if (!v) return E_INVALIDARG;
    if (f->range && !tom_range_ts(f->range)) return CO_E_RELEASED_;
    set = ind_value(f, x);
    *v = IS_(set, IND_START) && IS_(set, IND_OFFSET) ? points(x[IND_START] + x[IND_OFFSET]) : tomUndefinedF;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE p_getright(FMT *f, float *v)
{
    LONG x[3];
    DWORD set;
    if (!v) return E_INVALIDARG;
    if (f->range && !tom_range_ts(f->range)) return CO_E_RELEASED_;
    set = ind_value(f, x);
    *v = IS_(set, IND_RIGHT) ? points(x[IND_RIGHT]) : tomUndefinedF;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE p_setright(FMT *f, float v)
{
    LONG x[3] = { 0, 0, 0 };
    if (v == tomUndefinedF) return S_OK;
    x[IND_RIGHT] = twips(v);
    return ind_put(f, 1u << IND_RIGHT, x);
}
/* SetIndents(first, left, right): tomUndefined leaves one as it is */
static HRESULT STDMETHODCALLTYPE p_setindents(FMT *f, float first, float left, float right)
{
    LONG cur[3], x[3] = { 0, 0, 0 };
    DWORD set = 0, have;
    if (f->range && !tom_range_ts(f->range)) return CO_E_RELEASED_;
    have = ind_value(f, cur);
    if (first != tomUndefinedF || left != tomUndefinedF) {
        LONG fi = first != tomUndefinedF ? twips(first) : (IS_(have, IND_OFFSET) ? -cur[IND_OFFSET] : 0);
        LONG le = left != tomUndefinedF ? twips(left)
                : (IS_(have, IND_START) && IS_(have, IND_OFFSET) ? cur[IND_START] + cur[IND_OFFSET] : 0);
        x[IND_START] = le + fi;
        x[IND_OFFSET] = -fi;
        set |= (1u << IND_START) | (1u << IND_OFFSET);
    }
    if (right != tomUndefinedF) { x[IND_RIGHT] = twips(right); set |= 1u << IND_RIGHT; }
    return ind_put(f, set, x);
}

static HRESULT STDMETHODCALLTYPE p_getdup(FMT *f, void **out)
{
    FMT *d;
    HRESULT hr;
    LONG v;
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (f->range && !tom_range_ts(f->range)) return CO_E_RELEASED_;
    if (FAILED(hr = tom_format(FALSE, 0, (void **)&d))) return hr;
    v = para_value(f);
    d->align = pfa_align(v);
    d->indset = ind_value(f, d->ind);
    *out = d;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE p_setdup(FMT *f, FMT *from)
{
    LONG x[3];
    DWORD set;
    HRESULT hr;
    if (!from || !ours_para(from)) return E_INVALIDARG;
    set = ind_value(from, x);
    if (FAILED(hr = para_put(f, para_value(from)))) return hr;
    return ind_put(f, set, x);
}
static HRESULT STDMETHODCALLTYPE p_isequal(FMT *f, FMT *other, LONG *v)
{
    LONG x[3], y[3];
    BOOL same = other && ours_para(other) && para_value(f) == para_value(other)
                && ind_value(f, x) == ind_value(other, y) && !memcmp(x, y, sizeof(x));
    if (v) *v = same ? tomTrue : tomFalse_;
    return same ? S_OK : S_FALSE;
}

HRESULT tom_set_para(ITextRange2 *range, void *para)
{
    FMT *live;
    HRESULT hr;
    if (!para || !ours_para(para)) return E_INVALIDARG;
    if (FAILED(hr = tom_format(FALSE, range, (void **)&live))) return hr;
    hr = p_setdup(live, para);
    f_release(live);
    return hr;
}

#define N1x2 n1, n1
#define N1x4 N1x2, N1x2
#define N1x8 N1x4, N1x4
#define N1x16 N1x8, N1x8

/* ITextPara: 48 methods (GetDuplicate .. GetTab), then ITextPara2's 16 */
static const void *const para_vtbl[] = {
    f_qi, f_addref, f_release, f_tcount, f_tinfo, f_ids, f_invoke,
    p_getdup, p_setdup, fo_canchange, p_isequal, n1,   /* GetDuplicate, SetDuplicate, CanChange, IsEqual, Reset */
    n1, n1, p_getalign, p_setalign,               /* Get/SetStyle, Get/SetAlignment */
    n1, n1, p_getfirst,                           /* Get/SetHyphenation, GetFirstLineIndent */
    N1x4, p_getleft,                              /* Get/SetKeepTogether, Get/SetKeepWithNext, GetLeftIndent */
    N1x16,                                        /* GetLineSpacing .. SetPageBreakBefore (16) */
    p_getright, p_setright,                       /* Get/SetRightIndent */
    p_setindents, n2,                             /* SetIndents, SetLineSpacing */
    N1x4, n1, n1, n1,                             /* GetSpaceAfter .. GetTabCount (7) */
    n3, n0, n1, n4,                               /* AddTab, ClearAllTabs, DeleteTab, GetTab */
    /* ITextPara2 */
    N1x8, n1, n1, n1,                             /* GetBorders .. SetTrimPunctuationAtStart (11) */
    p_geteffects, n2, n2, p_seteffects, n2,       /* GetEffects, GetProperty, IsEqual2, SetEffects, SetProperty */
};

/* ITextFont: 55 methods (GetDuplicate .. SetWeight), then ITextFont2's 46 */
static const void *const font_vtbl[] = {
    f_qi, f_addref, f_release, f_tcount, f_tinfo, f_ids, f_invoke,
    fo_getdup, fo_setdup, fo_canchange, fo_isequal, n1,   /* GetDuplicate, SetDuplicate, CanChange, IsEqual, Reset */
    n1, n1, n1, n1, n1, n1, n1, n1,               /* GetStyle, SetStyle, Get/SetAllCaps, Get/SetAnimation, Get/SetBackColor */
    fo_getbold, fo_setbold, n1, n1,               /* Get/SetBold, Get/SetEmboss */
    fo_getforecolor, fo_setforecolor, n1, n1,     /* Get/SetForeColor, Get/SetHidden */
    n1, n1, fo_getitalic, fo_setitalic,           /* Get/SetEngrave, Get/SetItalic */
    n1, n1, n1, n1,                               /* Get/SetKerning, Get/SetLanguageID */
    fo_getname, fo_setname, n1, n1,               /* Get/SetName, Get/SetOutline */
    n1, n1, n1, n1, n1, n1,                       /* Get/SetPosition, Get/SetProtected, Get/SetShadow */
    fo_getsize, fo_setsize, n1, n1, n1, n1,       /* Get/SetSize, Get/SetSmallCaps, Get/SetSpacing */
    fo_getstrike, fo_setstrike, n1, n1, n1, n1,   /* Get/SetStrikeThrough, Get/SetSubscript, Get/SetSuperscript */
    fo_getunderline, fo_setunderline, fo_getweight, fo_setweight,   /* Get/SetUnderline, Get/SetWeight */
    /* ITextFont2 */
    N1x8, n1, n1, n1, n1, n1,                     /* GetCount .. SetCompressionMode (13) */
    c_get, c_set,                                 /* GetCookie, SetCookie */
    N1x16, N1x4, n1, n1, n1,                      /* GetDoubleStrike .. SetUnderlinePositionMode (23) */
    n2, n2, n2, n3, n2, n2, n2, n2,               /* GetEffects .. SetProperty */
};

_Static_assert(sizeof(para_vtbl) / sizeof(para_vtbl[0]) == 7 + 48 + 16, "ITextPara2's methods");
_Static_assert(sizeof(font_vtbl) / sizeof(font_vtbl[0]) == 7 + 55 + 46, "ITextFont2's methods");

HRESULT tom_format(BOOL font, ITextRange2 *range, void **out)
{
    FMT *f;
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!(f = alloc(sizeof(FMT)))) return E_OUTOFMEMORY;
    f->lpVtbl = font ? font_vtbl : para_vtbl;
    f->ref = 1;
    f->font = font;
    if (range) {
        f->range = range;
        range->lpVtbl->AddRef(range);
    }
    *out = f;
    return S_OK;
}
