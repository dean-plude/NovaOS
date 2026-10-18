/* uactest.exe — standard users and User Account Control (kernel/um/um_security.c,
 * kernel/wm/consent.c, shell32's "runas"):
 *
 *   - the desktop's token is an administrator's limited half at medium
 *     integrity;
 *   - ShellExecuteEx "runas" raises the consent prompt: No (the test
 *     harness presses Alt+N) fails with ERROR_CANCELLED, and a program
 *     whose manifest asks for administrator (setupadmin.exe) answered No
 *     fails with ERROR_ELEVATION_REQUIRED;
 *   - Yes (Alt+Y) runs it elevated at high integrity ("uactest admin"),
 *     which makes a standard user and a second administrator, signs the
 *     standard user in (LogonUser) and runs "uactest user" as it: a
 *     standard user's token (no split, medium, no Administrators), whose
 *     installer (setupadmin.exe) raises the credential prompt: a wrong
 *     password (typed by the harness) is refused, the administrator's
 *     runs it elevated, and "runas" with the administrator's password
 *     runs "uactest asadmin" as that administrator, elevated;
 *   - a medium-integrity process cannot open a high-integrity one for
 *     more than waiting on it and asking its exit code ("uactest wait":
 *     OpenProcess for writing, reading or ending it is denied, a limited
 *     handle can neither end it, read it nor be duplicated into more).
 *   - neither the limited administrator nor the standard user may change
 *     C:\Windows, C:\Programs or HKLM (ACCESS_DENIED: a new file or
 *     folder, writing or deleting a system file, a new machine key, a
 *     value set through a key opened for reading), while both read them
 *     and write C:\Windows\Temp; the elevated program changes them all
 *     (and puts them back).
 *
 * The children report through their exit codes (the number of checks
 * that failed); the accounts are deleted at the end. */
#include <stdio.h>
#include <string.h>
#include <winternl.h>
#include <windows.h>
#include <wchar.h>

/* (beyond NovaOS's headers) */
__declspec(dllimport) BOOL WINAPI LogonUserW(LPCWSTR user, LPCWSTR domain, LPCWSTR pass, DWORD type, DWORD prov, PHANDLE token);
__declspec(dllimport) BOOL WINAPI CreateProcessAsUserW(HANDLE token, LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
                                                       LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env,
                                                       LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi);
__declspec(dllimport) BOOL WINAPI DeleteProfileW(LPCWSTR sid, LPCWSTR path, LPCWSTR computer);
__declspec(dllimport) BOOL WINAPI GetUserNameW(LPWSTR name, LPDWORD n);
typedef struct {
    DWORD cbSize; ULONG fMask; HWND hwnd; LPCWSTR lpVerb, lpFile, lpParameters, lpDirectory; int nShow;
    HINSTANCE hInstApp; void *lpIDList; LPCWSTR lpClass; HANDLE hkeyClass; DWORD dwHotKey;
    HANDLE hIcon; HANDLE hProcess;
} SEI_;
__declspec(dllimport) BOOL WINAPI ShellExecuteExW(SEI_ *info);
typedef DWORD NET_API_STATUS;
typedef struct { LPWSTR name, password; DWORD password_age, priv; LPWSTR home_dir, comment; DWORD flags; LPWSTR script_path; } USER_INFO_1_;
__declspec(dllimport) NET_API_STATUS WINAPI NetUserAdd(LPCWSTR server, DWORD level, LPBYTE buf, LPDWORD parm_err);
__declspec(dllimport) NET_API_STATUS WINAPI NetUserDel(LPCWSTR server, LPCWSTR user);
__declspec(dllimport) NET_API_STATUS WINAPI NetUserGetInfo(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE *buf);
__declspec(dllimport) NET_API_STATUS WINAPI NetApiBufferFree(LPVOID buf);

