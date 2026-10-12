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
 *   pnputil /scan-devices [/instanceid <id>] [/search-online [/source <url>]]
 *                          rescan the devices; /search-online (NovaOS's):
 *                          then download a driver package by hardware ID
 *                          from the Microsoft Update Catalog for each device
 *                          with no driver (or the one named) and stage it in
 *                          the driver store (/source: another server that
 *                          answers as the Catalog does)
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
#include <winhttp.h>
#include <fdi.h>

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
    if (e == 0xE000024B) {                                  /* ERROR_FILE_HASH_NOT_IN_CATALOG */
        printf("The hash for the file is not present in the specified catalog file. "
               "The file is likely corrupt or the victim of tampering.\n");
        return;
    }
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
            if (db_get(sub, L"SourceName", v, 256)) {
                printf("Source:             %ls", v);
                if (db_get(sub, L"SourceUpdateId", v, 256)) printf(" (update %ls)", v);
                printf("\n");
            }
            v[0] = 0;
            db_get(sub, L"SignerName", v, 256);
            if (v[0]) printf("Signer Name:        %ls\n\n", v);
            else printf("Signer Name:        (none: the package is not signed)\n\n");
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
    BOOL amd64;                           /* only the models sections decorated for x64 */
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
    if (!m->amd64) section_lines(m->inf, models, model_line, m);
    while (f) {                           /* (each decorated section: Models.NTamd64, ...) */
        WCHAR dec[64];
        WCHAR *next = wcschr(f + 1, ',');
        size_t k = next ? (size_t)(next - f - 1) : wcslen(f + 1);
        if (k > 63) k = 63;
        memcpy(dec, f + 1, k * sizeof(WCHAR));
        dec[k] = 0;
        trim(dec);
        f = next;
        if (m->amd64 && !wcsstr(_wcslwr(dec), L"amd64")) continue;
        swprintf(sec, 200, L"%ls.%ls", models, dec);
        section_lines(m->inf, sec, model_line, m);
    }
}

/* Whether the INF at @path has a model for one of the device's IDs (@amd64:
 * in a models section decorated for x64), and the best one in @m */
