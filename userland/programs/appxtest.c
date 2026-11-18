/*
 * appxtest — app packages (step 32.6): appx.exe installs the test packages
 * tools/appx/mktest.py makes (C:\Tests\Appx), refusing the damaged, the
 * signed (signatures are not verified yet) and the unsigned without
 * Microsoft's "unsigned" publisher OID; the installed package's
 * application is started by its application user model id through
 * twinapi.appcore's ApplicationActivationManager, and runs as this same
 * program inside the package ("appxtest child ..."), where it checks
 * - its package identity (kernel32: full name, family, AUMID, path),
 * - its arguments and working folder as the activation gave them,
 * - the package's resources.pri strings (SHLoadIndirectString, MRM.dll),
 * - the package's Windows Runtime class (greeter.dll, RoActivateInstance
 *   through the package's ActivatableClasses registration),
 * - ApplicationData.Current, given to a packaged app: its LocalSettings,
 * writes what it found to C:\Temp\appxtest-child.log and exits with its
 * failure count; then "appx remove" takes the package away again.
 * Started packaged with no arguments (from the Start menu: the core
 * suite's tests/selftest/core/179-appxshell.py) it only writes the
 * application user model id it runs as to C:\Temp\appxtest-start.log.
 * Then signed packages: with the test root not trusted, the signed
 * framework package (NovaOS.TestFramework) is refused; trusted (the ROOT
 * store), it installs, and the copies whose records or publisher were
 * changed after signing are refused; the signed bundle FrameworkTest.
 * msixbundle installs its x64 application package (NovaOS.FrameworkTest,
 * which depends on the framework: refused before the framework is in),
 * whose program ("appxtest fwchild") finds the framework in its package
 * graph (GetCurrentPackageInfo), loads the framework's novafw.dll by name
 * (the loader searches the graph) and activates the framework's Windows
 * Runtime class; the framework cannot be removed while the app is
 * installed.  A bundle laid out as the Store's are (its block map naming
 * only its manifest) installs by its package's own signature, and is
 * refused when that package is unsigned or another publisher's.
 */
#include <windows.h>
#include <objbase.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

__declspec(dllimport) LONG WINAPI GetCurrentPackageFullName(UINT32 *len, PWSTR name);
__declspec(dllimport) LONG WINAPI GetCurrentPackageFamilyName(UINT32 *len, PWSTR name);
__declspec(dllimport) LONG WINAPI GetCurrentPackagePath(UINT32 *len, PWSTR path);
__declspec(dllimport) LONG WINAPI GetCurrentApplicationUserModelId(UINT32 *len, PWSTR id);
__declspec(dllimport) LONG WINAPI GetPackagePathByFullName(PCWSTR full, UINT32 *len, PWSTR path);
__declspec(dllimport) HRESULT WINAPI SHLoadIndirectString(LPCWSTR src, LPWSTR out, UINT n, void **reserved);
__declspec(dllimport) LPWSTR *WINAPI CommandLineToArgvW(LPCWSTR cmd, int *argc);
__declspec(dllimport) LONG WINAPI GetCurrentPackageInfo(UINT32 flags, UINT32 *len, BYTE *buf, UINT32 *count);
__declspec(dllimport) HANDLE WINAPI CertOpenSystemStoreW(ULONG_PTR prov, LPCWSTR name);
__declspec(dllimport) BOOL WINAPI CertAddEncodedCertificateToStore(HANDLE, DWORD, const BYTE *, DWORD, DWORD, const void **);
__declspec(dllimport) const void *WINAPI CertFindCertificateInStore(HANDLE, DWORD, DWORD, DWORD, const void *, const void *);
__declspec(dllimport) BOOL WINAPI CertDeleteCertificateFromStore(const void *);
__declspec(dllimport) BOOL WINAPI CertCloseStore(HANDLE, DWORD);

/* the package's names (its publisher id: tools/appx/test/AppxManifest.xml's
 * publisher, hashed as Windows hashes it) */
