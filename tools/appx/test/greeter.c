/*
 * greeter.dll — the in-process Windows Runtime class of the appx
 * self-test's package (tools/appx/mktest.py): NovaOS.PackageTest.Greeter,
 * which the package's manifest declares (windows.activatableClass.
 * inProcessServer) and its program activates with RoActivateInstance.
 * Its one method hands back a fixed string.  Freestanding: no imports, so
 * it is built with clang and lld-link alone.
 */
typedef long HRESULT;
typedef unsigned long ULONG;
typedef unsigned short WCHAR;
typedef struct { unsigned long a; unsigned short b, c; unsigned char d[8]; } GUID;
typedef struct { void *const *vt; } Object;

#define S_OK 0
#define S_FALSE 1
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)

static const GUID IID_IUnknown = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IActivationFactory = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
/* IGreeter: IInspectable, then HRESULT Greet(const WCHAR **text) */
static const GUID IID_IGreeter = { 0x6E6F7661, 0x0032, 0x0036, { 0x67, 0x72, 0x65, 0x65, 0x74, 0x65, 0x72, 0x31 } };

static const WCHAR greeting[] = u"Hello from a packaged Windows Runtime class";

static int same(const GUID *a, const GUID *b)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (int i = 0; i < 16; i++)
        if (x[i] != y[i]) return 0;
    return 1;
}

static ULONG addref(Object *o) { (void)o; return 2; }        /* (both objects are static) */
static ULONG release(Object *o) { (void)o; return 1; }
static HRESULT iids(Object *o, ULONG *n, GUID **list) { (void)o; *n = 0; *list = 0; return S_OK; }
static HRESULT class_name(Object *o, void **name) { (void)o; *name = 0; return S_OK; }   /* (the empty string) */
static HRESULT trust(Object *o, int *level) { (void)o; *level = 0; return S_OK; }

static HRESULT greeter_qi(Object *o, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (!same(iid, &IID_IUnknown) && !same(iid, &IID_IInspectable) && !same(iid, &IID_IGreeter)) {
        *out = 0;
        return E_NOINTERFACE;
    }
    *out = o;
    return S_OK;
}
static HRESULT greet(Object *o, const WCHAR **text) { (void)o; *text = greeting; return S_OK; }
static void *const greeter_vt[] = { greeter_qi, addref, release, iids, class_name, trust, greet };
static Object greeter = { greeter_vt };

static HRESULT factory_qi(Object *o, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (!same(iid, &IID_IUnknown) && !same(iid, &IID_IInspectable) && !same(iid, &IID_IActivationFactory)) {
        *out = 0;
        return E_NOINTERFACE;
    }
    *out = o;
    return S_OK;
}
static HRESULT activate(Object *o, Object **out) { (void)o; *out = &greeter; return S_OK; }
static void *const factory_vt[] = { factory_qi, addref, release, iids, class_name, trust, activate };
static Object factory = { factory_vt };

__declspec(dllexport) HRESULT DllGetActivationFactory(void *cls, Object **out)
{
    (void)cls;                    /* (the package's one class) */
    if (!out) return E_POINTER;
    *out = &factory;
    return S_OK;
}
__declspec(dllexport) HRESULT DllCanUnloadNow(void) { return S_FALSE; }
