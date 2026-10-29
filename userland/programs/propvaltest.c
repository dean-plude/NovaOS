/* propvaltest.exe — Windows.Foundation.PropertyValue (wintypes.dll), the
 * Windows Runtime's boxes for values, which Microsoft's XAML framework
 * hands out every value through (its value boxer): the class's statics
 * through RoGetActivationFactory (the registry's ActivatableClassId key),
 * boxes of numbers, booleans, strings, GUIDs, points and rectangles read
 * back through IPropertyValue (type, numeric conversions in range,
 * overflow and type mismatches refused) and their IReference<T> (whose
 * class name .NET Native unboxes by), arrays
 * through IReferenceArray<T>, CreateEmpty's null and CreateInspectable's
 * object itself.
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <roapi.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define VT(o, i, T) ((T)((void **)*(void **)(o))[i])
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(void *);
typedef HRESULT (STDMETHODCALLTYPE *QI_t)(void *, const GUID *, void **);
typedef HRESULT (STDMETHODCALLTYPE *Get_t)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *GetArr_t)(void *, UINT32 *, void **);
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)
#define QI(o, iid, out) VT(o, 0, QI_t)(o, iid, (void **)(out))

#define TYPE_E_TYPEMISMATCH_ ((HRESULT)0x80028CA0L)
#define DISP_E_OVERFLOW_     ((HRESULT)0x8002000AL)

static const GUID IID_IPropertyValueStatics_ = { 0x629BDBC8, 0xD932, 0x4FF4, { 0x96, 0xB9, 0x8D, 0x96, 0xC5, 0xC1, 0xE8, 0x58 } };
static const GUID IID_IPropertyValue_ = { 0x4BD682DD, 0x7554, 0x40E9, { 0x9A, 0x9B, 0x82, 0x65, 0x4E, 0xDE, 0x7E, 0x62 } };
static const GUID IID_IReference_Int32 = { 0x548CEFBD, 0xBC8A, 0x5FA0, { 0x8D, 0xF2, 0x95, 0x74, 0x40, 0xFC, 0x8B, 0xF4 } };
static const GUID IID_IReference_String = { 0xFD416DFB, 0x2A07, 0x52EB, { 0xAA, 0xE3, 0xDF, 0xCE, 0x14, 0x11, 0x6C, 0x05 } };
static const GUID IID_IReference_Double = { 0x2F2D6C29, 0x5473, 0x5F3E, { 0x92, 0xE7, 0x96, 0x57, 0x2B, 0xB9, 0x90, 0xE2 } };
static const GUID IID_IReferenceArray_Int32 = { 0xA6D080A5, 0xB087, 0x5BC2, { 0x9A, 0x9F, 0x5C, 0xD6, 0x87, 0xB4, 0xD1, 0xF7 } };

/* IPropertyValueStatics' slots (IInspectable's six first) */
enum { S_EMPTY = 6, S_UINT8, S_INT16, S_UINT16, S_INT32, S_UINT32, S_INT64, S_UINT64, S_SINGLE, S_DOUBLE, S_CHAR16,
       S_BOOLEAN, S_STRING, S_INSPECTABLE, S_GUID, S_DATETIME, S_TIMESPAN, S_POINT, S_SIZE, S_RECT,
       S_UINT8_ARRAY, S_INT16_ARRAY, S_UINT16_ARRAY, S_INT32_ARRAY, S_UINT32_ARRAY, S_INT64_ARRAY, S_UINT64_ARRAY,
       S_SINGLE_ARRAY, S_DOUBLE_ARRAY, S_CHAR16_ARRAY, S_BOOLEAN_ARRAY, S_STRING_ARRAY };
