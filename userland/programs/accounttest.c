/* accounttest.exe — local accounts and sign-in (kernel/um/um_accounts.c):
 * a second account made with NetUserAdd, LogonUser refusing a wrong
 * password and signing it in with the right one, its own SID, profile
 * (C:\Users\<name>, which other users' restricted tokens cannot open),
 * HKEY_CURRENT_USER and environment, a program run as it with
 * CreateProcessAsUser, a password change, and the lock screen
 * (LockWorkStation: the test harness types a wrong password there, then
 * the right one).  The account and its profile are deleted at the end.
 *
 *   accounttest          the test (as administrator: its manifest asks)
 *   accounttest child    what runs as the new account */
#include <stdio.h>
#include <string.h>
#include <winternl.h>
#include <windows.h>
#include <wchar.h>

/* (beyond NovaOS's headers) */
__declspec(dllimport) BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *out);
__declspec(dllimport) BOOL WINAPI LogonUserW(LPCWSTR user, LPCWSTR domain, LPCWSTR pass, DWORD type, DWORD prov, PHANDLE token);
__declspec(dllimport) BOOL WINAPI CreateProcessAsUserW(HANDLE token, LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
                                                       LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env,
                                                       LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi);
__declspec(dllimport) BOOL WINAPI CreateWellKnownSid(int type, PSID domain, PSID sid, DWORD *n);
#define LOGON32_LOGON_INTERACTIVE  2
#define ERROR_LOGON_FAILURE_       1326
#define ERROR_INVALID_PASSWORD_    86
#define MAX_PREFERRED_LENGTH_      ((DWORD)-1)
#define WinBuiltinAdministratorsSid_ 26
#define TokenElevationTypeDefault_ 1
#define FILE_FLAG_BACKUP_SEMANTICS_ 0x02000000

typedef DWORD NET_API_STATUS;
typedef struct { LPWSTR name, password; DWORD password_age, priv; LPWSTR home_dir, comment; DWORD flags; LPWSTR script_path; } USER_INFO_1_;
typedef struct { LPWSTR password; } USER_INFO_1003_;
__declspec(dllimport) NET_API_STATUS WINAPI NetUserAdd(LPCWSTR server, DWORD level, LPBYTE buf, LPDWORD parm_err);
__declspec(dllimport) NET_API_STATUS WINAPI NetUserDel(LPCWSTR server, LPCWSTR user);
__declspec(dllimport) NET_API_STATUS WINAPI NetUserEnum(LPCWSTR server, DWORD level, DWORD filter, LPBYTE *buf, DWORD pref,
                                                        LPDWORD read, LPDWORD total, PDWORD resume);
__declspec(dllimport) NET_API_STATUS WINAPI NetUserGetInfo(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE *buf);
__declspec(dllimport) NET_API_STATUS WINAPI NetUserSetInfo(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE buf, LPDWORD parm_err);
__declspec(dllimport) NET_API_STATUS WINAPI NetUserChangePassword(LPCWSTR domain, LPCWSTR user, LPCWSTR old_pw, LPCWSTR new_pw);
__declspec(dllimport) NET_API_STATUS WINAPI NetApiBufferFree(LPVOID buf);
typedef struct { DWORD dwSize, dwFlags; LPWSTR lpUserName, lpProfilePath, lpDefaultPath, lpServerName, lpPolicyPath; HANDLE hProfile; } PROFILEINFOW_;
__declspec(dllimport) BOOL WINAPI LoadUserProfileW(HANDLE token, PROFILEINFOW_ *info);
__declspec(dllimport) BOOL WINAPI UnloadUserProfile(HANDLE token, HANDLE profile);
__declspec(dllimport) BOOL WINAPI GetUserProfileDirectoryW(HANDLE token, LPWSTR dir, LPDWORD n);
__declspec(dllimport) BOOL WINAPI CreateEnvironmentBlock(LPVOID *env, HANDLE token, BOOL inherit);
__declspec(dllimport) BOOL WINAPI DestroyEnvironmentBlock(LPVOID env);
__declspec(dllimport) BOOL WINAPI DeleteProfileW(LPCWSTR sid, LPCWSTR path, LPCWSTR computer);

