/*
 * wintypes.dll — Windows.Foundation.PropertyValue: the Windows Runtime's
 * boxes for values.  IPropertyValueStatics makes a box of a number, a
 * character, a boolean, a string, a GUID, a date, a time span, a point, a
 * size or a rectangle, or an array of any of them (or of objects); the box
 * answers IPropertyValue (its type and getters) and the IReference<T> (an
 * array's IReferenceArray<T>) of its own type.  Microsoft's XAML framework
 * boxes every value it hands out this way (its value boxer,
 * components/valueboxer, through StaticStore's statics), and C++/WinRT's
 * box_value and C#'s boxing of Windows Runtime values do the same.
 *
 * As Windows' boxes do, a numeric getter converts between the numeric types
 * (UInt8 ... Double) when the value fits the type asked for
 * (DISP_E_OVERFLOW when it does not; a fraction does not convert to an
 * integer); any other getter takes only its own type, and an array getter
 * only its own array type (TYPE_E_TYPEMISMATCH otherwise).  An array
 * getter's result is the caller's (CoTaskMemAlloc; strings duplicated,
 * objects referenced).  CreateEmpty gives null and CreateInspectable the
 * object itself, as on Windows.  The boxes are agile (IAgileObject) and
 * never change.  Conversions from and to strings are not done.
 *
 * Programs reach the class through RoGetActivationFactory (the registry's
 * ActivatableClassId key, kernel/um/um_registry.c).
 */
#include <windows.h>
#include <roapi.h>
#include <stdint.h>
#include <string.h>

#define WT_API __declspec(dllexport)
int _fltused = 0x9875;                  /* (the boxes hold floating values; no C runtime here) */
typedef BYTE boolean_t;

#define TYPE_E_TYPEMISMATCH_ ((HRESULT)0x80028CA0L)
#define DISP_E_OVERFLOW_     ((HRESULT)0x8002000AL)

static const GUID IID_IUnknown_     = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_ = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_ = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IPropertyValue_ = { 0x4BD682DD, 0x7554, 0x40E9, { 0x9A, 0x9B, 0x82, 0x65, 0x4E, 0xDE, 0x7E, 0x62 } };
static const GUID IID_IPropertyValueStatics_ = { 0x629BDBC8, 0xD932, 0x4FF4, { 0x96, 0xB9, 0x8D, 0x96, 0xC5, 0xC1, 0xE8, 0x58 } };

/* Windows.Foundation.PropertyType: a scalar's code, an array's that plus 1024 */
enum { PT_EMPTY, PT_UINT8, PT_INT16, PT_UINT16, PT_INT32, PT_UINT32, PT_INT64, PT_UINT64, PT_SINGLE, PT_DOUBLE,
       PT_CHAR16, PT_BOOLEAN, PT_STRING, PT_INSPECTABLE, PT_DATETIME, PT_TIMESPAN, PT_GUID, PT_POINT, PT_SIZE,
       PT_RECT, PT_COUNT, PT_ARRAY = 1024 };

/* each type's element size and the IIDs of IReference<T> and
 * IReferenceArray<T> (the parameterized interfaces' IIDs, worked out from
 * their signatures as the Windows Runtime does) */
