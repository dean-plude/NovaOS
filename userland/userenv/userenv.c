/* userenv.dll — users' profiles and environment blocks
 *
 * A profile is the account's folder (C:\Users\<name>; the first account's
 * is C:\, as before NovaOS had accounts) and its registry hive
 * (HKEY_USERS\<SID>), both kept by the kernel (kernel/um/um_accounts.c,
 * NtNovaAccount): LoadUserProfile makes them at the first sign-in,
 * DeleteProfile removes them.  An environment block for a token has that
 * account's USERNAME, USERPROFILE, APPDATA, LOCALAPPDATA and TEMP. */
#include <winternl.h>
#include <windows.h>

#define UEAPI __declspec(dllexport)

typedef struct {                        /* PROFILEINFOW / PROFILEINFOA (the strings' type aside) */
    DWORD dwSize, dwFlags;
    void *lpUserName, *lpProfilePath, *lpDefaultPath, *lpServerName, *lpPolicyPath;
    HANDLE hProfile;
} ProfileInfo;

static BOOL put(const WCHAR *s, int n, LPWSTR out, LPDWORD cap)
{
    if (!out || *cap <= (DWORD)n) { *cap = (DWORD)n + 1; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    for (int i = 0; i <= n; i++) out[i] = s[i];
    *cap = (DWORD)n + 1;
    return TRUE;
}

static int env_or(const char *var, const char *dflt, WCHAR *w)
{
    char a[MAX_PATH];
    DWORD n = GetEnvironmentVariableA(var, a, MAX_PATH);
    const char *s = n && n < MAX_PATH ? a : dflt;
    int k = MultiByteToWideChar(CP_UTF8, 0, s, -1, w, MAX_PATH);
    return k > 0 ? k - 1 : 0;
}

/* @a then @b and @c in @out (user32's wsprintf is not one of this DLL's) */
static void join(char *out, int cap, const char *a, const char *b, const char *c)
{
    const char *parts[3] = { a, b, c };
    int o = 0;
    for (int i = 0; i < 3; i++)
        for (const char *q = parts[i]; *q && o < cap - 1; q++) out[o++] = *q;
    out[o] = 0;
}

/* The local account @token is (the caller's own when 0): false if none */
static BOOL token_account(HANDLE token, NOVA_ACCOUNT *a)
{
    HANDLE t = token;
    if (!t && !OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &t) && !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t))
        return FALSE;
    ULONG_PTR buf[16];
    DWORD got;
    BOOL ok = GetTokenInformation(t, TokenUser, buf, sizeof(buf), &got);
    if (t != token) CloseHandle(t);
    if (!ok) return FALSE;
    const BYTE *sid = *(const BYTE **)buf;
    static const BYTE domain[] = { 1, 5, 0, 0, 0, 0, 0, 5, 21, 0, 0, 0, 0xE8, 3, 0, 0, 0xD0, 7, 0, 0, 0xB8, 0x0B, 0, 0 };
    for (int i = 0; i < (int)sizeof(domain); i++) if (sid[i] != domain[i]) return FALSE;
    NOVA_ACCOUNT_REQUEST rq;
    ZeroMemory(&rq, sizeof(rq));
    CopyMemory(&rq.Rid, sid + 24, 4);
    ULONG n;
    return !NtNovaAccount(NOVA_ACCOUNT_GET, &rq, sizeof(rq), a, sizeof(*a), &n);
}

/* @a's profile folder (C:\Users\<name> until its first sign-in makes it) */
static void profile_of(const NOVA_ACCOUNT *a, char *out, int cap)
{
    if (a->Profile[0]) lstrcpynA(out, a->Profile, cap);
    else join(out, cap, "C:\\Users\\", a->Name, "");
}

static BOOL profile_dir(HANDLE token, LPWSTR dir, LPDWORD n)
{
    NOVA_ACCOUNT a;
    WCHAR w[MAX_PATH];
    int k;
    if (token_account(token, &a)) {
        char p[MAX_PATH];
        profile_of(&a, p, MAX_PATH);
        k = MultiByteToWideChar(CP_UTF8, 0, p, -1, w, MAX_PATH) - 1;
    } else {
        k = env_or("USERPROFILE", "C:\\", w);
    }
    if (k > 3 && w[k - 1] == '\\') w[--k] = 0;
    return put(w, k, dir, n);
}

UEAPI BOOL WINAPI GetUserProfileDirectoryW(HANDLE token, LPWSTR dir, LPDWORD n) { return profile_dir(token, dir, n); }

UEAPI BOOL WINAPI GetUserProfileDirectoryA(HANDLE token, LPSTR dir, LPDWORD n)
{
    WCHAR w[MAX_PATH];
    DWORD k = MAX_PATH;
    if (!profile_dir(token, w, &k)) return FALSE;
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    if (!dir || *n < (DWORD)need) { *n = (DWORD)need; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, dir, need, 0, 0);
    *n = (DWORD)need;
    return TRUE;
}

