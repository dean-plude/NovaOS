/*
 * volume.c — volumes and their drive letters: FindFirstVolume /
 * FindNextVolume, GetVolumeNameForVolumeMountPoint,
 * GetVolumePathNamesForVolumeName, SetVolumeMountPoint,
 * DeleteVolumeMountPoint and QueryDosDevice
 *
 * Every partition is a volume (\Device\HarddiskVolumeN, drive C:'s is 1)
 * named \\?\Volume{GUID}\, as on Windows; the kernel answers for each
 * through NovaOS's IOCTL_NOVA_VOLUME_INFO on its handle and changes its
 * letter with IOCTL_NOVA_VOLUME_SET_LETTER (kernel/um/um_disk.c).  Only
 * drive letters are mount points: a volume mounted in a folder is not
 * offered.
 */

#define NOVA_BUILD_KERNEL32
#include <winternl.h>
#include <winioctl.h>
#include "k32.h"

void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);

#define K32 __declspec(dllexport)
#define MAX_VOLUME_NUMBER 128          /* (numbers are not reused while NovaOS runs: disks plugged in again) */
#ifndef ERROR_NOT_A_REPARSE_POINT
#define ERROR_NOT_A_REPARSE_POINT 4390
#endif
#ifndef ERROR_FILENAME_EXCED_RANGE
#define ERROR_FILENAME_EXCED_RANGE 206
#endif

static int wlen(const WCHAR *s) { int n = 0; if (s) while (s[n]) n++; return n; }

static HANDLE open_nt(const WCHAR *name, int n, ACCESS_MASK access)
{
    UNICODE_STRING us = { (USHORT)(2 * n), (USHORT)(2 * n + 2), (PWSTR)name };
    OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &us, OBJ_CASE_INSENSITIVE, 0, 0 };
    IO_STATUS_BLOCK io;
    HANDLE h = 0;
    NTSTATUS s = NtOpenFile(&h, access | SYNCHRONIZE, &oa, &io, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(s)) { fail_status(s); return 0; }
    return h;
}

static BOOL ioctl(HANDLE h, DWORD code, void *in, DWORD in_n, void *out, DWORD out_n)
{
    IO_STATUS_BLOCK io = { 0 };
    NTSTATUS s = NtDeviceIoControlFile(h, 0, 0, 0, &io, code, in, in_n, out, out_n);
    return NT_SUCCESS(s) ? TRUE : fail_status(s);
}

static BOOL info_of(HANDLE h, NOVA_VOLUME_INFO *vi)
{
    if (!h) return FALSE;
    BOOL ok = ioctl(h, IOCTL_NOVA_VOLUME_INFO, 0, 0, vi, sizeof(*vi));
    NtClose(h);
    return ok;
}

static HANDLE open_number(DWORD n, ACCESS_MASK access)
{
    WCHAR name[40];
    static const char pre[] = "\\Device\\HarddiskVolume";
    int k = 0;
    for (; pre[k]; k++) name[k] = (WCHAR)pre[k];
    WCHAR digits[12];
    int d = 0;
    do { digits[d++] = (WCHAR)('0' + n % 10); n /= 10; } while (n);
    while (d) name[k++] = digits[--d];
    name[k] = 0;
    return open_nt(name, k, access);
}

static HANDLE open_letter(WCHAR c, ACCESS_MASK access)
{
    WCHAR name[] = { '\\', '?', '?', '\\', (WCHAR)(c & ~0x20), ':', 0 };
    return open_nt(name, 6, access);
}

/* "\\?\Volume{GUID}" (a trailing backslash or not) at @p: the characters up
 * to the '}', or 0 */
static int volume_name_len(LPCWSTR p)
{
    static const char pre[] = "\\\\?\\Volume{";
    for (int i = 0; pre[i]; i++) if (!p || (p[i] | (i > 3 ? 0x20 : 0)) != (pre[i] | (i > 3 ? 0x20 : 0))) return 0;
    for (int i = 11; i < 11 + 36; i++) if (!p[i]) return 0;
    return p[47] == '}' ? 48 : 0;
}