__declspec(dllimport) BOOL WINAPI ReadProcessMemory(HANDLE p, LPCVOID addr, LPVOID buf, SIZE_T n, SIZE_T *got);
#ifndef PROCESS_ALL_ACCESS
#define PROCESS_TERMINATE                 0x0001
#define PROCESS_VM_OPERATION              0x0008
#define PROCESS_VM_READ                   0x0010
#define PROCESS_VM_WRITE                  0x0020
#define PROCESS_QUERY_INFORMATION         0x0400
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define PROCESS_ALL_ACCESS                0x1FFFFF
#endif
#define LOGON32_LOGON_INTERACTIVE 2
#define SEE_MASK_NOCLOSEPROCESS_  0x40
#define SEE_MASK_NOASYNC_         0x100
#define ERROR_CANCELLED_          1223
#define ERROR_ELEVATION_REQUIRED_ 740
#define UF_SCRIPT_                0x1
#define TokenElevationType_       18
#define TokenElevation_           20
#define TokenIntegrityLevel_      25

#define STD_USER   L"uacuser"
#define STD_PASS   L"User-Pass-33"
#define ADMIN_USER L"uacadmin"
#define ADMIN_PASS L"Admin-Pass-33"
#define GO_EVENT   L"Local\\uactest-go"

static int pass, fail;
static const char *who = "uactest";
static void check(const char *what, int ok)
{
    if (ok) pass++;
    else { fail++; printf("FAIL: %s: %s (error %lu)\n", who, what, GetLastError()); }
}

static void say(const char *s)
{
    printf("uactest: %s\n", s);
    fflush(stdout);
}

static DWORD token_dword(int cls)
{
    HANDLE t;
    DWORD v = 0xFFFFFFFF, n;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return v;
    if (!GetTokenInformation(t, cls, &v, sizeof(v), &n)) v = 0xFFFFFFFE;
    CloseHandle(t);
    return v;
}

/* TokenIntegrityLevel's RID (0x2000 medium, 0x3000 high) */
static DWORD integrity(void)
{
    HANDLE t;
    BYTE buf[64];
    DWORD n;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return 0;
    BOOL ok = GetTokenInformation(t, TokenIntegrityLevel_, buf, sizeof(buf), &n);
    CloseHandle(t);
    if (!ok) return 0;
    PSID sid = *(PSID *)buf;                        /* TOKEN_MANDATORY_LABEL: Label.Sid first */
    return *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
}

static BOOL in_admins(void)
{
    SID_IDENTIFIER_AUTHORITY nt = { SECURITY_NT_AUTHORITY };
    PSID sid;
    BOOL m = FALSE;
    if (!AllocateAndInitializeSid(&nt, 2, 32, 544, 0, 0, 0, 0, 0, 0, &sid)) return FALSE;   /* S-1-5-32-544 */
    BOOL ok = CheckTokenMembership(NULL, sid, &m);
    FreeSid(sid);
    return ok && m;
}

static BOOL elevated(void)
{
    return token_dword(TokenElevation_) == 1 && integrity() == 0x3000 && in_admins();
}

static void my_dir(WCHAR *dir)
{
    GetModuleFileNameW(0, dir, MAX_PATH);
    WCHAR *s = wcsrchr(dir, L'\\');
    if (s) *s = 0;
}

/* ShellExecuteEx(@verb) this program with @args; its exit code, or
 * 0x10000 + the error when it did not start */
static DWORD shell(const WCHAR *verb, const WCHAR *args, HANDLE *keep)
{
    WCHAR exe[MAX_PATH];
    GetModuleFileNameW(0, exe, MAX_PATH);
    SEI_ sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS_ | SEE_MASK_NOASYNC_;
    sei.lpVerb = verb;
    sei.lpFile = exe;
    sei.lpParameters = args;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) return 0x10000 + GetLastError();
    if (keep) { *keep = sei.hProcess; return 0; }
    DWORD code = 0xFFFF;
    WaitForSingleObject(sei.hProcess, 300000);
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code;
}

/* CreateProcess(setupadmin.exe), whose manifest asks for administrator:
 * its exit code (0: it ran elevated), or 0x10000 + the error */