static const struct { UINT32 size; GUID ref, refarray; } g_types[PT_COUNT] = {
    [PT_UINT8] = { 1, { 0xE5198CC8, 0x2873, 0x55F5, { 0xB0, 0xA1, 0x84, 0xFF, 0x9E, 0x4A, 0xAD, 0x62 } },
                      { 0x2AF22683, 0x3734, 0x56D0, { 0xA6, 0x0E, 0x68, 0x8C, 0xC8, 0x5D, 0x16, 0x19 } } },
    [PT_INT16] = { 2, { 0x6EC9E41B, 0x6709, 0x5647, { 0x99, 0x18, 0xA1, 0x27, 0x01, 0x10, 0xFC, 0x4E } },
                      { 0x912F8FD7, 0xADC0, 0x5D60, { 0xA8, 0x96, 0x7E, 0xD7, 0x60, 0x89, 0xCC, 0x5B } } },
    [PT_UINT16] = { 2, { 0x5AB7D2C3, 0x6B62, 0x5E71, { 0xA4, 0xB6, 0x2D, 0x49, 0xC4, 0xF2, 0x38, 0xFD } },
                       { 0x6624A2DD, 0x83F7, 0x519C, { 0x9D, 0x55, 0xBB, 0x1F, 0x65, 0x60, 0x45, 0x6B } } },
    [PT_INT32] = { 4, { 0x548CEFBD, 0xBC8A, 0x5FA0, { 0x8D, 0xF2, 0x95, 0x74, 0x40, 0xFC, 0x8B, 0xF4 } },
                      { 0xA6D080A5, 0xB087, 0x5BC2, { 0x9A, 0x9F, 0x5C, 0xD6, 0x87, 0xB4, 0xD1, 0xF7 } } },
    [PT_UINT32] = { 4, { 0x513EF3AF, 0xE784, 0x5325, { 0xA9, 0x1E, 0x97, 0xC2, 0xB8, 0x11, 0x1C, 0xF3 } },
                       { 0x97374B68, 0xEB87, 0x56CC, { 0xB1, 0x8E, 0x27, 0xEF, 0x0F, 0x9C, 0xFC, 0x0C } } },
    [PT_INT64] = { 8, { 0x4DDA9E24, 0xE69F, 0x5C6A, { 0xA0, 0xA6, 0x93, 0x42, 0x73, 0x65, 0xAF, 0x2A } },
                      { 0x6E333271, 0x2E2A, 0x5955, { 0x87, 0x90, 0x83, 0x6C, 0x76, 0xEE, 0x53, 0xB6 } } },
    [PT_UINT64] = { 8, { 0x6755E376, 0x53BB, 0x568B, { 0xA1, 0x1D, 0x17, 0x23, 0x98, 0x68, 0x30, 0x9E } },
                       { 0x38B60434, 0xD67C, 0x523E, { 0x9D, 0x0E, 0x24, 0xD6, 0x43, 0x41, 0x10, 0x73 } } },
    [PT_SINGLE] = { 4, { 0x719CC2BA, 0x3E76, 0x5DEF, { 0x9F, 0x1A, 0x38, 0xD8, 0x5A, 0x14, 0x5E, 0xA8 } },
                       { 0x6AB1EA83, 0xCB41, 0x5F99, { 0x92, 0xCC, 0x23, 0xBD, 0x43, 0x36, 0xA1, 0xFB } } },
    [PT_DOUBLE] = { 8, { 0x2F2D6C29, 0x5473, 0x5F3E, { 0x92, 0xE7, 0x96, 0x57, 0x2B, 0xB9, 0x90, 0xE2 } },
                       { 0xD301F253, 0xE0A3, 0x5D2B, { 0x9A, 0x41, 0xA4, 0xD6, 0x2B, 0xEC, 0x46, 0x23 } } },
    [PT_CHAR16] = { 2, { 0xFB393EF3, 0xBBAC, 0x5BD5, { 0x91, 0x44, 0x84, 0xF2, 0x35, 0x76, 0xF4, 0x15 } },
                       { 0xA4095AAB, 0xEB7D, 0x5782, { 0x8F, 0xAD, 0x16, 0x09, 0xDE, 0xA2, 0x49, 0xAD } } },
    [PT_BOOLEAN] = { 1, { 0x3C00FD60, 0x2950, 0x5939, { 0xA2, 0x1A, 0x2D, 0x12, 0xC5, 0xA0, 0x1B, 0x8A } },
                        { 0xE8E72666, 0x48CC, 0x593F, { 0xBA, 0x85, 0x26, 0x63, 0x49, 0x69, 0x56, 0xE3 } } },
    [PT_STRING] = { sizeof(HSTRING), { 0xFD416DFB, 0x2A07, 0x52EB, { 0xAA, 0xE3, 0xDF, 0xCE, 0x14, 0x11, 0x6C, 0x05 } },
                    { 0x0385688E, 0xE3C7, 0x5C5E, { 0xA3, 0x89, 0x55, 0x24, 0xED, 0xE3, 0x49, 0xF1 } } },
    [PT_INSPECTABLE] = { sizeof(void *), { 0x06DCCC90, 0xA058, 0x5C88, { 0x87, 0xB7, 0x6F, 0x33, 0x60, 0xA2, 0xFC, 0x16 } },
                         { 0x9CD7A84F, 0x0C80, 0x59C5, { 0xB4, 0x4E, 0x97, 0x78, 0x41, 0xBB, 0x43, 0xD9 } } },
    [PT_DATETIME] = { 8, { 0x5541D8A7, 0x497C, 0x5AA4, { 0x86, 0xFC, 0x77, 0x13, 0xAD, 0xBF, 0x2A, 0x2C } },
                         { 0x1B8E9594, 0x588E, 0x5A07, { 0x9E, 0x65, 0x07, 0x31, 0xA4, 0xC9, 0xA2, 0xDB } } },
    [PT_TIMESPAN] = { 8, { 0x604D0C4C, 0x91DE, 0x5C2A, { 0x93, 0x5F, 0x36, 0x2F, 0x13, 0xEA, 0xF8, 0x00 } },
                         { 0xAD73197D, 0x2CFA, 0x57A6, { 0x89, 0x93, 0x9F, 0xAC, 0x40, 0xFE, 0xB7, 0x91 } } },
    [PT_GUID] = { 16, { 0x7D50F649, 0x632C, 0x51F9, { 0x84, 0x9A, 0xEE, 0x49, 0x42, 0x89, 0x33, 0xEA } },
                      { 0xEECF9838, 0xC1C2, 0x5B4A, { 0x97, 0x6F, 0xCE, 0xC2, 0x61, 0xAE, 0x1D, 0x55 } } },
    [PT_POINT] = { 8, { 0x84F14C22, 0xA00A, 0x5272, { 0x8D, 0x3D, 0x82, 0x11, 0x2E, 0x66, 0xDF, 0x00 } },
                      { 0x39313214, 0x5C7D, 0x599D, { 0xAE, 0x5A, 0x17, 0xD9, 0xD6, 0x49, 0x22, 0x58 } } },
    [PT_SIZE] = { 8, { 0x61723086, 0x8E53, 0x5276, { 0x9F, 0x36, 0x2A, 0x4B, 0xB9, 0x3E, 0x2B, 0x75 } },
                     { 0x3B40E9D4, 0xE0C3, 0x56F6, { 0xB8, 0x8B, 0xE5, 0x05, 0xEB, 0x73, 0x75, 0x7B } } },
    [PT_RECT] = { 16, { 0x80423F11, 0x054F, 0x5EAC, { 0xAF, 0xD3, 0x63, 0xB6, 0xCE, 0x15, 0xE7, 0x7B } },
                      { 0x8A444256, 0xD661, 0x5E9A, { 0xA7, 0x2B, 0xD8, 0xF1, 0xD7, 0x96, 0x2D, 0x0C } } },
};

