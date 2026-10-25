/*
 * package.c — packaged apps (MSIX and AppX): which package a process
 * belongs to, where an installed package lives, the families' members, and
 * the app policies a packaged process runs under.
 *
 * Packages are installed (userland/programs/appx.c) for the user, into
 * C:\Programs\WindowsApps\<package full name>, and recorded where Windows'
 * deployment records them, under HKEY_CURRENT_USER\REPO_KEY:
 *   Packages\<full name>      PackageRootFolder, DisplayName,
 *                             PublisherDisplayName
 *     Applications\<app id>   Executable (in the package), EntryPoint,
 *                             RuntimeBehavior ("windowsApp" for a UWP app,
 *                             "packagedClassicApp" for a desktop one),
 *                             DisplayName
 *   Families\<family name>\<full name>
 * A process has the package whose folder holds its program (Windows gives
 * a package's processes their identity when it activates them; NovaOS's
 * activation, ApplicationActivationManager in twinapi.appcore.dll, starts
 * that program), and is the application whose Executable it is.
 */
#define NOVA_BUILD_KERNEL32
#include <winternl.h>
#include <winreg.h>
#include "k32.h"

#define K32 __declspec(dllexport)
#define APPMODEL_ERROR_NO_PACKAGE     15700
#define APPMODEL_ERROR_NO_APPLICATION 15703
#define REPO_KEY L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository"

void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
BOOL WINAPI QueryFullProcessImageNameW(HANDLE p, DWORD flags, LPWSTR buf, PDWORD n);

static int wlen(const WCHAR *s) { int n = 0; if (s) while (s[n]) n++; return n; }
static WCHAR lower(WCHAR c) { return c >= 'A' && c <= 'Z' ? (WCHAR)(c + 32) : c == '/' ? '\\' : c; }

/* HKCU\REPO_KEY\@sub (opened for reading), or 0 */
static HKEY repo(const WCHAR *sub)
{
    WCHAR path[400];
    int n = 0;
    for (const WCHAR *s = REPO_KEY; *s && n < 380; s++) path[n++] = *s;
    if (sub && *sub) { path[n++] = '\\'; for (; *sub && n < 398; sub++) path[n++] = *sub; }
    path[n] = 0;
    HKEY k;
    return RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &k) ? 0 : k;
}

/* the string value @name of @k\@sub into @out (@cap characters) */
static BOOL reg_str(HKEY k, const WCHAR *sub, const WCHAR *name, WCHAR *out, DWORD cap)
{
    DWORD size = cap * sizeof(WCHAR);
    return !RegGetValueW(k, sub, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, 0, out, &size);
}

/* @a starts with the folder @dir (followed by a backslash), ignoring case */
static int in_folder(const WCHAR *a, const WCHAR *dir)
{
    int n = wlen(dir);
    if (!n) return 0;
    for (int i = 0; i < n; i++) if (lower(a[i]) != lower(dir[i])) return 0;
    return a[n] == '\\' || dir[n - 1] == '\\';
}

static int same_path(const WCHAR *a, const WCHAR *b)
{
    for (; *a && lower(*a) == lower(*b); a++, b++) { }
    return !*a && !*b;
}

/* The installed package whose folder holds @image: its full name and the
 * id of the application @image is (the package's first if none names it) */
static BOOL package_of(const WCHAR *image, WCHAR full[128], WCHAR app[64])
{
    HKEY pk = repo(L"Packages");
    if (!pk) return FALSE;
    BOOL found = FALSE;
    for (DWORD i = 0; !found; i++) {
        DWORD n = 128;
        if (RegEnumKeyExW(pk, i, full, &n, 0, 0, 0, 0)) break;
        WCHAR root[MAX_PATH];
        if (!reg_str(pk, full, L"PackageRootFolder", root, MAX_PATH) || !in_folder(image, root)) continue;
        found = TRUE;
        app[0] = 0;
        HKEY ak;
        WCHAR sub[160];
        int s = 0;
        for (const WCHAR *p = full; *p; p++) sub[s++] = *p;
        for (const WCHAR *p = L"\\Applications"; *p; p++) sub[s++] = *p;
        sub[s] = 0;
        if (RegOpenKeyExW(pk, sub, 0, KEY_READ, &ak)) break;
        for (DWORD j = 0;; j++) {
            WCHAR id[64], exe[MAX_PATH], path[2 * MAX_PATH];
            DWORD m = 64;
            if (RegEnumKeyExW(ak, j, id, &m, 0, 0, 0, 0)) break;
            if (!app[0]) memcpy(app, id, (m + 1) * sizeof(WCHAR));
            if (!reg_str(ak, id, L"Executable", exe, MAX_PATH)) continue;
            int r = wlen(root), e = wlen(exe);
            memcpy(path, root, r * sizeof(WCHAR));
            if (r && root[r - 1] != '\\') path[r++] = '\\';
            memcpy(path + r, exe, (e + 1) * sizeof(WCHAR));
            if (same_path(path, image)) { memcpy(app, id, (m + 1) * sizeof(WCHAR)); break; }
        }
        RegCloseKey(ak);
    }
    RegCloseKey(pk);
    return found;
}

