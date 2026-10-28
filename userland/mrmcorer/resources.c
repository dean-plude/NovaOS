/*
 * resources.c — the Windows Runtime's resource classes, in MrmCoreR.dll as
 * on Windows: Windows.ApplicationModel.Resources.Core's ResourceManager,
 * ResourceMap, ResourceCandidate and ResourceContext, and
 * Windows.ApplicationModel.Resources.ResourceLoader, over Microsoft's MRT
 * Core (MRM.dll, userland/mrm), which reads the resources.pri files.
 *
 * ResourceManager.Current's MainResourceMap is the process's resources.pri:
 * its package's (in the package's folder) or, for a program not in a
 * package, the one beside its program, as Windows reads an unpackaged
 * program's (XAML islands' WinUI 2 strings: NovaOS installs WinUI 2's
 * beside the programs in C:\Programs, as its NuGet package merges them into
 * an app's, userland/microsoft_ui_xaml/build.py).  AllResourceMaps has that
 * map, by its package's name.  ResourceMap.GetValue finds a string by its
 * name in the map (a path below it: "Resources/AppName") and GetSubtree
 * goes below it; a candidate's ValueAsString is the string (every
 * candidate is the neutral one: resources.pri qualifiers, languages,
 * scales and files are not chosen between).  ResourceLoader's GetString
 * reads its map's strings ("Resources" unless it was made for another).
 *
 * Not here: the maps' NamedResource collections (IMapView, IIterable),
 * LoadPriFiles, choosing by qualifiers (a ResourceContext has its values,
 * but the neutral candidate is used), file candidates.
 */
#include <windows.h>
#include <roapi.h>
#include "../winrtmap/strmap.h"

#define MRMCORER_API __declspec(dllexport)
#define ERROR_MRM_NAMED_RESOURCE_NOT_FOUND_ ((HRESULT)0x80073B17L)
#define E_BOUNDS_ ((HRESULT)0x8000000BL)
typedef BYTE boolean_t;

static const GUID IID_IUnknown_ = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_ = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_ = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IResourceManagerStatics = { 0x1CC0FDFC, 0x69EE, 0x4E43, { 0x99, 0x01, 0x47, 0xF1, 0x26, 0x87, 0xBA, 0xF7 } };
static const GUID IID_IResourceManager = { 0xF744D97B, 0x9988, 0x44FB, { 0xAB, 0xD6, 0x53, 0x78, 0x84, 0x4C, 0xFA, 0x8B } };
static const GUID IID_IResourceManager2 = { 0x9D66FE6C, 0xA4D7, 0x4C23, { 0x9E, 0x85, 0x67, 0x5F, 0x30, 0x4C, 0x25, 0x2D } };
static const GUID IID_IResourceMap = { 0x72284824, 0xDB8C, 0x42F8, { 0xB0, 0x8C, 0x53, 0xFF, 0x35, 0x7D, 0xAD, 0x82 } };
static const GUID IID_IResourceCandidate = { 0xAF5207D9, 0xC433, 0x4764, { 0xB3, 0xFD, 0x8F, 0xA6, 0xBF, 0xBC, 0xBA, 0xDC } };
static const GUID IID_IResourceContext = { 0x2FA22F4B, 0x707E, 0x4B27, { 0xAD, 0x0D, 0xD0, 0xD8, 0xCD, 0x46, 0x8F, 0xD2 } };
static const GUID IID_IResourceContextStatics = { 0x98BE9D6C, 0x6338, 0x4B31, { 0x99, 0xDF, 0xB2, 0xB4, 0x42, 0xF1, 0x71, 0x49 } };
static const GUID IID_IResourceContextStatics2 = { 0x41F752EF, 0x12AF, 0x41B9, { 0xAB, 0x36, 0xB1, 0xEB, 0x4B, 0x51, 0x24, 0x60 } };
static const GUID IID_IResourceContextStatics3 = { 0x20CF492C, 0xAF0F, 0x450B, { 0x9D, 0xA6, 0x10, 0x6D, 0xD0, 0xC2, 0x9A, 0x39 } };
/* IMapView<String, ResourceMap> (AllResourceMaps) */
static const GUID IID_IMapView_ResourceMap = { 0xD4349ADE, 0x93B1, 0x5325, { 0xBA, 0x5C, 0x05, 0xF3, 0x5E, 0xEF, 0xFC, 0x55 } };
static const GUID IID_IResourceLoader = { 0x08524908, 0x16EF, 0x45AD, { 0xA6, 0x02, 0x29, 0x36, 0x37, 0xD7, 0xE6, 0x1A } };
static const GUID IID_IResourceLoaderStatics = { 0xBF777CE1, 0x19C8, 0x49C2, { 0x95, 0x3C, 0x47, 0xE9, 0x22, 0x7B, 0x33, 0x4E } };
static const GUID IID_IResourceLoaderStatics2 = { 0x0CC04141, 0x6466, 0x4989, { 0x94, 0x94, 0x0B, 0x82, 0xDF, 0xC5, 0x3F, 0x1F } };
static const GUID IID_IResourceLoaderStatics3 = { 0x64609DFB, 0x64AC, 0x491B, { 0x81, 0x00, 0x0E, 0x55, 0x8D, 0x61, 0xC1, 0xD0 } };
static const GUID IID_IResourceLoaderFactory = { 0xC33A3603, 0x69DC, 0x4285, { 0xA0, 0x77, 0xD5, 0xC0, 0xE4, 0x7C, 0xCB, 0xE8 } };

