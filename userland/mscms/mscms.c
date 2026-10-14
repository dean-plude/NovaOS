/*
 * mscms.dll — Windows color management.  NovaOS installs no color
 * profiles: the color directory is reported (programs list the .icc
 * files in it; Inkscape asks for it at start) and holds none, so programs
 * fall back to sRGB, as on a Windows machine without monitor profiles.
 */
#include <windows.h>

#define MSCMSAPI __declspec(dllexport)

static const WCHAR g_dir[] = L"C:\\Windows\\System32\\spool\\drivers\\color";

MSCMSAPI BOOL WINAPI GetColorDirectoryW(LPCWSTR machine, LPWSTR buf, PDWORD size)
{
    DWORD need = sizeof(g_dir);
    if (machine || !size) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (!buf || *size < need) {
        *size = need;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (DWORD i = 0; i < need / sizeof(WCHAR); i++) buf[i] = g_dir[i];
    *size = need;
    return TRUE;
}

MSCMSAPI BOOL WINAPI GetColorDirectoryA(LPCSTR machine, LPSTR buf, PDWORD size)
{
    DWORD need = sizeof(g_dir) / sizeof(WCHAR);
    if (machine || !size) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (!buf || *size < need) {
        *size = need;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (DWORD i = 0; i < need; i++) buf[i] = (char)g_dir[i];
    *size = need;
    return TRUE;
}

/* No installed profiles: an empty list */
static BOOL no_profiles(PDWORD size, PDWORD count)
{
    if (!size) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *size = 0;
    if (count) *count = 0;
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}
MSCMSAPI BOOL WINAPI EnumColorProfilesW(LPCWSTR machine, void *record, PBYTE buf, PDWORD size, PDWORD count)
{ (void)machine; (void)record; (void)buf; return no_profiles(size, count); }
MSCMSAPI BOOL WINAPI EnumColorProfilesA(LPCSTR machine, void *record, PBYTE buf, PDWORD size, PDWORD count)
{ (void)machine; (void)record; (void)buf; return no_profiles(size, count); }

/* No default or standard profiles either: GIMP asks for the monitor's and
 * falls back to sRGB when there is none */
MSCMSAPI BOOL WINAPI WcsGetUsePerUserProfiles(LPCWSTR device, DWORD dev_class, PBOOL use)
{
    (void)device; (void)dev_class;
    if (!use) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *use = FALSE;
    return TRUE;
}
MSCMSAPI BOOL WINAPI WcsGetDefaultColorProfileSize(DWORD scope, LPCWSTR device, DWORD type, DWORD subtype,
                                                    DWORD id, PDWORD size)
{
    (void)scope; (void)device; (void)type; (void)subtype; (void)id;
    if (!size) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *size = 0;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}
MSCMSAPI BOOL WINAPI WcsGetDefaultColorProfile(DWORD scope, LPCWSTR device, DWORD type, DWORD subtype,
                                                DWORD id, DWORD size, LPWSTR name)
{
    (void)scope; (void)device; (void)type; (void)subtype; (void)id; (void)size;
    if (name && size >= sizeof(WCHAR)) name[0] = 0;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}
MSCMSAPI BOOL WINAPI GetStandardColorSpaceProfileW(LPCWSTR machine, DWORD id, LPWSTR name, PDWORD size)
{
    (void)machine; (void)id; (void)name;
    if (!size) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}
MSCMSAPI BOOL WINAPI GetStandardColorSpaceProfileA(LPCSTR machine, DWORD id, LPSTR name, PDWORD size)
{
    (void)machine; (void)id; (void)name;
    if (!size) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}