/* The volume \\?\Volume{GUID}[\] names, opened */
static HANDLE open_guid_path(LPCWSTR path, ACCESS_MASK access)
{
    int n = volume_name_len(path);
    if (!n || (path[n] && !(path[n] == '\\' && !path[n + 1]))) { SetLastError(ERROR_INVALID_NAME); return 0; }
    WCHAR name[56];
    name[0] = '\\'; name[1] = '?'; name[2] = '?'; name[3] = '\\';
    memcpy(name + 4, path + 4, 2 * (SIZE_T)(n - 4));
    name[n] = 0;
    return open_nt(name, n, access);
}

/* "\\?\Volume{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}\" for @g into @out (50 characters with the NUL) */
static void guid_path(const GUID *g, WCHAR *out)
{
    static const char hex[] = "0123456789abcdef";
    static const char pre[] = "\\\\?\\Volume{";
    int k = 0;
    for (; pre[k]; k++) out[k] = (WCHAR)pre[k];
    BYTE b[16];
    memcpy(b, g, 16);
    static const int order[16] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[k++] = '-';
        out[k++] = (WCHAR)hex[b[order[i]] >> 4];
        out[k++] = (WCHAR)hex[b[order[i]] & 15];
    }
    out[k++] = '}'; out[k++] = '\\'; out[k] = 0;
}

/* A drive root ("X:\" or "X:") or a volume name: its volume */
static BOOL info_of_root(LPCWSTR root, NOVA_VOLUME_INFO *vi)
{
    if (!root) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (volume_name_len(root)) return info_of(open_guid_path(root, 0), vi);
    WCHAR c = root[0] & ~0x20;
    if (c < 'A' || c > 'Z' || root[1] != ':' || (root[2] && !(root[2] == '\\' && !root[3]))) {
        SetLastError(ERROR_NOT_A_REPARSE_POINT);                /* (a folder: no volume is mounted in one) */
        return FALSE;
    }
    if (!info_of(open_letter(c, 0), vi)) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    return TRUE;
}

/* For extra.c: the drive letter (0: none) and NOVA_VOLUME_* flags of the
 * volume @path is on, when it is a volume name ("\\?\Volume{...}\...")
 * or a path on a drive; FALSE if it is neither */
BOOL k32_volume_of(LPCWSTR path, WCHAR *letter, DWORD *flags)
{
    NOVA_VOLUME_INFO vi;
    HANDLE h;
    if (volume_name_len(path)) {
        WCHAR name[56];
        int n = volume_name_len(path);
        memcpy(name, path, 2 * (SIZE_T)n);
        name[n] = 0;
        h = open_guid_path(name, 0);
    } else if (path && ((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z') && path[1] == ':') h = open_letter(path[0], 0);
    else return FALSE;
    if (!info_of(h, &vi)) return FALSE;
    if (letter) *letter = vi.Letter;
    if (flags) *flags = vi.Flags;
    return TRUE;
}

/* For extra.c: the volume a volume name names, opened for queries */
HANDLE k32_volume_open(LPCWSTR path)
{
    WCHAR name[56];
    int n = volume_name_len(path);
    if (!n) return 0;
    memcpy(name, path, 2 * (SIZE_T)n);
    name[n] = 0;
    return open_guid_path(name, 0);
}

/* -----------------------------------------------------------------------
 * Listing the volumes
 * ----------------------------------------------------------------------- */
typedef struct { DWORD magic, next; } VolumeFind;
#define FIND_MAGIC 0x4E56464Eu                  /* "NFVN" */

/* The next volume from number @from: its name into @name (@n characters) */
static BOOL next_volume(VolumeFind *f, LPWSTR name, DWORD n)
{
    NOVA_VOLUME_INFO vi;
    for (; f->next <= MAX_VOLUME_NUMBER; f->next++) {
        HANDLE h = open_number(f->next, 0);
        if (!h || !info_of(h, &vi)) continue;
        if (n < 50) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return FALSE; }
        guid_path(&vi.Guid, name);
        f->next++;
        return TRUE;
    }
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

K32 HANDLE WINAPI FindFirstVolumeW(LPWSTR name, DWORD n)
{
    VolumeFind *f = HeapAlloc(GetProcessHeap(), 0, sizeof(*f));
    if (!f) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE; }
    f->magic = FIND_MAGIC;
    f->next = 1;
    if (!next_volume(f, name, n)) { HeapFree(GetProcessHeap(), 0, f); return INVALID_HANDLE_VALUE; }
    return f;
}

K32 BOOL WINAPI FindNextVolumeW(HANDLE h, LPWSTR name, DWORD n)
{
    VolumeFind *f = h;
    if (!f || h == INVALID_HANDLE_VALUE || f->magic != FIND_MAGIC) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    return next_volume(f, name, n);
}

K32 BOOL WINAPI FindVolumeClose(HANDLE h)
{
    VolumeFind *f = h;
    if (!f || h == INVALID_HANDLE_VALUE || f->magic != FIND_MAGIC) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    f->magic = 0;
    HeapFree(GetProcessHeap(), 0, f);
    return TRUE;
}

static BOOL narrow(const WCHAR *w, LPSTR out, DWORD n)
{
    int k = wlen(w);
    if ((DWORD)k >= n) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return FALSE; }
    for (int i = 0; i <= k; i++) out[i] = (char)w[i];
    return TRUE;
}

K32 HANDLE WINAPI FindFirstVolumeA(LPSTR name, DWORD n)
{
    WCHAR w[64];
    HANDLE h = FindFirstVolumeW(w, 64);
    if (h != INVALID_HANDLE_VALUE && !narrow(w, name, n)) { FindVolumeClose(h); return INVALID_HANDLE_VALUE; }
    return h;
}

K32 BOOL WINAPI FindNextVolumeA(HANDLE h, LPSTR name, DWORD n)
{
    WCHAR w[64];
    return FindNextVolumeW(h, w, 64) && narrow(w, name, n);
}

/* -----------------------------------------------------------------------
 * Names and mount points
 * ----------------------------------------------------------------------- */
K32 BOOL WINAPI GetVolumeNameForVolumeMountPointW(LPCWSTR mount, LPWSTR buf, DWORD n)
{
    NOVA_VOLUME_INFO vi;
    if (!info_of_root(mount, &vi)) return FALSE;
    if (n < 50) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return FALSE; }
    guid_path(&vi.Guid, buf);
    return TRUE;
}

