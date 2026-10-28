/*
 * Windows.Storage.ApplicationData.dll — the app's own data store, as on
 * Windows: ApplicationData.Current and its settings containers.
 *
 * LocalSettings (and RoamingSettings, which stays on the machine as it does
 * on a PC not signed in to a Microsoft account) is an
 * ApplicationDataContainer whose Values are a PropertySet
 * (userland/winrtmap); CreateContainer makes or opens a named container
 * below it.
 *
 * Not here yet: the settings are kept for the process's life only (Windows
 * keeps them in the package's Settings\settings.dat); the data folders
 * (LocalFolder, LocalCacheFolder, RoamingFolder, TemporaryFolder), which
 * are StorageFolders; versions, clearing and DataChanged's raising
 * (handlers are accepted).
 */
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include "../winrtmap/strmap.h"

#define APPDATA_API __declspec(dllexport)
typedef BYTE boolean_t;
static const GUID IID_IUnknown_ = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_ = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_ = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IApplicationDataStatics_ = { 0x5612147B, 0xE843, 0x45E3, { 0x94, 0xD8, 0x06, 0x16, 0x9E, 0x3C, 0x8E, 0x17 } };
static const GUID IID_IApplicationData_ = { 0xC3DA6FB7, 0xB744, 0x4B45, { 0xB0, 0xB8, 0x22, 0x3A, 0x09, 0x38, 0xD0, 0xDC } };
static const GUID IID_IApplicationData2_ = { 0x9E65CD69, 0x0BA3, 0x4E32, { 0xBE, 0x29, 0xB0, 0x2D, 0xE6, 0x60, 0x76, 0x38 } };
static const GUID IID_IApplicationDataContainer_ = { 0xC5AEFD1E, 0xF467, 0x40BA, { 0x85, 0x66, 0xAB, 0x64, 0x0A, 0x44, 0x1E, 0x1D } };
static const GUID IID_IPropertySet_ = { 0x8A43ED9F, 0xF4E6, 0x4421, { 0xAC, 0xF9, 0x1D, 0xAB, 0x29, 0x86, 0x82, 0x0C } };
#define VT(o) (*(void ***)(o))

static HRESULT make_hstring(const WCHAR *s, HSTRING *out)
{
    if (!out) return E_POINTER;
    return WindowsCreateString(s, (UINT32)lstrlenW(s), out);
}
static HRESULT STDMETHODCALLTYPE no_iids(void *p, ULONG *n, GUID **iids) { (void)p; if (!n || !iids) return E_POINTER; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *p, int *t) { (void)p; if (!t) return E_POINTER; *t = 0; return S_OK; }
static ULONG STDMETHODCALLTYPE static_ref(void *p) { (void)p; return 2; }
static BOOL is_base(REFIID iid)
{
    return IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_);
}
#define INSP_FNS(qi, name) (void *)(qi), (void *)static_ref, (void *)static_ref, (void *)no_iids, (void *)(name), (void *)base_trust
static SRWLOCK g_lock = SRWLOCK_INIT;

/* ---- ApplicationDataContainer: kept for the process's life (so not
 * counted), its sub-containers in a list -------------------------------- */
typedef struct Container {
    const void *const *vt;
    WCHAR *name;
    int locality;                       /* ApplicationDataLocality: Local 0, Roaming 1, Temporary 2 */
    void *values;                       /* its PropertySet (IPropertySet) */
    struct Container *children, *next;
} Container;
static HRESULT STDMETHODCALLTYPE c_qi(void *p, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = is_base(iid) || IsEqualGUID(iid, &IID_IApplicationDataContainer_) ? p : 0;
    return *out ? S_OK : E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE c_class(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.Storage.ApplicationDataContainer", n); }
static HRESULT STDMETHODCALLTYPE c_name(Container *c, HSTRING *v) { return make_hstring(c->name, v); }
static HRESULT STDMETHODCALLTYPE c_locality(Container *c, int *v) { if (!v) return E_POINTER; *v = c->locality; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_values(Container *c, void **v)
{
    if (!v) return E_POINTER;
    ((ULONG (STDMETHODCALLTYPE *)(void *))VT(c->values)[1])(c->values);
    *v = c->values;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_containers(Container *c, void **v) { (void)c; if (v) *v = 0; return E_NOTIMPL; }
static Container *container_new(const WCHAR *name, int locality);
/* ApplicationDataCreateDisposition: Always 0 (made if missing), Existing 1 */
static HRESULT STDMETHODCALLTYPE c_create(Container *c, HSTRING name, int how, void **v)
{
    if (!v) return E_POINTER;
    *v = 0;
    const WCHAR *n = WindowsGetStringRawBuffer(name, 0);
    if (!n || !*n || lstrlenW(n) > 255) return E_INVALIDARG;
    AcquireSRWLockExclusive(&g_lock);
    Container *k = c->children;
    while (k && lstrcmpiW(k->name, n)) k = k->next;
    if (!k && how == 0 && (k = container_new(n, c->locality))) { k->next = c->children; c->children = k; }
    ReleaseSRWLockExclusive(&g_lock);
    if (!k) return how == 0 ? E_OUTOFMEMORY : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    *v = k;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_delete(Container *c, HSTRING name)
{
    const WCHAR *n = WindowsGetStringRawBuffer(name, 0);
    AcquireSRWLockExclusive(&g_lock);
    for (Container **k = &c->children; *k; k = &(*k)->next)
        if (!lstrcmpiW((*k)->name, n)) { *k = (*k)->next; break; }   /* (kept: an app may still hold it) */
    ReleaseSRWLockExclusive(&g_lock);
    return S_OK;
}
static const void *const container_vt[] = { INSP_FNS(c_qi, c_class), (void *)c_name, (void *)c_locality, (void *)c_values,
                                            (void *)c_containers, (void *)c_create, (void *)c_delete };
static Container *container_new(const WCHAR *name, int locality)
{
    Container *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *c);
    if (!c) return 0;
    void *map = 0;
    c->name = HeapAlloc(GetProcessHeap(), 0, (lstrlenW(name) + 1) * sizeof(WCHAR));
    if (!c->name || FAILED(nova_strmap_new(TRUE, &map)) ||
        FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, REFIID, void **))VT(map)[0])(map, &IID_IPropertySet_, &c->values))) {
        if (map) ((ULONG (STDMETHODCALLTYPE *)(void *))VT(map)[2])(map);
        HeapFree(GetProcessHeap(), 0, c->name);
        HeapFree(GetProcessHeap(), 0, c);
        return 0;
    }
    ((ULONG (STDMETHODCALLTYPE *)(void *))VT(map)[2])(map);
    lstrcpyW(c->name, name);
    c->vt = container_vt;
    c->locality = locality;
    return c;
}

