/*
 * setupapi.dll — driver packages: SetupCopyOEMInf, SetupUninstallOEMInf,
 * SetupGetInfDriverStoreLocation.
 *
 * As on Windows (Vista and later) a driver package is staged in the
 * driver store: the INF, its catalog and every file its SourceDisksFiles
 * sections list are copied to
 * C:\Windows\System32\DriverStore\FileRepository\<inf>_<arch>_<hash>, and
 * the INF is published as C:\Windows\INF\oemN.inf.  The registry's
 * HKLM\SYSTEM\DriverDatabase records both (DriverInfFiles\oemN.inf names the
 * store folder; DriverPackages\<folder> holds the original INF name and the
 * INF's provider, class and version).  A package whose INF or files are
 * missing is refused, and one already published is found again (the same
 * INF bytes) instead of being published twice.
 *
 * NovaOS's devices have NovaOS's own kernel drivers, and NovaOS does not
 * load Windows kernel-mode drivers (.sys files), so a staged package is
 * not started on a device: newdev's UpdateDriverForPlugAndPlayDevices finds
 * no device for its hardware IDs, as on a PC without the device.
 */
#include "inf.h"

#define SETUPAPI __declspec(dllexport)
#define SP_COPY_DELETESOURCE   0x0000001
#define SP_COPY_NOOVERWRITE    0x0000008
#define SPOST_NONE             0
#define SUOI_FORCEDELETE       0x0001
#define DB_KEY L"SYSTEM\\DriverDatabase"

/* The machine's architecture (a 32-bit installer on 64-bit NovaOS stages
 * the 64-bit package, as on Windows) and the INF decorations naming it */
static BOOL machine64(void)
{
#ifdef _WIN64
    return TRUE;
#else
    BOOL WINAPI IsWow64Process(HANDLE h, PBOOL wow);
    BOOL wow = FALSE;
    return IsWow64Process(GetCurrentProcess(), &wow) && wow;
#endif
}
static const WCHAR *arch(void)  { return machine64() ? L"amd64" : L"x86"; }
static const WCHAR *decor(void) { return machine64() ? L"NTamd64" : L"NTx86"; }

static void decorated(WCHAR *out, const WCHAR *name, const WCHAR *suffix)
{
    lstrcpyW(out, name);
    lstrcatW(out, L".");
    lstrcatW(out, suffix);
}

static void windir(WCHAR *out, const WCHAR *rest)
{
    UINT n = GetWindowsDirectoryW(out, MAX_PATH);
    if (!n || n >= MAX_PATH) lstrcpyW(out, L"C:\\Windows");
    lstrcatW(out, rest);
}

static void join(WCHAR *out, const WCHAR *a, const WCHAR *b)
{
    lstrcpynW(out, a, MAX_PATH);
    int n = lstrlenW(out);
    if (b && *b) {
        if (n && out[n - 1] != '\\' && n < MAX_PATH - 1) { out[n++] = '\\'; out[n] = 0; }
        lstrcpynW(out + n, b, MAX_PATH - n);
    }
}

static BOOL make_dirs(const WCHAR *dir)
{
    WCHAR p[MAX_PATH];
    lstrcpynW(p, dir, MAX_PATH);
    for (WCHAR *s = p + 3; *s; s++) {                 /* (past "C:\") */
        if (*s != '\\') continue;
        *s = 0;
        CreateDirectoryW(p, NULL);
        *s = '\\';
    }
    return CreateDirectoryW(p, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static BOOL exists(const WCHAR *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static BYTE *read_all(const WCHAR *p, DWORD *len)
{
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD n = GetFileSize(f, NULL), got = 0;
    BYTE *d = HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
    if (d && !(ReadFile(f, d, n, &got, NULL) && got == n)) { HeapFree(GetProcessHeap(), 0, d); d = NULL; }
    CloseHandle(f);
    if (d) *len = n;
    return d;
}

static BOOL same_bytes(const WCHAR *a, const BYTE *b, DWORD bn)
{
    DWORD an;
    BYTE *d = read_all(a, &an);
    BOOL same = d && an == bn && !memcmp(d, b, bn);
    if (d) HeapFree(GetProcessHeap(), 0, d);
    return same;
}

static const WCHAR *base_name(const WCHAR *p)
{
    const WCHAR *b = p;
    for (const WCHAR *s = p; *s; s++) if (*s == '\\' || *s == '/' || *s == ':') b = s + 1;
    return b;
}

static void put_number(WCHAR *out, unsigned v)
{
    WCHAR t[12];
    int k = 0;
    do { t[k++] = (WCHAR)('0' + v % 10); v /= 10; } while (v);
    while (k) *out++ = t[--k];
    *out = 0;
}

/* -----------------------------------------------------------------------
 * The registry's driver database
 * ----------------------------------------------------------------------- */
static void db_set(const WCHAR *sub, const WCHAR *name, const WCHAR *value)
{
    WCHAR k[MAX_PATH];
    join(k, DB_KEY, sub);
    HKEY h;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, k, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &h, NULL)) return;
    RegSetValueExW(h, name, 0, REG_SZ, (const BYTE *)value, (DWORD)(lstrlenW(value) + 1) * sizeof(WCHAR));
    RegCloseKey(h);
}

static BOOL db_get(const WCHAR *sub, const WCHAR *name, WCHAR *out, DWORD cap)
{
    WCHAR k[MAX_PATH];
    join(k, DB_KEY, sub);
    HKEY h;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &h)) return FALSE;
    DWORD type, n = cap * sizeof(WCHAR);
    LONG e = RegQueryValueExW(h, name, NULL, &type, (BYTE *)out, &n);
    RegCloseKey(h);
    if (e || type != REG_SZ) return FALSE;
    out[cap - 1] = 0;
    return TRUE;
}