static DWORD installer(void)
{
    WCHAR dir[MAX_PATH], cmd[MAX_PATH + 32];
    my_dir(dir);
    swprintf(cmd, MAX_PATH + 32, L"\"%ls\\setupadmin.exe\"", dir);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(0, cmd, 0, 0, FALSE, 0, 0, 0, &si, &pi)) return 0x10000 + GetLastError();
    DWORD code = 0xFFFF;
    WaitForSingleObject(pi.hProcess, 120000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static void remove_account(const WCHAR *name)
{
    for (int i = 0; i < 4; i++) {
        LPBYTE buf;
        if (NetUserGetInfo(0, name, 20, &buf)) return;
        DWORD rid = ((DWORD *)((LPWSTR *)buf + 3))[1];     /* USER_INFO_20.usri20_user_id */
        NetApiBufferFree(buf);
        if (NetUserDel(0, name)) return;
        WCHAR sid[64];
        swprintf(sid, 64, L"S-1-5-21-1000-2000-3000-%lu", rid);
        DeleteProfileW(sid, 0, 0);
    }
}

/* ---- "uactest asadmin": runas from the standard user, with uacadmin's password */
/* ---- C:\Windows, C:\Programs and HKLM: refused unless @elevated (which
 * leaves C:\Windows\uactest.txt for the standard user, @leftover, to fail
 * to delete; "uactest admin" deletes it afterwards) */
static BOOL make_file(const WCHAR *path)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(f);
    return TRUE;
}

static void protection_checks(BOOL elevated, BOOL leftover)
{
    const char *verb = elevated ? "allowed" : "denied";
    char what[128];
    DWORD e;
#define EXPECT(desc, ok) (snprintf(what, sizeof(what), "%s: %s", desc, verb), \
                          check(what, elevated ? (ok) : (!(ok) && (e = GetLastError(), e == ERROR_ACCESS_DENIED))))
    SetLastError(0);
    EXPECT("a new file in C:\\Windows", make_file(L"C:\\Windows\\uactest.txt"));
    SetLastError(0);
    EXPECT("a new file in C:\\Windows\\System32", make_file(L"C:\\Windows\\System32\\uactest.dll"));
    SetLastError(0);
    EXPECT("a new folder in C:\\Programs", CreateDirectoryW(L"C:\\Programs\\uactest", NULL));
    SetLastError(0);
    HANDLE f = CreateFileW(L"C:\\Windows\\System32\\cmd.exe", GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    EXPECT("opening a system program for writing", f != INVALID_HANDLE_VALUE);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    f = CreateFileW(L"C:\\Windows\\System32\\cmd.exe", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    check("reading a system program: allowed", f != INVALID_HANDLE_VALUE);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    check("a new file in C:\\Windows\\Temp: allowed", make_file(L"C:\\Windows\\Temp\\uactest.tmp") &&
          DeleteFileW(L"C:\\Windows\\Temp\\uactest.tmp"));
    if (elevated) {
        check("deleting the new file and folder: allowed", DeleteFileW(L"C:\\Windows\\System32\\uactest.dll") &&
              RemoveDirectoryW(L"C:\\Programs\\uactest"));
    } else if (leftover) {
        SetLastError(0);
        EXPECT("deleting the administrator's file in C:\\Windows", DeleteFileW(L"C:\\Windows\\uactest.txt"));
    }

    HKEY k = 0;
    LSTATUS r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\uactest", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    snprintf(what, sizeof(what), "a new key under HKLM\\SOFTWARE: %s", verb);
    check(what, elevated ? r == 0 : r == ERROR_ACCESS_DENIED);
    if (k) { RegCloseKey(k); k = 0; }
    r = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_SET_VALUE, &k);
    snprintf(what, sizeof(what), "HKLM\\...\\CurrentVersion opened to set values: %s", verb);
    check(what, elevated ? r == 0 : r == ERROR_ACCESS_DENIED);
    if (k) { RegCloseKey(k); k = 0; }
    r = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &k);
    WCHAR name[64];
    DWORD n = sizeof(name);
    check("HKLM read: ProductName", r == 0 && !RegQueryValueExW(k, L"ProductName", NULL, NULL, (BYTE *)name, &n));
    if (!elevated)
        check("a value set through a key opened for reading: denied",
              k && RegSetValueExW(k, L"uactest", 0, REG_DWORD, (const BYTE *)&n, 4) == ERROR_ACCESS_DENIED);
    if (k) RegCloseKey(k);
    k = 0;
    r = RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE", &k);          /* (MAXIMUM_ALLOWED) */
    check("RegOpenKey(HKLM\\SOFTWARE): opens", r == 0);
    r = k ? RegSetValueExW(k, L"uactest", 0, REG_DWORD, (const BYTE *)&n, 4) : 1;
    snprintf(what, sizeof(what), "... a value set through it: %s", verb);
    check(what, elevated ? r == 0 : r == ERROR_ACCESS_DENIED);
    if (k) { RegDeleteValueW(k, L"uactest"); RegCloseKey(k); }
    if (elevated) check("the new key deleted", RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\uactest") == 0);
#undef EXPECT
}