/* this process's package, looked up once */
static struct { LONG state; WCHAR full[128], app[64]; } g_me;   /* state: 0 not yet, 1 none, 2 packaged */
static BOOL current(void)
{
    if (!g_me.state) {
        WCHAR image[MAX_PATH];
        DWORD n = GetModuleFileNameW(0, image, MAX_PATH);
        g_me.state = n && n < MAX_PATH && package_of(image, g_me.full, g_me.app) ? 2 : 1;
    }
    return g_me.state == 2;
}

/* @p's package: this process's, or another's by its program */
static BOOL process_package(HANDLE p, WCHAR full[128], WCHAR app[64])
{
    if (p == GetCurrentProcess() || GetProcessId(p) == GetCurrentProcessId()) {
        if (!current()) return FALSE;
        memcpy(full, g_me.full, sizeof g_me.full);
        memcpy(app, g_me.app, sizeof g_me.app);
        return TRUE;
    }
    WCHAR image[MAX_PATH];
    DWORD n = MAX_PATH;
    return QueryFullProcessImageNameW(p, 0, image, &n) && package_of(image, full, app);
}

/* @s into @out (*@len characters, the terminator included), as the
 * package functions return strings */
static LONG give(const WCHAR *s, UINT32 *len, PWSTR out)
{
    if (!len) return ERROR_INVALID_PARAMETER;
    UINT32 need = (UINT32)wlen(s) + 1;
    if (!out || *len < need) { *len = need; return ERROR_INSUFFICIENT_BUFFER; }
    memcpy(out, s, need * sizeof(WCHAR));
    *len = need;
    return ERROR_SUCCESS;
}

K32 LONG WINAPI PackageFamilyNameFromFullName(PCWSTR full, UINT32 *len, PWSTR out);
K32 LONG WINAPI PackageIdFromFullName(PCWSTR full, UINT32 flags, UINT32 *len, BYTE *buf);

/* family!app */
static LONG give_aumid(const WCHAR *full, const WCHAR *app, UINT32 *len, PWSTR out)
{
    WCHAR id[200];
    UINT32 n = 128;
    if (!app[0] || PackageFamilyNameFromFullName(full, &n, id)) return APPMODEL_ERROR_NO_APPLICATION;
    int at = wlen(id), a = wlen(app);
    if (at + 1 + a >= 200) return APPMODEL_ERROR_NO_APPLICATION;
    id[at++] = '!';
    memcpy(id + at, app, (a + 1) * sizeof(WCHAR));
    return give(id, len, out);
}

