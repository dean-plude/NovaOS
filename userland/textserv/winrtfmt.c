/*
 * winrtfmt.c — Microsoft.UI.Text's ITextCharacterFormat, the
 * CharacterFormat of a RichEditBox's ranges and selection
 * (Document.Selection.CharacterFormat.Bold = FormatEffect.On): a thin
 * Windows Runtime face on the Text Object Model's ITextFont2 (tomfmt.c),
 * so a range's format is live as TOM's is and GetClone is TOM's
 * GetDuplicate.  Bold, italic, underline, strikethrough, the foreground
 * colour, the weight and the font style are the range's; the name and the
 * size are read; the effects not kept (all caps, hidden, outline, small
 * caps, subscript, superscript, protected) read Off and setting them does
 * nothing, as do kerning, spacing, position, the language and the
 * background colour.  A range's ParagraphFormat (ITextParagraphFormat) is
 * the same over TOM's ITextPara2: its alignment is the range's paragraphs'
 * (Left, Center, Right, Justify) and so are its indents (first line, left,
 * right: XAML text input, part 8); spacing, lists and tabs read 0 or Off
 * and setting them does nothing.  Written from the Windows App SDK's Microsoft.UI.Text
 * documentation; the methods in Microsoft.UI.Text.winmd's order.
 */
#include "tsint.h"

typedef HRESULT (STDMETHODCALLTYPE *VFN)(void);
typedef struct { BYTE A, R, G, B; } WCOLOR;        /* Windows.UI.Color */

static const IID iid_unk = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID iid_insp = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const IID iid_agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const IID iid_wcf = {0xf5710050, 0x98e5, 0x5788, {0xb1, 0xe3, 0x32, 0x19, 0x1e, 0xeb, 0xf9, 0x4d}};

typedef struct WCF {
    const VFN *vtbl;
    LONG ref;
    IUnknown *tom;                                  /* the TOM font (ITextFont2) */
} WCF;
#define W(p) ((WCF *)(p))

/* the TOM font's methods, by slot (IUnknown's and IDispatch's seven, then ITextFont's) */
#define FONT(me, i, type) ((type)((*(void ***)W(me)->tom)[7 + (i)]))
#define F_GETDUP 0
#define F_SETDUP 1
#define F_ISEQUAL 3
#define F_GETBOLD 13
#define F_GETFORE 17
#define F_GETITALIC 23
#define F_GETNAME 29
#define F_GETSIZE 39
#define F_GETSTRIKE 45
#define F_GETUNDERLINE 51
#define F_GETWEIGHT 53
#define tomFalse_ 0
#define tomAutoColor_ (-9999997)
typedef HRESULT (STDMETHODCALLTYPE *GETL)(IUnknown *, LONG *);
typedef HRESULT (STDMETHODCALLTYPE *SETL)(IUnknown *, LONG);