static BOOL rank_inf(const WCHAR *path, const WCHAR *hwids, const WCHAR *compat, BOOL amd64, Match *m)
{
    memset(m, 0, sizeof(*m));
    m->inf = read_inf(path);
    m->hwids = hwids;
    m->compat = compat;
    m->rank = ~0u;
    m->amd64 = amd64;
    if (!m->inf) return FALSE;
    section_lines(m->inf, L"Manufacturer", manufacturer_line, m);
    free((void *)m->inf);
    m->inf = NULL;
    return m->rank != ~0u;
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
        Match m;
        if (!rank_inf(path, hwids, compat, FALSE, &m)) continue;
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

/* -----------------------------------------------------------------------
 * /scan-devices /search-online: drivers by hardware ID from the Microsoft
 * Update Catalog
 *
 * The Catalog (https://www.catalog.update.microsoft.com) serves the driver
 * packages Windows Update offers, found by hardware ID: Search.aspx?q=ID
 * lists the updates (an HTML table: title, products, classification, date,
 * version, size), DownloadDialog.aspx (POST updateIDs=[{...}]) names each
 * update's files with their address, SHA-1 and SHA-256, and the file is a
 * cabinet holding the package.  For each device with problem code 28 (no
 * driver), the device's hardware IDs and then its compatible IDs are
 * searched for, most specific first; the driver updates are ranked
 * (Windows 11 client before Windows 10, client before server, no ARM64,
 * newest first), and the first whose cabinet has the SHA-256 the Catalog
 * gives and an INF with an x64 model for one of the device's IDs is staged
 * in the driver store and published as oemN.inf.  The cabinet stays in
 * %WINDIR%\SoftwareDistribution\Download\Drivers\<update ID>, and the
 * package's DriverDatabase key records where it came from (SourceName,
 * SourceUpdateId, SourceTitle, SourceUrl, SourceSha256, SourceSha1,
 * SourceCabinet, ...) for the signature checks to come.  Nothing is
 * downloaded unless the user asks with /search-online; /source names
 * another server that answers as the Catalog does (the self-tests' recorded
 * Catalog, tools/drivertest/catalog_fixture.py).
 * ----------------------------------------------------------------------- */
#define CATALOG L"https://www.catalog.update.microsoft.com"
#define MAX_UPDATES 64
#define MAX_TRIES 3

__declspec(dllimport) LONG WINAPI BCryptOpenAlgorithmProvider(void **alg, LPCWSTR id, LPCWSTR impl, ULONG flags);
__declspec(dllimport) LONG WINAPI BCryptCloseAlgorithmProvider(void *alg, ULONG flags);
__declspec(dllimport) LONG WINAPI BCryptCreateHash(void *alg, void **h, PUCHAR obj, ULONG objlen, PUCHAR secret,
                                                   ULONG slen, ULONG flags);
__declspec(dllimport) LONG WINAPI BCryptHashData(void *h, PUCHAR data, ULONG n, ULONG flags);
__declspec(dllimport) LONG WINAPI BCryptFinishHash(void *h, PUCHAR out, ULONG n, ULONG flags);
__declspec(dllimport) LONG WINAPI BCryptDestroyHash(void *h);

typedef struct { char *data; size_t len, cap; } Buf;

typedef struct {
    void *alg[2], *h[2];                  /* SHA-256, SHA-1 */
    BYTE sha256[32], sha1[20];
} Hashes;

typedef struct {
    char id[64], title[256], products[512], cls[128], version[64];
    int date;                             /* yyyymmdd */
    int score;                            /* the products' rank, -1 for not a driver for this PC */
} Update;

static void buf_add(Buf *b, const void *p, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = (b->len + n + 1) * 2;
        char *d = realloc(b->data, cap);
        if (!d) return;
        b->data = d;
        b->cap = cap;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void hashes_open(Hashes *x)
{
    static const WCHAR *const algs[2] = { L"SHA256", L"SHA1" };
    memset(x, 0, sizeof(*x));
    for (int i = 0; i < 2; i++)
        if (!BCryptOpenAlgorithmProvider(&x->alg[i], algs[i], NULL, 0)) BCryptCreateHash(x->alg[i], &x->h[i], NULL, 0, NULL, 0, 0);
}

static void hashes_close(Hashes *x)
{
    if (x->h[0]) BCryptFinishHash(x->h[0], x->sha256, 32, 0);
    if (x->h[1]) BCryptFinishHash(x->h[1], x->sha1, 20, 0);
    for (int i = 0; i < 2; i++) {
        if (x->h[i]) BCryptDestroyHash(x->h[i]);
        if (x->alg[i]) BCryptCloseAlgorithmProvider(x->alg[i], 0);
    }
}

static void base64(const BYTE *p, int n, char *out)
{
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        unsigned v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        out[o++] = abc[v >> 18 & 63];
        out[o++] = abc[v >> 12 & 63];
        out[o++] = i + 1 < n ? abc[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? abc[v & 63] : '=';
    }
    out[o] = 0;
}

static void hex(const BYTE *p, int n, char *out)
{
    for (int i = 0; i < n; i++) sprintf(out + i * 2, "%02x", p[i]);
}

/* GET @url (POST @body as a form when there is one): the response into @into,
 * or written to @file and hashed into @hash; FALSE with the reason in @why */
static BOOL http(const WCHAR *url, const char *body, Buf *into, HANDLE file, Hashes *hash, ULONGLONG *size,
                 char *why, size_t why_cap)
{
    WCHAR host[256], path[2048];
    URL_COMPONENTS uc = { sizeof(uc) };
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) { snprintf(why, why_cap, "not a web address: %ls", url); return FALSE; }
    BOOL ok = FALSE;
    DWORD status = 0, n = sizeof(status);
    HINTERNET s = WinHttpOpen(L"NovaOS-pnputil/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, body ? L"POST" : L"GET", path, NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    DWORD blen = body ? (DWORD)strlen(body) : 0;
    if (!r || !WinHttpSendRequest(r, body ? L"Content-Type: application/x-www-form-urlencoded\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS,
                                  (DWORD)-1, (void *)body, blen, blen, 0) || !WinHttpReceiveResponse(r, NULL)) {
        snprintf(why, why_cap, "%ls did not answer (error %lu)", host, (unsigned long)GetLastError());
        goto out;
    }
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &n, NULL);
    if (status != 200) { snprintf(why, why_cap, "%ls answered HTTP %lu", host, (unsigned long)status); goto out; }
    static BYTE chunk[65536];
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(r, chunk, sizeof(chunk), &got)) {
            snprintf(why, why_cap, "the download from %ls broke off (error %lu)", host, (unsigned long)GetLastError());
            goto out;
        }
        if (!got) break;
        if (into) buf_add(into, chunk, got);
        if (file) {
            DWORD put = 0;
            if (!WriteFile(file, chunk, got, &put, NULL) || put != got) {
                snprintf(why, why_cap, "could not write the download (error %lu)", (unsigned long)GetLastError());
                goto out;
            }
        }
        for (int i = 0; hash && i < 2; i++) if (hash->h[i]) BCryptHashData(hash->h[i], chunk, got, 0);
        if (size) *size += got;
    }
    ok = TRUE;