/* IPropertyValue's */
enum { P_TYPE = 6, P_NUMERIC, P_UINT8, P_INT16, P_UINT16, P_INT32, P_UINT32, P_INT64, P_UINT64, P_SINGLE, P_DOUBLE,
       P_CHAR16, P_BOOLEAN, P_STRING, P_GUID, P_DATETIME, P_TIMESPAN, P_POINT, P_SIZE, P_RECT,
       P_UINT8_ARRAY, P_INT16_ARRAY, P_UINT16_ARRAY, P_INT32_ARRAY, P_UINT32_ARRAY, P_INT64_ARRAY, P_UINT64_ARRAY,
       P_SINGLE_ARRAY, P_DOUBLE_ARRAY, P_CHAR16_ARRAY, P_BOOLEAN_ARRAY, P_STRING_ARRAY };
/* PropertyType */
enum { PT_INT32 = 4, PT_DOUBLE = 9, PT_BOOLEAN = 11, PT_STRING = 12, PT_GUID = 16, PT_POINT = 17, PT_RECT = 19,
       PT_INT32_ARRAY = 1028, PT_STRING_ARRAY = 1036 };

typedef struct { FLOAT x, y; } Point_;
typedef struct { FLOAT x, y, w, h; } Rect_;

static int type_of(void *pv)
{
    int t = -1;
    VT(pv, P_TYPE, Get_t)(pv, &t);
    return t;
}

static HSTRING hs(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, (UINT32)wcslen(s), &h);
    return h;
}

static BOOL hs_is(HSTRING h, const WCHAR *s)
{
    UINT32 n = 0;
    const WCHAR *b = WindowsGetStringRawBuffer(h, &n);
    return n == wcslen(s) && !memcmp(b, s, n * sizeof(WCHAR));
}