UEAPI BOOL WINAPI GetProfilesDirectoryW(LPWSTR dir, LPDWORD n) { static const WCHAR root[] = L"C:\\Users"; return put(root, 8, dir, n); }
UEAPI BOOL WINAPI GetDefaultUserProfileDirectoryW(LPWSTR dir, LPDWORD n) { WCHAR w[MAX_PATH]; return put(w, env_or("USERPROFILE", "C:\\", w), dir, n); }
UEAPI BOOL WINAPI GetAllUsersProfileDirectoryW(LPWSTR dir, LPDWORD n) { WCHAR w[MAX_PATH]; return put(w, env_or("ALLUSERSPROFILE", "C:\\ProgramData", w), dir, n); }

static int wieq_name(const WCHAR *entry, const char *name)
{
    int i = 0;
    for (; name[i]; i++) {
        WCHAR a = entry[i], b = (WCHAR)name[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return entry[i] == '=';
}

/* This process's environment with @token's account's variables in place
 * of the caller's (the same block when the token is no local account's) */
UEAPI BOOL WINAPI CreateEnvironmentBlock(LPVOID *env, HANDLE token, BOOL inherit)
{
    (void)inherit;
    static const char *const names[] = { "USERNAME", "USERPROFILE", "HOMEPATH", "APPDATA", "LOCALAPPDATA", "TEMP", "TMP" };
    enum { NV = sizeof(names) / sizeof(names[0]) };
    char vals[NV][MAX_PATH];
    NOVA_ACCOUNT a;
    BOOL mine = token && token_account(token, &a);
    if (mine) {
        char p[MAX_PATH];
        profile_of(&a, p, MAX_PATH);
        BOOL legacy = !lstrcmpA(p, "C:\\");             /* (the first account: C:\, C:\AppData, C:\Temp) */
        const char *base = legacy ? "C:" : p;
        lstrcpynA(vals[0], a.Name, MAX_PATH);
        lstrcpynA(vals[1], p, MAX_PATH);
        lstrcpynA(vals[2], legacy ? "\\" : p + 2, MAX_PATH);
        join(vals[3], MAX_PATH, base, "\\AppData\\Roaming", "");
        join(vals[4], MAX_PATH, base, "\\AppData\\Local", "");
        join(vals[5], MAX_PATH, legacy ? "C:\\Temp" : p, legacy ? "" : "\\AppData\\Local\\Temp", "");
        lstrcpyA(vals[6], vals[5]);
    }
    LPWSTR src = GetEnvironmentStringsW();
    SIZE_T n = 0;
    while (src[n] || src[n + 1]) n++;
    n += 2;
    WCHAR *copy = LocalAlloc(0, 2 * (n + NV * (MAX_PATH + 16)));
    if (!copy) { FreeEnvironmentStringsW(src); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    SIZE_T o = 0;
    for (const WCHAR *e = src; *e; ) {
        SIZE_T len = 0;
        while (e[len]) len++;
        BOOL drop = FALSE;
        for (int i = 0; mine && i < NV && !drop; i++) drop = wieq_name(e, names[i]);
        if (!drop) { CopyMemory(copy + o, e, 2 * (len + 1)); o += len + 1; }
        e += len + 1;
    }
    for (int i = 0; mine && i < NV; i++) {
        char line[MAX_PATH + 16];
        join(line, sizeof(line), names[i], "=", vals[i]);
        o += (SIZE_T)MultiByteToWideChar(CP_UTF8, 0, line, -1, copy + o, MAX_PATH + 16);
    }
    copy[o++] = 0;
    if (o == 1) copy[o++] = 0;
    FreeEnvironmentStringsW(src);
    *env = copy;
    return TRUE;
}

UEAPI BOOL WINAPI DestroyEnvironmentBlock(LPVOID env) { LocalFree(env); return TRUE; }

UEAPI BOOL WINAPI ExpandEnvironmentStringsForUserW(HANDLE token, LPCWSTR s, LPWSTR out, DWORD n)
{
    (void)token;
    DWORD r = ExpandEnvironmentStringsW(s, out, n);
    return r && r <= n;
}

/* The account's folder and hive, made at its first sign-in; hProfile is
 * its HKEY_USERS\<SID> key (UnloadUserProfile closes it) */
static BOOL load_profile(HANDLE token, ProfileInfo *info)
{
    NOVA_ACCOUNT a;
    if (!info || info->dwSize < sizeof(ProfileInfo)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (!token_account(token, &a)) { SetLastError(1317 /* ERROR_NO_SUCH_USER */); return FALSE; }
    NOVA_ACCOUNT_REQUEST rq;
    ZeroMemory(&rq, sizeof(rq));
    rq.Rid = a.Rid;
    ULONG got;
    NTSTATUS st = NtNovaAccount(NOVA_ACCOUNT_LOAD_PROFILE, &rq, sizeof(rq), &a, sizeof(a), &got);
    if (st) { SetLastError(RtlNtStatusToDosError(st)); return FALSE; }
    WCHAR key[64];
    static const WCHAR pre[] = L"S-1-5-21-1000-2000-3000-";
    int k = 0;
    for (; pre[k]; k++) key[k] = pre[k];
    char digits[12];
    int nd = 0;
    for (ULONG r = a.Rid; r || !nd; r /= 10) digits[nd++] = (char)('0' + r % 10);
    while (nd) key[k++] = (WCHAR)digits[--nd];
    key[k] = 0;
    HKEY h;
    LSTATUS e = RegCreateKeyExW(HKEY_USERS, key, 0, 0, 0, KEY_ALL_ACCESS, 0, &h, 0);
    if (e) { SetLastError((DWORD)e); return FALSE; }
    info->hProfile = h;
    return TRUE;
}

UEAPI BOOL WINAPI LoadUserProfileW(HANDLE token, LPVOID info) { return load_profile(token, info); }
UEAPI BOOL WINAPI LoadUserProfileA(HANDLE token, LPVOID info) { return load_profile(token, info); }
UEAPI BOOL WINAPI UnloadUserProfile(HANDLE token, HANDLE profile) { (void)token; if (profile) RegCloseKey(profile); return TRUE; }

/* DeleteProfile(SID string, path, computer): a local account's profile */
static BOOL delete_profile(const WCHAR *sid)
{
    static const WCHAR pre[] = L"S-1-5-21-1000-2000-3000-";
    int i = 0;
    for (; pre[i]; i++) if (!sid || sid[i] != pre[i]) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    ULONG rid = 0;
    for (; sid[i] >= '0' && sid[i] <= '9'; i++) rid = rid * 10 + (ULONG)(sid[i] - '0');
    if (sid[i]) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    NOVA_ACCOUNT_REQUEST rq;
    ZeroMemory(&rq, sizeof(rq));
    rq.Rid = rid;
    ULONG got;
    NTSTATUS st = NtNovaAccount(NOVA_ACCOUNT_DELETE_PROFILE, &rq, sizeof(rq), 0, 0, &got);
    if (st) { SetLastError(RtlNtStatusToDosError(st)); return FALSE; }
    return TRUE;
}

UEAPI BOOL WINAPI DeleteProfileW(LPCWSTR sid, LPCWSTR path, LPCWSTR computer) { (void)path; (void)computer; return delete_profile(sid); }
UEAPI BOOL WINAPI DeleteProfileA(LPCSTR sid, LPCSTR path, LPCSTR computer)
{
    (void)path; (void)computer;
    WCHAR w[64];
    if (!sid || !MultiByteToWideChar(CP_UTF8, 0, sid, -1, w, 64)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return delete_profile(w);
}

/* AppContainers: a container's SID is S-1-15-2- and seven numbers hashed
 * from its name (SHA-256 on Windows; here a simple hash: the SID only
 * has to be stable per name, and NovaOS enforces no containers) */
UEAPI HRESULT WINAPI DeriveAppContainerSidFromAppContainerName(LPCWSTR name, PSID *sid)
{
    if (!name || !sid) return E_INVALIDARG;
    DWORD h[7] = { 0x811C9DC5u, 0x01000193u, 0x2545F491u, 0x9E3779B9u, 0x85EBCA6Bu, 0xC2B2AE35u, 0x27D4EB2Fu };
    for (const WCHAR *c = name; *c; c++) {
        WCHAR ch = *c >= 'A' && *c <= 'Z' ? (WCHAR)(*c + 32) : *c;
        for (int i = 0; i < 7; i++) h[i] = (h[i] ^ ch) * 0x01000193u + (DWORD)i;
    }
    SID_IDENTIFIER_AUTHORITY app = { { 0, 0, 0, 0, 0, 15 } };
    return AllocateAndInitializeSid(&app, 8, 2, h[0], h[1], h[2], h[3], h[4], h[5], h[6], sid) ? S_OK : E_OUTOFMEMORY;
}

/* AppContainer profiles, kept where Windows keeps them: the mapping from
 * the container's SID to its name under HKCU\...\AppContainer\Mappings
 * and its folder %LOCALAPPDATA%\Packages\NAME.  NovaOS runs no process
 * inside a container, so a profile is only this record. */
#define AC_MAPPINGS L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Mappings\\"

static int ac_key(PSID sid, WCHAR *key, int cap)
{
    LPWSTR s = 0;
    if (!ConvertSidToStringSidW(sid, &s)) return 0;
    int n = lstrlenW(AC_MAPPINGS), k = lstrlenW(s);
    if (n + k >= cap) { LocalFree(s); return 0; }
    lstrcpyW(key, AC_MAPPINGS);
    lstrcpyW(key + n, s);
    LocalFree(s);
    return 1;
}

static int ac_folder(LPCWSTR name, WCHAR *dir, int cap, int create)
{
    WCHAR base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH || n + 10 + lstrlenW(name) >= (DWORD)cap) return 0;
    lstrcpyW(dir, base);
    lstrcatW(dir, L"\\Packages");
    if (create) CreateDirectoryW(dir, 0);
    lstrcatW(dir, L"\\");
    lstrcatW(dir, name);
    return 1;
}

static int ac_name_ok(LPCWSTR name)
{
    int n = name ? lstrlenW(name) : 0;
    if (n < 2 || n > 64) return 0;
    for (int i = 0; i < n; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') || name[i] == '.' || name[i] == '-' || name[i] == '_'))
            return 0;
    return 1;
}

UEAPI HRESULT WINAPI CreateAppContainerProfile(LPCWSTR name, LPCWSTR display, LPCWSTR description, PVOID caps,
                                               DWORD ncaps, PSID *sid)
{
    (void)caps; (void)ncaps;
    if (!sid) return E_INVALIDARG;
    *sid = 0;
    if (!ac_name_ok(name) || !display || !description) return E_INVALIDARG;
    PSID s;
    HRESULT hr = DeriveAppContainerSidFromAppContainerName(name, &s);
    if (FAILED(hr)) return hr;
    WCHAR key[512], dir[MAX_PATH];
    HKEY k;
    DWORD how = 0;
    if (!ac_key(s, key, 512) || RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, 0, 0, KEY_ALL_ACCESS, 0, &k, &how)) {
        FreeSid(s);
        return E_FAIL;
    }
    if (how == REG_OPENED_EXISTING_KEY) {
        RegCloseKey(k);
        FreeSid(s);
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    RegSetValueExW(k, L"Moniker", 0, REG_SZ, (const BYTE *)name, (DWORD)(lstrlenW(name) + 1) * 2);
    RegSetValueExW(k, L"DisplayName", 0, REG_SZ, (const BYTE *)display, (DWORD)(lstrlenW(display) + 1) * 2);
    RegSetValueExW(k, L"Description", 0, REG_SZ, (const BYTE *)description, (DWORD)(lstrlenW(description) + 1) * 2);
    RegCloseKey(k);
    if (ac_folder(name, dir, MAX_PATH, 1)) {
        CreateDirectoryW(dir, 0);
        lstrcatW(dir, L"\\AC");
        CreateDirectoryW(dir, 0);
    }
    *sid = s;
    return S_OK;
}

UEAPI HRESULT WINAPI DeleteAppContainerProfile(LPCWSTR name)
{
    if (!ac_name_ok(name)) return E_INVALIDARG;
    PSID s;
    HRESULT hr = DeriveAppContainerSidFromAppContainerName(name, &s);
    if (FAILED(hr)) return hr;
    WCHAR key[512], dir[MAX_PATH];
    int ok = ac_key(s, key, 512);
    FreeSid(s);
    if (ok) RegDeleteKeyW(HKEY_CURRENT_USER, key);          /* (gone already is not an error) */
    if (ac_folder(name, dir, MAX_PATH, 0)) {
        int n = lstrlenW(dir);
        lstrcatW(dir, L"\\AC");
        RemoveDirectoryW(dir);
        dir[n] = 0;
        RemoveDirectoryW(dir);
    }
    return S_OK;
}

/* Group Policy: NovaOS applies none, so nothing ever holds the policy
 * section for writing; the "section" handed out is a handle that only has
 * to close */
UEAPI HANDLE WINAPI EnterCriticalPolicySection(BOOL machine)
{
    (void)machine;
    HANDLE h = CreateEventW(0, TRUE, TRUE, 0);
    return h ? h : 0;
}
UEAPI BOOL WINAPI LeaveCriticalPolicySection(HANDLE section)
{
    if (!section) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return CloseHandle(section);
}

/* every NovaOS profile is a local one: 0 is a local profile (no PT_TEMPORARY,
 * PT_ROAMING or PT_MANDATORY bit) */
UEAPI BOOL WINAPI GetProfileType(DWORD *flags)
{
    if (!flags) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *flags = 0;
    return TRUE;
}

/* Group Policy change events: NovaOS applies no policy, so a registered
 * event is never signalled; registering and unregistering only check it */
UEAPI BOOL WINAPI RegisterGPNotification(HANDLE event, BOOL machine)
{
    (void)machine;
    if (!event) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return TRUE;
}
UEAPI BOOL WINAPI UnregisterGPNotification(HANDLE event)
{
    if (!event) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return TRUE;
}