typedef struct { INT64 v; } DateTime_;
typedef struct { INT64 v; } TimeSpan_;
typedef struct { FLOAT x, y; } Point_;
typedef struct { FLOAT w, h; } Size_;
typedef struct { FLOAT x, y, w, h; } Rect_;

/* A box: IPropertyValue first, then its IReference<T> or IReferenceArray<T> */
typedef struct {
    const void *const *vt_pv;
    const void *const *vt_ref;
    LONG refs;
    int type;                       /* PropertyType */
    UINT32 count;                   /* an array's elements */
    void *array;                    /* an array's elements (HeapAlloc) */
    union { BYTE b[16]; INT64 i64; UINT64 u64; double d; float f; HSTRING s; } v;
} Box;
#define BOX_OF_PV(p)  ((Box *)(p))
#define BOX_OF_REF(p) ((Box *)((BYTE *)(p) - offsetof(Box, vt_ref)))

static void *mem_alloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1); }
static void mem_free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
static ULONG obj_addref(void *o) { return ((ULONG (STDMETHODCALLTYPE *)(void *))((void **)*(void **)o)[1])(o); }
static ULONG obj_release(void *o) { return ((ULONG (STDMETHODCALLTYPE *)(void *))((void **)*(void **)o)[2])(o); }

static int elem_type(const Box *b) { return b->type & ~PT_ARRAY; }
static BOOL is_array(const Box *b) { return (b->type & PT_ARRAY) != 0; }

/* ---- IUnknown / IInspectable, for both interfaces ---- */
static HRESULT box_qi(Box *b, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IPropertyValue_))
        *out = &b->vt_pv;
    else if (IsEqualGUID(iid, is_array(b) ? &g_types[elem_type(b)].refarray : &g_types[elem_type(b)].ref))
        *out = &b->vt_ref;
    else {
        *out = 0;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&b->refs);
    return S_OK;
}
static ULONG box_release(Box *b)
{
    LONG n = InterlockedDecrement(&b->refs);
    if (n) return (ULONG)n;
    int t = elem_type(b);
    if (is_array(b)) {
        for (UINT32 i = 0; i < b->count; i++) {
            if (t == PT_STRING) WindowsDeleteString(((HSTRING *)b->array)[i]);
            else if (t == PT_INSPECTABLE && ((void **)b->array)[i]) obj_release(((void **)b->array)[i]);
        }
        mem_free(b->array);
    } else if (t == PT_STRING)
        WindowsDeleteString(b->v.s);
    mem_free(b);
    return 0;
}
static HRESULT STDMETHODCALLTYPE no_iids(void *o, ULONG *n, GUID **iids) { (void)o; if (!n || !iids) return E_POINTER; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *o, int *t) { (void)o; if (!t) return E_POINTER; *t = BaseTrust; return S_OK; }
/* A box's class name is Windows': its IReference<T> or IReferenceArray<T>
 * by T's name ("Windows.Foundation.IReference`1<Double>"), which .NET
 * Native's runtime reads to unbox it as T (an empty box: IPropertyValue) */