#define FAMILY L"NovaOS.PackageTest_rtg2wbn51qgvw"
#define FULL L"NovaOS.PackageTest_1.0.0.0_x64__rtg2wbn51qgvw"
#define ROOT L"C:\\Programs\\WindowsApps\\" FULL
#define LOGO ROOT L"\\Assets\\Square44x44Logo.scale-200.png"
#define LOG L"C:\\Temp\\appxtest-child.log"
/* the signed ones (tools/appx/mktest.py's SIGNER, hashed) */
#define FW_FULL L"NovaOS.TestFramework_1.0.0.0_x64__h384ge6vq8144"
#define FW_ROOT L"C:\\Programs\\WindowsApps\\" FW_FULL
#define FT_FAMILY L"NovaOS.FrameworkTest_h384ge6vq8144"
#define FT_FULL L"NovaOS.FrameworkTest_1.0.0.0_x64__h384ge6vq8144"
#define FT_ROOT L"C:\\Programs\\WindowsApps\\" FT_FULL
#define REPO L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository"

static const GUID CLSID_ApplicationActivationManager_ = { 0x45BA127D, 0x10A8, 0x46EA, { 0x8A, 0xB7, 0x56, 0xEA, 0x90, 0x78, 0x94, 0x3C } };
static const GUID IID_IApplicationActivationManager_ = { 0x2E941141, 0x7F97, 0x4756, { 0xBA, 0x1D, 0x9D, 0xEC, 0xDE, 0x89, 0x4A, 0x3D } };
static const GUID IID_IGreeter = { 0x6E6F7661, 0x0032, 0x0036, { 0x67, 0x72, 0x65, 0x65, 0x74, 0x65, 0x72, 0x31 } };

#define VT(o) (*(void ***)(o))
#ifndef APPMODEL_ERROR_NO_PACKAGE
#define APPMODEL_ERROR_NO_PACKAGE 15700L
#endif

