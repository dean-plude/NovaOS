/*
 * newdev.dll — installing driver packages: DiInstallDriver,
 * DiUninstallDriver, UpdateDriverForPlugAndPlayDevices.
 *
 * The package is staged in the driver store and published as an oemN.inf
 * by setupapi (driver.c).  Installing it on a device needs a device whose
 * hardware ID the INF names; NovaOS's devices have NovaOS's own drivers
 * and it does not load Windows kernel-mode drivers, so no device takes a
 * package: DiInstallDriver stages it (as Windows does when the device is
 * not plugged in) and UpdateDriverForPlugAndPlayDevices answers
 * ERROR_NO_SUCH_DEVINST (as Windows does for a device that is not present).
 */
#include <windows.h>
#include <string.h>

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

NEWDEV BOOL WINAPI UpdateDriverForPlugAndPlayDevicesW(HWND parent, LPCWSTR hwid, LPCWSTR inf, DWORD flags, PBOOL reboot)
{
    (void)parent; (void)flags;
    if (reboot) *reboot = FALSE;
    if (!hwid || !*hwid || !inf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    HANDLE h = SetupOpenInfFileW(inf, NULL, 2 /* INF_STYLE_WIN4 */, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    SetupCloseInfFile(h);
    SetLastError(ERROR_NO_SUCH_DEVINST_);           /* no device with that hardware ID */
    return FALSE;
}

NEWDEV BOOL WINAPI UpdateDriverForPlugAndPlayDevicesA(HWND parent, LPCSTR hwid, LPCSTR inf, DWORD flags, PBOOL reboot)
{
    WCHAR wh[512], wi[MAX_PATH];
    if (!hwid || !inf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    wide(hwid, wh, 512);
    wide(inf, wi, MAX_PATH);
    return UpdateDriverForPlugAndPlayDevicesW(parent, wh, wi, flags, reboot);
}