static void db_delete(const WCHAR *sub)
{
    WCHAR k[MAX_PATH];
    join(k, DB_KEY, sub);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, k);
}

/* -----------------------------------------------------------------------
 * Staging
 * ----------------------------------------------------------------------- */
typedef struct {
    const Inf *inf;
    WCHAR src[MAX_PATH], dst[MAX_PATH];
    DWORD err;
    WCHAR missing[MAX_PATH];
} Stage;

static BOOL copy_one(Stage *st, const WCHAR *from, const WCHAR *sub, const WCHAR *file)
{
    WCHAR d[MAX_PATH], to[MAX_PATH];
    join(d, st->dst, sub);
    make_dirs(d);
    join(to, d, file);
    if (CopyFileW(from, to, FALSE)) return TRUE;
    st->err = GetLastError() ? GetLastError() : ERROR_FILE_NOT_FOUND;
    lstrcpynW(st->missing, from, MAX_PATH);
    return FALSE;
}

/* the source folder of disk @id (SourceDisksNames: desc, tagfile, unused, path) */
static void disk_path(const Inf *inf, const WCHAR *id, WCHAR *out)
{
    WCHAR v[512], sec[64];
    out[0] = 0;
    if (!*id) return;
    decorated(sec, L"SourceDisksNames", arch());
    if (inf_get(inf, sec, id, v, 512) || inf_get(inf, L"SourceDisksNames", id, v, 512))
        inf_field(v, 3, out, MAX_PATH);
    while (out[0] == '\\') memmove(out, out + 1, lstrlenW(out) * sizeof(WCHAR));
}

static BOOL stage_file(const WCHAR *key, const WCHAR *value, void *ctx)
{
    Stage *st = ctx;
    WCHAR file[MAX_PATH], disk[16], sub[MAX_PATH], dpath[MAX_PATH], from[MAX_PATH], dir[MAX_PATH];
    if (*key) { lstrcpynW(file, key, MAX_PATH); inf_field(value, 0, disk, 16); inf_field(value, 1, sub, MAX_PATH); }
    else      { lstrcpynW(file, value, MAX_PATH); disk[0] = sub[0] = 0; }
    if (!file[0]) return TRUE;
    disk_path(st->inf, disk, dpath);
    join(dir, st->src, dpath);
    join(dir, dir, sub);
    join(from, dir, file);
    return copy_one(st, from, sub, file);
}

/* Copy the package beside @inf_path into the store folder @dst */
static BOOL stage(const Inf *inf, const WCHAR *dst)
{
    Stage st = { inf };
    lstrcpynW(st.src, inf->path, MAX_PATH);
    *(WCHAR *)base_name(st.src) = 0;
    lstrcpynW(st.dst, dst, MAX_PATH);
    if (!make_dirs(dst)) return FALSE;
    if (!copy_one(&st, inf->path, L"", base_name(inf->path))) return FALSE;
    WCHAR cat[MAX_PATH], from[MAX_PATH], key[64];
    decorated(key, L"CatalogFile", decor());
    if (inf_get(inf, L"Version", key, cat, MAX_PATH) ||
        inf_get(inf, L"Version", L"CatalogFile", cat, MAX_PATH)) {
        join(from, st.src, cat);
        if (!copy_one(&st, from, L"", cat)) { SetLastError(st.err); return FALSE; }
    }
    inf_lines(inf, L"SourceDisksFiles", stage_file, &st);
    decorated(key, L"SourceDisksFiles", arch());
    if (!st.err) inf_lines(inf, key, stage_file, &st);
    if (st.err) { SetLastError(st.err); return FALSE; }
    return TRUE;
}

