/*
 * pnputil.exe — Windows' driver package utility
 *
 *   pnputil /add-driver <file.inf | *.inf> [/subdirs] [/install]
 *                          stage packages in the driver store and publish
 *                          them as oemN.inf (/install: then install them on
 *                          the devices they match)
 *   pnputil /delete-driver <oemN.inf> [/uninstall] [/force]
 *   pnputil /enum-drivers  the published packages
 *   pnputil /enum-devices [/connected | /disconnected] [/class <name | GUID>]
 *                         [/instanceid <id>] [/deviceid <id>] [/ids] [/drivers]
 *                          the devices of the Plug and Play tree (cfgmgr32),
 *                          with their hardware and compatible IDs (/ids) and
 *                          the published packages whose models match them
 *                          (/drivers)
 *
 * (and Windows 7's -a, -i -a, -d and -e).  setupapi (driver.c) keeps the
 * store; newdev installs on devices, and on NovaOS no device takes a
 * Windows driver package (NovaOS's own drivers run its devices, and it
 * loads no kernel-mode .sys driver), so /install reports that no device
 * matched and /enum-devices /drivers shows a matching package as outranked
 * by the NovaOS driver.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

__declspec(dllimport) BOOL WINAPI SetupCopyOEMInfW(PCWSTR src, PCWSTR media_loc, DWORD media_type, DWORD style,
                                                   PWSTR dest, DWORD size, PDWORD need, PWSTR *component);
__declspec(dllimport) BOOL WINAPI SetupUninstallOEMInfW(PCWSTR inf_name, DWORD flags, PVOID reserved);
__declspec(dllimport) HANDLE WINAPI SetupOpenInfFileW(LPCWSTR name, LPCWSTR cls, DWORD style, PUINT err_line);
__declspec(dllimport) void WINAPI SetupCloseInfFile(HANDLE h);
__declspec(dllimport) BOOL WINAPI SetupGetLineTextW(PVOID ctx, HANDLE h, LPCWSTR section, LPCWSTR key,
                                                    LPWSTR buf, DWORD size, PDWORD need);

typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW(PULONG len, LPCWSTR filter, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_ListW(LPCWSTR filter, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW(DWORD dn, ULONG prop, PULONG type, PVOID buf, PULONG len,
                                                                        ULONG flags);

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

/* -----------------------------------------------------------------------
 * /enum-devices
 * ----------------------------------------------------------------------- */
#define CM_DRP_DEVICEDESC    0x01
#define CM_DRP_HARDWAREID    0x02
#define CM_DRP_COMPATIBLEIDS 0x03
#define CM_DRP_SERVICE       0x05
#define CM_DRP_CLASS         0x08
#define CM_DRP_CLASSGUID     0x09
#define CM_DRP_MFG           0x0C

static BOOL prop(DWORD dn, ULONG p, WCHAR *out, ULONG cap)
{
    ULONG type, len = cap * sizeof(WCHAR);
    memset(out, 0, cap * sizeof(WCHAR));
    return !CM_Get_DevNode_Registry_PropertyW(dn, p, &type, out, &len, 0);
}

/* An INF read whole, as UTF-16 (from UTF-16LE with a BOM, or ANSI) */
static WCHAR *read_inf(const WCHAR *path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(h, NULL), got = 0;
    BYTE *b = size < (8u << 20) ? malloc(size + 2) : NULL;
    if (b && !ReadFile(h, b, size, &got, NULL)) got = 0;
    CloseHandle(h);
    if (!b) return NULL;
    WCHAR *w;
    if (got >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        w = malloc(got);
        memcpy(w, b + 2, got - 2);
        w[(got - 2) / 2] = 0;
    } else {
        w = malloc((got + 1) * sizeof(WCHAR));
        int n = MultiByteToWideChar(CP_ACP, 0, (const char *)b, (int)got, w, (int)got);
        w[n] = 0;
    }
    free(b);
    return w;
}

static void trim(WCHAR *s)
{
    WCHAR *a = s;
    while (*a == ' ' || *a == '\t') a++;
    memmove(s, a, (wcslen(a) + 1) * sizeof(WCHAR));
    size_t n = wcslen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}

/* Call @fn(value, ctx) for each line of [@section] (comments taken off;
 * the part after '=' when there is one) */
static void section_lines(const WCHAR *inf, const WCHAR *section, void (*fn)(WCHAR *, void *), void *ctx)
{
    BOOL in = FALSE;
    for (const WCHAR *l = inf; *l;) {
        const WCHAR *e = l;
        while (*e && *e != '\n') e++;
        WCHAR line[512];
        size_t n = (size_t)(e - l) < 511 ? (size_t)(e - l) : 511;
        memcpy(line, l, n * sizeof(WCHAR));
        line[n] = 0;
        l = *e ? e + 1 : e;
        BOOL quoted = FALSE;
        for (WCHAR *c = line; *c; c++) {
            if (*c == '"') quoted = !quoted;
            else if (*c == ';' && !quoted) { *c = 0; break; }
        }
        trim(line);
        if (line[0] == '[') {
            WCHAR *close = wcschr(line, ']');
            if (close) *close = 0;
            in = !_wcsicmp(line + 1, section);
            continue;
        }
        if (!in || !line[0]) continue;
        WCHAR *eq = wcschr(line, '=');
        WCHAR *v = eq ? eq + 1 : line;
        trim(v);
        fn(v, ctx);
    }
}

