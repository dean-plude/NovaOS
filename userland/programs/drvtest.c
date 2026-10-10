/* drvtest.exe — installing a Windows driver package, the three ways one
 * arrives (tools/drivertest/mkdriver.py's package in C:\Tests\Driver):
 *
 *   drvtest exe   as a driver setup program does: SetupOpenInfFile and
 *                 SetupGetLineText read the INF ([Strings] substituted),
 *                 DiInstallDriver stages it in the driver store and
 *                 publishes it as oemN.inf, SetupCopyOEMInf finds it again
 *                 instead of publishing it twice, UpdateDriverForPlugAndPlay
 *                 Devices finds no device for its hardware ID, a package
 *                 with a file missing is refused and leaves nothing behind,
 *                 and DiUninstallDriver removes it again
 *   drvtest zip   as from an unpacked .zip: pnputil /add-driver on the
 *                 folder's INF, /enum-drivers, /delete-driver
 *   drvtest msi   msiexec installs driver.msi, whose custom action runs
 *                 pnputil /add-driver on the files it installed
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

__declspec(dllimport) BOOL WINAPI SetupCopyOEMInfW(PCWSTR src, PCWSTR media_loc, DWORD media_type, DWORD style,
                                                   PWSTR dest, DWORD size, PDWORD need, PWSTR *component);
__declspec(dllimport) BOOL WINAPI SetupUninstallOEMInfW(PCWSTR inf_name, DWORD flags, PVOID reserved);
__declspec(dllimport) HANDLE WINAPI SetupOpenInfFileW(LPCWSTR name, LPCWSTR cls, DWORD style, PUINT err_line);
__declspec(dllimport) void WINAPI SetupCloseInfFile(HANDLE h);
__declspec(dllimport) BOOL WINAPI SetupGetLineTextW(PVOID ctx, HANDLE h, LPCWSTR section, LPCWSTR key,
                                                    LPWSTR buf, DWORD size, PDWORD need);
__declspec(dllimport) BOOL WINAPI SetupGetInfDriverStoreLocationW(PCWSTR file, PVOID alt, PCWSTR locale, DWORD flags,
                                                                  PWSTR out, DWORD size, PDWORD need);
__declspec(dllimport) BOOL WINAPI DiInstallDriverW(HWND parent, LPCWSTR inf, DWORD flags, PBOOL reboot);
__declspec(dllimport) BOOL WINAPI DiUninstallDriverW(HWND parent, LPCWSTR inf, DWORD flags, PBOOL reboot);
__declspec(dllimport) BOOL WINAPI UpdateDriverForPlugAndPlayDevicesW(HWND parent, LPCWSTR hwid, LPCWSTR inf,
                                                                     DWORD flags, PBOOL reboot);

#define PKG       L"C:\\Tests\\Driver\\novadrv.inf"
#define MSI_INF   L"C:\\Programs\\NovaDriverTest\\novadrv.inf"
#define MSIEXEC   "C:\\Windows\\System32\\msiexec.exe"
#define PNPUTIL   "C:\\Windows\\System32\\pnputil.exe"
#define ERROR_NO_SUCH_DEVINST_ 0xE000020B
#define SP_COPY_NOOVERWRITE 0x8

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d, error 0x%lx)\n", what, __LINE__, (unsigned long)GetLastError()); } } while (0)

static bool exists(const WCHAR *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static int published_count(void)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(L"C:\\Windows\\INF\\oem*.inf", &fd);
    int n = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do n++; while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

/* The oemN.inf @inf is published as (SetupCopyOEMInf refuses to publish
 * it twice and names the copy); FALSE if it is not published */
static bool published_name(const WCHAR *inf, WCHAR *name)
{
    WCHAR dest[MAX_PATH], *comp = NULL;
    int before = published_count();
    BOOL ok = SetupCopyOEMInfW(inf, NULL, 1, SP_COPY_NOOVERWRITE, dest, MAX_PATH, NULL, &comp);
    DWORD e = GetLastError();
    if (ok && published_count() > before) {             /* it was not: undo */
        SetupUninstallOEMInfW(comp, 0, NULL);
        return false;
    }
    if (!comp || (!ok && e != ERROR_FILE_EXISTS)) return false;
    lstrcpyW(name, comp);
    return true;
}