static void remove_tree(const WCHAR *dir)
{
    WCHAR pat[MAX_PATH], p[MAX_PATH];
    join(pat, dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!lstrcmpW(fd.cFileName, L".") || !lstrcmpW(fd.cFileName, L"..")) continue;
            join(p, dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(p);
            else DeleteFileW(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

static BOOL give_name(const WCHAR *path, PWSTR dest, DWORD size, PDWORD need, PWSTR *component)
{
    DWORD n = (DWORD)lstrlenW(path) + 1;
    if (need) *need = n;
    if (!dest) return TRUE;
    if (size < n) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    memcpy(dest, path, n * sizeof(WCHAR));
    if (component) *component = dest + (base_name(path) - path);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupCopyOEMInfW(PCWSTR src, PCWSTR media_loc, DWORD media_type, DWORD style,
                                      PWSTR dest, DWORD size, PDWORD need, PWSTR *component)
{
    (void)media_loc; (void)media_type;
    if (component) *component = NULL;
    if (!src) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    HANDLE h = SetupOpenInfFileW(src, NULL, 2 /* INF_STYLE_WIN4 */, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    Inf *inf = (Inf *)h;
    DWORD len = 0;
    BYTE *bytes = read_all(inf->path, &len);
    if (!bytes) { SetupCloseInfFile(h); SetLastError(ERROR_READ_FAULT); return FALSE; }

    /* already published (the same INF): that name */
    WCHAR dir[MAX_PATH], path[MAX_PATH], num[12], name[32];
    windir(dir, L"\\INF");
    make_dirs(dir);
    int free_n = -1;
    for (unsigned i = 0; i < 10000; i++) {
        lstrcpyW(name, L"oem");
        put_number(num, i);
        lstrcatW(name, num);
        lstrcatW(name, L".inf");
        join(path, dir, name);
        if (!exists(path)) { if (free_n < 0) free_n = (int)i; if (free_n + 64 < (int)i) break; continue; }
        if (!same_bytes(path, bytes, len)) continue;
        HeapFree(GetProcessHeap(), 0, bytes);
        SetupCloseInfFile(h);
        if (!give_name(path, dest, size, need, component)) return FALSE;
        if (style & SP_COPY_NOOVERWRITE) { SetLastError(ERROR_FILE_EXISTS); return FALSE; }
        SetLastError(0);
        return TRUE;
    }
    if (free_n < 0) { HeapFree(GetProcessHeap(), 0, bytes); SetupCloseInfFile(h); SetLastError(ERROR_FILE_EXISTS); return FALSE; }

    /* the store folder: <inf name>_<arch>_<hash of the INF> */
    UINT64 hash = 0xcbf29ce484222325ull;
    for (DWORD i = 0; i < len; i++) hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    HeapFree(GetProcessHeap(), 0, bytes);
    WCHAR pkg[MAX_PATH], store[MAX_PATH];
    lstrcpynW(pkg, base_name(inf->path), MAX_PATH - 40);
    for (WCHAR *c = pkg; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
    lstrcatW(pkg, L"_");
    lstrcatW(pkg, arch());
    lstrcatW(pkg, L"_");
    int k = lstrlenW(pkg);
    for (int i = 15; i >= 0; i--) pkg[k++] = L"0123456789abcdef"[(hash >> (i * 4)) & 15];
    pkg[k] = 0;
    windir(store, L"\\System32\\DriverStore\\FileRepository\\");
    lstrcatW(store, pkg);
    if (!stage(inf, store)) {
        DWORD e = GetLastError();
        remove_tree(store);
        SetupCloseInfFile(h);
        SetLastError(e ? e : ERROR_FILE_NOT_FOUND);
        return FALSE;
    }

    lstrcpyW(name, L"oem");
    put_number(num, (unsigned)free_n);
    lstrcatW(name, num);
    lstrcatW(name, L".inf");
    join(path, dir, name);
    if (!CopyFileW(inf->path, path, TRUE)) {
        DWORD e = GetLastError();
        remove_tree(store);
        SetupCloseInfFile(h);
        SetLastError(e);
        return FALSE;
    }
    WCHAR sub[MAX_PATH], v[256];
    join(sub, L"DriverInfFiles", name);
    db_set(sub, L"", pkg);
    join(sub, L"DriverPackages", pkg);
    db_set(sub, L"InfName", base_name(inf->path));
    db_set(sub, L"OemPath", path);
    db_set(sub, L"Published", name);
    if (inf_get(inf, L"Version", L"Provider", v, 256)) db_set(sub, L"Provider", v);
    if (inf_get(inf, L"Version", L"Class", v, 256)) db_set(sub, L"Class", v);
    if (inf_get(inf, L"Version", L"ClassGuid", v, 256)) db_set(sub, L"ClassGuid", v);
    if (inf_get(inf, L"Version", L"DriverVer", v, 256)) db_set(sub, L"Version", v);
    if (style & SP_COPY_DELETESOURCE) DeleteFileW(inf->path);
    SetupCloseInfFile(h);
    if (!give_name(path, dest, size, need, component)) return FALSE;
    SetLastError(0);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupCopyOEMInfA(LPCSTR src, LPCSTR media_loc, DWORD media_type, DWORD style,
                                      LPSTR dest, DWORD size, PDWORD need, LPSTR *component)
{
    WCHAR ws[MAX_PATH], wd[MAX_PATH];
    if (component) *component = NULL;
    if (!src) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    MultiByteToWideChar(CP_ACP, 0, src, -1, ws, MAX_PATH);
    (void)media_loc;
    BOOL ok = SetupCopyOEMInfW(ws, NULL, media_type, style, wd, MAX_PATH, NULL, NULL);
    DWORD e = GetLastError();
    if (!ok && e != ERROR_FILE_EXISTS) return FALSE;
    DWORD n = (DWORD)WideCharToMultiByte(CP_ACP, 0, wd, -1, NULL, 0, NULL, NULL);
    if (need) *need = n;
    if (dest) {
        if (size < n) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        WideCharToMultiByte(CP_ACP, 0, wd, -1, dest, (int)size, NULL, NULL);
        if (component) {
            char *b = dest;
            for (char *s = dest; *s; s++) if (*s == '\\') b = s + 1;
            *component = b;
        }
    }
    SetLastError(e);
    return ok;
}

SETUPAPI BOOL WINAPI SetupUninstallOEMInfW(PCWSTR inf_name, DWORD flags, PVOID reserved)
{
    (void)flags; (void)reserved;
    if (!inf_name || base_name(inf_name) != inf_name) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    WCHAR dir[MAX_PATH], path[MAX_PATH], sub[MAX_PATH], pkg[MAX_PATH], store[MAX_PATH];
    windir(dir, L"\\INF");
    join(path, dir, inf_name);
    if (!exists(path)) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    join(sub, L"DriverInfFiles", inf_name);
    if (db_get(sub, L"", pkg, MAX_PATH) && pkg[0]) {
        windir(store, L"\\System32\\DriverStore\\FileRepository\\");
        lstrcatW(store, pkg);
        remove_tree(store);
        WCHAR psub[MAX_PATH];
        join(psub, L"DriverPackages", pkg);
        db_delete(psub);
    }
    db_delete(sub);
    if (!DeleteFileW(path)) return FALSE;
    WCHAR pnf[MAX_PATH];
    lstrcpynW(pnf, path, MAX_PATH);
    int n = lstrlenW(pnf);
    if (n > 4) { lstrcpyW(pnf + n - 4, L".pnf"); DeleteFileW(pnf); }
    SetLastError(0);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupUninstallOEMInfA(LPCSTR inf_name, DWORD flags, PVOID reserved)
{
    WCHAR w[MAX_PATH];
    if (!inf_name) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    MultiByteToWideChar(CP_ACP, 0, inf_name, -1, w, MAX_PATH);
    return SetupUninstallOEMInfW(w, flags, reserved);
}

/* The store copy of a published INF (oemN.inf or its full path) */
SETUPAPI BOOL WINAPI SetupGetInfDriverStoreLocationW(PCWSTR file, PVOID alt, PCWSTR locale, DWORD flags,
                                                     PWSTR out, DWORD size, PDWORD need)
{
    (void)alt; (void)locale; (void)flags;
    if (!file) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    WCHAR sub[MAX_PATH], pkg[MAX_PATH], path[MAX_PATH], orig[MAX_PATH];
    join(sub, L"DriverInfFiles", base_name(file));
    if (!db_get(sub, L"", pkg, MAX_PATH)) { SetLastError(ERROR_NOT_FOUND); return FALSE; }
    join(sub, L"DriverPackages", pkg);
    if (!db_get(sub, L"InfName", orig, MAX_PATH)) { SetLastError(ERROR_NOT_FOUND); return FALSE; }
    windir(path, L"\\System32\\DriverStore\\FileRepository\\");
    lstrcatW(path, pkg);
    join(path, path, orig);
    return give_name(path, out, size, need, NULL);
}