typedef struct {
    const WCHAR *inf;
    const WCHAR *hwids, *compat;          /* the device's, REG_MULTI_SZ */
    DWORD rank;                           /* the best match (lowest), or ~0 */
    WCHAR id[200];                        /* the device ID it matched */
} Match;

static void model_line(WCHAR *v, void *ctx)
{
    Match *m = ctx;
    WCHAR *f = wcschr(v, ',');            /* (field 0 is the install section) */
    while (f) {
        WCHAR id[200];
        WCHAR *next = wcschr(f + 1, ',');
        size_t n = next ? (size_t)(next - f - 1) : wcslen(f + 1);
        if (n > 199) n = 199;
        memcpy(id, f + 1, n * sizeof(WCHAR));
        id[n] = 0;
        trim(id);
        f = next;
        DWORD k = 0;
        for (const WCHAR *h = m->hwids; *h; h += wcslen(h) + 1, k++)
            if (!_wcsicmp(h, id) && k < m->rank) { m->rank = k; lstrcpynW(m->id, h, 200); }
        k = 0;
        for (const WCHAR *c = m->compat; *c; c += wcslen(c) + 1, k++)
            if (!_wcsicmp(c, id) && 0x2000 + k * 0x100 < m->rank) { m->rank = 0x2000 + k * 0x100; lstrcpynW(m->id, c, 200); }
    }
}

static void manufacturer_line(WCHAR *v, void *ctx)
{
    Match *m = ctx;
    WCHAR models[128], sec[200];
    WCHAR *f = wcschr(v, ',');
    size_t n = f ? (size_t)(f - v) : wcslen(v);
    if (n > 127) n = 127;
    memcpy(models, v, n * sizeof(WCHAR));
    models[n] = 0;
    trim(models);
    section_lines(m->inf, models, model_line, m);
    while (f) {                           /* (each decorated section: Models.NTamd64, ...) */
        WCHAR dec[64];
        WCHAR *next = wcschr(f + 1, ',');
        size_t k = next ? (size_t)(next - f - 1) : wcslen(f + 1);
        if (k > 63) k = 63;
        memcpy(dec, f + 1, k * sizeof(WCHAR));
        dec[k] = 0;
        trim(dec);
        f = next;
        swprintf(sec, 200, L"%ls.%ls", models, dec);
        section_lines(m->inf, sec, model_line, m);
    }
}

static void print_list(const char *label, const WCHAR *m)
{
    printf("%s\n", label);
    for (; *m; m += wcslen(m) + 1) printf("    %ls\n", m);
}