static const WCHAR *const c_type_names[PT_COUNT] = {
    [PT_UINT8] = L"UInt8", [PT_INT16] = L"Int16", [PT_UINT16] = L"UInt16", [PT_INT32] = L"Int32",
    [PT_UINT32] = L"UInt32", [PT_INT64] = L"Int64", [PT_UINT64] = L"UInt64", [PT_SINGLE] = L"Single",
    [PT_DOUBLE] = L"Double", [PT_CHAR16] = L"Char16", [PT_BOOLEAN] = L"Boolean", [PT_STRING] = L"String",
    [PT_INSPECTABLE] = L"Object", [PT_DATETIME] = L"Windows.Foundation.DateTime",
    [PT_TIMESPAN] = L"Windows.Foundation.TimeSpan", [PT_GUID] = L"Guid", [PT_POINT] = L"Windows.Foundation.Point",
    [PT_SIZE] = L"Windows.Foundation.Size", [PT_RECT] = L"Windows.Foundation.Rect",
};
static HRESULT box_class(const Box *b, HSTRING *s)
{
    if (!s) return E_POINTER;
    int t = elem_type(b);
    const WCHAR *tn = t > PT_EMPTY && t < PT_COUNT ? c_type_names[t] : 0;
    WCHAR name[96];
    UINT32 n = 0;
    const WCHAR *parts[3] = { tn ? (is_array(b) ? L"Windows.Foundation.IReferenceArray`1<" : L"Windows.Foundation.IReference`1<")
                                 : L"Windows.Foundation.IPropertyValue",
                              tn ? tn : L"", tn ? L">" : L"" };
    for (int i = 0; i < 3; i++)
        for (const WCHAR *c = parts[i]; *c && n < 95; c++) name[n++] = *c;
    return WindowsCreateString(name, n, s);
}
static HRESULT STDMETHODCALLTYPE pv_class(void *o, HSTRING *s) { return box_class(BOX_OF_PV(o), s); }
static HRESULT STDMETHODCALLTYPE ref_class(void *o, HSTRING *s) { return box_class(BOX_OF_REF(o), s); }

static HRESULT STDMETHODCALLTYPE pv_qi(void *o, REFIID iid, void **out) { return box_qi(BOX_OF_PV(o), iid, out); }
static ULONG STDMETHODCALLTYPE pv_addref(void *o) { return (ULONG)InterlockedIncrement(&BOX_OF_PV(o)->refs); }
static ULONG STDMETHODCALLTYPE pv_release(void *o) { return box_release(BOX_OF_PV(o)); }
static HRESULT STDMETHODCALLTYPE ref_qi(void *o, REFIID iid, void **out) { return box_qi(BOX_OF_REF(o), iid, out); }
static ULONG STDMETHODCALLTYPE ref_addref(void *o) { return (ULONG)InterlockedIncrement(&BOX_OF_REF(o)->refs); }
static ULONG STDMETHODCALLTYPE ref_release(void *o) { return box_release(BOX_OF_REF(o)); }

/* ---- the numeric conversions ----
 * A numeric scalar read as a signed, an unsigned or a floating value */
typedef struct { int kind; INT64 i; UINT64 u; double d; } Num;   /* kind: 0 signed, 1 unsigned, 2 floating */
static BOOL is_numeric(int t) { return t >= PT_UINT8 && t <= PT_DOUBLE; }
static Num num_of(const Box *b)
{
    Num n = { 0, 0, 0, 0 };
    switch (b->type) {
    case PT_UINT8: n.kind = 1; n.u = b->v.b[0]; break;
    case PT_INT16: n.i = *(const INT16 *)b->v.b; break;
    case PT_UINT16: n.kind = 1; n.u = *(const UINT16 *)b->v.b; break;
    case PT_INT32: n.i = *(const INT32 *)b->v.b; break;
    case PT_UINT32: n.kind = 1; n.u = *(const UINT32 *)b->v.b; break;
    case PT_INT64: n.i = b->v.i64; break;
    case PT_UINT64: n.kind = 1; n.u = b->v.u64; break;
    case PT_SINGLE: n.kind = 2; n.d = b->v.f; break;
    case PT_DOUBLE: n.kind = 2; n.d = b->v.d; break;
    }
    return n;
}
/* (a floating value with a fraction; one past 2^63 has none) */
static BOOL has_fraction(double d) { return d == d && d > -9.2e18 && d < 9.2e18 && (double)(INT64)d != d; }
/* @b's value as an integer in [lo, hi] (signed) */
static HRESULT get_signed(const Box *b, INT64 lo, INT64 hi, INT64 *out)
{
    if (!out) return E_POINTER;
    if (!is_numeric(b->type)) return TYPE_E_TYPEMISMATCH_;
    Num n = num_of(b);
    if (n.kind == 1) {
        if (n.u > (UINT64)hi) return DISP_E_OVERFLOW_;
        *out = (INT64)n.u;
    } else if (n.kind == 2) {
        if (has_fraction(n.d)) return TYPE_E_TYPEMISMATCH_;
        if (!(n.d >= (double)lo && n.d <= (double)hi)) return DISP_E_OVERFLOW_;
        *out = (INT64)n.d;
    } else {
        if (n.i < lo || n.i > hi) return DISP_E_OVERFLOW_;
        *out = n.i;
    }
    return S_OK;
}
static HRESULT get_unsigned(const Box *b, UINT64 hi, UINT64 *out)
{
    if (!out) return E_POINTER;
    if (!is_numeric(b->type)) return TYPE_E_TYPEMISMATCH_;
    Num n = num_of(b);
    if (n.kind == 0) {
        if (n.i < 0 || (UINT64)n.i > hi) return DISP_E_OVERFLOW_;
        *out = (UINT64)n.i;
    } else if (n.kind == 2) {
        if (has_fraction(n.d)) return TYPE_E_TYPEMISMATCH_;
        if (!(n.d >= 0 && n.d <= (double)hi)) return DISP_E_OVERFLOW_;
        *out = (UINT64)n.d;
    } else {
        if (n.u > hi) return DISP_E_OVERFLOW_;
        *out = n.u;
    }
    return S_OK;
}
static HRESULT get_floating(const Box *b, double *out)
{
    if (!out) return E_POINTER;
    if (!is_numeric(b->type)) return TYPE_E_TYPEMISMATCH_;
    Num n = num_of(b);
    *out = n.kind == 0 ? (double)n.i : n.kind == 1 ? (double)n.u : n.d;
    return S_OK;
}

