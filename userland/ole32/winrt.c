/*
 * Windows Runtime strings (HSTRING) and activation, the part of combase
 * (api-ms-win-core-winrt-*) Win32 programs touch: they make HSTRINGs to
 * name runtime classes, and ask for activation factories.  A class is
 * answered by ole32 itself (winrt_classes.c, uisettings.c) or, as Windows
 * does, by the in-process server the registry names for it:
 * HKLM\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\<class>
 * has DllPath, and that DLL's DllGetActivationFactory makes the factory
 * (twinapi.appcore.dll's CoreApplication, for one); a packaged app's own
 * classes are found first, under its package's key.  Others give
 * REGDB_E_CLASSNOTREG, as for a class Windows lacks.
 */
#define NOVA_BUILD_OLE32
#include <objbase.h>

typedef struct HSTRING_ *HSTRING;
/* An HSTRING is one of these, laid out as Windows lays them out: C++/WinRT
 * builds HSTRINGs itself (reference ones, and heap ones with the count
 * after the header and the characters after that, from the process heap)
 * and hands them to these functions.  A reference string lives in the
 * caller's HSTRING_HEADER (24 bytes on x64, 20 on x86), without a count. */
typedef struct {
    UINT32 flags;                       /* 1: a reference (not owned) */
    UINT32 length;
    UINT32 pad1, pad2;
    const WCHAR *chars;
    LONG refs;                          /* heap strings only */
} HStr;
typedef struct { BYTE b[sizeof(void *) == 8 ? 24 : 20]; } HSTRING_HEADER;
_Static_assert(__builtin_offsetof(HStr, refs) == sizeof(HSTRING_HEADER), "HSTRING_HEADER holds a reference string");

#define HSTR_REF 1
#define E_STRING_NOT_NULL_TERMINATED ((HRESULT)0x80000017L)

WINOLEAPI_(HRESULT) WindowsCreateString(const WCHAR *src, UINT32 len, HSTRING *out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!src && len) return E_POINTER;
    if (!len) return S_OK;                              /* the empty string is NULL */
    HStr *s = HeapAlloc(GetProcessHeap(), 0, sizeof(HStr) + (len + 1) * sizeof(WCHAR));
    if (!s) return E_OUTOFMEMORY;
    WCHAR *c = (WCHAR *)(s + 1);
    for (UINT32 i = 0; i < len; i++) c[i] = src[i];
    c[len] = 0;
    s->flags = 0;
    s->length = len;
    s->pad1 = s->pad2 = 0;
    s->chars = c;
    s->refs = 1;
    *out = (HSTRING)s;
    return S_OK;
}

WINOLEAPI_(HRESULT) WindowsCreateStringReference(const WCHAR *src, UINT32 len, HSTRING_HEADER *hdr, HSTRING *out)
{
    if (!out || !hdr) return E_INVALIDARG;
    *out = 0;
    if (!src && len) return E_POINTER;
    if (len && src[len]) return E_STRING_NOT_NULL_TERMINATED;
    if (!len) return S_OK;
    HStr *s = (HStr *)hdr;
    s->flags = HSTR_REF;
    s->length = len;
    s->pad1 = s->pad2 = 0;
    s->chars = src;
    *out = (HSTRING)s;
    return S_OK;
}

WINOLEAPI_(HRESULT) WindowsDeleteString(HSTRING h)
{
    HStr *s = (HStr *)h;
    if (s && !(s->flags & HSTR_REF) && !InterlockedDecrement(&s->refs)) HeapFree(GetProcessHeap(), 0, s);
    return S_OK;
}

WINOLEAPI_(HRESULT) WindowsDuplicateString(HSTRING h, HSTRING *out)
{
    HStr *s = (HStr *)h;
    if (!out) return E_INVALIDARG;
    if (!s) { *out = 0; return S_OK; }
    if (s->flags & HSTR_REF) return WindowsCreateString(s->chars, s->length, out);
    InterlockedIncrement(&s->refs);
    *out = h;
    return S_OK;
}