LONG WINAPI GetCurrentPackagePath(UINT32 *len, PWSTR path);
LONG WINAPI GetCurrentPackageFamilyName(UINT32 *len, PWSTR name);

static int wlen(const WCHAR *s) { int n = 0; while (s[n]) n++; return n; }
static void *alloc(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void release_mem(void *p) { HeapFree(GetProcessHeap(), 0, p); }
static WCHAR *wjoin(const WCHAR *a, int an, const WCHAR *sep, const WCHAR *b, int bn)
{
    int sn = an && bn ? wlen(sep) : 0;
    WCHAR *d = alloc((size_t)(an + sn + bn + 1) * sizeof(WCHAR));
    if (!d) return 0;
    for (int i = 0; i < an; i++) d[i] = a[i];
    for (int i = 0; i < sn; i++) d[an + i] = sep[i];
    for (int i = 0; i < bn; i++) d[an + sn + i] = b[i];
    return d;
}
static const WCHAR *hbuf(HSTRING s, UINT32 *n)
{
    const WCHAR *c = WindowsGetStringRawBuffer(s, n);
    return c ? c : L"";
}
static BOOL name_is(HSTRING s, const WCHAR *want)
{
    UINT32 n, i = 0;
    const WCHAR *c = hbuf(s, &n);
    for (; i < n && want[i]; i++) if (c[i] != want[i]) return FALSE;
    return i == n && !want[i];
}
static HRESULT STDMETHODCALLTYPE no_iids(void *p, ULONG *n, GUID **iids) { (void)p; if (!n || !iids) return E_POINTER; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *p, int *t) { (void)p; if (!t) return E_POINTER; *t = BaseTrust; return S_OK; }
static HRESULT STDMETHODCALLTYPE not_here(void *p) { (void)p; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE not_here1(void *p, void *a) { (void)p; (void)a; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE not_here2(void *p, void *a, void *b) { (void)p; (void)a; (void)b; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE not_here3(void *p, void *a, void *b, void *c) { (void)p; (void)a; (void)b; (void)c; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE out_null(void *p, void **out) { (void)p; if (!out) return E_POINTER; *out = 0; return E_NOTIMPL; }

/* ---- MRT Core: the process's resources.pri ---------------------------------------- */
typedef HRESULT (WINAPI *MrmCreate_t)(PCWSTR pri, void **mgr);
typedef HRESULT (WINAPI *MrmLoad_t)(void *mgr, void *context, void *map, PCWSTR id, PWSTR *out);
typedef void (WINAPI *MrmFree_t)(void *p);
static SRWLOCK g_lock = SRWLOCK_INIT;
static BOOL g_tried;
static void *g_mgr;                     /* the main resources.pri's MRT manager, or NULL */
static WCHAR g_name[128];               /* its package's name (AllResourceMaps' key) */
static MrmLoad_t g_load;
static MrmFree_t g_free;
static void *main_pri(void)
{
    AcquireSRWLockExclusive(&g_lock);
    if (!g_tried) {
        g_tried = TRUE;
        WCHAR path[MAX_PATH + 16];
        UINT32 n = MAX_PATH, fn = 128;
        if (!GetCurrentPackagePath(&n, path)) {
            if (GetCurrentPackageFamilyName(&fn, g_name)) g_name[0] = 0;
            for (int i = 0; g_name[i]; i++) if (g_name[i] == '_') { g_name[i] = 0; break; }   /* (the name, before the publisher's id) */
        } else {
            DWORD l = GetModuleFileNameW(0, path, MAX_PATH);
            while (l && path[l - 1] != '\\') l--;
            path[l ? l - 1 : 0] = 0;
        }
        int at = wlen(path);
        for (const WCHAR *f = L"\\resources.pri"; *f; f++) path[at++] = *f;
        path[at] = 0;
        HMODULE mrm = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES ? LoadLibraryW(L"MRM.dll") : 0;
        MrmCreate_t create = mrm ? (MrmCreate_t)GetProcAddress(mrm, "MrmCreateResourceManager") : 0;
        g_load = mrm ? (MrmLoad_t)GetProcAddress(mrm, "MrmLoadStringResource") : 0;
        g_free = mrm ? (MrmFree_t)GetProcAddress(mrm, "MrmFreeResource") : 0;
        if (create && g_load && g_free && FAILED(create(path, &g_mgr))) g_mgr = 0;
    }
    void *m = g_mgr;
    ReleaseSRWLockExclusive(&g_lock);
    return m;
}
/* the string @id (a path from the map's root) in the main resources.pri */
static HRESULT load_string(const WCHAR *id, HSTRING *out)
{
    *out = 0;
    void *mgr = main_pri();
    if (!mgr) return ERROR_MRM_NAMED_RESOURCE_NOT_FOUND_;
    PWSTR s = 0;
    HRESULT hr = g_load(mgr, 0, 0, id, &s);
    if (FAILED(hr)) return hr == E_INVALIDARG ? ERROR_MRM_NAMED_RESOURCE_NOT_FOUND_ : hr;
    hr = WindowsCreateString(s, (UINT32)wlen(s), out);
    g_free(s);
    return hr;
}

/* ---- objects: one interface (a few for the statics), counted ------------------------ */
typedef struct Obj {
    const void *const *vt;
    LONG refs;
    int kind;                           /* K_* */
    WCHAR *path;                        /* a map's place below the root ("" the root); a loader's map */
    HSTRING value;                      /* a candidate's string */
} Obj;
enum { K_MAP, K_CANDIDATE, K_CONTEXT, K_LOADER };
static const WCHAR *const k_names[] = {
    L"Windows.ApplicationModel.Resources.Core.ResourceMap", L"Windows.ApplicationModel.Resources.Core.ResourceCandidate",
    L"Windows.ApplicationModel.Resources.Core.ResourceContext", L"Windows.ApplicationModel.Resources.ResourceLoader",
};
static const GUID *const k_iids[] = { &IID_IResourceMap, &IID_IResourceCandidate, &IID_IResourceContext, &IID_IResourceLoader };
static HRESULT STDMETHODCALLTYPE o_qi(void *p, REFIID iid, void **out)
{
    Obj *o = p;
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, k_iids[o->kind])) {
        *out = o;
        InterlockedIncrement(&o->refs);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE o_addref(void *p) { return (ULONG)InterlockedIncrement(&((Obj *)p)->refs); }
static ULONG STDMETHODCALLTYPE o_release(void *p)
{
    Obj *o = p;
    LONG r = InterlockedDecrement(&o->refs);
    if (!r) {
        release_mem(o->path);
        if (o->value) WindowsDeleteString(o->value);
        release_mem(o);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE o_name(void *p, HSTRING *n)
{
    const WCHAR *s = k_names[((Obj *)p)->kind];
    return WindowsCreateString(s, (UINT32)wlen(s), n);
}
#define BASE (void *)o_qi, (void *)o_addref, (void *)o_release, (void *)no_iids, (void *)o_name, (void *)base_trust
static Obj *obj_new(int kind, const void *const *vt, const WCHAR *path, int n)
{
    Obj *o = alloc(sizeof *o);
    if (!o) return 0;
    o->vt = vt; o->refs = 1; o->kind = kind;
    if (path && !(o->path = wjoin(path, n, L"", L"", 0))) { release_mem(o); return 0; }
    return o;
}

/* IResourceCandidate: Qualifiers, IsMatch, IsMatchAsDefault, IsDefault (every
 * candidate the neutral one), ValueAsString, GetValueAsFileAsync, GetQualifierValue */
static HRESULT STDMETHODCALLTYPE rc_true(void *p, boolean_t *v) { (void)p; if (!v) return E_POINTER; *v = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE rc_string(void *p, HSTRING *v) { if (!v) return E_POINTER; return WindowsDuplicateString(((Obj *)p)->value, v); }
static const void *const candidate_vt[] = { BASE, (void *)out_null, (void *)rc_true, (void *)rc_true, (void *)rc_true, (void *)rc_string,
                                            (void *)out_null, (void *)not_here2 };

/* IResourceMap: Uri (not here), GetValue(name[, context]), GetSubtree */
static const void *const map_vt[];
static HRESULT map_value(Obj *m, HSTRING name, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    UINT32 n;
    const WCHAR *c = hbuf(name, &n);
    while (n && *c == '/') { c++; n--; }
    if (!n) return E_INVALIDARG;
    WCHAR *id = wjoin(m->path, wlen(m->path), L"/", c, (int)n);
    if (!id) return E_OUTOFMEMORY;
    HSTRING s;
    HRESULT hr = load_string(id, &s);
    release_mem(id);
    if (FAILED(hr)) return hr;
    Obj *cand = obj_new(K_CANDIDATE, candidate_vt, 0, 0);
    if (!cand) { WindowsDeleteString(s); return E_OUTOFMEMORY; }
    cand->value = s;
    *out = cand;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE map_get(void *p, HSTRING name, void **out) { return map_value(p, name, out); }
static HRESULT STDMETHODCALLTYPE map_get_in(void *p, HSTRING name, void *context, void **out) { (void)context; return map_value(p, name, out); }
static HRESULT STDMETHODCALLTYPE map_subtree(void *p, HSTRING name, void **out)
{
    Obj *m = p;
    if (!out) return E_POINTER;
    *out = 0;
    UINT32 n;
    const WCHAR *c = hbuf(name, &n);
    while (n && *c == '/') { c++; n--; }
    while (n && c[n - 1] == '/') n--;
    WCHAR *path = wjoin(m->path, wlen(m->path), L"/", c, (int)n);
    if (!path) return E_OUTOFMEMORY;
    Obj *sub = obj_new(K_MAP, map_vt, path, wlen(path));
    release_mem(path);
    if (!sub) return E_OUTOFMEMORY;
    *out = sub;
    return S_OK;
}
static const void *const map_vt[] = { BASE, (void *)out_null, (void *)map_get, (void *)map_get_in, (void *)map_subtree };
static HRESULT root_map(void **out)
{
    if (!out) return E_POINTER;
    Obj *m = obj_new(K_MAP, map_vt, L"", 0);
    *out = m;
    return m ? S_OK : E_OUTOFMEMORY;
}

/* IResourceContext.QualifierValues: an observable string map of the
 * qualifiers Windows fills a context with, at their NovaOS values (the
 * candidates are not chosen between by them: see the top) */
static HRESULT STDMETHODCALLTYPE context_qualifiers(void *p, void **out)
{
    static const WCHAR *const q[][2] = {
        { L"Language", L"en-US" }, { L"Contrast", L"standard" }, { L"Scale", L"100" }, { L"HomeRegion", L"US" },
        { L"TargetSize", L"0" }, { L"LayoutDirection", L"LTR" }, { L"Theme", L"dark" }, { L"AlternateForm", L"" },
        { L"DXFeatureLevel", L"DX11" }, { L"Configuration", L"" }, { L"DeviceFamily", L"Desktop" }, { L"Custom", L"" },
    };
    static const GUID iid = { 0x1E036276, 0x2F60, 0x55F6, { 0xB7, 0xF3, 0xF8, 0x60, 0x79, 0xE6, 0x90, 0x0B } };
    (void)p;
    if (!out) return E_POINTER;
    *out = 0;
    IUnknown *m;
    HRESULT hr = nova_strmap_new(FALSE, (void **)&m);
    for (unsigned i = 0; SUCCEEDED(hr) && i < sizeof q / sizeof *q; i++) hr = nova_strmap_put(m, q[i][0], q[i][1]);
    if (SUCCEEDED(hr)) hr = m->lpVtbl->QueryInterface(m, &iid, out);
    if (m) m->lpVtbl->Release(m);
    return hr;
}

/* IResourceContext.Languages: the user's language (NovaOS's one, en-US) */
static HRESULT STDMETHODCALLTYPE context_languages(void *p, void **out)
{
    static const WCHAR *const langs[] = { L"en-US" };
    (void)p;
    return nova_strvec_new(langs, 1, out);
}
static HRESULT context_new(void **out);
static HRESULT STDMETHODCALLTYPE context_clone(void *p, void **out) { (void)p; return context_new(out); }

/* IResourceContext: Reset, OverrideToMatch and setting its languages are not
 * here */
static const void *const context_vt[] = { BASE, (void *)context_qualifiers, (void *)not_here, (void *)not_here1, (void *)not_here1,
                                          (void *)context_clone, (void *)context_languages, (void *)not_here1 };
static HRESULT context_new(void **out)
{
    if (!out) return E_POINTER;
    Obj *c = obj_new(K_CONTEXT, context_vt, 0, 0);
    *out = c;
    return c ? S_OK : E_OUTOFMEMORY;
}

/* IResourceLoader: GetString (its map's string, "" when it has none);
 * IResourceLoader2's GetStringForUri is not here */
static HRESULT STDMETHODCALLTYPE ld_string(void *p, HSTRING name, HSTRING *out)
{
    Obj *l = p;
    if (!out) return E_POINTER;
    UINT32 n;
    const WCHAR *c = hbuf(name, &n);
    WCHAR *id = wjoin(l->path, wlen(l->path), L"/", c, (int)n);
    if (!id) return E_OUTOFMEMORY;
    HRESULT hr = load_string(id, out);
    release_mem(id);
    if (hr == ERROR_MRM_NAMED_RESOURCE_NOT_FOUND_) { *out = 0; hr = S_OK; }
    return hr;
}
static const void *const loader_vt[] = { BASE, (void *)ld_string };
static HRESULT loader_new(HSTRING name, void **out)
{
    if (!out) return E_POINTER;
    UINT32 n = 0;
    const WCHAR *c = name ? hbuf(name, &n) : L"Resources";
    if (!name) n = 9;
    Obj *l = obj_new(K_LOADER, loader_vt, c, (int)n);
    *out = l;
    return l ? S_OK : E_OUTOFMEMORY;
}

/* ---- the singletons: the resource manager, AllResourceMaps, the factories -------- */
typedef struct { const void *const *vt; } Static;
static ULONG STDMETHODCALLTYPE s_ref(void *p) { (void)p; return 2; }

/* AllResourceMaps: IMapView<String, ResourceMap> holding the main map, by its package's name */
static HRESULT STDMETHODCALLTYPE mv_qi(void *p, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IMapView_ResourceMap)) { *out = p; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE mv_name(void *p, HSTRING *n)
{
    (void)p;
    static const WCHAR s[] = L"Windows.Foundation.Collections.IMapView`2<String, Windows.ApplicationModel.Resources.Core.ResourceMap>";
    return WindowsCreateString(s, sizeof s / sizeof s[0] - 1, n);
}
static BOOL mv_has_key(HSTRING key) { main_pri(); return g_mgr && g_name[0] && name_is(key, g_name); }
static HRESULT STDMETHODCALLTYPE mv_lookup(void *p, HSTRING key, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    *out = 0;
    return mv_has_key(key) ? root_map(out) : E_BOUNDS_;
}
static HRESULT STDMETHODCALLTYPE mv_size(void *p, UINT32 *n) { (void)p; if (!n) return E_POINTER; main_pri(); *n = g_mgr && g_name[0] ? 1 : 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE mv_has(void *p, HSTRING key, boolean_t *v) { (void)p; if (!v) return E_POINTER; *v = mv_has_key(key); return S_OK; }
static HRESULT STDMETHODCALLTYPE mv_split(void *p, void **a, void **b) { (void)p; if (!a || !b) return E_POINTER; *a = *b = 0; return S_OK; }
static const void *const mapview_vt[] = { (void *)mv_qi, (void *)s_ref, (void *)s_ref, (void *)no_iids, (void *)mv_name, (void *)base_trust,
                                          (void *)mv_lookup, (void *)mv_size, (void *)mv_has, (void *)mv_split };
static const Static g_maps = { mapview_vt };

/* the ResourceManager: IResourceManager (MainResourceMap, AllResourceMaps,
 * DefaultContext; LoadPriFiles and UnloadPriFiles not here), IResourceManager2 (not here) */
static const void *const manager_vt[], *const manager2_vt[];
static const Static g_manager[2] = { { manager_vt }, { manager2_vt } };
static HRESULT STDMETHODCALLTYPE rm_qi(void *p, REFIID iid, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IResourceManager)) { *out = (void *)&g_manager[0]; return S_OK; }
    if (IsEqualGUID(iid, &IID_IResourceManager2)) { *out = (void *)&g_manager[1]; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE rm_name(void *p, HSTRING *n)
{
    (void)p;
    static const WCHAR s[] = L"Windows.ApplicationModel.Resources.Core.ResourceManager";
    return WindowsCreateString(s, sizeof s / sizeof s[0] - 1, n);
}
static HRESULT STDMETHODCALLTYPE rm_main(void *p, void **out) { (void)p; return root_map(out); }
static HRESULT STDMETHODCALLTYPE rm_all(void *p, void **out) { (void)p; if (!out) return E_POINTER; *out = (void *)&g_maps; return S_OK; }
static HRESULT STDMETHODCALLTYPE rm_context(void *p, void **out) { (void)p; return context_new(out); }
static const void *const manager_vt[] = { (void *)rm_qi, (void *)s_ref, (void *)s_ref, (void *)no_iids, (void *)rm_name, (void *)base_trust,
                                          (void *)rm_main, (void *)rm_all, (void *)rm_context, (void *)not_here1, (void *)not_here1 };
static const void *const manager2_vt[] = { (void *)rm_qi, (void *)s_ref, (void *)s_ref, (void *)no_iids, (void *)rm_name, (void *)base_trust,
                                           (void *)not_here3, (void *)not_here3 };

/* a class's factory: IActivationFactory and its statics interfaces (at
 * most four), each a Static of the same table of interfaces */
typedef struct { const GUID *iid; int at; } FIface;
typedef struct { const WCHAR *name; const FIface *ifaces; const Static *parts; } Factory;
static const Factory *factory_of(void *p);
static HRESULT STDMETHODCALLTYPE f_qi(void *p, REFIID iid, void **out)
{
    const Factory *f = factory_of(p);
    if (!out) return E_POINTER;
    *out = 0;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IActivationFactory_)) { *out = (void *)&f->parts[0]; return S_OK; }
    for (const FIface *i = f->ifaces; i->iid; i++)
        if (IsEqualGUID(iid, i->iid)) { *out = (void *)&f->parts[i->at]; return S_OK; }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE f_name(void *p, HSTRING *n) { const WCHAR *s = factory_of(p)->name; return WindowsCreateString(s, (UINT32)wlen(s), n); }
#define FBASE (void *)f_qi, (void *)s_ref, (void *)s_ref, (void *)no_iids, (void *)f_name, (void *)base_trust

/* ResourceManager: IResourceManagerStatics (Current, IsResourceReference) */
static HRESULT STDMETHODCALLTYPE rms_current(void *p, void **out) { (void)p; if (!out) return E_POINTER; *out = (void *)&g_manager[0]; return S_OK; }
static HRESULT STDMETHODCALLTYPE rms_is_ref(void *p, HSTRING s, boolean_t *v)
{
    (void)p;
    if (!v) return E_POINTER;
    UINT32 n;
    const WCHAR *c = hbuf(s, &n);
    static const WCHAR scheme[] = L"ms-resource:";
    *v = n >= 12;
    for (UINT32 i = 0; *v && i < 12; i++) if ((c[i] | 0x20) != (scheme[i] | 0x20) && c[i] != scheme[i]) *v = 0;
    return S_OK;
}
static const void *const rm_factory_vt[] = { FBASE, (void *)out_null };
static const void *const rm_statics_vt[] = { FBASE, (void *)rms_current, (void *)rms_is_ref };
static const Static g_rm_parts[] = { { rm_factory_vt }, { rm_statics_vt } };
static const FIface rm_ifaces[] = { { &IID_IResourceManagerStatics, 1 }, { 0, 0 } };
/* ResourceContext: activatable; its statics (CreateMatchingContext,
 * GetForCurrentView, GetForViewIndependentUse; the global qualifier values
 * are accepted, not used) */
static HRESULT STDMETHODCALLTYPE rcx_activate(void *p, void **out) { (void)p; return context_new(out); }
static HRESULT STDMETHODCALLTYPE rcx_matching(void *p, void *q, void **out) { (void)p; (void)q; return context_new(out); }
static HRESULT STDMETHODCALLTYPE rcx_get(void *p, void **out) { (void)p; return context_new(out); }
static HRESULT STDMETHODCALLTYPE rcx_set(void *p, HSTRING k, HSTRING v) { (void)p; (void)k; (void)v; return S_OK; }
static HRESULT STDMETHODCALLTYPE rcx_set3(void *p, HSTRING k, HSTRING v, int how) { (void)p; (void)k; (void)v; (void)how; return S_OK; }
static HRESULT STDMETHODCALLTYPE rcx_reset(void *p) { (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE rcx_reset_some(void *p, void *list) { (void)p; (void)list; return S_OK; }
static const void *const rcx_factory_vt[] = { FBASE, (void *)rcx_activate };
static const void *const rcx_statics_vt[] = { FBASE, (void *)rcx_matching };
static const void *const rcx_statics2_vt[] = { FBASE, (void *)rcx_get, (void *)rcx_set, (void *)rcx_reset, (void *)rcx_reset_some,
                                               (void *)rcx_get };
static const void *const rcx_statics3_vt[] = { FBASE, (void *)rcx_set3 };
static const Static g_rcx_parts[] = { { rcx_factory_vt }, { rcx_statics_vt }, { rcx_statics2_vt }, { rcx_statics3_vt } };
static const FIface rcx_ifaces[] = { { &IID_IResourceContextStatics, 1 }, { &IID_IResourceContextStatics2, 2 },
                                     { &IID_IResourceContextStatics3, 3 }, { 0, 0 } };
/* ResourceLoader: activatable (the "Resources" map's), by a map's name
 * (IResourceLoaderFactory), and its statics: GetForCurrentView,
 * GetForViewIndependentUse (with or without a name), GetForUIContext;
 * GetStringForReference is not here */
static HRESULT STDMETHODCALLTYPE ldf_activate(void *p, void **out) { (void)p; return loader_new(0, out); }
static HRESULT STDMETHODCALLTYPE ldf_named(void *p, HSTRING name, void **out) { (void)p; return loader_new(name, out); }
static HRESULT STDMETHODCALLTYPE ldf_default(void *p, void **out) { (void)p; return loader_new(0, out); }
static HRESULT STDMETHODCALLTYPE ldf_context(void *p, void *ui, void **out) { (void)p; (void)ui; return loader_new(0, out); }
static const void *const ld_factory_vt[] = { FBASE, (void *)ldf_activate };
static const void *const ld_byname_vt[] = { FBASE, (void *)ldf_named };
static const void *const ld_statics_vt[] = { FBASE, (void *)not_here2 };
static const void *const ld_statics2_vt[] = { FBASE, (void *)ldf_default, (void *)ldf_named, (void *)ldf_default, (void *)ldf_named };
static const void *const ld_statics3_vt[] = { FBASE, (void *)ldf_context };
static const Static g_ld_parts[] = { { ld_factory_vt }, { ld_byname_vt }, { ld_statics_vt }, { ld_statics2_vt }, { ld_statics3_vt } };
static const FIface ld_ifaces[] = { { &IID_IResourceLoaderFactory, 1 }, { &IID_IResourceLoaderStatics, 2 },
                                    { &IID_IResourceLoaderStatics2, 3 }, { &IID_IResourceLoaderStatics3, 4 }, { 0, 0 } };

static const Factory g_factories[] = {
    { L"Windows.ApplicationModel.Resources.Core.ResourceManager", rm_ifaces, g_rm_parts },
    { L"Windows.ApplicationModel.Resources.Core.ResourceContext", rcx_ifaces, g_rcx_parts },
    { L"Windows.ApplicationModel.Resources.ResourceLoader", ld_ifaces, g_ld_parts },
};
static const Factory *factory_of(void *p)
{
    for (int i = 0; i < 3; i++) {
        const Factory *f = &g_factories[i];
        int n = f == &g_factories[0] ? 2 : f == &g_factories[1] ? 4 : 5;
        if ((const Static *)p >= f->parts && (const Static *)p < f->parts + n) return f;
    }
    return &g_factories[0];
}

MRMCORER_API HRESULT WINAPI DllGetActivationFactory(HSTRING cls, void **factory)
{
    if (!factory) return E_POINTER;
    for (int i = 0; i < 3; i++)
        if (name_is(cls, g_factories[i].name)) { *factory = (void *)&g_factories[i].parts[0]; return S_OK; }
    *factory = 0;
    return CLASS_E_CLASSNOTAVAILABLE;
}
MRMCORER_API HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