/* ---- IPropertyValue ---- */
static HRESULT STDMETHODCALLTYPE pv_type(void *o, int *t) { if (!t) return E_POINTER; *t = BOX_OF_PV(o)->type; return S_OK; }
static HRESULT STDMETHODCALLTYPE pv_numeric(void *o, boolean_t *r) { if (!r) return E_POINTER; *r = is_numeric(BOX_OF_PV(o)->type); return S_OK; }

#define SIGNED_GETTER(NAME, T, LO, HI) \
    static HRESULT STDMETHODCALLTYPE NAME(void *o, T *out) \
    { INT64 v; HRESULT hr = get_signed(BOX_OF_PV(o), LO, HI, out ? &v : 0); if (SUCCEEDED(hr)) *out = (T)v; return hr; }
#define UNSIGNED_GETTER(NAME, T, HI) \
    static HRESULT STDMETHODCALLTYPE NAME(void *o, T *out) \
    { UINT64 v; HRESULT hr = get_unsigned(BOX_OF_PV(o), HI, out ? &v : 0); if (SUCCEEDED(hr)) *out = (T)v; return hr; }
UNSIGNED_GETTER(pv_uint8, BYTE, 0xFF)
SIGNED_GETTER(pv_int16, INT16, -32768, 32767)
UNSIGNED_GETTER(pv_uint16, UINT16, 0xFFFF)
SIGNED_GETTER(pv_int32, INT32, -2147483647LL - 1, 2147483647LL)
UNSIGNED_GETTER(pv_uint32, UINT32, 0xFFFFFFFFULL)
SIGNED_GETTER(pv_int64, INT64, INT64_MIN, INT64_MAX)
UNSIGNED_GETTER(pv_uint64, UINT64, UINT64_MAX)
static HRESULT STDMETHODCALLTYPE pv_single(void *o, FLOAT *out)
{
    double d;
    HRESULT hr = get_floating(BOX_OF_PV(o), out ? &d : 0);
    /* (an infinity or a NaN stays one) */
    if (SUCCEEDED(hr) && d - d == 0 && (d > 3.4028234663852886e38 || d < -3.4028234663852886e38)) return DISP_E_OVERFLOW_;
    if (SUCCEEDED(hr)) *out = (FLOAT)d;
    return hr;
}
static HRESULT STDMETHODCALLTYPE pv_double(void *o, double *out) { return get_floating(BOX_OF_PV(o), out); }

/* a getter of one type only: its value copied out */
static HRESULT get_exact(const Box *b, int type, void *out)
{
    if (!out) return E_POINTER;
    if (b->type != type) return TYPE_E_TYPEMISMATCH_;
    if (type == PT_STRING) return WindowsDuplicateString(b->v.s, (HSTRING *)out);
    memcpy(out, b->v.b, g_types[type].size);
    return S_OK;
}
#define EXACT_GETTER(NAME, T, PT) \
    static HRESULT STDMETHODCALLTYPE NAME(void *o, T *out) { return get_exact(BOX_OF_PV(o), PT, out); }