/* The volume's paths: "X:\" and the empty string after it, or just that
 * when it has no letter */
K32 BOOL WINAPI GetVolumePathNamesForVolumeNameW(LPCWSTR vol, LPWSTR buf, DWORD n, PDWORD ret)
{
    NOVA_VOLUME_INFO vi;
    if (!volume_name_len(vol)) { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    if (!info_of(open_guid_path(vol, 0), &vi)) return FALSE;
    DWORD need = vi.Letter ? 5 : 2;
    if (ret) *ret = need;
    if (!buf || n < need) { SetLastError(ERROR_MORE_DATA); return FALSE; }
    if (vi.Letter) { buf[0] = vi.Letter; buf[1] = ':'; buf[2] = '\\'; buf[3] = 0; buf[4] = 0; }
    else { buf[0] = 0; buf[1] = 0; }
    return TRUE;
}

/* Give the volume @vol (\\?\Volume{...}\) drive letter @mount ("X:\") */
K32 BOOL WINAPI SetVolumeMountPointW(LPCWSTR mount, LPCWSTR vol)
{
    WCHAR c = mount ? mount[0] & ~0x20 : 0;
    if (c < 'A' || c > 'Z' || mount[1] != ':' || mount[2] != '\\' || mount[3]) {
        SetLastError(mount && mount[0] && mount[1] != ':' ? ERROR_NOT_A_REPARSE_POINT : ERROR_INVALID_NAME);
        return FALSE;
    }
    HANDLE h = open_guid_path(vol, GENERIC_READ | GENERIC_WRITE);
    if (!h) return FALSE;
    NOVA_VOLUME_LETTER l = { c };
    BOOL ok = ioctl(h, IOCTL_NOVA_VOLUME_SET_LETTER, &l, sizeof(l), 0, 0);
    NtClose(h);
    return ok;
}

/* Take drive letter @mount ("X:\") from its volume */
K32 BOOL WINAPI DeleteVolumeMountPointW(LPCWSTR mount)
{
    WCHAR c = mount ? mount[0] & ~0x20 : 0;
    if (c < 'A' || c > 'Z' || mount[1] != ':' || mount[2] != '\\' || mount[3]) {
        SetLastError(ERROR_NOT_A_REPARSE_POINT);
        return FALSE;
    }
    HANDLE h = open_letter(c, GENERIC_READ | GENERIC_WRITE);
    if (!h) return FALSE;
    NOVA_VOLUME_LETTER l = { 0 };
    BOOL ok = ioctl(h, IOCTL_NOVA_VOLUME_SET_LETTER, &l, sizeof(l), 0, 0);
    NtClose(h);
    return ok;
}

static BOOL widen(LPCSTR s, WCHAR *w, int cap)
{
    if (!s) return FALSE;
    int i = 0;
    for (; s[i] && i < cap - 1; i++) w[i] = (BYTE)s[i];
    w[i] = 0;
    return !s[i];
}

K32 BOOL WINAPI GetVolumeNameForVolumeMountPointA(LPCSTR mount, LPSTR buf, DWORD n)
{
    WCHAR m[MAX_PATH], w[64];
    if (!widen(mount, m, MAX_PATH)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return GetVolumeNameForVolumeMountPointW(m, w, 64) && narrow(w, buf, n);
}

K32 BOOL WINAPI GetVolumePathNamesForVolumeNameA(LPCSTR vol, LPSTR buf, DWORD n, PDWORD ret)
{
    WCHAR v[64], w[8];
    if (!widen(vol, v, 64)) { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    DWORD need = 0;
    BOOL ok = GetVolumePathNamesForVolumeNameW(v, w, 8, &need);
    if (ret) *ret = need;
    if (!ok) return FALSE;
    if (!buf || n < need) { SetLastError(ERROR_MORE_DATA); return FALSE; }
    for (DWORD i = 0; i < need; i++) buf[i] = (char)w[i];
    return TRUE;
}

K32 BOOL WINAPI SetVolumeMountPointA(LPCSTR mount, LPCSTR vol)
{
    WCHAR m[MAX_PATH], v[64];
    if (!widen(mount, m, MAX_PATH) || !widen(vol, v, 64)) { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    return SetVolumeMountPointW(m, v);
}

K32 BOOL WINAPI DeleteVolumeMountPointA(LPCSTR mount)
{
    WCHAR m[MAX_PATH];
    if (!widen(mount, m, MAX_PATH)) { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    return DeleteVolumeMountPointW(m);
}

/* -----------------------------------------------------------------------
 * MS-DOS device names: the drive letters, each the link to its volume
 * ----------------------------------------------------------------------- */
K32 DWORD WINAPI QueryDosDeviceW(LPCWSTR name, LPWSTR buf, DWORD n)
{
    if (!name) {                                                 /* every name: "C:\0D:\0...\0" */
        DWORD map = GetLogicalDrives(), need = 1, k = 0;
        for (int i = 0; i < 26; i++) if (map & (1u << i)) need += 3;
        if (n < need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
        for (int i = 0; i < 26; i++)
            if (map & (1u << i)) { buf[k++] = (WCHAR)('A' + i); buf[k++] = ':'; buf[k++] = 0; }
        buf[k++] = 0;
        return k;
    }
    NOVA_VOLUME_INFO vi;
    WCHAR c = name[0] & ~0x20;
    if (c < 'A' || c > 'Z' || name[1] != ':' || name[2] || !info_of(open_letter(c, 0), &vi)) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return 0;
    }
    WCHAR dev[40];
    static const char pre[] = "\\Device\\HarddiskVolume";
    int k = 0;
    for (; pre[k]; k++) dev[k] = (WCHAR)pre[k];
    WCHAR digits[12];
    int d = 0;
    DWORD v = vi.Number;
    do { digits[d++] = (WCHAR)('0' + v % 10); v /= 10; } while (v);
    while (d) dev[k++] = digits[--d];
    if (n < (DWORD)k + 2) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    memcpy(buf, dev, 2 * (SIZE_T)k);
    buf[k] = 0; buf[k + 1] = 0;
    return (DWORD)k + 2;
}

K32 DWORD WINAPI QueryDosDeviceA(LPCSTR name, LPSTR buf, DWORD n)
{
    WCHAR w[8], out[512];
    if (name && !widen(name, w, 8)) { SetLastError(ERROR_FILE_NOT_FOUND); return 0; }
    DWORD k = QueryDosDeviceW(name ? w : 0, out, 512);
    if (!k) return 0;
    if (n < k) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    for (DWORD i = 0; i < k; i++) buf[i] = (char)out[i];
    return k;
}