static int pass, fail;
static FILE *out;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; fprintf(out, "FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

static int exists(const WCHAR *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

/* appx.exe with @args: its exit code */
static DWORD appx(const WCHAR *args)
{
    WCHAR cmd[600];
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    DWORD code = 0xFFFF;
    swprintf(cmd, 600, L"appx.exe %ls", args);
    printf("> appx %ls\n", args);
    fflush(stdout);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return code;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

/* ---- inside the package ---- */

/* ApplicationData.Current, a packaged app's (Calculator's start-up): its
 * LocalSettings, whose Values are a PropertySet taking, finding and
 * removing a boxed value (a setting not set looks up as null);
 * CreateContainer making then opening a named container, Existing
 * refusing a missing one.  (Unpackaged, Current is refused: wrtapptest.) */
static const GUID IID_IApplicationDataStatics_T = { 0x5612147B, 0xE843, 0x45E3, { 0x94, 0xD8, 0x06, 0x16, 0x9E, 0x3C, 0x8E, 0x17 } };
static const GUID IID_IMapStringObject_T = { 0x1B0D3570, 0x0877, 0x5EC2, { 0x8A, 0x2C, 0x3B, 0x95, 0x39, 0x50, 0x6A, 0xCA } };
static const GUID IID_IPropertyValueStatics_T = { 0x629BDBC8, 0xD932, 0x4FF4, { 0x96, 0xB9, 0x8D, 0x96, 0xC5, 0xC1, 0xE8, 0x58 } };
static const GUID IID_IPropertyValue_T = { 0x4BD682DD, 0x7554, 0x40E9, { 0x9A, 0x9B, 0x82, 0x65, 0x4E, 0xDE, 0x7E, 0x62 } };
#define VTF(o, i, T) ((T)((void **)*(void **)(o))[i])
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(void *);
typedef HRESULT (STDMETHODCALLTYPE *QI_t)(void *, const GUID *, void **);
typedef HRESULT (STDMETHODCALLTYPE *Get_t)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *GetS_t)(void *, HSTRING, void *);
#define RELEASE_(o) do { if (o) VTF(o, 2, Rel_t)(o); (o) = NULL; } while (0)
static HSTRING hs(const WCHAR *s) { HSTRING h = 0; WindowsCreateString(s, (UINT32)wcslen(s), &h); return h; }
static HRESULT factory(const WCHAR *cls, const GUID *iid, void **o)
{
    HSTRING s = hs(cls);
    HRESULT hr = RoGetActivationFactory(s, iid, o);
    WindowsDeleteString(s);
    if (hr != S_OK) fprintf(out, "RoGetActivationFactory(%ls): 0x%08lX\n", cls, (unsigned long)hr);
    return hr;
}

static void app_data(void)
{
    void *statics = 0, *data = 0, *local = 0, *values = 0, *map = 0, *pv = 0, *box = 0, *found = 0;
    CHECK("ApplicationData's statics", factory(L"Windows.Storage.ApplicationData", &IID_IApplicationDataStatics_T, &statics) == S_OK);
    if (!statics) return;
    VTF(statics, 6, Get_t)(statics, &data);                          /* Current */
    CHECK("ApplicationData.Current", data != 0);
    if (data) VTF(data, 10, Get_t)(data, &local);                    /* LocalSettings */
    CHECK("LocalSettings", local != 0);
    if (!local) goto out;
    HSTRING name = 0;
    int locality = -1;
    VTF(local, 6, Get_t)(local, &name);
    VTF(local, 7, Get_t)(local, &locality);
    CHECK("its Name (empty) and Locality (Local)", WindowsGetStringLen(name) == 0 && locality == 0);
    WindowsDeleteString(name);
    VTF(local, 8, Get_t)(local, &values);                            /* Values: IPropertySet */
    CHECK("its Values", values != 0);
    if (values) VTF(values, 0, QI_t)(values, &IID_IMapStringObject_T, &map);
    CHECK("Values: an IMap<String, Object>", map != 0);
    factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics_T, &pv);
    if (map && pv) {
        VTF(pv, 10, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, void **))(pv, 42, &box);   /* CreateInt32 */
        HSTRING k = hs(L"VeryFirstLaunch");
        BYTE had = 1, replaced = 1;
        VTF(map, 8, GetS_t)(map, k, &had);                           /* HasKey */
        CHECK("a new key: not there", had == 0);
        CHECK("Insert", VTF(map, 10, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, void *, BYTE *))(map, k, box, &replaced) == S_OK && !replaced);
        VTF(map, 8, GetS_t)(map, k, &had);
        CHECK("then there", had == 1);
        VTF(map, 6, GetS_t)(map, k, &found);                         /* Lookup */
        INT32 v = 0;
        void *fv = 0;
        if (found) VTF(found, 0, QI_t)(found, &IID_IPropertyValue_T, &fv);
        if (fv) VTF(fv, 11, Get_t)(fv, &v);                          /* GetInt32 */
        CHECK("Lookup: the value put", v == 42);
        RELEASE_(fv);
        UINT32 n = 0;
        VTF(map, 7, Get_t)(map, &n);
        CHECK("Size 1", n == 1);
        CHECK("Remove", VTF(map, 11, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING))(map, k) == S_OK);
        VTF(map, 8, GetS_t)(map, k, &had);
        CHECK("then gone", had == 0);
        void *none = (void *)1;                                     /* (a setting not set looks up as null, */
        CHECK("Lookup of a key not set: null, not an error",       /*  as Calculator's theme helper expects) */
              VTF(map, 6, GetS_t)(map, k, &none) == S_OK && none == 0);
        WindowsDeleteString(k);
    }
    void *c1 = 0, *c2 = 0, *c3 = 0;
    HSTRING cn = hs(L"Sub"), missing = hs(L"Missing");
    typedef HRESULT (STDMETHODCALLTYPE *Create_t)(void *, HSTRING, int, void **);
    CHECK("CreateContainer(Always)", VTF(local, 10, Create_t)(local, cn, 0, &c1) == S_OK && c1);
    CHECK("CreateContainer(Existing) opens it", VTF(local, 10, Create_t)(local, cn, 1, &c2) == S_OK && c2 == c1);
    CHECK("CreateContainer(Existing) of a missing one: refused", FAILED(VTF(local, 10, Create_t)(local, missing, 1, &c3)) && !c3);
    WindowsDeleteString(cn);
    WindowsDeleteString(missing);
    RELEASE_(c1);
    RELEASE_(c2);
