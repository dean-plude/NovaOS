/* switchtest.exe — Settings' Accounts page and fast user switching
 * (kernel/apps/settings.c, kernel/wm/logon.c, kernel/um/um_accounts.c).
 * The test harness does what a person does, when this asks:
 *
 *   "add the account in Settings"  Start, "add an account", the name and
 *                                  the password twice, Enter: Settings
 *                                  makes a standard user
 *   "switch to novaswitch"         Win+L, the other account, a wrong
 *                                  password (refused), then the right one
 *   "switch back"                  Win+L, the first account, Sign in
 *
 * Meanwhile this program, the first account's, keeps running: it sees the
 * switch happen, the new account's profile and hive made, both accounts
 * signed in at once, and the first account back on the screen.  Then it
 * signs the second one out and deletes it.
 *
 *   switchtest    the test (as administrator: its manifest asks) */
#include <stdio.h>
#include <string.h>
#include <winternl.h>
#include <windows.h>
#include <wchar.h>

typedef DWORD NET_API_STATUS;
typedef struct { LPWSTR name, password; DWORD password_age, priv; LPWSTR home_dir, comment; DWORD flags; LPWSTR script_path; } USER_INFO_1_;
__declspec(dllimport) NET_API_STATUS WINAPI NetUserDel(LPCWSTR server, LPCWSTR user);
__declspec(dllimport) NET_API_STATUS WINAPI NetUserGetInfo(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE *buf);
__declspec(dllimport) NET_API_STATUS WINAPI NetApiBufferFree(LPVOID buf);
__declspec(dllimport) BOOL WINAPI DeleteProfileW(LPCWSTR sid, LPCWSTR path, LPCWSTR computer);

#define USER L"novaswitch"

static int pass, fail;
static void check(const char *what, int ok)
{
    if (ok) pass++;
    else { fail++; printf("FAIL: %s (error %lu)\n", what, GetLastError()); }
}

static DWORD rid_of(const WCHAR *name)
{
    LPBYTE buf;
    if (NetUserGetInfo(0, name, 20, &buf)) return 0;
    DWORD rid = ((DWORD *)((LPWSTR *)buf + 3))[1];         /* USER_INFO_20.usri20_user_id */
    NetApiBufferFree(buf);
    return rid;
}

/* The screen: { the account on it, locked } */
static void session(ULONG s[2])
{
    ULONG got;
    s[0] = s[1] = 0;
    NtNovaAccount(NOVA_ACCOUNT_SESSION, 0, 0, s, 2 * sizeof(ULONG), &got);
}

/* Whether @rid is signed in (on the screen or switched away from) */
static int signed_in(DWORD rid)
{
    ULONG rids[8], got = 0;
    if (NtNovaAccount(NOVA_ACCOUNT_SESSIONS, 0, 0, rids, sizeof(rids), &got)) return 0;
    for (ULONG i = 0; i < got / sizeof(ULONG); i++) if (rids[i] == rid) return 1;
    return 0;
}

static NTSTATUS sign_out(DWORD rid)
{
    NOVA_ACCOUNT_REQUEST rq;
    memset(&rq, 0, sizeof(rq));
    rq.Rid = rid;
    return NtNovaAccount(NOVA_ACCOUNT_SIGN_OUT, &rq, sizeof(rq), 0, 0, 0);
}

static void say(const char *what)
{
    printf("switchtest: %s\n", what);
    fflush(stdout);
}

static void remove_account(void)
{
    WCHAR sid[64];
    for (int i = 0; i < 8; i++) {
        DWORD rid = rid_of(USER);
        if (!rid) break;
        if (signed_in(rid)) {                               /* (a run that died half-way) */
            sign_out(rid);
            for (int j = 0; j < 100 && signed_in(rid); j++) Sleep(100);
        }
        if (NetUserDel(0, USER)) break;
        swprintf(sid, 64, L"S-1-5-21-1000-2000-3000-%lu", rid);
        DeleteProfileW(sid, 0, 0);
    }
}

int main(void)
{
    remove_account();
    WCHAR me[64];
    DWORD n = 64;
    GetUserNameW(me, &n);
    DWORD my_rid = rid_of(me);
    ULONG s[2];
    session(s);
    check("I am the account on the screen", my_rid && s[0] == my_rid && !s[1]);

    /* Settings, Accounts, Add account */
    say("add the account in Settings");
    DWORD rid = 0;
    for (int i = 0; i < 1200 && !(rid = rid_of(USER)); i++) Sleep(100);
    check("Settings added the account", rid > my_rid);
    LPBYTE info = 0;
    check("... as a standard user", rid && !NetUserGetInfo(0, USER, 1, &info) && ((USER_INFO_1_ *)info)->priv == 1);
    if (info) NetApiBufferFree(info);
    check("... not signed in yet", !signed_in(rid));

    /* switch to it from the lock screen, my programs running on */
    say("switch to novaswitch");
    for (int i = 0; i < 300; i++) { session(s); if (s[1]) break; Sleep(100); }
    check("the screen locks", s[1] != 0);
    say("the sign-in screen is up");
    for (int i = 0; i < 1800; i++) { session(s); if (s[0] == rid && !s[1]) break; Sleep(100); }
    check("it switched to novaswitch while I kept running", s[0] == rid && !s[1]);
    check("both accounts are signed in", signed_in(rid) && signed_in(my_rid));
    WCHAR sid[64], key[96];
    swprintf(sid, 64, L"S-1-5-21-1000-2000-3000-%lu", rid);
    swprintf(key, 96, L"%ls\\Software", sid);
    HKEY k;
    check("its registry hive is loaded", !RegOpenKeyExW(HKEY_USERS, key, 0, KEY_READ, &k) && !RegCloseKey(k));
    DWORD attrs = GetFileAttributesW(L"C:\\Users\\" USER L"\\Documents");
    check("its profile was made", attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
    check("a signed-in account can't be deleted", NetUserDel(0, USER) != 0 && rid_of(USER) == rid);

    /* and back */
    say("switch back");
    for (int i = 0; i < 1800; i++) { session(s); if (s[0] == my_rid && !s[1]) break; Sleep(100); }
    check("the first account is back on the screen", s[0] == my_rid && !s[1]);
    check("... and novaswitch still signed in", signed_in(rid));

    /* sign it out, then it can go */
    check("signing novaswitch out", sign_out(rid) == 0);
    for (int i = 0; i < 100 && signed_in(rid); i++) Sleep(100);
    check("... signs it out", !signed_in(rid));
    session(s);
    check("... and leaves me on the screen", s[0] == my_rid && !s[1]);
    check("NetUserDel", NetUserDel(0, USER) == 0);
    check("DeleteProfile", DeleteProfileW(sid, 0, 0));
    remove_account();

    printf("switchtest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