int main(void)
{
    RoInitialize(RO_INIT_MULTITHREADED);
    HSTRING cls = hs(L"Windows.Foundation.PropertyValue");
    void *st = NULL, *pv = NULL, *ref = NULL, *o = NULL;
    HRESULT hr = RoGetActivationFactory(cls, &IID_IPropertyValueStatics_, &st);
    CHECK("PropertyValue's statics through RoGetActivationFactory", SUCCEEDED(hr) && st);
    WindowsDeleteString(cls);
    if (!st) {
        printf("propvaltest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
        return 1;
    }

    /* an Int32: its type, a numeric scalar, read as wider and narrower types */
    CHECK("CreateInt32", SUCCEEDED(VT(st, S_INT32, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, void **))(st, 100000, &o)) && o);
    CHECK("an IPropertyValue", o && SUCCEEDED(QI(o, &IID_IPropertyValue_, &pv)) && pv);
    if (pv) {
        BYTE numeric = 0;
        INT32 i32 = 0;
        INT64 i64 = 0;
        INT16 i16 = 0;
        double d = 0;
        HSTRING s = NULL;
        CHECK("Type is Int32", type_of(pv) == PT_INT32);
        CHECK("IsNumericScalar", SUCCEEDED(VT(pv, P_NUMERIC, Get_t)(pv, &numeric)) && numeric);
        CHECK("GetInt32", SUCCEEDED(VT(pv, P_INT32, Get_t)(pv, &i32)) && i32 == 100000);
        CHECK("GetInt64 converts", SUCCEEDED(VT(pv, P_INT64, Get_t)(pv, &i64)) && i64 == 100000);
        CHECK("GetDouble converts", SUCCEEDED(VT(pv, P_DOUBLE, Get_t)(pv, &d)) && d == 100000.0);
        CHECK("GetInt16 overflows", VT(pv, P_INT16, Get_t)(pv, &i16) == DISP_E_OVERFLOW_);
        CHECK("GetString is a type mismatch", VT(pv, P_STRING, Get_t)(pv, &s) == TYPE_E_TYPEMISMATCH_ && !s);
        CHECK("IReference<Int32>", SUCCEEDED(QI(o, &IID_IReference_Int32, &ref)) && ref);
        i32 = 0;
        CHECK("IReference<Int32>.Value", ref && SUCCEEDED(VT(ref, 6, Get_t)(ref, &i32)) && i32 == 100000);
        RELEASE(ref);
        CHECK("not an IReference<String>", QI(o, &IID_IReference_String, &ref) == E_NOINTERFACE && !ref);
    }
    RELEASE(pv);
    RELEASE(o);

    /* a negative Int32 is no unsigned value */
    VT(st, S_INT32, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, void **))(st, -5, &o);
    if (o) {
        UINT32 u = 7;
        CHECK("GetUInt32 of -5 overflows", VT(o, P_UINT32, Get_t)(o, &u) == DISP_E_OVERFLOW_);
        RELEASE(o);
    }

    /* a Double: whole values convert to integers, fractions do not */
    CHECK("CreateDouble", SUCCEEDED(VT(st, S_DOUBLE, HRESULT (STDMETHODCALLTYPE *)(void *, double, void **))(st, 2.0, &o)) && o);
    if (o) {
        INT32 i32 = 0;
        CHECK("Type is Double", type_of(o) == PT_DOUBLE);
        CHECK("GetInt32 of 2.0", SUCCEEDED(VT(o, P_INT32, Get_t)(o, &i32)) && i32 == 2);
        CHECK("IReference<Double>", SUCCEEDED(QI(o, &IID_IReference_Double, &ref)) && ref);
        RELEASE(ref);
        /* .NET Native unboxes by the class name, so it names the reference */
        HSTRING cn = NULL;
        CHECK("class name is IReference`1<Double>", SUCCEEDED(VT(o, 4, Get_t)(o, &cn))
              && hs_is(cn, L"Windows.Foundation.IReference`1<Double>"));
        WindowsDeleteString(cn);
        RELEASE(o);
    }
    VT(st, S_DOUBLE, HRESULT (STDMETHODCALLTYPE *)(void *, double, void **))(st, 2.5, &o);
    if (o) {
        INT32 i32 = 0;
        CHECK("GetInt32 of 2.5 is a type mismatch", VT(o, P_INT32, Get_t)(o, &i32) == TYPE_E_TYPEMISMATCH_);
        RELEASE(o);
    }

    /* a Boolean, a String, a GUID, a Point, a Rect */
    CHECK("CreateBoolean", SUCCEEDED(VT(st, S_BOOLEAN, HRESULT (STDMETHODCALLTYPE *)(void *, BYTE, void **))(st, 1, &o)) && o);
    if (o) {
        BYTE b = 0;
        INT32 i32 = 0;
        CHECK("Type is Boolean", type_of(o) == PT_BOOLEAN);
        CHECK("GetBoolean", SUCCEEDED(VT(o, P_BOOLEAN, Get_t)(o, &b)) && b == 1);
        CHECK("a Boolean is not numeric", VT(o, P_INT32, Get_t)(o, &i32) == TYPE_E_TYPEMISMATCH_);
        RELEASE(o);
    }
    HSTRING hello = hs(L"Hello, Kepler");
    CHECK("CreateString", SUCCEEDED(VT(st, S_STRING, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, void **))(st, hello, &o)) && o);
    WindowsDeleteString(hello);
    if (o) {
        HSTRING s = NULL;
        CHECK("Type is String", type_of(o) == PT_STRING);
        CHECK("GetString (a copy)", SUCCEEDED(VT(o, P_STRING, Get_t)(o, &s)) && hs_is(s, L"Hello, Kepler"));
        WindowsDeleteString(s);
        s = NULL;
        CHECK("IReference<String>.Value", SUCCEEDED(QI(o, &IID_IReference_String, &ref)) && ref &&
              SUCCEEDED(VT(ref, 6, Get_t)(ref, &s)) && hs_is(s, L"Hello, Kepler"));
        WindowsDeleteString(s);
        RELEASE(ref);
        RELEASE(o);
    }
    static const GUID g = { 0x12345678, 0x9ABC, 0xDEF0, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    CHECK("CreateGuid", SUCCEEDED(VT(st, S_GUID, HRESULT (STDMETHODCALLTYPE *)(void *, GUID, void **))(st, g, &o)) && o);
    if (o) {
        GUID back = { 0 };
        CHECK("GetGuid", type_of(o) == PT_GUID && SUCCEEDED(VT(o, P_GUID, Get_t)(o, &back)) && IsEqualGUID(&back, &g));
        RELEASE(o);
    }
    Point_ pt = { 1.5f, -2.0f };
    CHECK("CreatePoint", SUCCEEDED(VT(st, S_POINT, HRESULT (STDMETHODCALLTYPE *)(void *, Point_, void **))(st, pt, &o)) && o);
    if (o) {
        Point_ back = { 0, 0 };
        CHECK("GetPoint", type_of(o) == PT_POINT && SUCCEEDED(VT(o, P_POINT, Get_t)(o, &back)) && back.x == 1.5f && back.y == -2.0f);
        RELEASE(o);
    }
    Rect_ rc = { 1, 2, 30, 40 };
    CHECK("CreateRect", SUCCEEDED(VT(st, S_RECT, HRESULT (STDMETHODCALLTYPE *)(void *, Rect_, void **))(st, rc, &o)) && o);
    if (o) {
        Rect_ back = { 0, 0, 0, 0 };
        CHECK("GetRect", type_of(o) == PT_RECT && SUCCEEDED(VT(o, P_RECT, Get_t)(o, &back)) && back.w == 30 && back.h == 40);
        RELEASE(o);
    }

    /* arrays: Int32 and String, through IPropertyValue and IReferenceArray<T> */
    INT32 nums[3] = { 7, -8, 9 };
    CHECK("CreateInt32Array", SUCCEEDED(VT(st, S_INT32_ARRAY, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32, INT32 *, void **))(st, 3, nums, &o)) && o);
    if (o) {
        UINT32 n = 0;
        INT32 *got = NULL, one = 0;
        CHECK("Type is Int32Array", type_of(o) == PT_INT32_ARRAY);
        CHECK("GetInt32Array", SUCCEEDED(VT(o, P_INT32_ARRAY, GetArr_t)(o, &n, (void **)&got)) && n == 3 && got &&
              got[0] == 7 && got[1] == -8 && got[2] == 9);
        CoTaskMemFree(got);
        got = NULL;
        CHECK("an array is no scalar", VT(o, P_INT32, Get_t)(o, &one) == TYPE_E_TYPEMISMATCH_);
        CHECK("IReferenceArray<Int32>.Value", SUCCEEDED(QI(o, &IID_IReferenceArray_Int32, &ref)) && ref &&
              SUCCEEDED(VT(ref, 6, GetArr_t)(ref, &n, (void **)&got)) && n == 3 && got && got[2] == 9);
        CoTaskMemFree(got);
        RELEASE(ref);
        RELEASE(o);
    }
    HSTRING words[2] = { hs(L"alpha"), hs(L"beta") };
    CHECK("CreateStringArray", SUCCEEDED(VT(st, S_STRING_ARRAY, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32, HSTRING *, void **))(st, 2, words, &o)) && o);
    WindowsDeleteString(words[0]);
    WindowsDeleteString(words[1]);
    if (o) {
        UINT32 n = 0;
        HSTRING *got = NULL;
        CHECK("GetStringArray (copies)", type_of(o) == PT_STRING_ARRAY && SUCCEEDED(VT(o, P_STRING_ARRAY, GetArr_t)(o, &n, (void **)&got)) &&
              n == 2 && hs_is(got[0], L"alpha") && hs_is(got[1], L"beta"));
        for (UINT32 i = 0; got && i < n; i++) WindowsDeleteString(got[i]);
        CoTaskMemFree(got);
        RELEASE(o);
    }

    /* CreateEmpty is null; CreateInspectable is the object itself */
    o = (void *)1;
    CHECK("CreateEmpty is null", SUCCEEDED(VT(st, S_EMPTY, Get_t)(st, &o)) && o == NULL);
    void *obj = NULL;
    VT(st, S_INT32, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, void **))(st, 1, &obj);
    CHECK("CreateInspectable is the object", obj &&
          SUCCEEDED(VT(st, S_INSPECTABLE, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(st, obj, &o)) && o == obj);
    RELEASE(o);
    RELEASE(obj);
    RELEASE(st);
    printf("propvaltest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    return fail != 0;
}