out:
    RELEASE_(found);
    RELEASE_(box);
    RELEASE_(pv);
    RELEASE_(map);
    RELEASE_(values);
    RELEASE_(local);
    RELEASE_(data);
    RELEASE_(statics);
}


static int child(int argc, WCHAR **argv)
{
    WCHAR s[MAX_PATH], exe[MAX_PATH], cwd[MAX_PATH], want[MAX_PATH];
    UINT32 n;
    out = _wfopen(LOG, L"w");
    if (!out) return 100;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    n = MAX_PATH;
    CHECK("GetCurrentPackageFullName", !GetCurrentPackageFullName(&n, s) && !wcscmp(s, FULL));
    fprintf(out, "package: %ls\n", s);
    n = MAX_PATH;
    CHECK("GetCurrentPackageFamilyName", !GetCurrentPackageFamilyName(&n, s) && !wcscmp(s, FAMILY));
    n = MAX_PATH;
    CHECK("GetCurrentApplicationUserModelId", !GetCurrentApplicationUserModelId(&n, s) && !wcscmp(s, FAMILY L"!App"));
    fprintf(out, "aumid: %ls\n", s);
    n = 2;
    CHECK("GetCurrentPackageFullName (too small)", GetCurrentPackageFullName(&n, s) == ERROR_INSUFFICIENT_BUFFER &&
                                                   n == wcslen(FULL) + 1);
    n = MAX_PATH;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    CHECK("GetCurrentPackagePath", !GetCurrentPackagePath(&n, s) && !_wcsicmp(s, ROOT));
    swprintf(want, MAX_PATH, L"%ls\\appxtest.exe", s);
    CHECK("the program is the package's", !_wcsicmp(exe, want));
    GetCurrentDirectoryW(MAX_PATH, cwd);
    n = (UINT32)wcslen(cwd);
    if (n && cwd[n - 1] == '\\') cwd[n - 1] = 0;
    CHECK("working folder: the package's", !_wcsicmp(cwd, ROOT));
    fprintf(out, "folder: %ls\n", cwd);
    CHECK("arguments", argc == 4 && !wcscmp(argv[2], L"one") && !wcscmp(argv[3], L"two words"));

    /* resources.pri */
    s[0] = 0;
    CHECK("SHLoadIndirectString (ms-resource:Greeting)",
          SUCCEEDED(SHLoadIndirectString(L"@{" FULL L"?ms-resource:Greeting}", s, MAX_PATH, NULL)) &&
          !wcscmp(s, L"Hello from resources.pri"));
    fprintf(out, "Greeting: %ls\n", s);
    s[0] = 0;
    CHECK("SHLoadIndirectString (ms-resource://.../Resources/AppDisplayName)",
          SUCCEEDED(SHLoadIndirectString(L"@{" FULL L"?ms-resource://NovaOS.PackageTest/Resources/AppDisplayName}", s,
                                         MAX_PATH, NULL)) && !wcscmp(s, L"NovaOS Package Test"));
    s[0] = 0;
    CHECK("SHLoadIndirectString (a missing name fails)",
          FAILED(SHLoadIndirectString(L"@{" FULL L"?ms-resource:NoSuchString}", s, MAX_PATH, NULL)));

    /* the package's Windows Runtime class */
    HSTRING cls = NULL;
    void *insp = NULL, *g = NULL;
    const WCHAR *greeting = NULL;
    WindowsCreateString(L"NovaOS.PackageTest.Greeter", 26, &cls);
    HRESULT hr = RoActivateInstance(cls, &insp);
    CHECK("RoActivateInstance(NovaOS.PackageTest.Greeter)", SUCCEEDED(hr) && insp);
    fprintf(out, "RoActivateInstance: 0x%08lx\n", (unsigned long)hr);
    if (insp && SUCCEEDED(((IUnknown *)insp)->lpVtbl->QueryInterface(insp, &IID_IGreeter, &g)) && g) {
        typedef HRESULT (STDMETHODCALLTYPE *Greet)(void *, const WCHAR **);
        ((Greet)VT(g)[6])(g, &greeting);
        ((IUnknown *)g)->lpVtbl->Release(g);
    }
    CHECK("IGreeter::Greet", greeting && !wcscmp(greeting, L"Hello from a packaged Windows Runtime class"));
    HMODULE dll = GetModuleHandleW(L"greeter.dll");
    s[0] = 0;
    if (dll) GetModuleFileNameW(dll, s, MAX_PATH);
    CHECK("greeter.dll: the package's", !_wcsicmp(s, ROOT L"\\greeter.dll"));
    if (insp) ((IUnknown *)insp)->lpVtbl->Release(insp);
    WindowsDeleteString(cls);

    app_data();

    fprintf(out, "child: %d passed, %d failed\n", pass, fail);
    fclose(out);
    return fail;
}