static int as_admin(void)
{
    who = "uactest asadmin";
    WCHAR name[64];
    DWORD n = 64;
    check("runs elevated (TokenElevation, high integrity, Administrators)", elevated());
    check("runs as the administrator whose password was typed", GetUserNameW(name, &n) && !wcscmp(name, ADMIN_USER));
    return fail;
}

/* ---- "uactest user": a standard user */
static int user(void)
{
    who = "uactest user";
    WCHAR name[64];
    DWORD n = 64;
    check("runs as the standard user", GetUserNameW(name, &n) && !wcscmp(name, STD_USER));
    check("not split (TokenElevationTypeDefault)", token_dword(TokenElevationType_) == 1);
    check("not elevated", token_dword(TokenElevation_) == 0);
    check("medium integrity", integrity() == 0x2000);
    check("not in Administrators", !in_admins());
    protection_checks(FALSE, TRUE);
    /* an installer: the credential prompt (a wrong password first, refused) */
    DWORD code = installer();
    check("the installer ran elevated once an administrator's password was typed", code == 0);
    /* "runas": the credential prompt again, the administrator's password */
    code = shell(L"runas", L"asadmin", NULL);
    check("runas with the administrator's password: it ran as the administrator, elevated", code == 0);
    return fail;
}

/* ---- "uactest admin": elevated (runas, Yes) */
static int admin(void)
{
    who = "uactest admin";
    check("runas Yes: elevated (TokenElevation 1, high integrity, Administrators)", elevated());
    check("TokenElevationType full", token_dword(TokenElevationType_) == 2);
    protection_checks(TRUE, FALSE);
    remove_account(STD_USER);
    remove_account(ADMIN_USER);
    USER_INFO_1_ u = { STD_USER, STD_PASS, 0, 1 /* USER_PRIV_USER */, NULL, NULL, UF_SCRIPT_, NULL };
    DWORD perr = 0;
    check("NetUserAdd: a standard user", NetUserAdd(0, 1, (LPBYTE)&u, &perr) == 0);
    USER_INFO_1_ a = { ADMIN_USER, ADMIN_PASS, 0, 2 /* USER_PRIV_ADMIN */, NULL, NULL, UF_SCRIPT_, NULL };
    check("NetUserAdd: an administrator", NetUserAdd(0, 1, (LPBYTE)&a, &perr) == 0);

    HANDLE t = 0;
    check("LogonUser: the standard user", LogonUserW(STD_USER, L".", STD_PASS, LOGON32_LOGON_INTERACTIVE, 0, &t) && t);
    WCHAR exe[MAX_PATH], cmd[MAX_PATH + 16];
    GetModuleFileNameW(0, exe, MAX_PATH);
    swprintf(cmd, MAX_PATH + 16, L"\"%ls\" user", exe);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD code = 99;
    if (t && CreateProcessAsUserW(t, exe, cmd, 0, 0, FALSE, 0, 0, 0, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 300000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    check("the standard user's checks passed", code == 0);
    check("the file in C:\\Windows deleted", DeleteFileW(L"C:\\Windows\\uactest.txt"));
    if (t) CloseHandle(t);
    remove_account(STD_USER);
    remove_account(ADMIN_USER);
    check("the accounts are gone", NetUserGetInfo(0, STD_USER, 20, &(LPBYTE){ 0 }) != 0);
    return fail;
}

/* ---- "uactest wait": elevated until told to go */
static int wait_go(void)
{
    who = "uactest wait";
    check("elevated", elevated());
    HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, GO_EVENT);
    check("the medium process's event opens", go != NULL);
    if (go) { WaitForSingleObject(go, 120000); CloseHandle(go); }
    return fail;
}

/* ---- a medium process against a high one */
static void integrity_checks(void)
{
    HANDLE go = CreateEventW(NULL, TRUE, FALSE, GO_EVENT), proc = 0;
    say("prompt 4: Yes");
    DWORD st = shell(L"runas", L"wait", &proc);
    check("runas Yes: the program starts", st == 0 && proc);
    if (!proc) { CloseHandle(go); return; }
    DWORD pid = GetProcessId(proc);
    HANDLE h;
    SetLastError(0);
    h = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
    check("OpenProcess to write a high-integrity process: denied", !h && GetLastError() == ERROR_ACCESS_DENIED);
    if (h) CloseHandle(h);
    h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    check("OpenProcess(PROCESS_ALL_ACCESS): denied", !h && GetLastError() == ERROR_ACCESS_DENIED);
    if (h) CloseHandle(h);
    h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    check("OpenProcess(PROCESS_TERMINATE): denied", !h && GetLastError() == ERROR_ACCESS_DENIED);
    if (h) CloseHandle(h);
    h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    check("OpenProcess to read it: denied", !h && GetLastError() == ERROR_ACCESS_DENIED);
    if (h) CloseHandle(h);
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    check("OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE): allowed", h != NULL);
    if (h) {
        DWORD code = 0;
        check("... its exit code (still running)", GetExitCodeProcess(h, &code) && code == STILL_ACTIVE);
        check("... waiting on it", WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
        check("... it cannot end it", !TerminateProcess(h, 7));
        BYTE b[8];
        SIZE_T got = 0;
        check("... it cannot read its memory", !ReadProcessMemory(h, (LPCVOID)0x10000, b, sizeof(b), &got));
        HANDLE more = 0;
        check("... nor be duplicated into a handle with more rights",
              !DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &more, PROCESS_ALL_ACCESS, FALSE, 0));
        if (more) CloseHandle(more);
        CloseHandle(h);
    }
    SetEvent(go);
    DWORD code = 0xFFFF;
    WaitForSingleObject(proc, 120000);
    GetExitCodeProcess(proc, &code);
    check("the elevated program's checks passed", code == 0);
    CloseHandle(proc);
    CloseHandle(go);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "admin")) return admin();
    if (argc > 1 && !strcmp(argv[1], "user")) return user();
    if (argc > 1 && !strcmp(argv[1], "asadmin")) return as_admin();
    if (argc > 1 && !strcmp(argv[1], "wait")) return wait_go();
    if (argc > 1 && !strcmp(argv[1], "child")) return elevated() ? 0 : 1;

    /* me: the desktop's token, an administrator's limited half */
    check("TokenElevationType limited", token_dword(TokenElevationType_) == 3);
    check("TokenElevation 0", token_dword(TokenElevation_) == 0);
    check("medium integrity", integrity() == 0x2000);
    check("Administrators only for denying", !in_admins());
    protection_checks(FALSE, FALSE);

    say("prompt 1: No");
    DWORD r = shell(L"runas", L"child", NULL);
    check("runas, No at the prompt: ERROR_CANCELLED", r == 0x10000 + ERROR_CANCELLED_);

    say("prompt 2: No (an installer)");
    r = installer();
    check("an installer (requireAdministrator), No at the prompt: ERROR_ELEVATION_REQUIRED", r == 0x10000 + ERROR_ELEVATION_REQUIRED_);

    say("prompt 3: Yes, then a standard user's installer and runas");
    r = shell(L"runas", L"admin", NULL);
    check("runas Yes: the elevated program and the standard user's checks passed", r == 0);

    integrity_checks();

    printf("uactest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