/* ---- ApplicationData.Current ----------------------------------------- */
typedef struct {
    const void *const *v1, *const *v2;
    Container *local, *roaming;
    INT64 next_token;
} AppData;
static AppData g_data;
static HRESULT STDMETHODCALLTYPE d_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IApplicationData_)) *out = &g_data.v1;
    else if (IsEqualGUID(iid, &IID_IApplicationData2_)) *out = &g_data.v2;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE d_class(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.Storage.ApplicationData", n); }
static HRESULT STDMETHODCALLTYPE d_version(void *p, UINT32 *v) { (void)p; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_set_version(void *p, UINT32 n, void *h, void **v) { (void)p; (void)n; (void)h; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_clear_all(void *p, void **v) { (void)p; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_clear(void *p, int l, void **v) { (void)p; (void)l; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT settings(Container **slot, int locality, void **v)
{
    if (!v) return E_POINTER;
    AcquireSRWLockExclusive(&g_lock);
    if (!*slot) *slot = container_new(L"", locality);
    *v = *slot;
    ReleaseSRWLockExclusive(&g_lock);
    return *v ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE d_local(void *p, void **v) { (void)p; return settings(&g_data.local, 0, v); }
static HRESULT STDMETHODCALLTYPE d_roaming(void *p, void **v) { (void)p; return settings(&g_data.roaming, 1, v); }
static HRESULT STDMETHODCALLTYPE d_folder(void *p, void **v) { (void)p; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_add_changed(void *p, void *h, INT64 *t)
{
    (void)p;
    if (!h || !t) return E_INVALIDARG;
    *t = InterlockedIncrement64(&g_data.next_token);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE d_remove_changed(void *p, INT64 t) { (void)p; (void)t; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_signal(void *p) { (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_quota(void *p, UINT64 *v) { (void)p; if (!v) return E_POINTER; *v = 100 * 1024; return S_OK; }
static const void *const data_vt[] = { INSP_FNS(d_qi, d_class), (void *)d_version, (void *)d_set_version, (void *)d_clear_all,
                                       (void *)d_clear, (void *)d_local, (void *)d_roaming, (void *)d_folder, (void *)d_folder,
                                       (void *)d_folder, (void *)d_add_changed, (void *)d_remove_changed, (void *)d_signal,
                                       (void *)d_quota };
static const void *const data2_vt[] = { INSP_FNS(d_qi, d_class), (void *)d_folder };
static AppData g_data = { (const void *const *)data_vt, (const void *const *)data2_vt };

/* ---- its statics ------------------------------------------------------ */
typedef struct { const void *const *factory, *const *statics; } Statics;
static Statics g_statics;
static HRESULT STDMETHODCALLTYPE s_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (is_base(iid) || IsEqualGUID(iid, &IID_IActivationFactory_)) *out = &g_statics.factory;
    else if (IsEqualGUID(iid, &IID_IApplicationDataStatics_)) *out = &g_statics.statics;
    else { *out = 0; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_activate(void *p, void **v) { (void)p; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE s_current(void *p, void **v) { (void)p; if (!v) return E_POINTER; *v = &g_data.v1; return S_OK; }
static const void *const factory_vt[] = { INSP_FNS(s_qi, d_class), (void *)s_activate };
static const void *const statics_vt[] = { INSP_FNS(s_qi, d_class), (void *)s_current };
static Statics g_statics = { (const void *const *)factory_vt, (const void *const *)statics_vt };

APPDATA_API HRESULT WINAPI DllGetActivationFactory(HSTRING cls, void **factory)
{
    if (!factory) return E_POINTER;
    const WCHAR *n = WindowsGetStringRawBuffer(cls, 0);
    if (n && !lstrcmpW(n, L"Windows.Storage.ApplicationData")) { *factory = &g_statics.factory; return S_OK; }
    *factory = 0;
    return CLASS_E_CLASSNOTAVAILABLE;
}
APPDATA_API HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