EXACT_GETTER(pv_char16, WCHAR, PT_CHAR16)
EXACT_GETTER(pv_boolean, boolean_t, PT_BOOLEAN)
EXACT_GETTER(pv_string, HSTRING, PT_STRING)
EXACT_GETTER(pv_guid, GUID, PT_GUID)
EXACT_GETTER(pv_datetime, DateTime_, PT_DATETIME)
EXACT_GETTER(pv_timespan, TimeSpan_, PT_TIMESPAN)
EXACT_GETTER(pv_point, Point_, PT_POINT)
EXACT_GETTER(pv_size, Size_, PT_SIZE)
EXACT_GETTER(pv_rect, Rect_, PT_RECT)

/* an array getter: a copy the caller frees */
static HRESULT get_array(const Box *b, int type, UINT32 *count, void **out)
{
    if (!count || !out) return E_POINTER;
    *count = 0;
    *out = 0;
    if (b->type != (type | PT_ARRAY)) return TYPE_E_TYPEMISMATCH_;
    UINT32 size = g_types[type].size;
    void *copy = CoTaskMemAlloc(b->count ? (SIZE_T)b->count * size : 1);
    if (!copy) return E_OUTOFMEMORY;
    memcpy(copy, b->array, (SIZE_T)b->count * size);
    for (UINT32 i = 0; i < b->count; i++) {
        if (type == PT_STRING) {
            HSTRING *s = &((HSTRING *)copy)[i];
            HRESULT hr = WindowsDuplicateString(*s, s);
            if (FAILED(hr)) {
                while (i--) WindowsDeleteString(((HSTRING *)copy)[i]);
                CoTaskMemFree(copy);
                return hr;
            }
        } else if (type == PT_INSPECTABLE && ((void **)copy)[i])
            obj_addref(((void **)copy)[i]);
    }
    *count = b->count;
    *out = copy;
    return S_OK;
}
#define ARRAY_GETTER(NAME, T, PT) \
    static HRESULT STDMETHODCALLTYPE NAME(void *o, UINT32 *n, T **out) { return get_array(BOX_OF_PV(o), PT, n, (void **)out); }
ARRAY_GETTER(pv_uint8_array, BYTE, PT_UINT8)
ARRAY_GETTER(pv_int16_array, INT16, PT_INT16)
ARRAY_GETTER(pv_uint16_array, UINT16, PT_UINT16)
ARRAY_GETTER(pv_int32_array, INT32, PT_INT32)
ARRAY_GETTER(pv_uint32_array, UINT32, PT_UINT32)
ARRAY_GETTER(pv_int64_array, INT64, PT_INT64)
ARRAY_GETTER(pv_uint64_array, UINT64, PT_UINT64)
ARRAY_GETTER(pv_single_array, FLOAT, PT_SINGLE)
ARRAY_GETTER(pv_double_array, double, PT_DOUBLE)
ARRAY_GETTER(pv_char16_array, WCHAR, PT_CHAR16)
ARRAY_GETTER(pv_boolean_array, boolean_t, PT_BOOLEAN)
ARRAY_GETTER(pv_string_array, HSTRING, PT_STRING)
ARRAY_GETTER(pv_inspectable_array, void *, PT_INSPECTABLE)
ARRAY_GETTER(pv_guid_array, GUID, PT_GUID)
ARRAY_GETTER(pv_datetime_array, DateTime_, PT_DATETIME)
ARRAY_GETTER(pv_timespan_array, TimeSpan_, PT_TIMESPAN)
ARRAY_GETTER(pv_point_array, Point_, PT_POINT)
ARRAY_GETTER(pv_size_array, Size_, PT_SIZE)
ARRAY_GETTER(pv_rect_array, Rect_, PT_RECT)

/* IPropertyValue, in its vtable's order */
static const void *const g_pv_vtbl[] = {
    pv_qi, pv_addref, pv_release, no_iids, pv_class, base_trust,
    pv_type, pv_numeric,
    pv_uint8, pv_int16, pv_uint16, pv_int32, pv_uint32, pv_int64, pv_uint64, pv_single, pv_double,
    pv_char16, pv_boolean, pv_string, pv_guid, pv_datetime, pv_timespan, pv_point, pv_size, pv_rect,
    pv_uint8_array, pv_int16_array, pv_uint16_array, pv_int32_array, pv_uint32_array, pv_int64_array,
    pv_uint64_array, pv_single_array, pv_double_array, pv_char16_array, pv_boolean_array, pv_string_array,
    pv_inspectable_array, pv_guid_array, pv_datetime_array, pv_timespan_array, pv_point_array, pv_size_array,
    pv_rect_array,
};

/* IReference<T>.Value (a numeric one is its own type, so get_exact serves
 * every type) and IReferenceArray<T>.Value */