out:
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return ok;
}

static void url_encode(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 4 < cap; in++) {
        unsigned char ch = (unsigned char)*in;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || strchr("-_.~", ch))
            out[o++] = (char)ch;
        else o += (size_t)sprintf(out + o, "%%%02X", ch);
    }
    out[o] = 0;
}

static const char *stristr(const char *h, const char *n)
{
    size_t k = strlen(n);
    for (; *h; h++) if (!_strnicmp(h, n, k)) return h;
    return NULL;
}

/* The text of the HTML between @a and @e: tags taken out, entities decoded,
 * white space folded */
static void text_of(const char *a, const char *e, char *out, size_t cap)
{
    size_t o = 0;
    BOOL space = FALSE;
    while (a < e && o + 1 < cap) {
        char ch = *a;
        if (ch == '<') { while (a < e && *a != '>') a++; a++; space = TRUE; continue; }
        if (ch == '&') {
            static const struct { const char *name; char ch; } ent[] = {
                { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&#39;", '\'' }, { "&nbsp;", ' ' } };
            int k = 0;
            for (; k < 6; k++) if (!strncmp(a, ent[k].name, strlen(ent[k].name))) break;
            if (k < 6) { a += strlen(ent[k].name); ch = ent[k].ch; } else a++;
        } else a++;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { space = TRUE; continue; }
        if (space && o) out[o++] = ' ';
        space = FALSE;
        if (o + 1 < cap) out[o++] = ch;
    }
    out[o] = 0;
}

/* The Catalog's "products" rank for a 64-bit Windows 11 PC, or -1 */
static int products_score(const Update *u)
{
    if (_strnicmp(u->cls, "Drivers", 7) || stristr(u->products, "ARM64") || stristr(u->title, "ARM64")) return -1;
    for (const char *p = u->products; (p = stristr(p, "Windows 11 Client")); p++)
        if (strncmp(p + 17, " S,", 3) && strncmp(p + 17, " S ", 3)) return 4;
    if (stristr(u->products, "Windows 10") || stristr(u->products, "Windows - Client")) return 3;
    if (stristr(u->products, "Windows 11")) return 2;
    return 1;                             /* (servers: Windows Server's drivers run on the client too) */
}

/* The results table of a Search.aspx page */
static int parse_results(const char *page, Update *out, int max)
{
    int n = 0;
    for (const char *p = page; n < max && (p = strstr(p, "<tr id=\""));) {
        p += 8;
        const char *q = strchr(p, '"'), *end = strstr(p, "</tr>");
        if (!q || !end) break;
        const char *r = strstr(p, "_R");
        if (!r || r > q || q - p > 60 || r - p < 30) continue;          /* (the header row) */
        Update *u = &out[n];
        memset(u, 0, sizeof(*u));
        memcpy(u->id, p, (size_t)(r - p));
        char cells[7][512];
        int c = 0;
        for (const char *t = p; c < 7 && (t = strstr(t, "<td")) && t < end; c++) {
            const char *a = strchr(t, '>'), *e = a ? strstr(a, "</td>") : NULL;
            if (!e || e > end) break;
            text_of(a + 1, e, cells[c], sizeof(cells[c]));
            t = e;
        }
        p = end;
        if (c < 6) continue;
        snprintf(u->title, sizeof(u->title), "%s", cells[1]);
        snprintf(u->products, sizeof(u->products), "%s", cells[2]);
        snprintf(u->cls, sizeof(u->cls), "%s", cells[3]);
        snprintf(u->version, sizeof(u->version), "%s", cells[5]);
        int mo = 0, d = 0, y = 0;
        if (sscanf(cells[4], "%d/%d/%d", &mo, &d, &y) == 3) u->date = y * 10000 + mo * 100 + d;
        u->score = products_score(u);
        n++;
    }
    return n;
}

static int by_rank(const void *a, const void *b)
{
    const Update *x = a, *y = b;
    if (x->score != y->score) return y->score - x->score;
    return y->date - x->date;
}

/* A value the download dialog's script sets: KEY = 'value'; */
static BOOL js_value(const char *page, const char *key, char *out, size_t cap)
{
    const char *p = strstr(page, key);
    if (!p) return FALSE;
    p += strlen(key);
    while (*p == ' ' || *p == '=') p++;
    if (*p != '\'') return FALSE;
    const char *e = strchr(++p, '\'');
    if (!e || (size_t)(e - p) >= cap) return FALSE;
    memcpy(out, p, (size_t)(e - p));
    out[e - p] = 0;
    return TRUE;
}

/* ---- cabinet.dll's FDI, extracting into a folder ---- */
static FNALLOC(fdi_alloc) { return malloc(cb); }
static FNFREE(fdi_free) { free(pv); }
static FNOPEN(fdi_open)
{
    (void)pmode;
    HANDLE h = CreateFileA(pszFile, (oflag & 3) ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ, NULL,
                           (oflag & 0x0100) ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return h == INVALID_HANDLE_VALUE ? -1 : (INT_PTR)h;
}
static FNREAD(fdi_read) { DWORD got = 0; return ReadFile((HANDLE)hf, pv, cb, &got, NULL) ? got : (UINT)-1; }
static FNWRITE(fdi_write) { DWORD put = 0; return WriteFile((HANDLE)hf, pv, cb, &put, NULL) ? put : (UINT)-1; }
static FNCLOSE(fdi_close) { CloseHandle((HANDLE)hf); return 0; }
static FNSEEK(fdi_seek)
{
    DWORD r = SetFilePointer((HANDLE)hf, dist, NULL, seektype);
    return r == INVALID_SET_FILE_POINTER ? -1 : (long)r;
}

static FNFDINOTIFY(fdi_notify)
{
    const char *dir = pfdin->pv;
    if (fdint == fdintCOPY_FILE) {
        char path[MAX_PATH];
        if (strstr(pfdin->psz1, "..") || pfdin->psz1[0] == '\\' || strchr(pfdin->psz1, ':')) return 0;   /* (not out of the folder) */
        snprintf(path, sizeof(path), "%s%s", dir, pfdin->psz1);
        for (char *s = path + strlen(dir); *s; s++)
            if (*s == '\\' || *s == '/') { *s = 0; CreateDirectoryA(path, NULL); *s = '\\'; }
        return fdi_open(path, 0x8000 | 0x0100 | 0x0001 | 0x0200, 0x0180);
    }
    if (fdint == fdintCLOSE_FILE_INFO) { fdi_close(pfdin->hf); return TRUE; }
    return 0;
}

static BOOL extract(const char *cab_dir, const char *cab_name, const char *into)
{
    ERF erf;
    HFDI h = FDICreate(fdi_alloc, fdi_free, fdi_open, fdi_read, fdi_write, fdi_close, fdi_seek, cpuUNKNOWN, &erf);
    if (!h) return FALSE;
    BOOL ok = FDICopy(h, (char *)cab_name, (char *)cab_dir, 0, fdi_notify, NULL, (void *)into);
    FDIDestroy(h);
    return ok;
}

static void mkdirs(const WCHAR *path)
{
    WCHAR p[MAX_PATH];
    lstrcpynW(p, path, MAX_PATH);
    for (WCHAR *s = p + 3; *s; s++)
        if (*s == '\\') { *s = 0; CreateDirectoryW(p, NULL); *s = '\\'; }
    CreateDirectoryW(p, NULL);
}

/* The best INF in the extracted package (and its subfolders) for the device */
static BOOL find_inf(const WCHAR *dir, const WCHAR *hw, const WCHAR *compat, WCHAR *best, Match *bm, int depth)
{
    WCHAR pat[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    swprintf(pat, MAX_PATH, L"%ls*", dir);
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return bm->rank != ~0u;
    do {
        if (fd.cFileName[0] == '.') continue;
        swprintf(path, MAX_PATH, L"%ls%ls", dir, fd.cFileName);
        size_t n = wcslen(fd.cFileName);
        Match m;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 2) { lstrcatW(path, L"\\"); find_inf(path, hw, compat, best, bm, depth + 1); }
        } else if (n > 4 && !_wcsicmp(fd.cFileName + n - 4, L".inf") && rank_inf(path, hw, compat, TRUE, &m) &&
                   m.rank < bm->rank) {
            *bm = m;
            lstrcpynW(best, path, MAX_PATH);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return bm->rank != ~0u;
}

static void db_set(const WCHAR *sub, const WCHAR *name, const WCHAR *value)
{
    WCHAR k[MAX_PATH];
    HKEY h;
    swprintf(k, MAX_PATH, L"%ls\\%ls", DB_KEY, sub);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, k, 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL)) return;
    RegSetValueExW(h, name, 0, REG_SZ, (const BYTE *)value, (DWORD)(wcslen(value) + 1) * sizeof(WCHAR));
    RegCloseKey(h);
}

static void db_set_a(const WCHAR *sub, const WCHAR *name, const char *value)
{
    WCHAR w[2048];
    MultiByteToWideChar(CP_UTF8, 0, value, -1, w, 2048);
    db_set(sub, name, w);
}

/* A published package already in the driver store for the device */
static BOOL in_store(const WCHAR *hw, const WCHAR *compat, WCHAR *name)
{
    WCHAR dir[MAX_PATH], pat[MAX_PATH], path[MAX_PATH];
    GetWindowsDirectoryW(dir, MAX_PATH);
    lstrcatW(dir, L"\\INF\\");
    swprintf(pat, MAX_PATH, L"%lsoem*.inf", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    BOOL found = FALSE;
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    do {
        Match m;
        swprintf(path, MAX_PATH, L"%ls%ls", dir, fd.cFileName);
        if (rank_inf(path, hw, compat, TRUE, &m)) { lstrcpynW(name, fd.cFileName, MAX_PATH); found = TRUE; }
    } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

/* Download update @u, check it, and stage its package if it has one for the
 * device: TRUE once a package is published */
static BOOL try_update(const WCHAR *source, const Update *u, const WCHAR *hw, const WCHAR *compat)
{
    char why[512], body[512], cab_url[1024], sha256[64], sha1[64], file_name[MAX_PATH];
    printf("Downloading:                %s\n", u->title);
    printf("    Update ID:              %s\n", u->id);
    printf("    Products:               %s\n", u->products);
    snprintf(body, sizeof(body), "[{\"size\":0,\"languages\":\"\",\"uidInfo\":\"%s\",\"updateID\":\"%s\"}]", u->id, u->id);
    char form[1024];
    lstrcpyA(form, "updateIDs=");
    url_encode(body, form + 10, sizeof(form) - 10);
    WCHAR url[2048];
    swprintf(url, 2048, L"%ls/DownloadDialog.aspx", source);
    Buf dialog = { 0 };
    why[0] = 0;
    if (!http(url, form, &dialog, NULL, NULL, NULL, why, sizeof(why)) || !dialog.data) {
        printf("    Not used:               the Catalog did not list its files: %s\n", why[0] ? why : "an empty answer");
        free(dialog.data);
        return FALSE;
    }
    int f = 0;
    for (; f < 16; f++) {
        char key[96];
        snprintf(key, sizeof(key), "downloadInformation[0].files[%d].fileName", f);
        if (!js_value(dialog.data, key, file_name, sizeof(file_name))) { f = 16; break; }
        size_t n = strlen(file_name);
        if (n > 4 && !_stricmp(file_name + n - 4, ".cab")) break;
    }
    char key[96];
    BOOL listed = f < 16;
    if (listed) {
        snprintf(key, sizeof(key), "downloadInformation[0].files[%d].url", f);
        listed = js_value(dialog.data, key, cab_url, sizeof(cab_url));
        snprintf(key, sizeof(key), "downloadInformation[0].files[%d].sha256", f);
        if (!js_value(dialog.data, key, sha256, sizeof(sha256))) sha256[0] = 0;
        snprintf(key, sizeof(key), "downloadInformation[0].files[%d].digest", f);
        if (!js_value(dialog.data, key, sha1, sizeof(sha1))) sha1[0] = 0;
    }
    free(dialog.data);
    if (!listed || strpbrk(file_name, "\\/:") || (!sha256[0] && !sha1[0])) {
        printf("    Not used:               the Catalog lists no driver cabinet with a hash for it\n");
        return FALSE;
    }

    WCHAR dir[MAX_PATH], cab[MAX_PATH], pkg[MAX_PATH];
    GetWindowsDirectoryW(dir, MAX_PATH);
    swprintf(dir + wcslen(dir), MAX_PATH - wcslen(dir), L"\\SoftwareDistribution\\Download\\Drivers\\%hs\\", u->id);
    mkdirs(dir);
    swprintf(cab, MAX_PATH, L"%ls%hs", dir, file_name);
    swprintf(pkg, MAX_PATH, L"%lspackage\\", dir);
    HANDLE h = CreateFileW(cab, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("    Not used:               could not create %ls (error %lu)\n", cab, (unsigned long)GetLastError());
        return FALSE;
    }
    Hashes hs;
    ULONGLONG size = 0;
    hashes_open(&hs);
    swprintf(url, 2048, L"%hs", cab_url);
    BOOL got = http(url, NULL, NULL, h, &hs, &size, why, sizeof(why));
    hashes_close(&hs);
    CloseHandle(h);
    char b256[64], b1[64], hex256[72];
    base64(hs.sha256, 32, b256);
    base64(hs.sha1, 20, b1);
    hex(hs.sha256, 32, hex256);
    if (!got) {
        printf("    Not used:               %s\n", why);
        DeleteFileW(cab);
        return FALSE;
    }
    if ((sha256[0] && strcmp(sha256, b256)) || (!sha256[0] && strcmp(sha1, b1))) {
        printf("    Not used:               its SHA-256 is not the one the Catalog gives (%s, not %s)\n", b256,
               sha256[0] ? sha256 : sha1);
        DeleteFileW(cab);
        return FALSE;
    }
    printf("    Downloaded:             %ls (%llu bytes, SHA-256 %s as the Catalog gives)\n", cab,
           (unsigned long long)size, hex256);

    char cab_dir[MAX_PATH], cab_name[MAX_PATH], into[MAX_PATH];
    mkdirs(pkg);
    WideCharToMultiByte(CP_ACP, 0, dir, -1, cab_dir, MAX_PATH, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, pkg, -1, into, MAX_PATH, NULL, NULL);
    snprintf(cab_name, sizeof(cab_name), "%s", file_name);
    if (!extract(cab_dir, cab_name, into)) {
        printf("    Not used:               the cabinet could not be expanded\n");
        return FALSE;
    }
    WCHAR inf[MAX_PATH];
    Match m = { 0 };
    m.rank = ~0u;
    if (!find_inf(pkg, hw, compat, inf, &m, 0)) {
        printf("    Not used:               no INF in it has an x64 model for this device\n");
        return FALSE;
    }
    printf("    Matching Device ID:     %ls\n", m.id);
    WCHAR dest[MAX_PATH], *comp = NULL;
    BOOL ok = SetupCopyOEMInfW(inf, NULL, SPOST_PATH, SP_COPY_NOOVERWRITE, dest, MAX_PATH, NULL, &comp);
    DWORD e = GetLastError();
    if (!ok && !(e == ERROR_FILE_EXISTS && comp)) {
        printf("    Not used:               ");
        message(e);
        return FALSE;
    }
    const WCHAR *published = comp ? comp : dest;
    WCHAR sub[MAX_PATH], folder[MAX_PATH], w[2048];
    swprintf(sub, MAX_PATH, L"DriverInfFiles\\%ls", published);
    if (db_get(sub, L"", folder, MAX_PATH)) {
        swprintf(sub, MAX_PATH, L"DriverPackages\\%ls", folder);
        db_set(sub, L"SourceName", L"Microsoft Update Catalog");
        db_set(sub, L"SourceServer", source);
        db_set_a(sub, L"SourceUpdateId", u->id);
        db_set_a(sub, L"SourceTitle", u->title);
        db_set_a(sub, L"SourceProducts", u->products);
        db_set_a(sub, L"SourceVersion", u->version);
        swprintf(w, 64, L"%04d-%02d-%02d", u->date / 10000, u->date / 100 % 100, u->date % 100);
        db_set(sub, L"SourceDate", w);
        db_set_a(sub, L"SourceUrl", cab_url);
        db_set_a(sub, L"SourceSha256", hex256);
        db_set_a(sub, L"SourceSha1", b1);
        db_set(sub, L"SourceCabinet", cab);
        db_set(sub, L"SourceHardwareId", m.id);
    }
    printf("Driver package added successfully.\n");
    printf("Published Name:             %ls\n", published);
    printf("Driver Status:              Staged (NovaOS does not start Windows drivers on devices yet)\n");
    return TRUE;
}

static BOOL g_unreachable;                /* a search did not reach the server */

/* Search the Catalog for the device's IDs, most specific first */
static BOOL search_device(const WCHAR *source, const WCHAR *hw, const WCHAR *compat)
{
    static Update ups[MAX_UPDATES];
    for (int list = 0; list < 2; list++) {
        for (const WCHAR *id = list ? compat : hw; *id; id += wcslen(id) + 1) {
            char a[400], q[1200], why[512];
            WideCharToMultiByte(CP_UTF8, 0, id, -1, a, sizeof(a), NULL, NULL);
            url_encode(a, q, sizeof(q));
            WCHAR url[2048];
            swprintf(url, 2048, L"%ls/Search.aspx?q=%hs", source, q);
            Buf page = { 0 };
            why[0] = 0;
            if (!http(url, NULL, &page, NULL, NULL, NULL, why, sizeof(why)) || !page.data) {
                printf("Searching for:              %s (failed: %s)\n", a, why[0] ? why : "an empty answer");
                free(page.data);
                g_unreachable = TRUE;
                return FALSE;
            }
            int n = parse_results(page.data, ups, MAX_UPDATES), usable = 0;
            free(page.data);
            for (int i = 0; i < n; i++) if (ups[i].score >= 0) usable++;
            printf("Searching for:              %s (%d update%s, %d for this PC)\n", a, n, n == 1 ? "" : "s", usable);
            if (!usable) continue;
            qsort(ups, (size_t)n, sizeof(Update), by_rank);
            for (int i = 0; i < n && i < MAX_TRIES && ups[i].score >= 0; i++)
                if (try_update(source, &ups[i], hw, compat)) return TRUE;
        }
    }
    return FALSE;
}

static int scan_devices(const WCHAR *instance, BOOL online, const WCHAR *source)
{
    printf("Scanning for device hardware changes...\nScan complete.\n");
    if (!online) return 0;
    printf("\nSearching for drivers online: %ls\n\n", source);
    ULONG len = 0;
    CM_Get_Device_ID_List_SizeW(&len, NULL, 0);
    WCHAR *ids = malloc((len + 1) * sizeof(WCHAR));
    if (!ids || CM_Get_Device_ID_ListW(NULL, ids, len + 1, 0)) { printf("Failed to enumerate devices.\n"); return 1; }
    int searched = 0, found = 0;
    for (WCHAR *id = ids; *id; id += wcslen(id) + 1) {
        DWORD dn;
        ULONG status = 0, problem = 0;
        if (instance && _wcsicmp(instance, id)) continue;
        if (CM_Locate_DevNodeW(&dn, id, 0) || CM_Get_DevNode_Status(&status, &problem, dn, 0)) continue;
        if (problem != 28 && !instance) continue;
        WCHAR desc[256], hw[1024], compat[1024], have[MAX_PATH];
        prop(dn, CM_DRP_DEVICEDESC, desc, 256);
        prop(dn, CM_DRP_HARDWAREID, hw, 1024);
        prop(dn, CM_DRP_COMPATIBLEIDS, compat, 1024);
        printf("Instance ID:                %ls\n", id);
        printf("Device Description:         %ls\n", desc);
        searched++;
        if (!hw[0] && !compat[0]) printf("Not searched:               the device has no hardware IDs\n");
        else if (in_store(hw, compat, have)) {
            printf("Driver package already in the driver store: %ls\n", have);
            found++;
        } else if (search_device(source, hw, compat)) found++;
        else printf("No driver was found for this device.\n");
        printf("\n");
    }
    free(ids);
    if (!searched) printf("%s\n", instance ? "No such device is on the system." : "No device is missing a driver.");
    else printf("Devices searched:           %d\nDrivers found:              %d\n", searched, found);
    return g_unreachable ? 1 : 0;
}

static BOOL is(const char *a, const char *b) { return !_stricmp(a, b); }

static int usage(void)
{
    printf("PNPUTIL [/add-driver <filename.inf | *.inf> [/subdirs] [/install]]\n"
           "        [/delete-driver <oem#.inf> [/uninstall] [/force]]\n"
           "        [/enum-drivers]\n"
           "        [/enum-devices [/connected | /disconnected] [/class <name>] [/ids] [/drivers]]\n"
           "        [/scan-devices [/instanceid <id>] [/search-online [/source <url>]]]\n");
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
    if (is(op, "/scan-devices")) {
        static WCHAR inst[200], source[512] = CATALOG;
        BOOL online = FALSE, one = FALSE;
        for (int i = 2; i < argc; i++) {
            if (is(argv[i], "/search-online")) online = TRUE;
            else if ((is(argv[i], "/instanceid") || is(argv[i], "/source")) && i + 1 < argc) {
                BOOL src = is(argv[i], "/source");
                MultiByteToWideChar(CP_ACP, 0, argv[++i], -1, src ? source : inst, src ? 512 : 200);
                if (!src) one = TRUE;
            } else if (is(argv[i], "/async")) ;
            else return usage();
        }
        size_t n = wcslen(source);
        while (n && source[n - 1] == '/') source[--n] = 0;
        return scan_devices(one ? inst : NULL, online, source);
    }
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
