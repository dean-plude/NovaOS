/*
 * newdev.dll — installing driver packages: DiInstallDriver,
 * DiUninstallDriver, UpdateDriverForPlugAndPlayDevices.
 *
 * The package is staged in the driver store and published as an oemN.inf
 * by setupapi (driver.c).  DiInstallDriver only stages it (as Windows does
 * when no device it matches is plugged in).  UpdateDriverForPlugAndPlay-
 * Devices also installs it on each present device (PCI, USB, or a bus
 * driver's child such as a USB serial converter's COM port) whose hardware or
 * compatible IDs hold the given ID and that no NovaOS driver runs
 * (setupapi's NovaInstallDeviceDriverW writes the device's keys and the
 * service), then has the kernel's PnP manager start it (kernel/nt/pnp.c:
 * AddDevice, IRP_MN_START_DEVICE); a device whose Windows driver it
 * replaces is removed from that driver first.  No such device:
 * ERROR_NO_SUCH_DEVINST, as on Windows.
 */
#include <windows.h>
#include <string.h>
#include "../include/novapnp.h"

#ifndef ERROR_DEVICE_IN_USE
#define ERROR_DEVICE_IN_USE 2404
#endif
#ifndef ERROR_NOT_FOUND
#define ERROR_NOT_FOUND 1168
#endif

#define NEWDEV __declspec(dllexport)
#define ERROR_NO_SUCH_DEVINST_ 0xE000020B
#define SPOST_PATH 1

__declspec(dllimport) BOOL WINAPI SetupCopyOEMInfW(PCWSTR src, PCWSTR media_loc, DWORD media_type, DWORD style,
                                                   PWSTR dest, DWORD size, PDWORD need, PWSTR *component);
__declspec(dllimport) BOOL WINAPI SetupUninstallOEMInfW(PCWSTR inf_name, DWORD flags, PVOID reserved);
__declspec(dllimport) HANDLE WINAPI SetupOpenInfFileW(LPCWSTR name, LPCWSTR cls, DWORD style, PUINT err_line);
__declspec(dllimport) void WINAPI SetupCloseInfFile(HANDLE h);
__declspec(dllimport) BOOL WINAPI NovaInstallDeviceDriverW(LPCWSTR inf_path, LPCWSTR instance, LPCWSTR hwid);

static void wide(const char *s, WCHAR *out, int cap) { MultiByteToWideChar(CP_ACP, 0, s, -1, out, cap); }