/* @cmd's exit code, its output in @out */
static DWORD run(const char *cmd, char *out, int cap)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE r, w;
    out[0] = 0;
    if (!CreatePipe(&r, &w, &sa, 0)) return 0xFFFFFFFF;
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = w;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    char line[1024];
    snprintf(line, sizeof(line), "%s", cmd);
    printf("  %s\n", cmd);
    if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(r); CloseHandle(w);
        return 0xFFFFFFFF;
    }
    CloseHandle(w);
    int n = 0;
    DWORD got;
    while (n < cap - 1 && ReadFile(r, out + n, (DWORD)(cap - 1 - n), &got, NULL) && got) n += (int)got;
    out[n] = 0;
    CloseHandle(r);
    WaitForSingleObject(pi.hProcess, 300000);
    DWORD code = 0xFFFFFFFE;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static void store_has_files(const WCHAR *oem)
{
    WCHAR loc[MAX_PATH], p[MAX_PATH];
    CHECK("SetupGetInfDriverStoreLocation names the store copy",
          SetupGetInfDriverStoreLocationW(oem, NULL, NULL, 0, loc, MAX_PATH, NULL) &&
          wcsstr(loc, L"\\DriverStore\\FileRepository\\novadrv.inf_") != NULL);
    CHECK("the store holds the INF", exists(loc));
    WCHAR *slash = wcsrchr(loc, L'\\');
    if (!slash) return;
    slash[1] = 0;
    swprintf(p, MAX_PATH, L"%lsnovadrv.sys", loc);
    CHECK("the store holds the driver file (SourceDisksFiles)", exists(p));
    swprintf(p, MAX_PATH, L"%lsnovadrv.cat", loc);
    CHECK("the store holds the catalog (CatalogFile)", exists(p));
}

static void test_exe(void)
{
    UINT line = 0;
    HANDLE inf = SetupOpenInfFileW(PKG, NULL, 2, &line);
    CHECK("SetupOpenInfFile", inf != INVALID_HANDLE_VALUE);
    WCHAR v[256];
    CHECK("SetupGetLineText substitutes [Strings]",
          inf != INVALID_HANDLE_VALUE && SetupGetLineTextW(NULL, inf, L"Version", L"Provider", v, 256, NULL) &&
          !wcscmp(v, L"NovaOS Test"));
    CHECK("SetupGetLineText takes the quotes off",
          inf != INVALID_HANDLE_VALUE && SetupGetLineTextW(NULL, inf, L"Version", L"Signature", v, 256, NULL) &&
          !wcscmp(v, L"$WINDOWS NT$"));
    DWORD need = 0;
    CHECK("SetupGetLineText reports a missing line",
          inf != INVALID_HANDLE_VALUE && !SetupGetLineTextW(NULL, inf, L"Version", L"NoSuchKey", v, 256, &need) &&
          GetLastError() == 0xE0000102);
    if (inf != INVALID_HANDLE_VALUE) SetupCloseInfFile(inf);
    CHECK("a file without [Version] is not an INF",
          SetupOpenInfFileW(L"C:\\Tests\\Driver\\novadrv.cat", NULL, 2, NULL) == INVALID_HANDLE_VALUE);

    int before = published_count();
    BOOL reboot = TRUE;
    CHECK("DiInstallDriver stages the package", DiInstallDriverW(NULL, PKG, 0, &reboot) && !reboot);
    CHECK("it is published as one oemN.inf", published_count() == before + 1);
    WCHAR oem[MAX_PATH] = L"";
    CHECK("SetupCopyOEMInf finds it published (ERROR_FILE_EXISTS, its name)", published_name(PKG, oem));
    printf("  published as %ls\n", oem);
    CHECK("DiInstallDriver again publishes nothing more",
          DiInstallDriverW(NULL, PKG, 0, NULL) && published_count() == before + 1);
    store_has_files(oem);
    CHECK("no device takes it (ERROR_NO_SUCH_DEVINST)",
          !UpdateDriverForPlugAndPlayDevicesW(NULL, L"ROOT\\NOVADRV", PKG, 0, &reboot) &&
          GetLastError() == ERROR_NO_SUCH_DEVINST_);

    /* the INF without its .sys: refused, nothing left behind */
    CreateDirectoryW(L"C:\\Tests\\DriverOut", NULL);
    CreateDirectoryW(L"C:\\Tests\\DriverOut\\broken", NULL);
    CopyFileW(PKG, L"C:\\Tests\\DriverOut\\broken\\novadrv.inf", FALSE);
    CopyFileW(L"C:\\Tests\\Driver\\novadrv.cat", L"C:\\Tests\\DriverOut\\broken\\novadrv.cat", FALSE);
    int now = published_count();
    /* (its bytes match the published INF: changed by one comment line) */
    HANDLE f = CreateFileW(L"C:\\Tests\\DriverOut\\broken\\novadrv.inf", FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD put;
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, "; broken\r\n", 10, &put, NULL); CloseHandle(f); }
    CHECK("a package with a file missing is refused",
          !DiInstallDriverW(NULL, L"C:\\Tests\\DriverOut\\broken\\novadrv.inf", 0, NULL) &&
          GetLastError() == ERROR_FILE_NOT_FOUND);
    CHECK("... and nothing is published", published_count() == now);

    CHECK("DiUninstallDriver removes it", DiUninstallDriverW(NULL, PKG, 0, NULL));
    WCHAR p[MAX_PATH];
    swprintf(p, MAX_PATH, L"C:\\Windows\\INF\\%ls", oem);
    CHECK("... the oemN.inf is gone", !exists(p) && published_count() == before);
    CHECK("... and the store copy", !SetupGetInfDriverStoreLocationW(oem, NULL, NULL, 0, p, MAX_PATH, NULL));
    CHECK("SetupUninstallOEMInf of it now: not found",
          !SetupUninstallOEMInfW(oem, 0, NULL) && GetLastError() == ERROR_FILE_NOT_FOUND);
}