static HRESULT STDMETHODCALLTYPE ref_value(void *o, void *out)
{
    Box *b = BOX_OF_REF(o);
    return get_exact(b, b->type, out);
}
static HRESULT STDMETHODCALLTYPE refarray_value(void *o, UINT32 *n, void **out)
{
    Box *b = BOX_OF_REF(o);
    return get_array(b, elem_type(b), n, out);
}
static const void *const g_ref_vtbl[] = { ref_qi, ref_addref, ref_release, no_iids, ref_class, base_trust, ref_value };
static const void *const g_refarray_vtbl[] = { ref_qi, ref_addref, ref_release, no_iids, ref_class, base_trust, refarray_value };

/* ---- making boxes ---- */
static Box *box_new(int type)
{
    Box *b = mem_alloc(sizeof *b);
    if (!b) return 0;
    b->vt_pv = g_pv_vtbl;
    b->vt_ref = (type & PT_ARRAY) ? g_refarray_vtbl : g_ref_vtbl;
    b->refs = 1;
    b->type = type;
    return b;
}
static HRESULT make_scalar(int type, const void *value, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    Box *b = box_new(type);
    if (!b) return E_OUTOFMEMORY;
    if (type == PT_STRING) {
        HRESULT hr = WindowsDuplicateString(*(const HSTRING *)value, &b->v.s);
        if (FAILED(hr)) { mem_free(b); return hr; }
    } else
        memcpy(b->v.b, value, g_types[type].size);
    *out = &b->vt_pv;
    return S_OK;
}
static HRESULT make_array(int type, UINT32 count, const void *values, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (count && !values) return E_POINTER;
    Box *b = box_new(type | PT_ARRAY);
    if (!b) return E_OUTOFMEMORY;
    UINT32 size = g_types[type].size;
    b->array = mem_alloc((SIZE_T)count * size);
    if (!b->array) { mem_free(b); return E_OUTOFMEMORY; }
    for (UINT32 i = 0; i < count; i++) {
        const BYTE *src = (const BYTE *)values + (SIZE_T)i * size;
        BYTE *dst = (BYTE *)b->array + (SIZE_T)i * size;
        if (type == PT_STRING) {
            HRESULT hr = WindowsDuplicateString(*(const HSTRING *)src, (HSTRING *)dst);
            if (FAILED(hr)) { b->count = i; box_release(b); return hr; }
        } else {
            memcpy(dst, src, size);
            if (type == PT_INSPECTABLE && *(void **)dst) obj_addref(*(void **)dst);
        }
        b->count = i + 1;
    }
    *out = &b->vt_pv;
    return S_OK;
}

/* ---- IPropertyValueStatics (the class's activation factory) ---- */
typedef struct { const void *const *vt; } Factory;
static const void *const g_factory_vtbl[];
static const void *const g_statics_vtbl[];
static Factory g_factory = { g_factory_vtbl }, g_statics = { g_statics_vtbl };

static HRESULT STDMETHODCALLTYPE f_qi(void *o, REFIID iid, void **out)
{
    (void)o;
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IActivationFactory_))
        *out = &g_factory;
    else if (IsEqualGUID(iid, &IID_IPropertyValueStatics_))
        *out = &g_statics;
    else {
        *out = 0;
        return E_NOINTERFACE;
    }
    return S_OK;
}
static ULONG STDMETHODCALLTYPE f_addref(void *o) { (void)o; return 2; }
static ULONG STDMETHODCALLTYPE f_release(void *o) { (void)o; return 1; }
static const WCHAR c_runtime_class[] = L"Windows.Foundation.PropertyValue";
static HRESULT STDMETHODCALLTYPE f_class(void *o, HSTRING *s)
{
    (void)o;
    if (!s) return E_POINTER;
    return WindowsCreateString(c_runtime_class, (UINT32)(sizeof c_runtime_class / sizeof(WCHAR) - 1), s);
}
/* (PropertyValue is a static class: no instances of its own) */
static HRESULT STDMETHODCALLTYPE f_activate(void *o, void **out) { (void)o; if (out) *out = 0; return E_NOTIMPL; }
static const void *const g_factory_vtbl[] = { f_qi, f_addref, f_release, no_iids, f_class, base_trust, f_activate };

static HRESULT STDMETHODCALLTYPE s_empty(void *o, void **out) { (void)o; if (!out) return E_POINTER; *out = 0; return S_OK; }
#define SCALAR_MAKER(NAME, T, PT) \
    static HRESULT STDMETHODCALLTYPE NAME(void *o, T value, void **out) { (void)o; return make_scalar(PT, &value, out); }