#define USER L"novatest"
#define PASS L"Nova-Pass-33"
#define PASS2 L"Nova-Pass-34"
#define LOCK_PASS L"Unlock-33"

static int pass, fail;
static void check(const char *what, int ok)
{
    if (ok) pass++;
    else { fail++; printf("FAIL: %s (error %lu)\n", what, GetLastError()); }
}

/* The account's RID (0: none) */
static DWORD rid_of(const WCHAR *name)
{
    LPBYTE buf;
    if (NetUserGetInfo(0, name, 20, &buf)) return 0;
    DWORD rid = ((DWORD *)((LPWSTR *)buf + 3))[1];         /* USER_INFO_20.usri20_user_id */
    NetApiBufferFree(buf);
    return rid;
}

static void remove_test_account(void)
{
    DWORD rid = rid_of(USER);
    if (rid) NetUserDel(0, USER);
    WCHAR sid[64];
    if (rid) { swprintf(sid, 64, L"S-1-5-21-1000-2000-3000-%lu", rid); DeleteProfileW(sid, 0, 0); }
}

static PSID token_user(HANDLE t, BYTE *buf, DWORD n)
{
    DWORD got;
    return GetTokenInformation(t, TokenUser, buf, n, &got) ? ((TOKEN_USER *)buf)->User.Sid : 0;
}

/* The screen's state: { the signed-in account's RID, locked } */
static void session(ULONG s[2])
{
    ULONG got;
    s[0] = s[1] = 0;
    NtNovaAccount(NOVA_ACCOUNT_SESSION, 0, 0, s, 2 * sizeof(ULONG), &got);
}

