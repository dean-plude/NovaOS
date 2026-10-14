/*
 * fmifs.dll — formatting a volume: FormatEx, as format.com and Disk
 * Management call it
 *
 * The volume ("E:\" or "\\?\Volume{...}\") is opened and the kernel
 * formats it (NovaOS's IOCTL_NOVA_VOLUME_FORMAT, kernel/um/um_disk.c):
 * NTFS or FAT32, always a quick format; the cluster size is the file
 * system's default.  The callback hears PROGRESS (0, then 100) and DONE
 * with the outcome, or FSNOTSUPPORTED, VOLUMEINUSE or INSUFFICIENTRIGHTS
 * first when the volume can't be formatted.
 */

#include <windows.h>
#include <winioctl.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

typedef enum {
    PROGRESS = 0, DONEWITHSTRUCTURE = 1, INSUFFICIENTRIGHTS = 6, FSNOTSUPPORTED = 7, VOLUMEINUSE = 8,
    DONE = 11, OUTPUT = 14, STRUCTUREPROGRESS = 15, CLUSTERSIZETOOSMALL = 16
} CALLBACKCOMMAND;
typedef BOOLEAN (WINAPI *PFMIFSCALLBACK)(CALLBACKCOMMAND command, DWORD sub, PVOID info);

static int ieq_ascii(const WCHAR *a, const char *b)
{
    for (; *a && *b; a++, b++) if ((*a | 0x20) != (*b | 0x20)) return 0;
    return !*a && !*b;
}

/* The device to open for @root: "\\.\E:" or the volume name without its backslash */
static int device_of(const WCHAR *root, WCHAR *out, int cap)
{
    int n = 0;
    while (root[n]) n++;
    if (n >= 2 && root[1] == ':' && (n == 2 || (n == 3 && root[2] == '\\'))) {
        static const WCHAR pre[] = { '\\', '\\', '.', '\\' };
        for (int i = 0; i < 4; i++) out[i] = pre[i];
        out[4] = root[0]; out[5] = ':'; out[6] = 0;
        return 1;
    }
    if (n < 4 || n >= cap || root[0] != '\\' || root[1] != '\\') return 0;
    for (int i = 0; i <= n; i++) out[i] = root[i];
    if (out[n - 1] == '\\') out[n - 1] = 0;
    return 1;
}

EXPORT VOID WINAPI FormatEx(PWCHAR root, DWORD media, PWCHAR format, PWCHAR label, BOOL quick, DWORD cluster,
                            PFMIFSCALLBACK callback)
{
    (void)media; (void)quick; (void)cluster;
    BOOLEAN ok = FALSE;
    DWORD percent = 0;
    WCHAR dev[MAX_PATH];
    NOVA_VOLUME_FORMAT f;
    memset(&f, 0, sizeof(f));
    if (!format || ieq_ascii(format, "NTFS")) f.FileSystem = 0;
    else if (ieq_ascii(format, "FAT32")) f.FileSystem = 1;
    else {
        if (callback) callback(FSNOTSUPPORTED, 0, NULL);
        if (callback) callback(DONE, 0, &ok);
        return;
    }
    for (int i = 0; label && label[i] && i < 32; i++) f.Label[i] = label[i];
    if (!root || !device_of(root, dev, MAX_PATH)) {
        if (callback) callback(DONE, 0, &ok);
        return;
    }
    if (callback) callback(PROGRESS, 0, &percent);
    HANDLE h = CreateFileW(dev, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    DWORD got = 0;
    if (h == INVALID_HANDLE_VALUE) {
        if (callback) callback(GetLastError() == ERROR_ACCESS_DENIED ? INSUFFICIENTRIGHTS : VOLUMEINUSE, 0, NULL);
    } else {
        ok = DeviceIoControl(h, IOCTL_NOVA_VOLUME_FORMAT, &f, sizeof(f), 0, 0, &got, 0) != 0;
        DWORD err = GetLastError();
        CloseHandle(h);
        if (!ok && callback) callback(err == ERROR_ACCESS_DENIED ? VOLUMEINUSE : err == ERROR_INVALID_PARAMETER ?
                                      CLUSTERSIZETOOSMALL : DONEWITHSTRUCTURE, 0, NULL);
        if (!ok) SetLastError(err);
    }
    if (ok && callback) {
        percent = 100;
        callback(PROGRESS, 0, &percent);
        callback(STRUCTUREPROGRESS, 0, NULL);
    }
    if (callback) callback(DONE, 0, &ok);
}

/* The file systems FormatEx makes, by index: "NTFS", "FAT32" */
EXPORT BOOLEAN WINAPI QueryAvailableFileSystemFormat(DWORD index, PWCHAR name, PUCHAR major, PUCHAR minor, BOOLEAN *latest)
{
    static const char *fs[] = { "NTFS", "FAT32" };
    if (index >= 2) return FALSE;
    int i = 0;
    for (; fs[index][i]; i++) name[i] = (WCHAR)fs[index][i];
    name[i] = 0;
    if (major) *major = index ? 0 : 3;
    if (minor) *minor = index ? 0 : 1;
    if (latest) *latest = TRUE;
    return TRUE;
}
