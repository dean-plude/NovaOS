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
 * writes what it found to C:\Temp\appxtest-child.log and exits with its
 * failure count; then "appx remove" takes the package away again.
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
 * installed.
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
#define LINK L"C:\\AppData\\Roaming\\Start Menu\\Programs\\NovaOS Package Test.lnk"
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
    CHECK("its Start menu entry", exists(LINK));
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
    CHECK("removed: its Start menu entry", !exists(LINK));
    CHECK("removed: unknown to kernel32", GetPackagePathByFullName(FULL, &n, s) != 0);
    CHECK("removing it again fails", appx(L"remove NovaOS.PackageTest") == 1);

    signed_packages();

    printf("appxtest: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}