static void test_zip(void)
{
    static char out[8192];
    DWORD code = run(PNPUTIL " /add-driver C:\\Tests\\Driver\\*.inf /install", out, sizeof(out));
    printf("%s", out);
    CHECK("pnputil /add-driver succeeds", code == 0 && strstr(out, "Driver package added successfully."));
    char name[64] = "";
    const char *pn = strstr(out, "Published Name:");
    if (pn) sscanf(pn, "Published Name: %63s", name);
    CHECK("... names the published INF", !strncmp(name, "oem", 3));
    CHECK("... and says no device took it", strstr(out, "not installed on any device") != NULL);
    code = run(PNPUTIL " /add-driver C:\\Tests\\Driver\\novadrv.inf", out, sizeof(out));
    CHECK("adding it again: up to date", code == 0 && strstr(out, "up-to-date"));
    code = run(PNPUTIL " /enum-drivers", out, sizeof(out));
    printf("%s", out);
    CHECK("pnputil /enum-drivers lists it",
          code == 0 && strstr(out, name) && strstr(out, "Original Name:      novadrv.inf") &&
          strstr(out, "Provider Name:      NovaOS Test") && strstr(out, "Class Name:         System") &&
          strstr(out, "Driver Version:     10/10/2026 1.0.0.0"));
    char cmd[128];
    snprintf(cmd, sizeof(cmd), PNPUTIL " /delete-driver %s", name);
    code = run(cmd, out, sizeof(out));
    CHECK("pnputil /delete-driver", code == 0 && strstr(out, "Driver package deleted successfully."));
    code = run(cmd, out, sizeof(out));
    CHECK("deleting it again fails", code != 0);
}

static void test_msi(void)
{
    static char out[4096];
    int before = published_count();
    DWORD code = run("\"" MSIEXEC "\" /i C:\\Tests\\Msi\\driver.msi /qn", out, sizeof(out));
    CHECK("msiexec installs driver.msi", code == 0);
    CHECK("its files are installed", exists(MSI_INF));
    CHECK("its custom action published the package", published_count() == before + 1);
    WCHAR oem[MAX_PATH];
    CHECK("... the INF it installed", published_name(MSI_INF, oem));
    store_has_files(oem);
    CHECK("DiUninstallDriver removes it", DiUninstallDriverW(NULL, MSI_INF, 0, NULL) && published_count() == before);
    code = run("\"" MSIEXEC "\" /x C:\\Tests\\Msi\\driver.msi /qn", out, sizeof(out));
    CHECK("msiexec uninstalls driver.msi", code == 0 && !exists(MSI_INF));
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";
    if (!strcmp(what, "exe")) test_exe();
    else if (!strcmp(what, "zip")) test_zip();
    else if (!strcmp(what, "msi")) test_msi();
    else { printf("usage: drvtest exe | zip | msi\n"); return 2; }
    printf("drvtest %s: %d passed, %d failed\n", what, pass, fail);
    return fail != 0;
}