WINOLEAPI_(const WCHAR *) WindowsGetStringRawBuffer(HSTRING h, UINT32 *len)
{
    HStr *s = (HStr *)h;
    if (len) *len = s ? s->length : 0;
    return s ? s->chars : L"";
}
WINOLEAPI_(UINT32) WindowsGetStringLen(HSTRING h) { return h ? ((HStr *)h)->length : 0; }
WINOLEAPI_(BOOL) WindowsIsStringEmpty(HSTRING h) { return !h || !((HStr *)h)->length; }
WINOLEAPI_(HRESULT) WindowsStringHasEmbeddedNull(HSTRING h, BOOL *has)
{
    HStr *s = (HStr *)h;
    if (!has) return E_INVALIDARG;
    *has = FALSE;
    for (UINT32 i = 0; s && i < s->length; i++) if (!s->chars[i]) *has = TRUE;
    return S_OK;
}
WINOLEAPI_(HRESULT) WindowsCompareStringOrdinal(HSTRING a, HSTRING b, INT32 *r)
{
    if (!r) return E_INVALIDARG;
    UINT32 la, lb;
    const WCHAR *x = WindowsGetStringRawBuffer(a, &la), *y = WindowsGetStringRawBuffer(b, &lb);
    *r = 0;
    for (UINT32 i = 0; i < la && i < lb && !*r; i++) if (x[i] != y[i]) *r = x[i] < y[i] ? -1 : 1;
    if (!*r && la != lb) *r = la < lb ? -1 : 1;
    return S_OK;
}
WINOLEAPI_(HRESULT) WindowsConcatString(HSTRING a, HSTRING b, HSTRING *out)
{
    if (!out) return E_INVALIDARG;
    UINT32 la, lb;
    const WCHAR *x = WindowsGetStringRawBuffer(a, &la), *y = WindowsGetStringRawBuffer(b, &lb);
    *out = 0;
    if (!la && !lb) return S_OK;
    WCHAR *t = HeapAlloc(GetProcessHeap(), 0, (la + lb + 1) * sizeof(WCHAR));
    if (!t) return E_OUTOFMEMORY;
    for (UINT32 i = 0; i < la; i++) t[i] = x[i];
    for (UINT32 i = 0; i < lb; i++) t[la + i] = y[i];
    t[la + lb] = 0;
    HRESULT hr = WindowsCreateString(t, la + lb, out);
    HeapFree(GetProcessHeap(), 0, t);
    return hr;
}

/* A string built in place: the caller fills the characters
 * WindowsPreallocateStringBuffer hands out, then promotes the buffer to the
 * HSTRING it already is (or deletes it).  Zero characters give the empty
 * string and no buffer, as on Windows. */
typedef struct HSTRING_BUFFER_ *HSTRING_BUFFER;
WINOLEAPI_(HRESULT) WindowsPreallocateStringBuffer(UINT32 len, WCHAR **chars, HSTRING_BUFFER *out)
{
    static const WCHAR empty[1];
    if (!chars || !out) return E_POINTER;
    *chars = 0;
    *out = 0;
    if (!len) {
        *chars = (WCHAR *)empty;
        return S_OK;
    }
    HSTRING h;
    WCHAR *fill = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (len + 1) * sizeof(WCHAR));
    if (!fill) return E_OUTOFMEMORY;
    HRESULT hr = WindowsCreateString(fill, len, &h);      /* len characters (zeros) and the terminator */
    HeapFree(GetProcessHeap(), 0, fill);
    if (FAILED(hr)) return hr;
    *chars = (WCHAR *)((HStr *)h)->chars;
    *out = (HSTRING_BUFFER)h;
    return S_OK;
}
WINOLEAPI_(HRESULT) WindowsPromoteStringBuffer(HSTRING_BUFFER buf, HSTRING *out)
{
    HStr *s = (HStr *)buf;
    if (!out) return E_POINTER;
    *out = 0;
    if (!s) return S_OK;
    if (s->chars[s->length]) return E_INVALIDARG;         /* the terminator was overwritten */
    *out = (HSTRING)s;
    return S_OK;
}
WINOLEAPI_(HRESULT) WindowsDeleteStringBuffer(HSTRING_BUFFER buf)
{
    return WindowsDeleteString((HSTRING)buf);
}

/* Activation: the classes in winrt_classes.c, then the registry's */
HRESULT ole32_winrt_factory(const WCHAR *name, UINT32 len, REFIID iid, void **out);
WINOLEAPI_(HRESULT) RoInitialize(int type) { return CoInitializeEx(0, type == 0 ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED); }
WINOLEAPI_(void) RoUninitialize(void) { CoUninitialize(); }
static const GUID IID_IActivationFactory__ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };

/* An in-process server: ActivatableClassId\<class> (ActivationType 0)
 * names the DLL; its DllGetActivationFactory(class, IActivationFactory **)
 * makes the factory, which is then asked for the interface wanted.  The
 * DLL stays loaded, as combase keeps it while the process runs.  A
 * packaged process looks first among its package graph's classes (each
 * manifest's windows.activatableClass.inProcessServer extensions, which
 * the package's installation registered under
 * HKCU\Software\Classes\ActivatableClasses\Package\<full name>: its own
 * package's, then the framework packages' it depends on), then
 * the system's (HKLM\SOFTWARE\Microsoft\WindowsRuntime). */