static int child(void)
{
    WCHAR me[64], prof[MAX_PATH], val[64];
    DWORD n = 64, bad = 0;
    if (!GetUserNameW(me, &n) || wcscmp(me, USER)) { printf("FAIL: child: GetUserName says %ls\n", me); bad++; }
    if (!GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH) || _wcsicmp(prof, L"C:\\Users\\" USER)) {
        printf("FAIL: child: USERPROFILE is %ls\n", prof);
        bad++;
    }
    DWORD sz = sizeof(val), type;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\NovaTest", L"Owner", RRF_RT_REG_SZ, &type, val, &sz) || wcscmp(val, USER)) {
        printf("FAIL: child: HKCU\\Software\\NovaTest isn't its own\n");
        bad++;
    }
    WCHAR file[MAX_PATH];
    swprintf(file, MAX_PATH, L"%ls\\Documents\\hello.txt", prof);
    HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE) { printf("FAIL: child: can't write %ls (%lu)\n", file, GetLastError()); bad++; }
    else { DWORD w; WriteFile(f, "hi", 2, &w, 0); CloseHandle(f); }
    printf("accounttest child: %s as %ls\n", bad ? "failed" : "ran", me);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "child")) return child();
    remove_test_account();                                  /* (a run that died half-way) */

    /* me: the first account, an administrator (elevated now) */
    WCHAR me[64];
    DWORD n = 64;
    check("GetUserName", GetUserNameW(me, &n));
    DWORD my_rid = rid_of(me);
    check("my account is listed", my_rid >= 1001);
    BYTE mine_buf[128], its_buf[128];
    HANDLE mine;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &mine);
    PSID my_sid = token_user(mine, mine_buf, sizeof(mine_buf));

    /* a second account, a standard user */
    USER_INFO_1_ ui = { USER, PASS, 0, 1 /* USER_PRIV_USER */, 0, 0, 0x201 /* UF_SCRIPT | UF_NORMAL_ACCOUNT */, 0 };
    DWORD perr;
    check("NetUserAdd", NetUserAdd(0, 1, (LPBYTE)&ui, &perr) == 0);
    check("NetUserAdd refuses the same name", NetUserAdd(0, 1, (LPBYTE)&ui, &perr) == 2224 /* NERR_UserExists */);
    USER_INFO_1_ bad = ui;
    bad.name = L"no/slash";
    check("NetUserAdd refuses a bad name", NetUserAdd(0, 1, (LPBYTE)&bad, &perr) == 2202 /* NERR_BadUsername */);
    DWORD rid = rid_of(USER);
    check("the new account has its own RID", rid > my_rid);
    LPBYTE list;
    DWORD read = 0, total = 0;
    int seen = 0;
    if (!NetUserEnum(0, 0, 0, &list, MAX_PREFERRED_LENGTH_, &read, &total, 0)) {
        for (DWORD i = 0; i < read; i++) seen += !_wcsicmp(((LPWSTR *)list)[i], USER) || !_wcsicmp(((LPWSTR *)list)[i], me);
        NetApiBufferFree(list);
    }
    check("NetUserEnum lists both accounts", seen == 2);
    LPBYTE info;
    check("a standard user (USER_PRIV_USER)", !NetUserGetInfo(0, USER, 1, &info) && ((USER_INFO_1_ *)info)->priv == 1);
    if (info) NetApiBufferFree(info);

    /* LogonUser */
    HANDLE t = 0;
    DWORD t0 = GetTickCount();
    check("a wrong password is refused", !LogonUserW(USER, L".", L"wrong", LOGON32_LOGON_INTERACTIVE, 0, &t) &&
                                         GetLastError() == ERROR_LOGON_FAILURE_ && !t);
    check("... after a second", GetTickCount() - t0 >= 900);
    check("an unknown user is refused", !LogonUserW(L"nobody-here", 0, PASS, LOGON32_LOGON_INTERACTIVE, 0, &t));
    check("the right password signs in", LogonUserW(USER, L".", PASS, LOGON32_LOGON_INTERACTIVE, 0, &t) && t);
    PSID its_sid = token_user(t, its_buf, sizeof(its_buf));
    check("its token is its own user", its_sid && my_sid && !EqualSid(its_sid, my_sid));
    LPWSTR str = 0;
    WCHAR want[64];
    swprintf(want, 64, L"S-1-5-21-1000-2000-3000-%lu", rid);
    check("its SID", its_sid && ConvertSidToStringSidW(its_sid, &str) && !wcscmp(str, want));
    if (str) LocalFree(str);
    WCHAR nm[64], dom[64];
    DWORD nn = 64, nd = 64;
    SID_NAME_USE use;
    check("LookupAccountSid names it", its_sid && LookupAccountSidW(0, its_sid, nm, &nn, dom, &nd, &use) &&
                                       !wcscmp(nm, USER) && use == SidTypeUser);
    DWORD et = 0;
    DWORD got;
    check("not split (a standard user)", GetTokenInformation(t, TokenElevationType, &et, sizeof(et), &got) &&
                                         et == TokenElevationTypeDefault_);
    BYTE gbuf[1024];
    BYTE admins[SECURITY_MAX_SID_SIZE];
    DWORD as = sizeof(admins);
    CreateWellKnownSid(WinBuiltinAdministratorsSid_, 0, admins, &as);
    int has_admins = 0;
    if (GetTokenInformation(t, TokenGroups, gbuf, sizeof(gbuf), &got))
        for (DWORD i = 0; i < ((TOKEN_GROUPS *)gbuf)->GroupCount; i++)
            has_admins |= EqualSid(((TOKEN_GROUPS *)gbuf)->Groups[i].Sid, admins);
    check("not in Administrators", !has_admins);

    /* its profile and hive */
    PROFILEINFOW_ pi = { sizeof(pi), 1 /* PI_NOUI */, USER };
    check("LoadUserProfile", LoadUserProfileW(t, &pi) && pi.hProfile);
    WCHAR prof[MAX_PATH];
    DWORD pn = MAX_PATH;
    check("its profile is C:\\Users\\novatest", GetUserProfileDirectoryW(t, prof, &pn) && !_wcsicmp(prof, L"C:\\Users\\" USER));
    WCHAR docs[MAX_PATH];
    swprintf(docs, MAX_PATH, L"%ls\\Documents", prof);
    DWORD attrs = GetFileAttributesW(docs);
    check("... with its folders", attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
    HKEY k;
    check("its hive takes a value", pi.hProfile &&
          !RegCreateKeyExW(pi.hProfile, L"Software\\NovaTest", 0, 0, 0, KEY_ALL_ACCESS, 0, &k, 0) &&
          !RegSetValueExW(k, L"Owner", 0, REG_SZ, (const BYTE *)USER, sizeof(USER)) && !RegCloseKey(k));
    check("... which isn't in mine", RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\NovaTest", 0, KEY_READ, &k) == ERROR_FILE_NOT_FOUND);
    check("nobody opens HKLM\\SAM\\SAM", RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SAM\\SAM\\Domains\\Account\\Users", 0, KEY_READ, &k) ==
                                          ERROR_ACCESS_DENIED);

    /* the profile is the user's: a token without Administrators (mine,
     * restricted) can't open it */
    HANDLE restricted = 0;
    SID_AND_ATTRIBUTES dis = { admins, 0 };
    BOOL denied = FALSE;
    if (CreateRestrictedToken(mine, 0, 1, &dis, 0, 0, 0, 0, &restricted) && ImpersonateLoggedOnUser(restricted)) {
        HANDLE d = CreateFileW(docs, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS_, 0);
        denied = d == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED;
        if (d != INVALID_HANDLE_VALUE) CloseHandle(d);
        RevertToSelf();
    }
    check("another user can't open its profile", denied);
    if (restricted) CloseHandle(restricted);

    /* a program run as it: its name, profile, HKCU and a file of its own */
    WCHAR exe[MAX_PATH], cmd[MAX_PATH + 16];
    GetModuleFileNameW(0, exe, MAX_PATH);
    swprintf(cmd, MAX_PATH + 16, L"\"%ls\" child", exe);
    LPVOID env = 0;
    check("CreateEnvironmentBlock", CreateEnvironmentBlock(&env, t, FALSE));
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION proc;
    DWORD code = 99;
    if (CreateProcessAsUserW(t, exe, cmd, 0, 0, FALSE, CREATE_UNICODE_ENVIRONMENT, env, prof, &si, &proc)) {
        WaitForSingleObject(proc.hProcess, 60000);
        GetExitCodeProcess(proc.hProcess, &code);
        CloseHandle(proc.hThread);
        CloseHandle(proc.hProcess);
    } else {
        printf("FAIL: CreateProcessAsUser (%lu)\n", GetLastError());
    }
    check("the program run as it passed", code == 0);
    if (env) DestroyEnvironmentBlock(env);
    UnloadUserProfile(t, pi.hProfile);
    CloseHandle(t);

    /* the user changes its password */
    check("a wrong old password is refused", NetUserChangePassword(0, USER, L"nope", PASS2) == ERROR_INVALID_PASSWORD_);
    check("NetUserChangePassword", NetUserChangePassword(0, USER, PASS, PASS2) == 0);
    t = 0;
    check("the old password no longer signs in", !LogonUserW(USER, 0, PASS, LOGON32_LOGON_INTERACTIVE, 0, &t));
    check("the new one does", LogonUserW(USER, 0, PASS2, LOGON32_LOGON_INTERACTIVE, 0, &t));
    if (t) CloseHandle(t);

    /* the lock screen, with a password on my account for the while */
    USER_INFO_1003_ pw = { LOCK_PASS };
    check("an administrator sets my password", NetUserSetInfo(0, me, 1003, (LPBYTE)&pw, &perr) == 0);
    ULONG s[2];
    check("LockWorkStation", LockWorkStation());
    for (int i = 0; i < 50; i++) { session(s); if (s[1]) break; Sleep(100); }
    check("the screen locks", s[1] && s[0] == my_rid);
    printf("accounttest: the screen is locked\n");
    fflush(stdout);
    for (int i = 0; i < 1200; i++) { session(s); if (!s[1]) break; Sleep(100); }
    check("my password unlocks it", !s[1] && s[0] == my_rid);
    pw.password = L"";
    check("my password is removed again", NetUserSetInfo(0, me, 1003, (LPBYTE)&pw, &perr) == 0);

    /* gone again */
    check("NetUserDel", NetUserDel(0, USER) == 0);
    check("... and it no longer signs in", !LogonUserW(USER, 0, PASS2, LOGON32_LOGON_INTERACTIVE, 0, &t));
    check("NetUserDel refuses me (signed in)", NetUserDel(0, me) != 0);
    check("DeleteProfile", DeleteProfileW(want, 0, 0));
    check("... takes its folder", GetFileAttributesW(prof) == INVALID_FILE_ATTRIBUTES);
    CloseHandle(mine);

    printf("accounttest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