NEWDEV BOOL WINAPI DiInstallDriverW(HWND parent, LPCWSTR inf, DWORD flags, PBOOL reboot)
{
    (void)parent; (void)flags;
    if (reboot) *reboot = FALSE;
    if (!inf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    WCHAR dest[MAX_PATH];
    if (!SetupCopyOEMInfW(inf, NULL, SPOST_PATH, 0, dest, MAX_PATH, NULL, NULL)) return FALSE;
    SetLastError(0);
    return TRUE;
}

NEWDEV BOOL WINAPI DiInstallDriverA(HWND parent, LPCSTR inf, DWORD flags, PBOOL reboot)
{
    WCHAR w[MAX_PATH];
    if (!inf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    wide(inf, w, MAX_PATH);
    return DiInstallDriverW(parent, w, flags, reboot);
}

/* The published oemN.inf holding the same bytes as @inf (*@name) */
static BOOL find_published(LPCWSTR inf, WCHAR *name)
{
    HANDLE f = CreateFileW(inf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    DWORD n = GetFileSize(f, NULL), got;
    BYTE *a = HeapAlloc(GetProcessHeap(), 0, n + 1), *b = HeapAlloc(GetProcessHeap(), 0, n + 1);
    BOOL ok = a && b && ReadFile(f, a, n, &got, NULL) && got == n, found = FALSE;
    CloseHandle(f);
    WCHAR dir[MAX_PATH], pat[MAX_PATH], p[MAX_PATH];
    UINT k = GetWindowsDirectoryW(dir, MAX_PATH);
    if (!k || k >= MAX_PATH - 20) lstrcpyW(dir, L"C:\\Windows");
    lstrcatW(dir, L"\\INF\\");
    lstrcpyW(pat, dir);
    lstrcatW(pat, L"oem*.inf");
    WIN32_FIND_DATAW fd;
    HANDLE h = ok ? FindFirstFileW(pat, &fd) : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.nFileSizeLow != n || fd.nFileSizeHigh) continue;
            lstrcpyW(p, dir);
            lstrcatW(p, fd.cFileName);
            HANDLE g = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (g == INVALID_HANDLE_VALUE) continue;
            found = ReadFile(g, b, n, &got, NULL) && got == n && !memcmp(a, b, n);
            CloseHandle(g);
            if (found) lstrcpynW(name, fd.cFileName, MAX_PATH);
        } while (!found && FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (a) HeapFree(GetProcessHeap(), 0, a);
    if (b) HeapFree(GetProcessHeap(), 0, b);
    return found;
}

NEWDEV BOOL WINAPI DiUninstallDriverW(HWND parent, LPCWSTR inf, DWORD flags, PBOOL reboot)
{
    (void)parent; (void)flags;
    if (reboot) *reboot = FALSE;
    if (!inf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    WCHAR name[MAX_PATH];
    if (!find_published(inf, name)) { SetLastError(ERROR_NOT_FOUND); return FALSE; }
    return SetupUninstallOEMInfW(name, 0, NULL);
}

NEWDEV BOOL WINAPI DiUninstallDriverA(HWND parent, LPCSTR inf, DWORD flags, PBOOL reboot)
{
    WCHAR w[MAX_PATH];
    if (!inf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    wide(inf, w, MAX_PATH);
    return DiUninstallDriverW(parent, w, flags, reboot);
}

/* REG_MULTI_SZ @m holds @id (ASCII case ignored) */
static BOOL multi_has(const char *m, const WCHAR *id)
{
    for (; *m; m += lstrlenA(m) + 1) {
        WCHAR w[160];
        wide(m, w, 160);
        if (!lstrcmpiW(w, id)) return TRUE;
    }
    return FALSE;
}

NEWDEV BOOL WINAPI UpdateDriverForPlugAndPlayDevicesW(HWND parent, LPCWSTR hwid, LPCWSTR inf, DWORD flags, PBOOL reboot)
{
    (void)parent; (void)flags;
    if (reboot) *reboot = FALSE;
    if (!hwid || !*hwid || !inf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    HANDLE h = SetupOpenInfFileW(inf, NULL, 2 /* INF_STYLE_WIN4 */, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    SetupCloseInfFile(h);
    WCHAR published[MAX_PATH];
    if (!SetupCopyOEMInfW(inf, NULL, SPOST_PATH, 0, published, MAX_PATH, NULL, NULL)) return FALSE;
    int done = 0;
    DWORD err = ERROR_NO_SUCH_DEVINST_;
    NovaPnpNode n;
    for (DWORD id = 1, count = nova_pnp_count(); id <= count; id++) {
        if (!nova_pnp_node(id, &n) || !(n.flags & NOVA_PNP_PRESENT)) continue;
        if (!multi_has(n.hwids, hwid) && !multi_has(n.compat, hwid)) continue;
        BOOL windows = (n.flags & NOVA_PNP_WINDOWS) != 0;
        /* (a NovaOS driver runs it: only an inbox one steps aside, at start) */
        if ((n.flags & NOVA_PNP_STARTED) && !windows && !(n.flags & NOVA_PNP_INBOX)) continue;
        if ((n.flags & NOVA_PNP_STARTED) && windows && nova_pnp_stop(id) < 0) { err = ERROR_DEVICE_IN_USE; continue; }
        WCHAR inst[128];
        wide(n.instance, inst, 128);
        if (!NovaInstallDeviceDriverW(published, inst, hwid)) { err = GetLastError(); continue; }
        /* (a driver that fails to start leaves the device installed with
         * its problem code, as on Windows) */
        if (nova_pnp_start(id) == (DWORD)-1) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
        done++;
    }
    if (!done) { SetLastError(err); return FALSE; }
    SetLastError(0);
    return TRUE;
}

NEWDEV BOOL WINAPI UpdateDriverForPlugAndPlayDevicesA(HWND parent, LPCSTR hwid, LPCSTR inf, DWORD flags, PBOOL reboot)
{
    WCHAR wh[512], wi[MAX_PATH];
    if (!hwid || !inf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    wide(hwid, wh, 512);
    wide(inf, wi, MAX_PATH);
    return UpdateDriverForPlugAndPlayDevicesW(parent, wh, wi, flags, reboot);
}