SCALAR_MAKER(s_uint8, BYTE, PT_UINT8)
SCALAR_MAKER(s_int16, INT16, PT_INT16)
SCALAR_MAKER(s_uint16, UINT16, PT_UINT16)
SCALAR_MAKER(s_int32, INT32, PT_INT32)
SCALAR_MAKER(s_uint32, UINT32, PT_UINT32)
SCALAR_MAKER(s_int64, INT64, PT_INT64)
SCALAR_MAKER(s_uint64, UINT64, PT_UINT64)
SCALAR_MAKER(s_single, FLOAT, PT_SINGLE)
SCALAR_MAKER(s_double, double, PT_DOUBLE)
SCALAR_MAKER(s_char16, WCHAR, PT_CHAR16)
SCALAR_MAKER(s_string, HSTRING, PT_STRING)
SCALAR_MAKER(s_guid, GUID, PT_GUID)
SCALAR_MAKER(s_datetime, DateTime_, PT_DATETIME)
SCALAR_MAKER(s_timespan, TimeSpan_, PT_TIMESPAN)
SCALAR_MAKER(s_point, Point_, PT_POINT)
SCALAR_MAKER(s_size, Size_, PT_SIZE)
SCALAR_MAKER(s_rect, Rect_, PT_RECT)
static HRESULT STDMETHODCALLTYPE s_boolean(void *o, boolean_t value, void **out)
{
    (void)o;
    boolean_t v = value != 0;
    return make_scalar(PT_BOOLEAN, &v, out);
}
/* an object is its own box */
static HRESULT STDMETHODCALLTYPE s_inspectable(void *o, void *value, void **out)
{
    (void)o;
    if (!out) return E_POINTER;
    *out = value;
    if (value) obj_addref(value);
    return S_OK;
}
#define ARRAY_MAKER(NAME, T, PT) \
    static HRESULT STDMETHODCALLTYPE NAME(void *o, UINT32 n, T *values, void **out) { (void)o; return make_array(PT, n, values, out); }
ARRAY_MAKER(s_uint8_array, BYTE, PT_UINT8)
ARRAY_MAKER(s_int16_array, INT16, PT_INT16)
ARRAY_MAKER(s_uint16_array, UINT16, PT_UINT16)
ARRAY_MAKER(s_int32_array, INT32, PT_INT32)
ARRAY_MAKER(s_uint32_array, UINT32, PT_UINT32)
ARRAY_MAKER(s_int64_array, INT64, PT_INT64)
ARRAY_MAKER(s_uint64_array, UINT64, PT_UINT64)
ARRAY_MAKER(s_single_array, FLOAT, PT_SINGLE)
ARRAY_MAKER(s_double_array, double, PT_DOUBLE)
ARRAY_MAKER(s_char16_array, WCHAR, PT_CHAR16)
ARRAY_MAKER(s_boolean_array, boolean_t, PT_BOOLEAN)
ARRAY_MAKER(s_string_array, HSTRING, PT_STRING)
ARRAY_MAKER(s_inspectable_array, void *, PT_INSPECTABLE)
ARRAY_MAKER(s_guid_array, GUID, PT_GUID)
ARRAY_MAKER(s_datetime_array, DateTime_, PT_DATETIME)
ARRAY_MAKER(s_timespan_array, TimeSpan_, PT_TIMESPAN)
ARRAY_MAKER(s_point_array, Point_, PT_POINT)
ARRAY_MAKER(s_size_array, Size_, PT_SIZE)
ARRAY_MAKER(s_rect_array, Rect_, PT_RECT)

/* IPropertyValueStatics, in its vtable's order */
static const void *const g_statics_vtbl[] = {
    f_qi, f_addref, f_release, no_iids, f_class, base_trust,
    s_empty, s_uint8, s_int16, s_uint16, s_int32, s_uint32, s_int64, s_uint64, s_single, s_double,
    s_char16, s_boolean, s_string, s_inspectable, s_guid, s_datetime, s_timespan, s_point, s_size, s_rect,
    s_uint8_array, s_int16_array, s_uint16_array, s_int32_array, s_uint32_array, s_int64_array, s_uint64_array,
    s_single_array, s_double_array, s_char16_array, s_boolean_array, s_string_array, s_inspectable_array,
    s_guid_array, s_datetime_array, s_timespan_array, s_point_array, s_size_array, s_rect_array,
};

WT_API HRESULT WINAPI DllGetActivationFactory(HSTRING cls, void **factory)
{
    if (!factory) return E_POINTER;
    UINT32 n = 0;
    const WCHAR *s = WindowsGetStringRawBuffer(cls, &n);
    if (n == sizeof c_runtime_class / sizeof(WCHAR) - 1 && !memcmp(s, c_runtime_class, n * sizeof(WCHAR))) {
        *factory = &g_factory;
        return S_OK;
    }
    *factory = 0;
    return CLASS_E_CLASSNOTAVAILABLE;
}
WT_API HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)inst; (void)reason; (void)reserved;
    return TRUE;
}
