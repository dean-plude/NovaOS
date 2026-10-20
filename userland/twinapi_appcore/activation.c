/*
 * activation.c — ApplicationActivationManager: starting a packaged app by
 * its application user model id ("<package family>!<app id>"), as the Start
 * menu, the shell and the package installer start one on Windows.
 *
 * CoCreateInstance(CLSID_ApplicationActivationManager) reaches this
 * DLL's DllGetClassObject through HKCR\CLSID (kernel/um/um_registry.c).
 * IApplicationActivationManager::ActivateApplication finds the family's
 * installed package and the application in the package repository that
 * the installer (userland/programs/appx.c) writes and kernel32's
 * package.c reads, and starts the application's program in the package's
 * folder with the arguments after its name.  The process belongs to the
 * package because its program is in it (kernel32's package.c); a UWP
 * app's CoreApplication hands it the arguments as its launch's
 * (LaunchActivatedEventArgs.Arguments, appcore.c).
 *
 * Activating for files and protocols (ActivateForFile,
 * ActivateForProtocol) needs the packages' file type and protocol
 * associations, not recorded yet: E_NOTIMPL.
 */
#include <windows.h>
#include <objbase.h>

void *memcpy(void *d, const void *s, size_t n);

#define APPCORE_API __declspec(dllexport)
#define REPO_KEY L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository"

static const GUID IID_IUnknown_ = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IClassFactory_ = { 0x00000001, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID CLSID_ApplicationActivationManager_ = { 0x45BA127D, 0x10A8, 0x46EA, { 0x8A, 0xB7, 0x56, 0xEA, 0x90, 0x78, 0x94, 0x3C } };
static const GUID IID_IApplicationActivationManager_ = { 0x2E941141, 0x7F97, 0x4756, { 0xBA, 0x1D, 0x9D, 0xEC, 0xDE, 0x89, 0x4A, 0x3D } };

static int wlen(const WCHAR *s) { int n = 0; while (s[n]) n++; return n; }
static void wcat(WCHAR *d, int *at, int cap, const WCHAR *s, int n)
{
    for (int i = 0; (n < 0 ? s[i] != 0 : i < n) && *at < cap - 1; i++) d[(*at)++] = s[i];
    d[*at] = 0;
}

/* The package @family's installed full name (its newest: the last
 * listed) and the program of its application @app, with the folder it is in */
static HRESULT find_app(const WCHAR *family, int flen, const WCHAR *app, WCHAR exe[MAX_PATH], WCHAR dir[MAX_PATH])
{
    WCHAR key[600], full[128] = { 0 }, rel[MAX_PATH];
    int at = 0;
    wcat(key, &at, 600, REPO_KEY L"\\Families\\", -1);
    wcat(key, &at, 600, family, flen);
    HKEY fk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &fk)) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for (DWORD i = 0;; i++) {
        WCHAR name[128];
        DWORD n = 128;
        if (RegEnumKeyExW(fk, i, name, &n, 0, 0, 0, 0)) break;
        memcpy(full, name, (n + 1) * sizeof(WCHAR));
    }
    RegCloseKey(fk);
    if (!full[0]) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    at = 0;
    wcat(key, &at, 600, REPO_KEY L"\\Packages\\", -1);
    wcat(key, &at, 600, full, -1);
    DWORD size = MAX_PATH * sizeof(WCHAR);
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"PackageRootFolder", RRF_RT_REG_SZ, 0, dir, &size))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    wcat(key, &at, 600, L"\\Applications\\", -1);
    wcat(key, &at, 600, app, -1);
    size = sizeof rel;
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"Executable", RRF_RT_REG_SZ, 0, rel, &size))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    at = 0;
    wcat(exe, &at, MAX_PATH, dir, -1);
    if (at && exe[at - 1] != '\\') wcat(exe, &at, MAX_PATH, L"\\", -1);
    wcat(exe, &at, MAX_PATH, rel, -1);
    return S_OK;
}

typedef struct { const void *const *vt; } Manager;