K32 LONG WINAPI GetCurrentPackageFullName(UINT32 *len, PWSTR name)
{
    if (!current()) { if (len) *len = 0; return APPMODEL_ERROR_NO_PACKAGE; }
    return give(g_me.full, len, name);
}
K32 LONG WINAPI GetCurrentPackageFamilyName(UINT32 *len, PWSTR name)
{
    if (!current()) { if (len) *len = 0; return APPMODEL_ERROR_NO_PACKAGE; }
    return PackageFamilyNameFromFullName(g_me.full, len, name);
}
K32 LONG WINAPI GetCurrentPackageId(UINT32 *len, BYTE *buf)
{
    if (!current()) { if (len) *len = 0; return APPMODEL_ERROR_NO_PACKAGE; }
    return PackageIdFromFullName(g_me.full, 0, len, buf);
}
K32 LONG WINAPI GetPackagePathByFullName(PCWSTR full, UINT32 *len, PWSTR path)
{
    if (!full || !len) return ERROR_INVALID_PARAMETER;
    HKEY pk = repo(L"Packages");
    WCHAR root[MAX_PATH];
    BOOL ok = pk && reg_str(pk, full, L"PackageRootFolder", root, MAX_PATH);
    if (pk) RegCloseKey(pk);
    return ok ? give(root, len, path) : ERROR_NOT_FOUND;
}
K32 LONG WINAPI GetCurrentPackagePath(UINT32 *len, PWSTR path)
{
    if (!current()) { if (len) *len = 0; return APPMODEL_ERROR_NO_PACKAGE; }
    return GetPackagePathByFullName(g_me.full, len, path);
}
K32 LONG WINAPI GetStagedPackagePathByFullName(PCWSTR full, UINT32 *len, PWSTR path) { return GetPackagePathByFullName(full, len, path); }
K32 LONG WINAPI GetCurrentApplicationUserModelId(UINT32 *len, PWSTR id)
{
    if (!current()) { if (len) *len = 0; return APPMODEL_ERROR_NO_APPLICATION; }
    return give_aumid(g_me.full, g_me.app, len, id);
}
K32 LONG WINAPI GetApplicationUserModelId(HANDLE p, UINT32 *len, PWSTR id)
{
    WCHAR full[128], app[64];
    if (!process_package(p, full, app)) { if (len) *len = 0; return APPMODEL_ERROR_NO_APPLICATION; }
    return give_aumid(full, app, len, id);
}
K32 LONG WINAPI GetPackageFullName(HANDLE p, UINT32 *len, PWSTR name)
{
    WCHAR full[128], app[64];
    if (!process_package(p, full, app)) { if (len) *len = 0; return APPMODEL_ERROR_NO_PACKAGE; }
    return give(full, len, name);
}
K32 LONG WINAPI GetPackageFamilyName(HANDLE p, UINT32 *len, PWSTR name)
{
    WCHAR full[128], app[64];
    if (!process_package(p, full, app)) { if (len) *len = 0; return APPMODEL_ERROR_NO_PACKAGE; }
    return PackageFamilyNameFromFullName(full, len, name);
}

/* The installed members of @family: *@count full names, pointers to them in
 * @names, the strings in @buf (*@len characters) */
static LONG family_members(PCWSTR family, UINT32 *count, PWSTR *names, UINT32 *len, WCHAR *buf, UINT32 *props)
{
    if (!family || !count || !len) return ERROR_INVALID_PARAMETER;
    WCHAR sub[200];
    int n = 0;
    for (const WCHAR *p = L"Families\\"; *p; p++) sub[n++] = *p;
    for (; *family && n < 198; family++) sub[n++] = *family == '\\' ? '_' : *family;
    sub[n] = 0;
    HKEY fk = repo(sub);
    UINT32 c = 0, chars = 0;
    WCHAR full[128];
    for (DWORD i = 0; fk; i++) {
        DWORD m = 128;
        if (RegEnumKeyExW(fk, i, full, &m, 0, 0, 0, 0)) break;
        c++;
        chars += m + 1;
    }
    LONG r = ERROR_SUCCESS;
    if (c && (!names || !buf || *count < c || *len < chars)) r = ERROR_INSUFFICIENT_BUFFER;
    else {
        UINT32 at = 0;
        for (DWORD i = 0; i < c; i++) {
            DWORD m = 128;
            if (RegEnumKeyExW(fk, i, full, &m, 0, 0, 0, 0)) break;
            names[i] = buf + at;
            memcpy(buf + at, full, (m + 1) * sizeof(WCHAR));
            at += m + 1;
            if (props) props[i] = 0;
        }
    }
    if (fk) RegCloseKey(fk);
    *count = c;
    *len = chars;
    return r;
}
K32 LONG WINAPI FindPackagesByPackageFamily(PCWSTR family, UINT32 filter, UINT32 *count, PWSTR *names,
                                            UINT32 *len, WCHAR *buf, UINT32 *props)
{
    (void)filter;
    return family_members(family, count, names, len, buf, props);
}
K32 LONG WINAPI GetPackagesByPackageFamily(PCWSTR family, UINT32 *count, PWSTR *names, UINT32 *len, WCHAR *buf)
{
    return family_members(family, count, names, len, buf, 0);
}