static HRESULT registry_factory_in(HKEY hive, const WCHAR *root, HSTRING cls, const WCHAR *name, UINT32 len,
                                   REFIID iid, void **out)
{
    WCHAR key[400], path[MAX_PATH];
    UINT32 n = 0;
    for (const WCHAR *r = root; *r && n < 300; r++) key[n++] = *r;
    if (!len || n + len >= sizeof key / sizeof key[0]) return REGDB_E_CLASSNOTREG;
    for (UINT32 i = 0; i < len; i++) {
        if (!name[i] || name[i] == '\\') return REGDB_E_CLASSNOTREG;
        key[n++] = name[i];
    }
    key[n] = 0;
    DWORD type = 0, size = sizeof type;
    if (!RegGetValueW(hive, key, L"ActivationType", RRF_RT_REG_DWORD, 0, &type, &size) && type != 0)
        return REGDB_E_CLASSNOTREG;                   /* out of process: not served yet */
    size = sizeof path;
    if (RegGetValueW(hive, key, L"DllPath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, 0, path, &size))
        return REGDB_E_CLASSNOTREG;
    if (!out) return S_OK;                            /* only asked whether it is there */
    HMODULE mod = LoadLibraryExW(path, 0, 0);
    if (!mod) return CO_E_DLLNOTFOUND;
    typedef HRESULT (WINAPI *GetFactory)(HSTRING, IUnknown **);
    GetFactory get = (GetFactory)GetProcAddress(mod, "DllGetActivationFactory");
    if (!get) return CO_E_ERRORINDLL;
    IUnknown *f = 0;
    HRESULT hr = get(cls, &f);
    if (FAILED(hr)) return hr;
    if (!f) return E_UNEXPECTED;
    hr = f->lpVtbl->QueryInterface(f, iid, out);
    f->lpVtbl->Release(f);
    return hr;
}

LONG WINAPI GetCurrentPackageInfo(UINT32 flags, UINT32 *len, BYTE *buf, UINT32 *count);
typedef struct { UINT32 reserved, flags; PWSTR path, full, family; BYTE id[48]; } PackageInfo_;
static HRESULT registry_factory(HSTRING cls, const WCHAR *name, UINT32 len, REFIID iid, void **out)
{
    static const WCHAR pkg[] = L"Software\\Classes\\ActivatableClasses\\Package\\";
    WCHAR root[300];
    UINT32 n = 0, count = 0;
    /* the package graph's classes: the package's own, then its frameworks'
     * (kernel32's GetCurrentPackageInfo, in the order the loader searches) */
    if (GetCurrentPackageInfo(0, &n, NULL, &count) == ERROR_INSUFFICIENT_BUFFER && n) {
        BYTE *buf = HeapAlloc(GetProcessHeap(), 0, n);
        if (buf && !GetCurrentPackageInfo(0, &n, buf, &count)) {
            for (UINT32 i = 0; i < count; i++) {
                const WCHAR *full = ((PackageInfo_ *)buf)[i].full;
                UINT32 at = 0;
                for (const WCHAR *r = pkg; *r; r++) root[at++] = *r;
                for (const WCHAR *r = full; r && *r && at < 250; r++) root[at++] = *r;
                for (const WCHAR *r = L"\\ActivatableClassId\\"; *r; r++) root[at++] = *r;
                root[at] = 0;
                HRESULT hr = registry_factory_in(HKEY_CURRENT_USER, root, cls, name, len, iid, out);
                if (hr != REGDB_E_CLASSNOTREG) { HeapFree(GetProcessHeap(), 0, buf); return hr; }
            }
        }
        if (buf) HeapFree(GetProcessHeap(), 0, buf);
    }
    return registry_factory_in(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsRuntime\\ActivatableClassId\\",
                               cls, name, len, iid, out);
}

/* Whether the registry names an in-process server for class @name, without
 * loading it (ApiInformation.IsTypePresent, winrt_classes.c) */
BOOL ole32_winrt_registered(const WCHAR *name, UINT32 len)
{
    return registry_factory(0, name, len, 0, 0) == S_OK;
}

/* The factories a process registers for its own classes
 * (RoRegisterActivationFactories, as an executable that serves Windows
 * Runtime classes registers them): each registration is a set of class
 * names, each with the function that makes its factory; RoGetActivationFactory
 * looks among them first, newest registration first, until
 * RoRevokeActivationFactories takes the set away. */
typedef HRESULT (WINAPI *PFNGETACTIVATIONFACTORY)(HSTRING, IUnknown **);
typedef void *RO_REGISTRATION_COOKIE;
typedef struct Registration {
    struct Registration *next;
    UINT32 count;
    HSTRING *names;
    PFNGETACTIVATIONFACTORY *makers;
} Registration;
static Registration *g_registrations;
static SRWLOCK g_registrations_lock = SRWLOCK_INIT;

WINOLEAPI_(HRESULT) RoRegisterActivationFactories(HSTRING *classes, PFNGETACTIVATIONFACTORY *makers, UINT32 count,
                                                  RO_REGISTRATION_COOKIE *cookie)
{
    if (!cookie) return E_POINTER;
    *cookie = 0;
    if (!classes || !makers || !count) return E_INVALIDARG;
    Registration *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                sizeof *r + count * (sizeof(HSTRING) + sizeof(PFNGETACTIVATIONFACTORY)));
    if (!r) return E_OUTOFMEMORY;
    r->count = count;
    r->names = (HSTRING *)(r + 1);
    r->makers = (PFNGETACTIVATIONFACTORY *)(r->names + count);
    for (UINT32 i = 0; i < count; i++) {
        if (!classes[i] || !makers[i] || FAILED(WindowsDuplicateString(classes[i], &r->names[i]))) {
            for (UINT32 j = 0; j < i; j++) WindowsDeleteString(r->names[j]);
            HeapFree(GetProcessHeap(), 0, r);
            return E_INVALIDARG;
        }
        r->makers[i] = makers[i];
    }
    AcquireSRWLockExclusive(&g_registrations_lock);
    r->next = g_registrations;
    g_registrations = r;
    ReleaseSRWLockExclusive(&g_registrations_lock);
    *cookie = (RO_REGISTRATION_COOKIE)r;
    return S_OK;
}

WINOLEAPI_(void) RoRevokeActivationFactories(RO_REGISTRATION_COOKIE cookie)
{
    Registration *found = 0;
    AcquireSRWLockExclusive(&g_registrations_lock);
    for (Registration **pp = &g_registrations; *pp; pp = &(*pp)->next)
        if (*pp == (Registration *)cookie) { found = *pp; *pp = found->next; break; }
    ReleaseSRWLockExclusive(&g_registrations_lock);
    if (!found) return;
    for (UINT32 i = 0; i < found->count; i++) WindowsDeleteString(found->names[i]);
    HeapFree(GetProcessHeap(), 0, found);
}

static HRESULT registered_factory(HSTRING cls, const WCHAR *name, UINT32 len, REFIID iid, void **out)
{
    PFNGETACTIVATIONFACTORY maker = 0;
    AcquireSRWLockShared(&g_registrations_lock);
    for (Registration *r = g_registrations; r && !maker; r = r->next)
        for (UINT32 i = 0; i < r->count && !maker; i++) {
            UINT32 n;
            const WCHAR *s = WindowsGetStringRawBuffer(r->names[i], &n);
            if (n == len && CompareStringOrdinal(s, (int)n, name, (int)len, FALSE) == CSTR_EQUAL) maker = r->makers[i];
        }
    ReleaseSRWLockShared(&g_registrations_lock);
    if (!maker) return REGDB_E_CLASSNOTREG;
    IUnknown *f = 0;
    HRESULT hr = maker(cls, &f);
    if (FAILED(hr)) return hr;
    if (!f) return E_UNEXPECTED;
    hr = f->lpVtbl->QueryInterface(f, iid, out);
    f->lpVtbl->Release(f);
    return hr;
}

WINOLEAPI_(HRESULT) RoGetActivationFactory(HSTRING cls, REFIID iid, void **f)
{
    if (!f) return E_POINTER;
    *f = 0;
    UINT32 len;
    const WCHAR *name = WindowsGetStringRawBuffer(cls, &len);
    HRESULT hr = registered_factory(cls, name, len, iid, f);
    if (hr == REGDB_E_CLASSNOTREG) hr = ole32_winrt_factory(name, len, iid, f);
    if (hr == REGDB_E_CLASSNOTREG) hr = registry_factory(cls, name, len, iid, f);
    if (SUCCEEDED(hr)) return hr;
    /* (on the debugger's output: the class, and why when it has a factory
     * that failed, such as a factory interface it does not have) */
    char msg[200] = "ole32: no Windows Runtime class ";
    UINT32 at = 32;
    if (hr != REGDB_E_CLASSNOTREG) {
        static const char hex[] = "0123456789abcdef";
        const char *t = "ole32: factory failed 0x";
        for (at = 0; t[at]; at++) msg[at] = t[at];
        for (int sh = 28; sh >= 0; sh -= 4) msg[at++] = hex[((ULONG)hr >> sh) & 15];
        msg[at++] = ':'; msg[at++] = ' ';
    }
    for (UINT32 i = 0; i < len && at < sizeof msg - 2; i++) msg[at++] = name[i] < 0x80 ? (char)name[i] : '?';
    if (hr != REGDB_E_CLASSNOTREG && iid && at < sizeof msg - 48) {
        /* (with the interface asked for: {Data1-...}) */
        static const char hex[] = "0123456789abcdef";
        const BYTE *g = (const BYTE *)iid;
        static const signed char order[] = {3, 2, 1, 0, -1, 5, 4, -1, 7, 6, -1, 8, 9, -1, 10, 11, 12, 13, 14, 15};
        msg[at++] = ' '; msg[at++] = '{';
        for (int i = 0; i < 20; i++) {
            if (order[i] < 0) { msg[at++] = '-'; continue; }
            msg[at++] = hex[g[order[i]] >> 4]; msg[at++] = hex[g[order[i]] & 15];
        }
        msg[at++] = '}';
    }
    msg[at++] = '\n';
    msg[at] = 0;
    OutputDebugStringA(msg);
    return hr;
}
/* The class's factory's IActivationFactory::ActivateInstance, the slot
 * after IInspectable's six */
WINOLEAPI_(HRESULT) RoActivateInstance(HSTRING cls, void **inst)
{
    if (!inst) return E_POINTER;
    *inst = 0;
    IUnknown *f;
    HRESULT hr = RoGetActivationFactory(cls, &IID_IActivationFactory__, (void **)&f);
    if (FAILED(hr)) return hr;
    typedef HRESULT (STDMETHODCALLTYPE *Activate)(IUnknown *, void **);
    hr = ((Activate)((void **)f->lpVtbl)[6])(f, inst);
    f->lpVtbl->Release(f);
    return hr;
}
/* RoParseTypeName (wintypes.dll's, api-ms-win-ro-typeresolution): a type
 * name split into its parts, a generic's name then its arguments' parts in
 * order: "A`1<B, C`1<D>>" -> "A`1", "B", "C`1", "D".  XAML's reflection
 * reads a box's element type this way (IReference`1<T>). */
#ifndef RO_E_METADATA_INVALID_TYPE_FORMAT
#define RO_E_METADATA_INVALID_TYPE_FORMAT ((HRESULT)0x80000011L)
#endif
WINOLEAPI_(HRESULT) RoParseTypeName(HSTRING name, DWORD *count, HSTRING **parts)
{
    if (!count || !parts) return E_INVALIDARG;
    *count = 0;
    *parts = 0;
    UINT32 n;
    const WCHAR *s = WindowsGetStringRawBuffer(name, &n);
    if (!n) return E_INVALIDARG;
    DWORD max = 1, got = 0;
    for (UINT32 i = 0; i < n; i++)
        if (s[i] == '<' || s[i] == ',') max++;
    HSTRING *a = (HSTRING *)CoTaskMemAlloc(max * sizeof(HSTRING));
    if (!a) return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    int depth = 0;
    UINT32 start = 0;
    for (UINT32 i = 0; i <= n && SUCCEEDED(hr); i++) {
        WCHAR c = i < n ? s[i] : 0;
        if (c != '<' && c != '>' && c != ',' && c) continue;
        UINT32 b = start, e = i;
        while (b < e && s[b] == ' ') b++;
        while (e > b && s[e - 1] == ' ') e--;
        if (e > b) {
            if (got == max) { hr = RO_E_METADATA_INVALID_TYPE_FORMAT; break; }
            hr = WindowsCreateString(s + b, e - b, &a[got]);
            if (SUCCEEDED(hr)) got++;
        } else if (c == '<' || c == ',' || (c == 0 && !got)) {
            hr = RO_E_METADATA_INVALID_TYPE_FORMAT;   /* an empty part */
        }
        if (c == '<') depth++;
        else if (c == '>' && --depth < 0) hr = RO_E_METADATA_INVALID_TYPE_FORMAT;
        start = i + 1;
    }
    if (SUCCEEDED(hr) && depth) hr = RO_E_METADATA_INVALID_TYPE_FORMAT;
    if (FAILED(hr)) {
        while (got) WindowsDeleteString(a[--got]);
        CoTaskMemFree(a);
        return hr;
    }
    *count = got;
    *parts = a;
    return S_OK;
}
/* The Windows Runtime's error objects: RoOriginateError makes an
 * IRestrictedErrorInfo (the error and its message) the thread's, which
 * GetRestrictedErrorInfo takes (and clears) and SetRestrictedErrorInfo
 * replaces.  NovaOS keeps no capability SID or reference, and no stack.
 * RoGetErrorReportingFlags answers RO_ERROR_REPORTING_NONE: originating an
 * error does not also set the COM error object (SetErrorInfo), so callers
 * that want one (XAML's error helper) set it themselves, as they do on
 * Windows under those flags. */
typedef struct RErr RErr;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(RErr *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(RErr *);
    ULONG   (STDMETHODCALLTYPE *Release)(RErr *);
    HRESULT (STDMETHODCALLTYPE *GetErrorDetails)(RErr *, WCHAR **, HRESULT *, WCHAR **, WCHAR **);
    HRESULT (STDMETHODCALLTYPE *GetReference)(RErr *, WCHAR **);
} RErrVtbl;
struct RErr { const RErrVtbl *lpVtbl; LONG refs; HRESULT hr; WCHAR *msg; UINT32 len; };
static const GUID IID_IRestrictedErrorInfo_ = { 0x82BA7092, 0x4C88, 0x427D, { 0xA7, 0xBC, 0x16, 0xDD, 0x93, 0xFE, 0xB6, 0x7E } };
static const GUID IID_IUnknown_re = { 0, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static DWORD g_rerr_tls = TLS_OUT_OF_INDEXES;
static INIT_ONCE g_rerr_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK rerr_tls_init(INIT_ONCE *o, void *p, void **c) { (void)o; (void)p; (void)c; g_rerr_tls = TlsAlloc(); return TRUE; }
static DWORD rerr_tls(void) { InitOnceExecuteOnce(&g_rerr_once, rerr_tls_init, 0, 0); return g_rerr_tls; }
/* a WCHAR * as oleaut32 makes them (its length before it, in CoTaskMem) */
static WCHAR *rerr_bstr(const WCHAR *s, UINT32 n)
{
    BYTE *p = CoTaskMemAlloc(sizeof(DWORD) + n * sizeof(WCHAR) + sizeof(WCHAR));
    if (!p) return 0;
    *(DWORD *)p = n * sizeof(WCHAR);
    WCHAR *b = (WCHAR *)(p + sizeof(DWORD));
    for (UINT32 i = 0; i < n; i++) b[i] = s[i];
    b[n] = 0;
    return b;
}
static HRESULT STDMETHODCALLTYPE rerr_qi(RErr *e, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_re) || IsEqualGUID(iid, &IID_IRestrictedErrorInfo_)) {
        *out = e;
        InterlockedIncrement(&e->refs);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE rerr_addref(RErr *e) { return (ULONG)InterlockedIncrement(&e->refs); }
static ULONG STDMETHODCALLTYPE rerr_release(RErr *e)
{
    LONG r = InterlockedDecrement(&e->refs);
    if (!r) { HeapFree(GetProcessHeap(), 0, e->msg); HeapFree(GetProcessHeap(), 0, e); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE rerr_details(RErr *e, WCHAR **desc, HRESULT *hr, WCHAR **rdesc, WCHAR **sid)
{
    if (!desc || !hr || !rdesc || !sid) return E_POINTER;
    *hr = e->hr;
    *desc = rerr_bstr(e->msg ? e->msg : L"", e->msg ? e->len : 0);
    *rdesc = rerr_bstr(e->msg ? e->msg : L"", e->msg ? e->len : 0);
    *sid = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE rerr_reference(RErr *e, WCHAR **ref) { (void)e; if (!ref) return E_POINTER; *ref = 0; return S_OK; }
static const RErrVtbl rerr_vtbl = { rerr_qi, rerr_addref, rerr_release, rerr_details, rerr_reference };

WINOLEAPI SetRestrictedErrorInfo(IUnknown *info)
{
    DWORD tls = rerr_tls();
    if (tls == TLS_OUT_OF_INDEXES) return E_OUTOFMEMORY;
    IUnknown *old = TlsGetValue(tls);
    if (info) info->lpVtbl->AddRef(info);
    TlsSetValue(tls, info);
    if (old) old->lpVtbl->Release(old);
    return S_OK;
}
WINOLEAPI GetRestrictedErrorInfo(IUnknown **info)
{
    if (!info) return E_POINTER;
    DWORD tls = rerr_tls();
    *info = tls == TLS_OUT_OF_INDEXES ? 0 : TlsGetValue(tls);
    if (!*info) return S_FALSE;
    TlsSetValue(tls, 0);                                /* (the caller now holds the thread's reference) */
    return S_OK;
}
WINOLEAPI RoGetErrorReportingFlags(UINT32 *flags)
{
    if (!flags) return E_POINTER;
    *flags = 0;                                         /* RO_ERROR_REPORTING_NONE */
    return S_OK;
}
/* A message of len characters (0: up to its NUL), as Windows keeps them:
 * at most 512 characters */
/* The caller's stack on the debugger's output, each frame as
 * module+offset (a fail fast's, so the failing call can be found) */
__declspec(dllimport) USHORT __stdcall RtlCaptureStackBackTrace(ULONG skip, ULONG count, PVOID *frames, PULONG hash);
static void trace_stack(const char *what)
{
    void *frames[24];
    USHORT n = RtlCaptureStackBackTrace(1, 24, frames, 0);
    for (USHORT i = 0; i < n; i++) {
        HMODULE m = 0;
        char name[MAX_PATH] = "?", line[MAX_PATH + 64];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (const char *)frames[i], &m) && m) {
            GetModuleFileNameA(m, name, sizeof name);
        }
        const char *base = name;
        for (const char *c = name; *c; c++)
            if (*c == '\\') base = c + 1;
        wsprintfA(line, "%s: #%u %s+0x%lx\n", what, (unsigned)i, base,
                  (unsigned long)((char *)frames[i] - (char *)m));
        OutputDebugStringA(line);
    }
}
WINOLEAPI_(BOOL) RoOriginateErrorW(HRESULT hr, UINT32 len, const WCHAR *msg)
{
    if (SUCCEEDED(hr)) return FALSE;
    RErr *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *e);
    if (!e) return FALSE;
    e->lpVtbl = &rerr_vtbl;
    e->refs = 1;
    e->hr = hr;
    if (msg) {
        UINT32 n = 0;
        while ((len == 0 || n < len) && msg[n] && n < 512) n++;
        e->msg = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
        if (e->msg) {
            for (UINT32 i = 0; i < n; i++) e->msg[i] = msg[i];
            e->msg[n] = 0;
            e->len = n;
        }
    }
    SetRestrictedErrorInfo((IUnknown *)e);
    rerr_release(e);
    return TRUE;
}
WINOLEAPI_(BOOL) RoOriginateError(HRESULT hr, HSTRING msg)
{
    UINT32 n = 0;
    const WCHAR *s = msg ? WindowsGetStringRawBuffer(msg, &n) : 0;
    return RoOriginateErrorW(hr, n, s);
}
/* An error a caller changed into another (C++/WinRT's
 * hresult_error::to_abi when it maps one HRESULT to another): the new one
 * is originated with the message, as an error of its own; nothing is
 * reported when the new one is a success or the same error */
WINOLEAPI_(BOOL) RoTransformErrorW(HRESULT old_hr, HRESULT new_hr, UINT32 len, const WCHAR *msg)
{
    if (SUCCEEDED(new_hr) || new_hr == old_hr) return FALSE;
    return RoOriginateErrorW(new_hr, len, msg);
}
WINOLEAPI_(BOOL) RoTransformError(HRESULT old_hr, HRESULT new_hr, HSTRING msg)
{
    UINT32 n = 0;
    const WCHAR *s = msg ? WindowsGetStringRawBuffer(msg, &n) : 0;
    return RoTransformErrorW(old_hr, new_hr, n, s);
}
/* An error no handler took: Windows raises CoreApplication's
 * UnhandledErrorDetected and FinalUnhandledErrorDetected events, which
 * NovaOS's CoreApplication does not have yet; so this reports the error
 * (its HRESULT and message) on the debugger's output and fails, which
 * tells the caller the events were not raised (XAML's error helper then
 * handles the error itself) */
WINOLEAPI RoReportUnhandledError(IUnknown *info)
{
    if (!info) return E_INVALIDARG;
    char line[1024];
    HRESULT hr = 0;
    WCHAR *desc = 0, *rdesc = 0, *sid = 0;
    RErr *e = 0;
    if (SUCCEEDED(info->lpVtbl->QueryInterface(info, &IID_IRestrictedErrorInfo_, (void **)&e)) && e) {
        e->lpVtbl->GetErrorDetails(e, &desc, &hr, &rdesc, &sid);
        e->lpVtbl->Release(e);
    }
    wsprintfA(line, "RoReportUnhandledError: an unhandled Windows Runtime error 0x%08lx: %.500ls\n", (unsigned long)hr,
              rdesc && *rdesc ? rdesc : desc ? desc : L"");
    OutputDebugStringA(line);
    if (desc) CoTaskMemFree((BYTE *)desc - sizeof(DWORD));
    if (rdesc) CoTaskMemFree((BYTE *)rdesc - sizeof(DWORD));
    if (sid) CoTaskMemFree((BYTE *)sid - sizeof(DWORD));
    return E_NOTIMPL;
}
/* A fail fast with the Windows Runtime errors stowed for it (combase's;
 * XAML's error context calls it, components/base/errorcontext.cpp): the
 * error goes on the debugger's output and the process ends with it, as
 * Windows ends it (without the crash report) */
WINOLEAPI_(void) RoFailFastWithErrorContextInternal2(HRESULT hr, ULONG count, void **stowed)
{
    char msg[96];
    (void)stowed;
    wsprintfA(msg, "RoFailFastWithErrorContextInternal2: fail fast 0x%08lx (%lu stowed)\n", (unsigned long)hr,
              (unsigned long)count);
    OutputDebugStringA(msg);
    trace_stack("fail fast");
    TerminateProcess(GetCurrentProcess(), (UINT)hr);
}
/* C++/WinRT's hresult_error and XAML's error info (dxaml/lib ErrorInfo.cpp)
 * report through this before they throw or return: the error is originated
 * as RoOriginateError originates it (the thread's IRestrictedErrorInfo, which
 * they then take with GetRestrictedErrorInfo); the language exception, whose
 * stack Windows would keep for a debugger, is not kept */
WINOLEAPI_(BOOL) RoOriginateLanguageException(HRESULT hr, HSTRING msg, IUnknown *e) { (void)e; return RoOriginateError(hr, msg); }

/* Agile references: every object here can be used from any thread, so
 * the reference holds the object and Resolve is a QueryInterface */
typedef struct AgileRef AgileRef;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(AgileRef *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(AgileRef *);
    ULONG   (STDMETHODCALLTYPE *Release)(AgileRef *);
    HRESULT (STDMETHODCALLTYPE *Resolve)(AgileRef *, REFIID, void **);
} AgileRefVtbl;
struct AgileRef { const AgileRefVtbl *lpVtbl; LONG refs; IUnknown *obj; };
static const GUID IID_IAgileReference_ = { 0xc03f6a43, 0x65a4, 0x9818, { 0x98, 0x7e, 0xe0, 0xb8, 0x10, 0xd2, 0xa6, 0xf2 } };
static const GUID IID_IUnknown__ = { 0, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static HRESULT STDMETHODCALLTYPE ar_qi(AgileRef *a, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown__) || IsEqualGUID(iid, &IID_IAgileReference_)) {
        *out = a;
        InterlockedIncrement(&a->refs);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ar_addref(AgileRef *a) { return (ULONG)InterlockedIncrement(&a->refs); }
static ULONG STDMETHODCALLTYPE ar_release(AgileRef *a)
{
    LONG r = InterlockedDecrement(&a->refs);
    if (!r) { a->obj->lpVtbl->Release(a->obj); HeapFree(GetProcessHeap(), 0, a); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE ar_resolve(AgileRef *a, REFIID iid, void **out) { return a->obj->lpVtbl->QueryInterface(a->obj, iid, out); }
static const AgileRefVtbl g_agile_vtbl = { ar_qi, ar_addref, ar_release, ar_resolve };

WINOLEAPI_(HRESULT) RoGetAgileReference(int options, REFIID iid, IUnknown *obj, void **out)
{
    (void)options;
    if (!obj || !out) return E_INVALIDARG;
    *out = 0;
    void *check;
    HRESULT hr = obj->lpVtbl->QueryInterface(obj, iid, &check);    /* the object must have @iid */
    if (FAILED(hr)) return hr;
    ((IUnknown *)check)->lpVtbl->Release((IUnknown *)check);
    AgileRef *a = HeapAlloc(GetProcessHeap(), 0, sizeof(AgileRef));
    if (!a) return E_OUTOFMEMORY;
    a->lpVtbl = &g_agile_vtbl;
    a->refs = 1;
    a->obj = obj;
    obj->lpVtbl->AddRef(obj);
    *out = a;
    return S_OK;
}

/* Channel hooks see cross-apartment calls; there are none */
WINOLEAPI_(HRESULT) CoRegisterChannelHook(REFGUID ext, void *hook) { (void)ext; (void)hook; return S_OK; }

/* OleDuplicateData: a copy of clipboard data; HGLOBAL-backed formats
 * (all but bitmaps, metafiles and palettes) by copying the memory */
WINOLEAPI_(HANDLE) OleDuplicateData(HANDLE src, WORD fmt, UINT flags)
{
    if (!src || fmt == 2 /* CF_BITMAP */ || fmt == 3 /* CF_METAFILEPICT */ || fmt == 9 /* CF_PALETTE */ ||
        fmt == 14 /* CF_ENHMETAFILE */) return 0;
    SIZE_T n = GlobalSize(src);
    HGLOBAL dst = GlobalAlloc(flags ? flags : GMEM_MOVEABLE, n);
    if (!dst) return 0;
    void *d = GlobalLock(dst), *s = GlobalLock(src);
    if (d && s) CopyMemory(d, s, n);
    if (s) GlobalUnlock(src);
    if (d) GlobalUnlock(dst);
    return dst;
}

/* No data object carries an OLE link */
WINOLEAPI_(HRESULT) OleQueryLinkFromData(IUnknown *data) { (void)data; return S_FALSE; }

/* Compound files (structured storage) cannot be created */
WINOLEAPI_(HRESULT) StgCreateStorageEx(const WCHAR *name, DWORD mode, DWORD fmt, DWORD attrs, void *opts, void *sd,
                                      REFIID iid, void **out)
{
    (void)name; (void)mode; (void)fmt; (void)attrs; (void)opts; (void)sd; (void)iid;
    if (out) *out = 0;
    return (HRESULT)0x80030001L;                    /* STG_E_INVALIDFUNCTION */
}