/* ---- inside the package that uses the framework ---- */

typedef struct { UINT32 reserved, flags; PWSTR path, full, family; BYTE id[48]; } PackageInfo_;

static int fwchild(void)
{
    WCHAR s[MAX_PATH];
    out = _wfopen(LOG, L"w");
    if (!out) return 100;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    UINT32 n = 0, count = 0;
    BYTE *buf = NULL;
    CHECK("GetCurrentPackageInfo (its size)", GetCurrentPackageInfo(0, &n, NULL, &count) == ERROR_INSUFFICIENT_BUFFER && n);
    if (n && (buf = malloc(n)) && !GetCurrentPackageInfo(0, &n, buf, &count)) {
        PackageInfo_ *pi = (PackageInfo_ *)buf;
        CHECK("the package graph: the package, then the framework", count == 2 && !wcscmp(pi[0].full, FT_FULL) &&
              !wcscmp(pi[1].full, FW_FULL) && !_wcsicmp(pi[1].path, FW_ROOT));
        for (UINT32 i = 0; i < count; i++) fprintf(out, "graph %u: %ls at %ls\n", i, pi[i].full, pi[i].path);
    } else CHECK("GetCurrentPackageInfo", 0);
    free(buf);
    HMODULE fw = LoadLibraryW(L"novafw.dll");
    s[0] = 0;
    if (fw) GetModuleFileNameW(fw, s, MAX_PATH);
    CHECK("novafw.dll by name: the framework's", fw && !_wcsicmp(s, FW_ROOT L"\\novafw.dll"));
    fprintf(out, "novafw.dll: %ls\n", s);
    typedef const WCHAR *(*Greeting)(void);
    Greeting g = fw ? (Greeting)GetProcAddress(fw, "NovaFrameworkGreeting") : NULL;
    CHECK("NovaFrameworkGreeting", g && !wcscmp(g(), L"Hello from a framework package"));

    HSTRING cls = NULL;
    void *insp = NULL;
    WindowsCreateString(L"NovaOS.TestFramework.Greeter", 28, &cls);
    HRESULT hr = RoActivateInstance(cls, &insp);
    CHECK("RoActivateInstance(NovaOS.TestFramework.Greeter): the framework's class", SUCCEEDED(hr) && insp);
    fprintf(out, "RoActivateInstance: 0x%08lx\n", (unsigned long)hr);
    HMODULE dll = GetModuleHandleW(L"fwgreeter.dll");
    s[0] = 0;
    if (dll) GetModuleFileNameW(dll, s, MAX_PATH);
    CHECK("fwgreeter.dll: the framework's", !_wcsicmp(s, FW_ROOT L"\\fwgreeter.dll"));
    if (insp) ((IUnknown *)insp)->lpVtbl->Release(insp);
    WindowsDeleteString(cls);
    fprintf(out, "fwchild: %d passed, %d failed\n", pass, fail);
    fclose(out);
    return fail;
}

