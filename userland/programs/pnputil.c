/*
 * pnputil.exe — Windows' driver package utility
 *
 *   pnputil /add-driver <file.inf | *.inf> [/subdirs] [/install]
 *                          stage packages in the driver store and publish
 *                          them as oemN.inf (/install: then install them on
 *                          the devices they match)
 *   pnputil /delete-driver <oemN.inf> [/uninstall] [/force]
 *   pnputil /enum-drivers  the published packages
 *
 * (and Windows 7's -a, -i -a, -d and -e).  setupapi (driver.c) keeps the
 * store; newdev installs on devices, and on NovaOS no device takes a
 * Windows driver package (NovaOS's own drivers run its devices), so
 * /install reports that no device matched.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

__declspec(dllimport) BOOL WINAPI SetupCopyOEMInfW(PCWSTR src, PCWSTR media_loc, DWORD media_type, DWORD style,
                                                   PWSTR dest, DWORD size, PDWORD need, PWSTR *component);
__declspec(dllimport) BOOL WINAPI SetupUninstallOEMInfW(PCWSTR inf_name, DWORD flags, PVOID reserved);
__declspec(dllimport) HANDLE WINAPI SetupOpenInfFileW(LPCWSTR name, LPCWSTR cls, DWORD style, PUINT err_line);
__declspec(dllimport) void WINAPI SetupCloseInfFile(HANDLE h);
__declspec(dllimport) BOOL WINAPI SetupGetLineTextW(PVOID ctx, HANDLE h, LPCWSTR section, LPCWSTR key,
                                                    LPWSTR buf, DWORD size, PDWORD need);

#define DB_KEY L"SYSTEM\\DriverDatabase"
#define SPOST_PATH 1
#define SP_COPY_NOOVERWRITE 0x8

static int g_total, g_added, g_failed;

static void message(DWORD e)
{
    char m[256];
    if (e == 0xE0000100) { printf("The INF file is not a valid driver package (no [Version] section).\n"); return; }
    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, m, sizeof(m), NULL))
        snprintf(m, sizeof(m), "Error 0x%08lX.\n", (unsigned long)e);
    printf("%s", m);
    size_t n = strlen(m);
    if (n && m[n - 1] != '\n') printf("\n");
}

static void add_one(const WCHAR *path, BOOL install)
{
    const WCHAR *base = path;
    for (const WCHAR *s = path; *s; s++) if (*s == '\\' || *s == '/') base = s + 1;
    g_total++;
    printf("Adding driver package:  %ls\n", base);
    WCHAR full[MAX_PATH], dest[MAX_PATH], *comp = NULL;
    GetFullPathNameW(path, MAX_PATH, full, NULL);
    BOOL ok = SetupCopyOEMInfW(full, NULL, SPOST_PATH, SP_COPY_NOOVERWRITE, dest, MAX_PATH, NULL, &comp);
    DWORD e = GetLastError();
    if (!ok && e == ERROR_FILE_EXISTS && comp) {
        printf("Driver package is up-to-date on the system.\n");
        ok = TRUE;
    } else if (!ok) {
        printf("Failed to add driver package: ");
        message(e);
        printf("\n");
        g_failed++;
        return;
    } else {
        printf("Driver package added successfully.\n");
        g_added++;
    }
    printf("Published Name:         %ls\n", comp ? comp : dest);
    if (install) printf("Driver package is not installed on any device: no device on this PC matches it.\n");
    printf("\n");
}

static void add_matches(const WCHAR *pattern, BOOL subdirs, BOOL install)
{
    WCHAR dir[MAX_PATH], spec[MAX_PATH], p[MAX_PATH];
    lstrcpynW(dir, pattern, MAX_PATH);
    WCHAR *slash = NULL;
    for (WCHAR *s = dir; *s; s++) if (*s == '\\' || *s == '/') slash = s;
    if (slash) { lstrcpynW(spec, slash + 1, MAX_PATH); slash[1] = 0; }
    else { lstrcpynW(spec, dir, MAX_PATH); dir[0] = 0; }
    lstrcpyW(p, dir);
    lstrcatW(p, spec);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(p, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            lstrcpyW(p, dir);
            lstrcatW(p, fd.cFileName);
            add_one(p, install);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!subdirs) return;
    lstrcpyW(p, dir);
    lstrcatW(p, L"*");
    h = FindFirstFileW(p, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        lstrcpyW(p, dir);
        lstrcatW(p, fd.cFileName);
        lstrcatW(p, L"\\");
        lstrcatW(p, spec);
        add_matches(p, TRUE, install);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static BOOL db_get(const WCHAR *sub, const WCHAR *name, WCHAR *out, DWORD cap)
{
    WCHAR k[MAX_PATH];
    swprintf(k, MAX_PATH, L"%ls\\%ls", DB_KEY, sub);
    HKEY h;
    out[0] = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &h)) return FALSE;
    DWORD type, n = cap * sizeof(WCHAR);
    LONG e = RegQueryValueExW(h, name, NULL, &type, (BYTE *)out, &n);
    RegCloseKey(h);
    if (e || type != REG_SZ) { out[0] = 0; return FALSE; }
    out[cap - 1] = 0;
    return TRUE;
}

static void enum_drivers(void)
{
    WCHAR dir[MAX_PATH], pat[MAX_PATH], sub[MAX_PATH], pkg[MAX_PATH], v[256];
    GetWindowsDirectoryW(dir, MAX_PATH);
    lstrcatW(dir, L"\\INF\\");
    swprintf(pat, MAX_PATH, L"%lsoem*.inf", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    int n = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            swprintf(sub, MAX_PATH, L"DriverInfFiles\\%ls", fd.cFileName);
            if (!db_get(sub, L"", pkg, MAX_PATH)) continue;
            swprintf(sub, MAX_PATH, L"DriverPackages\\%ls", pkg);
            printf("Published Name:     %ls\n", fd.cFileName);
            db_get(sub, L"InfName", v, 256);   printf("Original Name:      %ls\n", v);
            db_get(sub, L"Provider", v, 256);  printf("Provider Name:      %ls\n", v);
            db_get(sub, L"Class", v, 256);     printf("Class Name:         %ls\n", v);
            db_get(sub, L"ClassGuid", v, 256); printf("Class GUID:         %ls\n", v);
            db_get(sub, L"Version", v, 256);
            for (WCHAR *c = v; *c; c++) if (*c == ',') *c = ' ';
            printf("Driver Version:     %ls\n", v);
            printf("Signer Name:        (not checked: NovaOS does not load kernel-mode drivers)\n\n");
            n++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!n) printf("No driver packages found on the system.\n");
}

static BOOL is(const char *a, const char *b) { return !_stricmp(a, b); }

static int usage(void)
{
    printf("PNPUTIL [/add-driver <filename.inf | *.inf> [/subdirs] [/install]]\n"
           "        [/delete-driver <oem#.inf> [/uninstall] [/force]]\n"
           "        [/enum-drivers]\n");
    return 1;
}

int main(int argc, char **argv)
{
    printf("Microsoft PnP Utility\n\n");
    if (argc < 2) return usage();
    const char *op = argv[1];
    if (is(op, "/add-driver") || is(op, "-a") || (is(op, "-i") && argc > 2 && is(argv[2], "-a"))) {
        int at = is(op, "-i") ? 3 : 2;
        BOOL install = is(op, "-i"), subdirs = FALSE;
        if (argc <= at) return usage();
        for (int i = at + 1; i < argc; i++) {
            if (is(argv[i], "/install")) install = TRUE;
            else if (is(argv[i], "/subdirs")) subdirs = TRUE;
            else if (is(argv[i], "/reboot")) ;
            else return usage();
        }
        WCHAR pat[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[at], -1, pat, MAX_PATH);
        add_matches(pat, subdirs, install);
        if (!g_total) { printf("No driver packages were found: %s\n", argv[at]); return 2; }
        printf("Total driver packages:  %d\nAdded driver packages:  %d\n", g_total, g_added);
        return g_failed ? 1 : 0;
    }
    if (is(op, "/delete-driver") || is(op, "-d")) {
        if (argc < 3) return usage();
        WCHAR name[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, name, MAX_PATH);
        if (!SetupUninstallOEMInfW(name, 0, NULL)) {
            printf("Failed to delete driver package: ");
            message(GetLastError());
            return 1;
        }
        printf("Driver package deleted successfully.\n");
        return 0;
    }
    if (is(op, "/enum-drivers") || is(op, "-e")) { enum_drivers(); return 0; }
    return usage();
}