/* The package information API (OpenPackageInfoByFullName's references)
 * is not served: a full name is not found */
K32 LONG WINAPI OpenPackageInfoByFullName(PCWSTR full, UINT32 reserved, void **ref)
{
    (void)reserved;
    if (!full || !ref) return ERROR_INVALID_PARAMETER;
    *ref = 0;
    return ERROR_NOT_FOUND;
}
K32 LONG WINAPI GetPackageApplicationIds(void *ref, UINT32 *len, BYTE *buf, UINT32 *count)
{
    (void)buf;
    if (len) *len = 0;
    if (count) *count = 0;
    return ref ? ERROR_NOT_FOUND : ERROR_INVALID_PARAMETER;   /* no reference was ever handed out */
}
K32 LONG WINAPI ClosePackageInfo(void *ref) { return ref ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER; }


/* A package full name is Name_Version_Architecture_ResourceId_PublisherId
 * (the resource id may be empty); its family name is Name_PublisherId */
static int package_parts(PCWSTR full, PCWSTR part[5], int len[5])
{
    int n = 0;
    PCWSTR s = full;
    if (!full) return 0;
    for (PCWSTR p = full;; p++) {
        if (*p == '_' || !*p) {
            if (n == 5) return 0;
            part[n] = s; len[n] = (int)(p - s); n++;
            if (!*p) break;
            s = p + 1;
        }
    }
    if (n != 5 || !len[0] || !len[1] || !len[2] || !len[4]) return 0;
    return 1;
}

K32 LONG WINAPI PackageFamilyNameFromFullName(PCWSTR full, UINT32 *len, PWSTR out)
{
    PCWSTR part[5]; int l[5];
    if (!len || !package_parts(full, part, l)) return ERROR_INVALID_PARAMETER;
    UINT32 need = (UINT32)(l[0] + 1 + l[4] + 1);
    if (!out || *len < need) { *len = need; return ERROR_INSUFFICIENT_BUFFER; }
    memcpy(out, part[0], l[0] * sizeof(WCHAR));
    out[l[0]] = '_';
    memcpy(out + l[0] + 1, part[4], l[4] * sizeof(WCHAR));
    out[need - 1] = 0;
    *len = need;
    return ERROR_SUCCESS;
}

/* PACKAGE_ID, then its strings; the publisher's full name (only in the
 * installed package's manifest) is left out */
typedef struct { UINT32 reserved, processorArchitecture; UINT64 version; PWSTR name, publisher, resourceId, publisherId; } PackageId;

K32 LONG WINAPI PackageIdFromFullName(PCWSTR full, UINT32 flags, UINT32 *len, BYTE *buf)
{
    static const struct { const char *s; UINT32 arch; } archs[] = {
        { "x86", 0 }, { "arm", 5 }, { "x64", 9 }, { "neutral", 11 }, { "arm64", 12 }, { "x86a64", 14 } };
    PCWSTR part[5]; int l[5];
    if (!len || !package_parts(full, part, l)) return ERROR_INVALID_PARAMETER;
    if (flags & 0x100 /* PACKAGE_INFORMATION_FULL */) return APPMODEL_ERROR_NO_PACKAGE;
    UINT64 ver = 0; int fields = 0; UINT32 v = 0;
    for (int i = 0; i <= l[1]; i++) {
        WCHAR c = i < l[1] ? part[1][i] : '.';
        if (c == '.') { if (fields == 4 || v > 0xFFFF) return ERROR_INVALID_PARAMETER; ver = ver << 16 | v; v = 0; fields++; }
        else if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
        else return ERROR_INVALID_PARAMETER;
    }
    if (fields != 4) return ERROR_INVALID_PARAMETER;
    UINT32 arch = ~0u;
    for (int i = 0; i < 6; i++) {
        int k = 0;
        while (k < l[2] && archs[i].s[k] && (part[2][k] | 32) == archs[i].s[k]) k++;
        if (k == l[2] && !archs[i].s[k]) arch = archs[i].arch;
    }
    if (arch == ~0u) return ERROR_INVALID_PARAMETER;
    UINT32 need = sizeof(PackageId) + (UINT32)(l[0] + 1 + l[3] + 1 + l[4] + 1) * sizeof(WCHAR);
    if (!buf || *len < need) { *len = need; return ERROR_INSUFFICIENT_BUFFER; }
    PackageId *id = (PackageId *)buf;
    WCHAR *w = (WCHAR *)(id + 1);
    memset(id, 0, sizeof *id);
    id->processorArchitecture = arch;
    id->version = ver;
    PWSTR *dst[3] = { &id->name, &id->resourceId, &id->publisherId };
    int which[3] = { 0, 3, 4 };
    for (int i = 0; i < 3; i++) {
        *dst[i] = w;
        memcpy(w, part[which[i]], l[which[i]] * sizeof(WCHAR));
        w[l[which[i]]] = 0;
        w += l[which[i]] + 1;
    }
    *len = need;
    return ERROR_SUCCESS;
}