static HRESULT STDMETHODCALLTYPE c_qi(void *me, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (!IsEqualGUID(iid, &iid_unk) && !IsEqualGUID(iid, &iid_insp) && !IsEqualGUID(iid, &iid_agile) &&
        !IsEqualGUID(iid, &iid_wcf)) return E_NOINTERFACE;
    InterlockedIncrement(&W(me)->ref);
    *out = me;
    return S_OK;
}
static ULONG STDMETHODCALLTYPE c_addref(void *me) { return (ULONG)InterlockedIncrement(&W(me)->ref); }
static ULONG STDMETHODCALLTYPE c_release(void *me)
{
    LONG n = InterlockedDecrement(&W(me)->ref);
    if (!n) {
        W(me)->tom->lpVtbl->Release(W(me)->tom);
        release(me);
    }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE c_iids(void *me, ULONG *n, IID **iids) { (void)me; if (n) *n = 0; if (iids) *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_name(void *me, HSTRING *out)
{
    static const WCHAR name[] = L"Microsoft.UI.Text.RichEditTextCharacterFormat";
    (void)me;
    return make_hstring(name, sizeof(name) / sizeof(WCHAR) - 1, out);
}
static HRESULT STDMETHODCALLTYPE c_trust(void *me, INT32 *level) { (void)me; if (level) *level = 0; return S_OK; }

/* ---- FormatEffect (Off 0, On 1, Toggle 2, Undefined 3) over TOM's values ---- */
static HRESULT get_fx(void *me, int slot, INT32 *value)
{
    LONG v = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = FONT(me, slot, GETL)(W(me)->tom, &v);
    *value = FAILED(hr) || v == tomUndefined ? 3 : v == tomFalse_ ? 0 : 1;
    return hr;
}
static HRESULT put_fx(void *me, int slot, INT32 value)
{
    LONG v = value == 0 ? tomFalse_ : value == 1 ? tomTrue : value == 2 ? tomToggle : tomUndefined;
    return FONT(me, slot + 1, SETL)(W(me)->tom, v);
}
/* (the effects not kept: Off, and setting them does nothing) */
static HRESULT STDMETHODCALLTYPE c_get_off(void *me, INT32 *value) { (void)me; if (!value) return E_POINTER; *value = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_put_none(void *me, INT32 value) { (void)me; (void)value; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_get_zero(void *me, float *value) { (void)me; if (!value) return E_POINTER; *value = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_put_float(void *me, float value) { (void)me; (void)value; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_get_empty(void *me, HSTRING *value) { (void)me; if (!value) return E_POINTER; *value = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_put_string(void *me, HSTRING value) { (void)me; (void)value; return S_OK; }

static HRESULT STDMETHODCALLTYPE c_get_Bold(void *me, INT32 *v) { return get_fx(me, F_GETBOLD, v); }
static HRESULT STDMETHODCALLTYPE c_put_Bold(void *me, INT32 v) { return put_fx(me, F_GETBOLD, v); }
static HRESULT STDMETHODCALLTYPE c_get_Italic(void *me, INT32 *v) { return get_fx(me, F_GETITALIC, v); }
static HRESULT STDMETHODCALLTYPE c_put_Italic(void *me, INT32 v) { return put_fx(me, F_GETITALIC, v); }
static HRESULT STDMETHODCALLTYPE c_get_Strikethrough(void *me, INT32 *v) { return get_fx(me, F_GETSTRIKE, v); }
static HRESULT STDMETHODCALLTYPE c_put_Strikethrough(void *me, INT32 v) { return put_fx(me, F_GETSTRIKE, v); }

/* BackgroundColor: none (transparent), not kept */
static HRESULT STDMETHODCALLTYPE c_get_BackgroundColor(void *me, WCOLOR *value)
{
    (void)me;
    if (!value) return E_POINTER;
    memset(value, 0, sizeof(*value));
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_put_BackgroundColor(void *me, WCOLOR value) { (void)me; (void)value; return S_OK; }

/* FontStretch: Normal (5); FontStyle: Normal 0, Italic 2 */
static HRESULT STDMETHODCALLTYPE c_get_FontStretch(void *me, INT32 *value) { (void)me; if (!value) return E_POINTER; *value = 5; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_get_FontStyle(void *me, INT32 *value)
{
    LONG v = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = FONT(me, F_GETITALIC, GETL)(W(me)->tom, &v);
    *value = SUCCEEDED(hr) && v != tomFalse_ && v != tomUndefined ? 2 : 0;
    return hr;
}
static HRESULT STDMETHODCALLTYPE c_put_FontStyle(void *me, INT32 value)
{
    return FONT(me, F_GETITALIC + 1, SETL)(W(me)->tom, value ? tomTrue : tomFalse_);
}

/* ForegroundColor: TOM's colour (the window text's when automatic) */
static HRESULT STDMETHODCALLTYPE c_get_ForegroundColor(void *me, WCOLOR *value)
{
    LONG v = 0;
    COLORREF c;
    HRESULT hr;
    if (!value) return E_POINTER;
    memset(value, 0, sizeof(*value));
    hr = FONT(me, F_GETFORE, GETL)(W(me)->tom, &v);
    if (FAILED(hr) || v == tomUndefined) return hr;
    c = v == tomAutoColor_ ? GetSysColor(COLOR_WINDOWTEXT) : (COLORREF)v;
    value->A = 255;
    value->R = GetRValue(c);
    value->G = GetGValue(c);
    value->B = GetBValue(c);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_put_ForegroundColor(void *me, WCOLOR value)
{
    return FONT(me, F_GETFORE + 1, SETL)(W(me)->tom, (LONG)RGB(value.R, value.G, value.B));
}

static HRESULT STDMETHODCALLTYPE c_get_LinkType(void *me, INT32 *value) { (void)me; if (!value) return E_POINTER; *value = 1; return S_OK; }   /* (NotALink) */

static HRESULT STDMETHODCALLTYPE c_get_Name(void *me, HSTRING *value)
{
    BSTR b = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = 0;
    if (FAILED(hr = FONT(me, F_GETNAME, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, BSTR *))(W(me)->tom, &b))) return hr;
    hr = make_hstring(b ? b : L"", b ? SysStringLen(b) : 0, value);
    SysFreeString(b);
    return hr;
}
static HRESULT STDMETHODCALLTYPE c_get_Size(void *me, float *value)
{
    if (!value) return E_POINTER;
    return FONT(me, F_GETSIZE, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, float *))(W(me)->tom, value);
}

/* Underline: UnderlineType (Undefined 0, None 1, Single 2, the others drawn as Single) */
static HRESULT STDMETHODCALLTYPE c_get_Underline(void *me, INT32 *value)
{
    LONG v = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = FONT(me, F_GETUNDERLINE, GETL)(W(me)->tom, &v);
    *value = FAILED(hr) || v == tomUndefined ? 0 : v == tomFalse_ ? 1 : 2;
    return hr;
}
static HRESULT STDMETHODCALLTYPE c_put_Underline(void *me, INT32 value)
{
    if (!value) return S_OK;
    return FONT(me, F_GETUNDERLINE + 1, SETL)(W(me)->tom, value == 1 ? tomFalse_ : 1 /* tomSingle */);
}

static HRESULT STDMETHODCALLTYPE c_get_Weight(void *me, INT32 *value)
{
    LONG v = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = FONT(me, F_GETWEIGHT, GETL)(W(me)->tom, &v);
    *value = v == tomUndefined ? FW_NORMAL : v;
    return hr;
}
static HRESULT STDMETHODCALLTYPE c_put_Weight(void *me, INT32 value)
{
    return FONT(me, F_GETWEIGHT + 1, SETL)(W(me)->tom, value);
}

/* SetClone, GetClone, IsEqual: TOM's SetDuplicate, GetDuplicate, IsEqual */
static HRESULT STDMETHODCALLTYPE c_SetClone(void *me, void *value)
{
    IUnknown *from = wfmt_tom(value);
    if (!from) return E_INVALIDARG;
    return FONT(me, F_SETDUP, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, IUnknown *))(W(me)->tom, from);
}
static HRESULT STDMETHODCALLTYPE c_GetClone(void *me, void **value)
{
    IUnknown *dup = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = 0;
    if (FAILED(hr = FONT(me, F_GETDUP, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, IUnknown **))(W(me)->tom, &dup))) return hr;
    hr = wfmt_wrap(dup, value);
    dup->lpVtbl->Release(dup);
    return hr;
}
static HRESULT STDMETHODCALLTYPE c_IsEqual(void *me, void *format, BYTE *result)
{
    IUnknown *other = wfmt_tom(format);
    LONG v = 0;
    if (!result) return E_POINTER;
    *result = 0;
    if (!other) return S_OK;
    FONT(me, F_ISEQUAL, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, LONG *))(W(me)->tom, other, &v);
    *result = v == tomTrue;
    return S_OK;
}

/* Microsoft.UI.Text.ITextCharacterFormat {f5710050-98e5-5788-b1e3-32191eebf94d} */
static const VFN wcf_vtbl[] = {
    (VFN)c_qi, (VFN)c_addref, (VFN)c_release, (VFN)c_iids, (VFN)c_name, (VFN)c_trust,
    (VFN)c_get_off, (VFN)c_put_none,                       /* AllCaps */
    (VFN)c_get_BackgroundColor, (VFN)c_put_BackgroundColor,
    (VFN)c_get_Bold, (VFN)c_put_Bold,
    (VFN)c_get_FontStretch, (VFN)c_put_none,               /* FontStretch */
    (VFN)c_get_FontStyle, (VFN)c_put_FontStyle,
    (VFN)c_get_ForegroundColor, (VFN)c_put_ForegroundColor,
    (VFN)c_get_off, (VFN)c_put_none,                       /* Hidden */
    (VFN)c_get_Italic, (VFN)c_put_Italic,
    (VFN)c_get_zero, (VFN)c_put_float,                     /* Kerning */
    (VFN)c_get_empty, (VFN)c_put_string,                   /* LanguageTag */
    (VFN)c_get_LinkType,
    (VFN)c_get_Name, (VFN)c_put_string,                    /* Name (the default's) */
    (VFN)c_get_off, (VFN)c_put_none,                       /* Outline */
    (VFN)c_get_zero, (VFN)c_put_float,                     /* Position */
    (VFN)c_get_off, (VFN)c_put_none,                       /* ProtectedText */
    (VFN)c_get_Size, (VFN)c_put_float,                     /* Size (the default's) */
    (VFN)c_get_off, (VFN)c_put_none,                       /* SmallCaps */
    (VFN)c_get_zero, (VFN)c_put_float,                     /* Spacing */
    (VFN)c_get_Strikethrough, (VFN)c_put_Strikethrough,
    (VFN)c_get_off, (VFN)c_put_none,                       /* Subscript */
    (VFN)c_get_off, (VFN)c_put_none,                       /* Superscript */
    (VFN)c_get_off, (VFN)c_put_none,                       /* TextScript (Undefined) */
    (VFN)c_get_Underline, (VFN)c_put_Underline,
    (VFN)c_get_Weight, (VFN)c_put_Weight,
    (VFN)c_SetClone, (VFN)c_GetClone, (VFN)c_IsEqual,
};
_Static_assert(sizeof(wcf_vtbl) / sizeof(wcf_vtbl[0]) == 6 + 50, "ITextCharacterFormat's methods");

HRESULT wfmt_wrap(void *tomfont, void **out)
{
    WCF *w;
    if (!out) return E_POINTER;
    *out = 0;
    if (!tomfont) return E_INVALIDARG;
    if (!(w = alloc(sizeof(WCF)))) return E_OUTOFMEMORY;
    w->vtbl = wcf_vtbl;
    w->ref = 1;
    w->tom = tomfont;
    w->tom->lpVtbl->AddRef(w->tom);
    *out = w;
    return S_OK;
}

void *wfmt_tom(void *format)
{
    return (format && W(format)->vtbl == wcf_vtbl) ? W(format)->tom : 0;
}

/* ---- Microsoft.UI.Text.ITextParagraphFormat {219b6cdf-0d0b-5701-b8a1-6c906b3ebbe1} ---- */
static const IID iid_wpf = {0x219b6cdf, 0x0d0b, 0x5701, {0xb8, 0xa1, 0x6c, 0x90, 0x6b, 0x3e, 0xbb, 0xe1}};

/* the TOM paragraph's methods, by slot after IDispatch's */
#define PARA(me, i, type) ((type)((*(void ***)W(me)->tom)[7 + (i)]))
#define P_GETDUP 0
#define P_SETDUP 1
#define P_ISEQUAL 3
#define P_GETALIGN 7
#define P_GETFIRST 11
#define P_GETLEFT 16
#define P_GETRIGHT 33
#define P_SETRIGHT 34
#define P_SETINDENTS 35
typedef HRESULT (STDMETHODCALLTYPE *GETF)(IUnknown *, float *);

static HRESULT STDMETHODCALLTYPE p_qi(void *me, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (!IsEqualGUID(iid, &iid_unk) && !IsEqualGUID(iid, &iid_insp) && !IsEqualGUID(iid, &iid_agile) &&
        !IsEqualGUID(iid, &iid_wpf)) return E_NOINTERFACE;
    InterlockedIncrement(&W(me)->ref);
    *out = me;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE p_name(void *me, HSTRING *out)
{
    static const WCHAR name[] = L"Microsoft.UI.Text.RichEditTextParagraphFormat";
    (void)me;
    return make_hstring(name, sizeof(name) / sizeof(WCHAR) - 1, out);
}

/* Alignment: ParagraphAlignment (Undefined 0, Left 1, Center 2, Right 3,
 * Justify 4) over TOM's (tomAlignLeft 0 .. tomAlignJustify 3) */
static HRESULT STDMETHODCALLTYPE p_get_Alignment(void *me, INT32 *value)
{
    LONG v = tomUndefined;
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = PARA(me, P_GETALIGN, GETL)(W(me)->tom, &v);
    *value = FAILED(hr) || v < 0 || v > 3 ? 0 : v + 1;
    return hr;
}
static HRESULT STDMETHODCALLTYPE p_put_Alignment(void *me, INT32 value)
{
    if (value < 1 || value > 4) return value ? E_INVALIDARG : S_OK;
    return PARA(me, P_GETALIGN + 1, SETL)(W(me)->tom, value - 1);
}

/* (the properties not kept: 0 or Off, and setting them does nothing) */
static HRESULT STDMETHODCALLTYPE p_get_long(void *me, INT32 *value) { (void)me; if (!value) return E_POINTER; *value = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE p_get_one(void *me, INT32 *value) { (void)me; if (!value) return E_POINTER; *value = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE p_get_rule(void *me, INT32 *value) { (void)me; if (!value) return E_POINTER; *value = 0; return S_OK; }   /* (LineSpacingRule Undefined) */
static HRESULT STDMETHODCALLTYPE p_get_spacing(void *me, float *value) { (void)me; if (!value) return E_POINTER; *value = 1.0f; return S_OK; }  /* (single) */
static HRESULT STDMETHODCALLTYPE p_AddTab(void *me, float position, INT32 align, INT32 leader)
{ (void)me; (void)position; (void)align; (void)leader; return S_OK; }
static HRESULT STDMETHODCALLTYPE p_ClearAllTabs(void *me) { (void)me; return S_OK; }
static HRESULT STDMETHODCALLTYPE p_DeleteTab(void *me, float position) { (void)me; (void)position; return S_OK; }
static HRESULT STDMETHODCALLTYPE p_GetTab(void *me, INT32 index, float *position, INT32 *align, INT32 *leader)
{
    (void)me; (void)index;
    if (position) *position = 0;
    if (align) *align = 0;
    if (leader) *leader = 0;
    return E_INVALIDARG;                            /* (no tabs) */
}
/* the indents, in points, TOM's (undefined where the paragraphs differ: 0) */
static HRESULT get_indent(void *me, int slot, float *value)
{
    float v = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    hr = PARA(me, slot, GETF)(W(me)->tom, &v);
    *value = FAILED(hr) || v == (float)tomUndefined ? 0 : v;
    return hr;
}
static HRESULT STDMETHODCALLTYPE p_get_FirstLineIndent(void *me, float *value) { return get_indent(me, P_GETFIRST, value); }
static HRESULT STDMETHODCALLTYPE p_get_LeftIndent(void *me, float *value) { return get_indent(me, P_GETLEFT, value); }
static HRESULT STDMETHODCALLTYPE p_get_RightIndent(void *me, float *value) { return get_indent(me, P_GETRIGHT, value); }
static HRESULT STDMETHODCALLTYPE p_put_RightIndent(void *me, float value)
{
    return PARA(me, P_SETRIGHT, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, float))(W(me)->tom, value);
}
static HRESULT STDMETHODCALLTYPE p_SetIndents(void *me, float start, float left, float right)
{
    return PARA(me, P_SETINDENTS, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, float, float, float))(W(me)->tom, start, left, right);
}
static HRESULT STDMETHODCALLTYPE p_SetLineSpacing(void *me, INT32 rule, float spacing)
{ (void)me; (void)rule; (void)spacing; return S_OK; }

/* SetClone, GetClone, IsEqual: TOM's SetDuplicate, GetDuplicate, IsEqual */
static HRESULT STDMETHODCALLTYPE p_SetClone(void *me, void *value)
{
    IUnknown *from = wpara_tom(value);
    if (!from) return E_INVALIDARG;
    return PARA(me, P_SETDUP, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, IUnknown *))(W(me)->tom, from);
}
static HRESULT STDMETHODCALLTYPE p_GetClone(void *me, void **value)
{
    IUnknown *dup = 0;
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = 0;
    if (FAILED(hr = PARA(me, P_GETDUP, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, IUnknown **))(W(me)->tom, &dup))) return hr;
    hr = wpara_wrap(dup, value);
    dup->lpVtbl->Release(dup);
    return hr;
}
static HRESULT STDMETHODCALLTYPE p_IsEqual(void *me, void *format, BYTE *result)
{
    IUnknown *other = wpara_tom(format);
    LONG v = 0;
    if (!result) return E_POINTER;
    *result = 0;
    if (!other) return S_OK;
    PARA(me, P_ISEQUAL, HRESULT (STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, LONG *))(W(me)->tom, other, &v);
    *result = v == tomTrue;
    return S_OK;
}

static const VFN wpf_vtbl[] = {
    (VFN)p_qi, (VFN)c_addref, (VFN)c_release, (VFN)c_iids, (VFN)p_name, (VFN)c_trust,
    (VFN)p_get_Alignment, (VFN)p_put_Alignment,
    (VFN)p_get_FirstLineIndent,                            /* FirstLineIndent */
    (VFN)c_get_off, (VFN)c_put_none,                       /* KeepTogether */
    (VFN)c_get_off, (VFN)c_put_none,                       /* KeepWithNext */
    (VFN)p_get_LeftIndent,                                 /* LeftIndent */
    (VFN)p_get_spacing,                                    /* LineSpacing */
    (VFN)p_get_rule,                                       /* LineSpacingRule */
    (VFN)p_get_long, (VFN)c_put_none,                      /* ListAlignment */
    (VFN)p_get_long, (VFN)c_put_none,                      /* ListLevelIndex */
    (VFN)p_get_one, (VFN)c_put_none,                       /* ListStart */
    (VFN)p_get_long, (VFN)c_put_none,                      /* ListStyle */
    (VFN)c_get_zero, (VFN)c_put_float,                     /* ListTab */
    (VFN)p_get_long, (VFN)c_put_none,                      /* ListType (None) */
    (VFN)c_get_off, (VFN)c_put_none,                       /* NoLineNumber */
    (VFN)c_get_off, (VFN)c_put_none,                       /* PageBreakBefore */
    (VFN)p_get_RightIndent, (VFN)p_put_RightIndent,        /* RightIndent */
    (VFN)c_get_off, (VFN)c_put_none,                       /* RightToLeft */
    (VFN)p_get_long, (VFN)c_put_none,                      /* Style */
    (VFN)c_get_zero, (VFN)c_put_float,                     /* SpaceAfter */
    (VFN)c_get_zero, (VFN)c_put_float,                     /* SpaceBefore */
    (VFN)c_get_off, (VFN)c_put_none,                       /* WidowControl */
    (VFN)p_get_long,                                       /* TabCount */
    (VFN)p_AddTab, (VFN)p_ClearAllTabs, (VFN)p_DeleteTab, (VFN)p_GetClone, (VFN)p_GetTab,
    (VFN)p_IsEqual, (VFN)p_SetClone, (VFN)p_SetIndents, (VFN)p_SetLineSpacing,
};
_Static_assert(sizeof(wpf_vtbl) / sizeof(wpf_vtbl[0]) == 6 + 48, "ITextParagraphFormat's methods");

HRESULT wpara_wrap(void *tompara, void **out)
{
    WCF *w;
    if (!out) return E_POINTER;
    *out = 0;
    if (!tompara) return E_INVALIDARG;
    if (!(w = alloc(sizeof(WCF)))) return E_OUTOFMEMORY;
    w->vtbl = wpf_vtbl;
    w->ref = 1;
    w->tom = tompara;
    w->tom->lpVtbl->AddRef(w->tom);
    *out = w;
    return S_OK;
}

void *wpara_tom(void *format)
{
    return (format && W(format)->vtbl == wpf_vtbl) ? W(format)->tom : 0;
}