/* The test root (C:\Tests\Appx\testroot.cer) in the ROOT store: added, or
 * every copy removed (as authtest does) */
static BOOL set_test_root(BOOL trust)
{
    BYTE der[4096];
    DWORD n = 0;
    HANDLE f = CreateFileW(L"C:\\Tests\\Appx\\testroot.cer", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    ReadFile(f, der, sizeof der, &n, NULL);
    CloseHandle(f);
    HANDLE root = CertOpenSystemStoreW(0, L"ROOT");
    BOOL ok = n && root;
    if (ok && trust) ok = CertAddEncodedCertificateToStore(root, 1, der, n, 3 /* REPLACE_EXISTING */, NULL);
    else if (ok) {
        struct { DWORD enc; BYTE *b; DWORD n; void *info, *store; } want = { 1, der, n, NULL, NULL };
        const void *c;
        while ((c = CertFindCertificateInStore(root, 1, 0, 13 << 16 /* CERT_FIND_EXISTING */, &want, NULL)) != NULL)
            CertDeleteCertificateFromStore(c);
    }
    if (root) CertCloseStore(root, 0);
    return ok;
}

/* the started application @aumid's exit code, the child's log shown */
static DWORD run_packaged(const WCHAR *aumid, const WCHAR *args)
{
    void *am = NULL;
    DWORD pid = 0, code = 0xFFFF;
    DeleteFileW(LOG);
    HRESULT hr = CoCreateInstance(&CLSID_ApplicationActivationManager_, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IApplicationActivationManager_, &am);
    if (am) {
        typedef HRESULT (STDMETHODCALLTYPE *Activate)(void *, LPCWSTR, LPCWSTR, int, DWORD *);
        hr = ((Activate)VT(am)[3])(am, aumid, args, 0, &pid);
        ((IUnknown *)am)->lpVtbl->Release(am);
    }
    HANDLE p = SUCCEEDED(hr) && pid ? OpenProcess(SYNCHRONIZE | 0x1000, FALSE, pid) : NULL;
    if (p) {
        WaitForSingleObject(p, 60000);
        GetExitCodeProcess(p, &code);
        CloseHandle(p);
    }
    FILE *log = _wfopen(LOG, L"r");
    char line[512];
    while (log && fgets(line, sizeof line, log)) printf("  packaged: %s", line);
    if (log) fclose(log);
    return code;
}

static void signed_packages(void)
{
    appx(L"remove NovaOS.FrameworkTest");                     /* (a run that stopped half way) */
    appx(L"remove NovaOS.TestFramework");
    set_test_root(FALSE);
    CHECK("refused: signed under a root nobody trusts", appx(L"install C:\\Tests\\Appx\\Framework.msix") == 1);
    CHECK("trust the test root", set_test_root(TRUE));
    CHECK("refused: the app before its framework", appx(L"install C:\\Tests\\Appx\\FrameworkTest.msixbundle") == 1);
    CHECK("refused: records changed after signing", appx(L"install C:\\Tests\\Appx\\FrameworkTampered.msix") == 1);
    CHECK("refused: publisher not the signer",
          appx(L"install C:\\Tests\\Appx\\FrameworkWrongPublisher.msix") == 1);
    CHECK("nothing installed by those", !exists(FW_ROOT) && !exists(FT_ROOT));
    CHECK("appx install (signed framework)", appx(L"install C:\\Tests\\Appx\\Framework.msix") == 0);
    CHECK("the framework's files", exists(FW_ROOT L"\\novafw.dll") && exists(FW_ROOT L"\\AppxSignature.p7x"));
    CHECK("appx install (signed bundle)", appx(L"install C:\\Tests\\Appx\\FrameworkTest.msixbundle") == 0);
    CHECK("the bundle's x64 package", exists(FT_ROOT L"\\appxtest.exe") &&
                                      !exists(L"C:\\Programs\\WindowsApps\\NovaOS.FrameworkTest_1.0.0.0_x86__h384ge6vq8144"));
    CHECK("the packaged program found its framework", run_packaged(FT_FAMILY L"!App", L"fwchild") == 0);
    CHECK("refused: removing a framework in use", appx(L"remove NovaOS.TestFramework") == 1 && exists(FW_ROOT));
    CHECK("appx remove (the app)", appx(L"remove NovaOS.FrameworkTest") == 0 && !exists(FT_ROOT));
    /* the Store's layout: the bundle's block map names only its manifest,
     * each package is signed by itself */
    CHECK("refused: a Store-layout bundle's package not signed",
          appx(L"install C:\\Tests\\Appx\\StoreBundleUnsigned.msixbundle") == 1 && !exists(FT_ROOT));
    CHECK("refused: a Store-layout bundle's package not the bundle's publisher",
          appx(L"install C:\\Tests\\Appx\\StoreBundleOtherPublisher.msixbundle") == 1 &&
          !exists(L"C:\\Programs\\WindowsApps\\NovaOS.FrameworkTest_1.0.0.0_x64__vbgq7nvafm2e2"));
    CHECK("appx install (Store-layout bundle)", appx(L"install C:\\Tests\\Appx\\StoreBundle.msixbundle") == 0 &&
                                                exists(FT_ROOT L"\\appxtest.exe"));
    CHECK("appx remove (the Store-layout app)", appx(L"remove NovaOS.FrameworkTest") == 0 && !exists(FT_ROOT));
    CHECK("appx remove (the framework)", appx(L"remove NovaOS.TestFramework") == 0 && !exists(FW_ROOT));
    set_test_root(FALSE);
}

/* ---- the installer and the activation ---- */

int main(void)
{
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2 && !wcscmp(argv[1], L"child")) return child(argc, argv);
    if (argc >= 2 && !wcscmp(argv[1], L"fwchild")) return fwchild();
    {   /* packaged with no arguments: started from the Start menu (tests/selftest/core/179-appxshell.py) */
        WCHAR id[400];
        UINT32 len = 400;
        if (argc < 2 && !GetCurrentApplicationUserModelId(&len, id)) {
            FILE *f = _wfopen(L"C:\\Temp\\appxtest-start.log", L"w");
            printf("appxtest: started as %ls\n", id);
            if (f) { fprintf(f, "appxtest: started as %ls\n", id); fclose(f); }
            return 0;
        }
    }
    out = stdout;
    CreateDirectoryW(L"C:\\Temp", NULL);
    appx(L"remove NovaOS.PackageTest");                       /* (a run that stopped half way) */

    CHECK("refused: unsigned, no -AllowUnsigned", appx(L"install C:\\Tests\\Appx\\PackageTest.msix") == 1);
    CHECK("refused: a file changed after its block map", appx(L"C:\\Tests\\Appx\\Tampered.msix -AllowUnsigned") == 1);
    CHECK("refused: a signature that does not verify", appx(L"C:\\Tests\\Appx\\Signed.msix -AllowUnsigned") == 1);
    CHECK("refused: unsigned without the OID", appx(L"C:\\Tests\\Appx\\NoOid.msix -AllowUnsigned") == 1);
    CHECK("nothing installed by those", !exists(ROOT));

    CHECK("appx install", appx(L"install C:\\Tests\\Appx\\PackageTest.msix -AllowUnsigned") == 0);
    CHECK("the package's files", exists(ROOT L"\\appxtest.exe") && exists(ROOT L"\\greeter.dll") &&
                                 exists(ROOT L"\\resources.pri") && exists(ROOT L"\\AppxManifest.xml"));
    WCHAR s[MAX_PATH];
    UINT32 n = MAX_PATH;
    CHECK("GetPackagePathByFullName", !GetPackagePathByFullName(FULL, &n, s) && !_wcsicmp(s, ROOT));
    n = MAX_PATH;
    CHECK("not packaged itself", GetCurrentPackageFullName(&n, s) == APPMODEL_ERROR_NO_PACKAGE);
    DWORD size = sizeof s;
    s[0] = 0;
    RegGetValueW(HKEY_CURRENT_USER, REPO L"\\Packages\\" FULL, L"DisplayName", RRF_RT_REG_SZ, NULL, s, &size);
    CHECK("its name, from resources.pri", !wcscmp(s, L"NovaOS Package Test"));
    printf("DisplayName: %ls\n", s);
    size = sizeof s;
    s[0] = 0;
    RegGetValueW(HKEY_CURRENT_USER, REPO L"\\Packages\\" FULL L"\\Applications\\App", L"Logo", RRF_RT_REG_SZ, NULL,
                 s, &size);
    CHECK("its tile logo for the Start menu (the scale-200 one)", !_wcsicmp(s, LOGO) && exists(LOGO));
    printf("Logo: %ls\n", s);
    CHECK("appx list", appx(L"list") == 0);

    /* started by its application user model id */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    void *am = NULL;
    DWORD pid = 0, code = 0xFFFF;
    DeleteFileW(LOG);
    HRESULT hr = CoCreateInstance(&CLSID_ApplicationActivationManager_, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IApplicationActivationManager_, &am);
    CHECK("CoCreateInstance(ApplicationActivationManager)", SUCCEEDED(hr) && am);
    if (am) {
        typedef HRESULT (STDMETHODCALLTYPE *Activate)(void *, LPCWSTR, LPCWSTR, int, DWORD *);
        hr = ((Activate)VT(am)[3])(am, FAMILY L"!App", L"child one \"two words\"", 0, &pid);
        CHECK("ActivateApplication", SUCCEEDED(hr) && pid);
        ((IUnknown *)am)->lpVtbl->Release(am);
    }
    HANDLE p = pid ? OpenProcess(SYNCHRONIZE | 0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, FALSE, pid) : NULL;
    if (p) {
        WaitForSingleObject(p, 60000);
        GetExitCodeProcess(p, &code);
        CloseHandle(p);
    }
    FILE *log = _wfopen(LOG, L"r");
    char line[512];
    while (log && fgets(line, sizeof line, log)) printf("  packaged: %s", line);
    if (log) fclose(log);
    CHECK("the packaged program passed", code == 0);
    CHECK("an unknown application", FAILED(hr = CoCreateInstance(&CLSID_ApplicationActivationManager_, NULL,
                                            CLSCTX_INPROC_SERVER, &IID_IApplicationActivationManager_, &am)) ||
          (((HRESULT (STDMETHODCALLTYPE *)(void *, LPCWSTR, LPCWSTR, int, DWORD *))VT(am)[3])(
               am, FAMILY L"!Nope", NULL, 0, &pid) != S_OK));

    CHECK("appx remove", appx(L"remove NovaOS.PackageTest") == 0);
    n = MAX_PATH;
    CHECK("removed: its folder", !exists(ROOT));
    CHECK("removed: out of the Start menu's list",
          RegGetValueW(HKEY_CURRENT_USER, REPO L"\\Packages\\" FULL L"\\Applications\\App", L"Logo", RRF_RT_REG_SZ,
                       NULL, NULL, NULL) != ERROR_SUCCESS);
    CHECK("removed: unknown to kernel32", GetPackagePathByFullName(FULL, &n, s) != 0);
    CHECK("removing it again fails", appx(L"remove NovaOS.PackageTest") == 1);

    signed_packages();

    printf("appxtest: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}