/* The current package graph: this process's package, then the framework
 * packages it depends on, in its manifest's order, as appx recorded them
 * machine-wide (HKLM\...\AppModel\PackageRepository\Packages\<full
 * name>\Dependencies: their families; Families\<family>\FullName, Path)
 * and the kernel's loader searches them for its DLLs */
#define MACHINE_REPO L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModel\\PackageRepository"
static int graph(WCHAR full[][128], WCHAR path[][MAX_PATH], int cap)
{
    if (!current() || cap < 1) return 0;
    UINT32 n = MAX_PATH;
    memcpy(full[0], g_me.full, sizeof g_me.full);
    if (GetPackagePathByFullName(g_me.full, &n, path[0])) path[0][0] = 0;
    int count = 1;
    WCHAR key[400];
    int k = 0;
    for (const WCHAR *p = MACHINE_REPO L"\\Packages\\"; *p; p++) key[k++] = *p;
    for (const WCHAR *p = g_me.full; *p && k < 380; p++) key[k++] = *p;
    for (const WCHAR *p = L"\\Dependencies"; *p && k < 398; p++) key[k++] = *p;
    key[k] = 0;
    HKEY deps;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &deps)) return count;
    for (DWORD i = 0; count < cap; i++) {
        WCHAR name[12], fam[160], fkey[400];
        int m = 0;
        DWORD v = i;
        do { name[m++] = (WCHAR)('0' + v % 10); v /= 10; } while (v);
        for (int a = 0, b = m - 1; a < b; a++, b--) { WCHAR t = name[a]; name[a] = name[b]; name[b] = t; }
        name[m] = 0;
        if (!reg_str(deps, NULL, name, fam, 160)) break;
        m = 0;
        for (const WCHAR *p = MACHINE_REPO L"\\Families\\"; *p; p++) fkey[m++] = *p;
        for (const WCHAR *p = fam; *p && m < 398; p++) fkey[m++] = *p;
        fkey[m] = 0;
        HKEY f;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, fkey, 0, KEY_READ, &f)) continue;
        if (reg_str(f, NULL, L"FullName", full[count], 128) && reg_str(f, NULL, L"Path", path[count], MAX_PATH)) count++;
        RegCloseKey(f);
    }
    RegCloseKey(deps);
    return count;
}

/* GetCurrentPackageInfo: the graph as PACKAGE_INFO records, then their
 * strings (PACKAGE_FILTER_HEAD: the process's own package only) */
typedef struct { UINT32 reserved, flags; PWSTR path, packageFullName, packageFamilyName; PackageId packageId; } PackageInfo;