/* The published packages whose models name one of the device's IDs */
static void matching_drivers(const WCHAR *hwids, const WCHAR *compat, const WCHAR *service)
{
    WCHAR dir[MAX_PATH], pat[MAX_PATH], path[MAX_PATH], sub[MAX_PATH], pkg[MAX_PATH], v[256];
    GetWindowsDirectoryW(dir, MAX_PATH);
    lstrcatW(dir, L"\\INF\\");
    swprintf(pat, MAX_PATH, L"%lsoem*.inf", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    BOOL any = FALSE;
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        swprintf(path, MAX_PATH, L"%ls%ls", dir, fd.cFileName);
        Match m = { 0 };
        m.inf = read_inf(path);
        m.hwids = hwids;
        m.compat = compat;
        m.rank = ~0u;
        if (!m.inf) continue;
        section_lines(m.inf, L"Manufacturer", manufacturer_line, &m);
        free((void *)m.inf);
        if (m.rank == ~0u) continue;
        if (!any) printf("Matching Drivers:\n");
        any = TRUE;
        swprintf(sub, MAX_PATH, L"DriverInfFiles\\%ls", fd.cFileName);
        db_get(sub, L"", pkg, MAX_PATH);
        swprintf(sub, MAX_PATH, L"DriverPackages\\%ls", pkg);
        printf("    Driver Name:            %ls\n", fd.cFileName);
        db_get(sub, L"InfName", v, 256);   printf("    Original Name:          %ls\n", v);
        db_get(sub, L"Provider", v, 256);  printf("    Provider Name:          %ls\n", v);
        db_get(sub, L"Class", v, 256);     printf("    Class Name:             %ls\n", v);
        db_get(sub, L"ClassGuid", v, 256); printf("    Class GUID:             %ls\n", v);
        db_get(sub, L"Version", v, 256);
        for (WCHAR *c = v; *c; c++) if (*c == ',') *c = ' ';
        printf("    Driver Version:         %ls\n", v);
        printf("    Matching Device ID:     %ls\n", m.id);
        printf("    Driver Rank:            %08lX\n", (unsigned long)(0x00FF0000u | m.rank));   /* (unsigned) */
        if (service[0]) printf("    Driver Status:          Outranked by the NovaOS driver %ls (NovaOS loads no kernel-mode driver)\n", service);
        else printf("    Driver Status:          Not installed (NovaOS loads no kernel-mode driver)\n");
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static const char *problem_name(ULONG p)
{
    switch (p) {
    case 10: return "CM_PROB_FAILED_START";
    case 22: return "CM_PROB_DISABLED";
    case 24: return "CM_PROB_DEVICE_NOT_THERE";
    case 28: return "CM_PROB_FAILED_INSTALL";
    case 43: return "CM_PROB_FAILED_POST_START";
    }
    return "CM_PROB_UNKNOWN";
}

typedef struct {
    int present;                          /* 1 connected, 2 disconnected, 0 either */
    const WCHAR *cls, *instance, *device_id;
    BOOL ids, drivers;
} EnumOpts;

static BOOL in_multi(const WCHAR *m, const WCHAR *id)
{
    for (; *m; m += wcslen(m) + 1) if (!_wcsicmp(m, id)) return TRUE;
    return FALSE;
}

static int enum_devices(const EnumOpts *o)
{
    ULONG len = 0;
    CM_Get_Device_ID_List_SizeW(&len, NULL, 0);
    WCHAR *ids = malloc((len + 1) * sizeof(WCHAR));
    if (!ids || CM_Get_Device_ID_ListW(NULL, ids, len + 1, 0)) { printf("Failed to enumerate devices.\n"); return 1; }
    int n = 0;
    for (WCHAR *id = ids; *id; id += wcslen(id) + 1) {
        DWORD dn;
        ULONG status = 0, problem = 0;
        if (CM_Locate_DevNodeW(&dn, id, 1 /* CM_LOCATE_DEVNODE_PHANTOM */)) continue;
        BOOL here = !CM_Get_DevNode_Status(&status, &problem, dn, 0);
        if ((o->present == 1 && !here) || (o->present == 2 && here)) continue;
        if (o->instance && _wcsicmp(o->instance, id)) continue;
        WCHAR desc[256], cls[64], guid[64], mfg[128], service[64], hw[256], compat[256];
        prop(dn, CM_DRP_DEVICEDESC, desc, 256);
        prop(dn, CM_DRP_CLASS, cls, 64);
        prop(dn, CM_DRP_CLASSGUID, guid, 64);
        prop(dn, CM_DRP_MFG, mfg, 128);
        prop(dn, CM_DRP_SERVICE, service, 64);
        prop(dn, CM_DRP_HARDWAREID, hw, 256);
        prop(dn, CM_DRP_COMPATIBLEIDS, compat, 256);
        if (o->cls && _wcsicmp(o->cls, cls) && _wcsicmp(o->cls, guid)) continue;
        if (o->device_id && !in_multi(hw, o->device_id) && !in_multi(compat, o->device_id)) continue;
        printf("Instance ID:                %ls\n", id);
        printf("Device Description:         %ls\n", desc);
        if (cls[0]) printf("Class Name:                 %ls\n", cls);
        if (guid[0]) printf("Class GUID:                 %ls\n", guid);
        if (mfg[0]) printf("Manufacturer Name:          %ls\n", mfg);
        if (!here) printf("Status:                     Disconnected\n");
        else if (problem) {
            printf("Status:                     Problem\n");
            printf("Problem Code:               %lu (0x%lX) [%s]\n", (unsigned long)problem, (unsigned long)problem, problem_name(problem));
        } else printf("Status:                     %s\n", status & 0x8 /* DN_STARTED */ ? "Started" : "Stopped");
        if (service[0]) printf("Driver Name:                %ls (a NovaOS driver)\n", service);
        if (o->ids) {
            if (hw[0]) print_list("Hardware IDs:", hw);
            if (compat[0]) print_list("Compatible IDs:", compat);
        }
        if (o->drivers) matching_drivers(hw, compat, service);
        printf("\n");
        n++;
    }
    free(ids);
    if (!n) printf("No devices were found on the system.\n");
    return 0;
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
    if (is(op, "/enum-devices")) {
        static WCHAR cls[64], inst[200], devid[200];
        EnumOpts o = { 0 };
        for (int i = 2; i < argc; i++) {
            const char *a = argv[i];
            WCHAR *into = NULL;
            if (is(a, "/connected")) o.present = 1;
            else if (is(a, "/disconnected")) o.present = 2;
            else if (is(a, "/ids")) o.ids = TRUE;
            else if (is(a, "/drivers")) o.drivers = TRUE;
            else if (is(a, "/class")) { into = cls; o.cls = cls; }
            else if (is(a, "/instanceid")) { into = inst; o.instance = inst; }
            else if (is(a, "/deviceid")) { into = devid; o.device_id = devid; }
            else return usage();
            if (into) {
                if (++i >= argc) return usage();
                MultiByteToWideChar(CP_ACP, 0, argv[i], -1, into, into == cls ? 64 : 200);
            }
        }
        return enum_devices(&o);
    }
    return usage();
}