static HRESULT STDMETHODCALLTYPE am_qi(Manager *m, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!IsEqualGUID(iid, &IID_IUnknown_) && !IsEqualGUID(iid, &IID_IApplicationActivationManager_)) {
        *out = 0;
        return E_NOINTERFACE;
    }
    *out = m;
    return S_OK;
}
static ULONG STDMETHODCALLTYPE am_addref(Manager *m) { (void)m; return 2; }   /* static */
static ULONG STDMETHODCALLTYPE am_release(Manager *m) { (void)m; return 1; }

static HRESULT STDMETHODCALLTYPE am_activate(Manager *m, LPCWSTR aumid, LPCWSTR args, int options, DWORD *pid)
{
    (void)m; (void)options;                         /* (no splash screen to leave out, no debugger to attach) */
    if (pid) *pid = 0;
    if (!aumid || !pid) return E_INVALIDARG;
    const WCHAR *bang = aumid;
    while (*bang && *bang != '!') bang++;
    if (!*bang || bang == aumid || !bang[1]) return E_INVALIDARG;
    WCHAR exe[MAX_PATH], dir[MAX_PATH];
    HRESULT hr = find_app(aumid, (int)(bang - aumid), bang + 1, exe, dir);
    if (FAILED(hr)) return hr;
    int n = wlen(exe) + (args ? wlen(args) : 0) + 4;
    WCHAR *cmd = HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR));
    if (!cmd) return E_OUTOFMEMORY;
    int at = 0;
    wcat(cmd, &at, n, L"\"", -1);
    wcat(cmd, &at, n, exe, -1);
    wcat(cmd, &at, n, L"\"", -1);
    if (args && *args) { wcat(cmd, &at, n, L" ", -1); wcat(cmd, &at, n, args, -1); }
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessW(exe, cmd, 0, 0, FALSE, 0, 0, dir, &si, &pi);
    DWORD err = GetLastError();
    HeapFree(GetProcessHeap(), 0, cmd);
    if (!ok) return HRESULT_FROM_WIN32(err);
    *pid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE am_for_file(Manager *m, LPCWSTR aumid, void *items, LPCWSTR verb, DWORD *pid)
{
    (void)m; (void)aumid; (void)items; (void)verb;
    if (pid) *pid = 0;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE am_for_protocol(Manager *m, LPCWSTR aumid, void *items, DWORD *pid)
{
    (void)m; (void)aumid; (void)items;
    if (pid) *pid = 0;
    return E_NOTIMPL;
}
static const void *const am_vt[] = { (void *)am_qi, (void *)am_addref, (void *)am_release, (void *)am_activate,
                                     (void *)am_for_file, (void *)am_for_protocol };
static Manager g_manager = { am_vt };

/* its class factory */
typedef struct { const void *const *vt; } Factory;
static HRESULT STDMETHODCALLTYPE cf_qi(Factory *f, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (!IsEqualGUID(iid, &IID_IUnknown_) && !IsEqualGUID(iid, &IID_IClassFactory_)) { *out = 0; return E_NOINTERFACE; }
    *out = f;
    return S_OK;
}
static ULONG STDMETHODCALLTYPE cf_addref(Factory *f) { (void)f; return 2; }
static ULONG STDMETHODCALLTYPE cf_release(Factory *f) { (void)f; return 1; }
static HRESULT STDMETHODCALLTYPE cf_create(Factory *f, IUnknown *outer, REFIID iid, void **out)
{
    (void)f;
    if (!out) return E_POINTER;
    *out = 0;
    if (outer) return CLASS_E_NOAGGREGATION;
    return am_qi(&g_manager, iid, out);
}
static HRESULT STDMETHODCALLTYPE cf_lock(Factory *f, BOOL lock) { (void)f; (void)lock; return S_OK; }
static const void *const cf_vt[] = { (void *)cf_qi, (void *)cf_addref, (void *)cf_release, (void *)cf_create, (void *)cf_lock };
static Factory g_factory = { cf_vt };

APPCORE_API HRESULT WINAPI DllGetClassObject(REFCLSID cls, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (!IsEqualGUID(cls, &CLSID_ApplicationActivationManager_)) return CLASS_E_CLASSNOTAVAILABLE;
    return cf_qi(&g_factory, iid, out);
}