K32 LONG WINAPI GetCurrentPackageInfo(UINT32 flags, UINT32 *len, BYTE *buf, UINT32 *count)
{
    WCHAR full[8][128], path[8][MAX_PATH];
    if (!len) return ERROR_INVALID_PARAMETER;
    if (!current()) { *len = 0; if (count) *count = 0; return APPMODEL_ERROR_NO_PACKAGE; }
    int n = graph(full, path, 8);
    if (flags & 0x10 /* PACKAGE_FILTER_HEAD */) n = 1;
    UINT32 need = 0;
    UINT32 idlen[8];
    for (int i = 0; i < n; i++) {
        WCHAR fam[160];
        UINT32 f = 160;
        idlen[i] = 0;
        PackageIdFromFullName(full[i], 0, &idlen[i], 0);
        PackageFamilyNameFromFullName(full[i], &f, fam);
        need += sizeof(PackageInfo) + (idlen[i] - sizeof(PackageId) + 7) / 8 * 8 +
                (UINT32)(wlen(path[i]) + 1 + wlen(full[i]) + 1 + f) * sizeof(WCHAR);
    }
    if (count) *count = (UINT32)n;
    if (!buf || *len < need) { *len = need; return ERROR_INSUFFICIENT_BUFFER; }
    PackageInfo *pi = (PackageInfo *)buf;
    BYTE *strs = buf + n * sizeof(PackageInfo);
    for (int i = 0; i < n; i++) {
        BYTE tmp[sizeof(PackageId) + 512];
        UINT32 l = sizeof tmp, f = 160;
        memset(&pi[i], 0, sizeof pi[i]);
        pi[i].flags = i ? 0x10 /* PACKAGE_PROPERTY_FRAMEWORK */ : 0;
        if (PackageIdFromFullName(full[i], 0, &l, tmp)) continue;
        PackageId *id = (PackageId *)tmp;
        pi[i].packageId = *id;
        /* its id's strings, moved after the records */
        UINT32 sl = l - sizeof(PackageId);
        memcpy(strs, tmp + sizeof(PackageId), sl);
        PWSTR *fields[4] = { &pi[i].packageId.name, &pi[i].packageId.publisher, &pi[i].packageId.resourceId,
                             &pi[i].packageId.publisherId };
        for (int j = 0; j < 4; j++)
            if (*fields[j]) *fields[j] = (PWSTR)(strs + ((BYTE *)*fields[j] - (tmp + sizeof(PackageId))));
        strs += (sl + 7) / 8 * 8;
        WCHAR *w = (WCHAR *)strs;
        pi[i].path = w;
        memcpy(w, path[i], (wlen(path[i]) + 1) * sizeof(WCHAR));
        w += wlen(path[i]) + 1;
        pi[i].packageFullName = w;
        memcpy(w, full[i], (wlen(full[i]) + 1) * sizeof(WCHAR));
        w += wlen(full[i]) + 1;
        pi[i].packageFamilyName = w;
        PackageFamilyNameFromFullName(full[i], &f, w);
        w += f;
        strs = (BYTE *)w;
    }
    *len = need;
    return ERROR_SUCCESS;
}

/* A UWP app (an application whose RuntimeBehavior is "windowsApp") runs
 * under the Windows Runtime's policies: it ends with TerminateProcess, its
 * threads start in the Windows Runtime, its window is a CoreWindow.  A
 * desktop program, packaged or not, runs under the classic desktop's. */
static BOOL universal(void)
{
    static LONG known;                              /* 0 not yet, 1 no, 2 yes */
    if (!known) {
        known = 1;
        HKEY pk = current() ? repo(L"Packages") : 0;
        if (pk) {
            WCHAR sub[220], v[32];
            int n = 0;
            for (const WCHAR *p = g_me.full; *p; p++) sub[n++] = *p;
            for (const WCHAR *p = L"\\Applications\\"; *p; p++) sub[n++] = *p;
            for (const WCHAR *p = g_me.app; *p && n < 218; p++) sub[n++] = *p;
            sub[n] = 0;
            if (reg_str(pk, sub, L"RuntimeBehavior", v, 32) && same_path(v, L"windowsApp")) known = 2;
            RegCloseKey(pk);
        }
    }
    return known == 2;
}

K32 LONG WINAPI AppPolicyGetProcessTerminationMethod(HANDLE token, int *policy)
{
    (void)token;
    if (!policy) return ERROR_INVALID_PARAMETER;
    *policy = universal() ? 1 : 0;                  /* AppPolicyProcessTerminationMethod_TerminateProcess / _ExitProcess */
    return ERROR_SUCCESS;
}

K32 LONG WINAPI AppPolicyGetThreadInitializationType(HANDLE token, int *policy)
{
    (void)token;
    if (!policy) return ERROR_INVALID_PARAMETER;
    *policy = universal() ? 1 : 0;                  /* AppPolicyThreadInitializationType_InitializeWinRT / _None */
    return ERROR_SUCCESS;
}

/* (XAML asks before it makes a CoreWindow or a desktop window of its own) */
K32 LONG WINAPI AppPolicyGetWindowingModel(HANDLE token, int *policy)
{
    (void)token;
    if (!policy) return ERROR_INVALID_PARAMETER;
    *policy = universal() ? 1 : 2;                  /* AppPolicyWindowingModel_Universal / _ClassicDesktop */
    return ERROR_SUCCESS;
}
