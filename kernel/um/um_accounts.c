/*
 * um_accounts.c — local user accounts (the SAM), profiles and the
 * interactive session
 *
 * Accounts live where Windows keeps them, in the registry under
 * HKLM\SAM\SAM\Domains\Account\Users\<RID as 8 hex digits> (no program may
 * open that key: um_registry.c refuses it, as Windows lets only SYSTEM):
 *
 *   Name          REG_SZ     the sign-in name (1-20 characters)
 *   FullName      REG_SZ     what the sign-in screen shows (may be empty)
 *   Flags         REG_DWORD  UM_ACCT_ADMIN, UM_ACCT_DISABLED
 *   Hash          REG_BINARY the password: version 1, Argon2id's blocks and
 *                            passes, a 16-byte salt and the 32-byte hash
 *                            (no Hash: the account has no password)
 *
 * and HKLM\SAM\SAM\Domains\Account NextRid.  A user's SID is the machine
 * domain's, S-1-5-21-1000-2000-3000, and the RID: the first account (the
 * one Setup's first-boot screens name, apps/welcome.c) is 1001, as on
 * Windows, and every NovaOS before accounts ran as it.  Passwords are
 * hashed with Argon2id (Monocypher, BSD-2-Clause) rather than Windows' NT
 * hash: NovaOS speaks no NTLM, so nothing needs the weaker one.
 *
 * Profiles are C:\Users\<name> (Desktop, Documents, Downloads, Music,
 * Pictures, Videos, AppData\Roaming, AppData\Local\Temp), listed in
 * HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList\<SID>
 * ProfileImagePath, with a DACL only the user, SYSTEM and Administrators
 * pass; the user's registry hive is HKEY_USERS\<SID>.  The first account
 * keeps the layout NovaOS had before accounts (its profile is C:\, with
 * C:\Desktop and C:\AppData) so nothing already installed moves.
 *
 * The interactive session is the account signed in on the screen
 * (wm/logon.c): programs the desktop starts get its token
 * (um_security.c).  The last one signed in is kept in
 * HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\LogonUI
 * LastLoggedOnUserSID and signs in again at the next start, unless it has
 * a password or there are other accounts to choose from: then the
 * sign-in screen comes first, as on Windows.
 *
 * Programs reach all of this through NtNovaAccount (advapi32's LogonUser,
 * netapi32's NetUser*, userenv's profiles, user32's LockWorkStation).
 */

#include "um_internal.h"
#include "../ke/printf.h"
#include "../ke/probe.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../fs/ramfs.h"
#include "../fs/fsec.h"
#include "../net/tls.h"
#include "monocypher.h"
#include "../wm/logon.h"

#define ST_SUCCESS              0x00000000u
#define ST_INVALID_PARAMETER    0xC000000Du
#define ST_NO_MEMORY            0xC0000017u
#define ST_BUFFER_TOO_SMALL     0xC0000023u
#define ST_ACCESS_DENIED        0xC0000022u
#define ST_ACCESS_VIOLATION     0xC0000005u
#define ST_INVALID_ACCOUNT_NAME 0xC0000062u
#define ST_USER_EXISTS          0xC0000063u
#define ST_NO_SUCH_USER         0xC0000064u
#define ST_LAST_ADMIN           0xC0000069u
#define ST_WRONG_PASSWORD       0xC000006Au
#define ST_PASSWORD_RESTRICTION 0xC000006Cu
#define ST_LOGON_FAILURE        0xC000006Du
#define ST_ACCOUNT_DISABLED     0xC0000072u
#define ST_TOO_MANY_HANDLES     0xC000011Fu
#define ST_INVALID_INFO_CLASS   0xC0000003u

#define SAM_KEY      "Machine\\SAM\\SAM\\Domains\\Account"
#define USERS_KEY    SAM_KEY "\\Users"
#define PROFILE_LIST "Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList"
#define LOGONUI_KEY  "Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\LogonUI"
#define FIRST_RID    1001
#define MAX_ACCOUNTS 32

#define HASH_VERSION 1
#define HASH_BLOCKS  2048                   /* Argon2id: 2 MiB, */
#define HASH_PASSES  3                      /* 3 passes */
#define HASH_LEN     (1 + 4 + 4 + 16 + 32)

/* Account changes and sign-ins, one at a time (hashing takes a moment,
 * so not a spinlock) */
static UmLock g_lock;
static volatile UINT32 g_session_rid;      /* 0: not read yet */

static void users_key(UINT32 rid, char *out, int cap) { ksnprintf(out, (size_t)cap, USERS_KEY "\\%08X", rid); }

void um_account_sid_string(UINT32 rid, char *out, int cap)
{
    ksnprintf(out, (size_t)cap, "S-1-5-21-1000-2000-3000-%u", rid);
}

void um_account_sid(UINT32 rid, UINT8 sid[28])
{
    static const UINT8 prefix[] = { 1, 5, 0, 0, 0, 0, 0, 5, 21, 0, 0, 0, 0xE8, 3, 0, 0, 0xD0, 7, 0, 0, 0xB8, 0x0B, 0, 0 };
    memcpy(sid, prefix, sizeof(prefix));
    memcpy(sid + 24, &rid, 4);
}

/* The RID of a local account's SID, else 0 */
UINT32 um_account_rid_of_sid(const UINT8 *sid)
{
    UINT8 mine[28];
    um_account_sid(0, mine);
    if (sid[1] != 5 || memcmp(sid, mine, 24)) return 0;
    UINT32 rid;
    memcpy(&rid, sid + 24, 4);
    return rid >= 1000 ? rid : 0;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static bool ieq(const char *a, const char *b)
{
    for (; *a && lower(*a) == lower(*b); a++, b++) {}
    return !*a && !*b;
}

/* Windows' rules for a user name: 1-20 characters, not only dots and
 * spaces, none of " / \ [ ] : ; | = , + * ? < > @ */
bool um_account_name_ok(const char *n)
{
    size_t len = strlen(n);
    if (!len || len > UM_ACCOUNT_NAME_MAX) return false;
    bool real = false;
    for (const char *p = n; *p; p++) {
        if ((UINT8)*p < 0x20 || (UINT8)*p > 0x7E || strchr("\"/\\[]:;|=,+*?<>@", *p)) return false;
        if (*p != '.' && *p != ' ') real = true;
    }
    return real && n[len - 1] != '.' && n[0] != ' ' && n[len - 1] != ' ';
}

/* The name the first-boot screens gave (um.c's USERNAME before accounts) */
static void setup_name(char *out, int cap)
{
    if (!um_registry_get_sz(UM_SETUP_KEY, "UserName", out, cap) || !um_account_name_ok(out))
        ksnprintf(out, (size_t)cap, "dean");
}

/* The first account, made from what NovaOS had before accounts */
static void ensure_first(void)
{
    char k[96];
    users_key(FIRST_RID, k, sizeof(k));
    if (um_registry_key_exists(k)) return;
    char name[UM_ACCOUNT_NAME_MAX + 1];
    setup_name(name, sizeof(name));
    um_registry_set_sz(k, "Name", name);
    um_registry_set_sz(k, "FullName", "");
    um_registry_set_dword(k, "Flags", UM_ACCT_ADMIN);
    UINT32 next;
    if (!um_registry_get_dword(SAM_KEY, "NextRid", &next) || next <= FIRST_RID)
        um_registry_set_dword(SAM_KEY, "NextRid", FIRST_RID + 1);
    char sid[48], pk[160];
    um_account_sid_string(FIRST_RID, sid, sizeof(sid));
    ksnprintf(pk, sizeof(pk), PROFILE_LIST "\\%s", sid);
    um_registry_set_sz(pk, "ProfileImagePath", "C:\\");
    kprintf("[ACCT] First account %s (%s)\n", name, sid);
}

static bool read_account(UINT32 rid, UmAccount *a)
{
    char k[96];
    users_key(rid, k, sizeof(k));
    memset(a, 0, sizeof(*a));
    if (!um_registry_get_sz(k, "Name", a->name, sizeof(a->name)) || !a->name[0]) return false;
    a->rid = rid;
    um_registry_get_sz(k, "FullName", a->full, sizeof(a->full));
    um_registry_get_dword(k, "Flags", &a->flags);
    UINT8 h[HASH_LEN];
    a->password = um_registry_get_bin(k, "Hash", h, sizeof(h)) == HASH_LEN;
    char sid[48], pk[160];
    um_account_sid_string(rid, sid, sizeof(sid));
    ksnprintf(pk, sizeof(pk), PROFILE_LIST "\\%s", sid);
    if (!um_registry_get_sz(pk, "ProfileImagePath", a->profile, sizeof(a->profile))) a->profile[0] = 0;
    return true;
}

/* -----------------------------------------------------------------------
 * Lookups
 * ----------------------------------------------------------------------- */
int um_accounts_list(UmAccount *out, int cap)
{
    um_lock(&g_lock);
    ensure_first();
    int n = 0;
    UINT16 w[64];
    for (UINT32 i = 0; n < cap; i++) {
        int len = um_registry_enum_key(USERS_KEY, i, w, 63, NULL);
        if (len < 0) break;
        UINT32 rid = 0;
        bool hex = len == 8;
        for (int j = 0; hex && j < len; j++) {
            UINT16 c = w[j];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0) hex = false;
            rid = rid << 4 | (UINT32)(d & 15);
        }
        if (!hex || !read_account(rid, &out[n])) continue;
        /* by RID */
        UmAccount t = out[n];
        int j = n;
        for (; j > 0 && out[j - 1].rid > t.rid; j--) out[j] = out[j - 1];
        out[j] = t;
        n++;
    }
    um_unlock(&g_lock);
    return n;
}

bool um_account_get(UINT32 rid, UmAccount *a)
{
    um_lock(&g_lock);
    ensure_first();
    bool ok = read_account(rid, a);
    um_unlock(&g_lock);
    return ok;
}

bool um_account_find(const char *name, UmAccount *a)
{
    UmAccount *all = kmalloc(sizeof(UmAccount) * MAX_ACCOUNTS);
    if (!all) return false;
    int n = um_accounts_list(all, MAX_ACCOUNTS);
    bool ok = false;
    for (int i = 0; i < n && !ok; i++)
        if (ieq(all[i].name, name)) { *a = all[i]; ok = true; }
    kfree(all);
    return ok;
}

/* -----------------------------------------------------------------------
 * Passwords
 * ----------------------------------------------------------------------- */
static bool hash_password(const char *pw, const UINT8 salt[16], UINT32 blocks, UINT32 passes, UINT8 out[32])
{
    void *work = kmalloc((size_t)blocks * 1024);
    if (!work) return false;
    crypto_argon2_config cfg = { CRYPTO_ARGON2_ID, blocks, passes, 1 };
    crypto_argon2_inputs in = { (const uint8_t *)pw, salt, (uint32_t)strlen(pw), 16 };
    crypto_argon2(out, 32, work, cfg, in, crypto_argon2_no_extras);
    crypto_wipe(work, (size_t)blocks * 1024);
    kfree(work);
    return true;
}

/* Store @pw as rid's password ("" removes it); caller holds g_lock */
static UINT32 store_password(UINT32 rid, const char *pw)
{
    char k[96];
    users_key(rid, k, sizeof(k));
    if (!pw[0]) { um_registry_delete_value(k, "Hash"); return ST_SUCCESS; }
    UINT8 h[HASH_LEN];
    UINT32 blocks = HASH_BLOCKS, passes = HASH_PASSES;
    h[0] = HASH_VERSION;
    memcpy(h + 1, &blocks, 4);
    memcpy(h + 5, &passes, 4);
    TlsEntropyOutput(h + 9, 16);
    if (!hash_password(pw, h + 9, blocks, passes, h + 25)) return ST_NO_MEMORY;
    um_registry_set_bin(k, "Hash", h, sizeof(h));
    crypto_wipe(h, sizeof(h));
    return ST_SUCCESS;
}

/* Whether @pw is rid's password (an account without one takes "") */
static bool password_ok(UINT32 rid, const char *pw)
{
    char k[96];
    users_key(rid, k, sizeof(k));
    UINT8 h[HASH_LEN], got[32];
    int len = um_registry_get_bin(k, "Hash", h, sizeof(h));
    if (len < 0) return !pw[0];
    if (len != HASH_LEN || h[0] != HASH_VERSION) return false;
    UINT32 blocks, passes;
    memcpy(&blocks, h + 1, 4);
    memcpy(&passes, h + 5, 4);
    if (blocks < 8 || blocks > 65536 || !passes || passes > 16) return false;
    if (!hash_password(pw, h + 9, blocks, passes, got)) return false;
    bool ok = crypto_verify32(got, h + 25) == 0;
    crypto_wipe(got, sizeof(got));
    return ok;
}

/* (not under g_lock) */
static void wait_a_second(void)
{
    for (int i = 0; i < 100; i++) sched_sleep_tick();
}

/* Sign-in: @name's account if @pw is its password.  A wrong one waits a
 * second (as Windows does) so passwords cannot be tried quickly */
UINT32 um_account_logon(const char *name, const char *pw, UmAccount *a)
{
    if (!um_account_find(name, a)) { wait_a_second(); return ST_LOGON_FAILURE; }
    um_lock(&g_lock);
    bool ok = password_ok(a->rid, pw);
    um_unlock(&g_lock);
    if (!ok) {
        kprintf("[ACCT] Sign-in as %s refused: wrong password\n", a->name);
        wait_a_second();
        return ST_LOGON_FAILURE;
    }
    if (a->flags & UM_ACCT_DISABLED) return ST_ACCOUNT_DISABLED;
    return ST_SUCCESS;
}

bool um_account_verify(UINT32 rid, const char *pw)
{
    um_lock(&g_lock);
    bool ok = password_ok(rid, pw);
    um_unlock(&g_lock);
    return ok;
}

bool um_account_check(UINT32 rid, const char *pw)
{
    bool ok = um_account_verify(rid, pw);
    if (!ok) wait_a_second();
    return ok;
}

/* -----------------------------------------------------------------------
 * Changes
 * ----------------------------------------------------------------------- */
UINT32 um_account_add(const char *name, const char *pw, UINT32 flags, UINT32 *rid_out)
{
    if (!um_account_name_ok(name)) return ST_INVALID_ACCOUNT_NAME;
    if (strlen(pw) > UM_PASSWORD_MAX) return ST_PASSWORD_RESTRICTION;
    UmAccount a;
    if (um_account_find(name, &a)) return ST_USER_EXISTS;
    um_lock(&g_lock);
    UINT32 rid = FIRST_RID + 1;
    um_registry_get_dword(SAM_KEY, "NextRid", &rid);
    if (rid <= FIRST_RID) rid = FIRST_RID + 1;
    char k[96];
    users_key(rid, k, sizeof(k));
    um_registry_set_dword(SAM_KEY, "NextRid", rid + 1);          /* (RIDs are never reused) */
    um_registry_set_sz(k, "Name", name);
    um_registry_set_sz(k, "FullName", "");
    um_registry_set_dword(k, "Flags", flags & (UM_ACCT_ADMIN | UM_ACCT_DISABLED));
    UINT32 st = store_password(rid, pw);
    um_unlock(&g_lock);
    if (st) { um_registry_delete_tree(k); return st; }
    char sid[48];
    um_account_sid_string(rid, sid, sizeof(sid));
    kprintf("[ACCT] Account %s added (%s, %s)\n", name, sid, (flags & UM_ACCT_ADMIN) ? "administrator" : "standard user");
    if (rid_out) *rid_out = rid;
    return ST_SUCCESS;
}

UINT32 um_account_set_password(UINT32 rid, const char *pw)
{
    if (strlen(pw) > UM_PASSWORD_MAX) return ST_PASSWORD_RESTRICTION;
    UmAccount a;
    if (!um_account_get(rid, &a)) return ST_NO_SUCH_USER;
    um_lock(&g_lock);
    UINT32 st = store_password(rid, pw);
    um_unlock(&g_lock);
    if (!st) kprintf("[ACCT] %s's password %s\n", a.name, pw[0] ? "set" : "removed");
    return st;
}

bool um_account_rename(UINT32 rid, const char *name)
{
    UmAccount a, other;
    if (!um_account_name_ok(name) || !um_account_get(rid, &a)) return false;
    if (um_account_find(name, &other) && other.rid != rid) return false;
    char k[96];
    users_key(rid, k, sizeof(k));
    um_lock(&g_lock);
    um_registry_set_sz(k, "Name", name);
    um_unlock(&g_lock);
    return true;
}

/* Delete @dir and everything in it; caller holds FsLock */
static void delete_tree(RamNode *dir)
{
    while (dir->child) delete_tree(dir->child);
    RamfsDelete(dir);
}

UINT32 um_account_delete(UINT32 rid)
{
    UmAccount a;
    if (!um_account_get(rid, &a)) return ST_NO_SUCH_USER;
    if (rid == um_session_rid()) return ST_ACCESS_DENIED;          /* (signed in) */
    if (a.flags & UM_ACCT_ADMIN) {
        UmAccount *all = kmalloc(sizeof(UmAccount) * MAX_ACCOUNTS);
        if (!all) return ST_NO_MEMORY;
        int n = um_accounts_list(all, MAX_ACCOUNTS), admins = 0;
        for (int i = 0; i < n; i++) if ((all[i].flags & UM_ACCT_ADMIN) && !(all[i].flags & UM_ACCT_DISABLED)) admins++;
        kfree(all);
        if (admins <= 1) return ST_LAST_ADMIN;
    }
    char k[96];
    users_key(rid, k, sizeof(k));
    um_lock(&g_lock);
    um_registry_delete_tree(k);
    um_unlock(&g_lock);
    kprintf("[ACCT] Account %s deleted\n", a.name);
    return ST_SUCCESS;
}

/* DeleteProfile: the folder, the HKEY_USERS hive and the ProfileList
 * entry of @rid (whose account may be gone already) */
UINT32 um_account_delete_profile(UINT32 rid)
{
    if (rid == um_session_rid()) return ST_ACCESS_DENIED;
    char sid[48], pk[160], hk[80], path[96];
    um_account_sid_string(rid, sid, sizeof(sid));
    ksnprintf(pk, sizeof(pk), PROFILE_LIST "\\%s", sid);
    if (!um_registry_get_sz(pk, "ProfileImagePath", path, sizeof(path))) return ST_NO_SUCH_USER;
    if (strcmp(path, "C:\\") && !strncmp(path, "C:\\Users\\", 9) && path[9]) {   /* (never the first account's C:\) */
        FsLock();
        RamNode *d = RamfsResolve(NULL, path + 2);
        if (d && d->dir) delete_tree(d);
        FsUnlock();
    }
    ksnprintf(hk, sizeof(hk), "User\\%s", sid);
    um_registry_delete_tree(hk);
    um_registry_delete_tree(pk);
    kprintf("[ACCT] Profile %s deleted\n", path);
    return ST_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Profiles
 * ----------------------------------------------------------------------- */
/* The profile's DACL, protected and inherited by everything in it: the
 * user, SYSTEM and Administrators have full control (as Windows makes
 * C:\Users\<name>) */
static void protect_profile(RamNode *d, UINT32 rid)
{
    UINT8 user[28];
    um_account_sid(rid, user);
    static const UINT8 system[] = { 1, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0 };
    static const UINT8 admins[] = { 1, 2, 0, 0, 0, 0, 0, 5, 0x20, 0, 0, 0, 0x20, 2, 0, 0 };
    const UINT8 *sids[3] = { system, admins, user };
    const UINT32 lens[3] = { sizeof(system), sizeof(admins), sizeof(user) };
    UINT8 sd[20 + 28 + 8 + 3 * (8 + 28)];
    memset(sd, 0, sizeof(sd));
    UINT32 off = 20;
    memcpy(sd + off, user, 28);                                     /* owner */
    UINT32 owner = off;
    off += 28;
    UINT32 dacl = off, a = off + 8;
    for (int i = 0; i < 3; i++) {
        UINT16 size = (UINT16)(8 + lens[i]);
        sd[a] = 0;                                                  /* ACCESS_ALLOWED */
        sd[a + 1] = 0x03;                                           /* OBJECT_INHERIT | CONTAINER_INHERIT */
        memcpy(sd + a + 2, &size, 2);
        UINT32 mask = 0x001F01FFu;                                  /* FILE_ALL_ACCESS */
        memcpy(sd + a + 4, &mask, 4);
        memcpy(sd + a + 8, sids[i], lens[i]);
        a += size;
    }
    UINT16 acl_size = (UINT16)(a - dacl), count = 3;
    sd[dacl] = 2;                                                   /* ACL_REVISION */
    memcpy(sd + dacl + 2, &acl_size, 2);
    memcpy(sd + dacl + 4, &count, 2);
    sd[0] = 1;
    UINT16 control = 0x8000 | 0x1000 | 0x0004;                      /* SELF_RELATIVE, DACL_PROTECTED, DACL_PRESENT */
    memcpy(sd + 2, &control, 2);
    memcpy(sd + 4, &owner, 4);
    memcpy(sd + 16, &dacl, 4);
    FsecSet(d, 1 | 4, sd, a);
}

static RamNode *mkdir_in(RamNode *dir, const char *name)
{
    RamNode *n = RamfsFind(dir, name);
    return n ? n : RamfsCreate(dir, name, true);
}

/* Make sure @rid's profile is there (the first sign-in makes it): its
 * folders, its HKEY_USERS hive and its ProfileList entry; its path in @path */
bool um_account_load_profile(UINT32 rid, char *path, int cap)
{
    UmAccount a;
    if (!um_account_get(rid, &a)) return false;
    char sid[48], pk[160], hk[80];
    um_account_sid_string(rid, sid, sizeof(sid));
    ksnprintf(hk, sizeof(hk), "User\\%s\\Software", sid);
    um_registry_create_key(hk);
    if (!a.profile[0]) {
        FsLock();
        RamNode *root = RamfsResolve(NULL, "\\");
        RamNode *users = root ? mkdir_in(root, "Users") : NULL;
        char dir[64];
        ksnprintf(dir, sizeof(dir), "%s", a.name);
        if (users && RamfsFind(users, dir)) ksnprintf(dir, sizeof(dir), "%s.NOVA-PC", a.name);   /* (an old one's folder) */
        RamNode *home = users && !RamfsFind(users, dir) ? RamfsCreate(users, dir, true) : NULL;
        if (home) {
            protect_profile(home, rid);
            static const char *const sub[] = { "Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos" };
            for (unsigned i = 0; i < sizeof(sub) / sizeof(sub[0]); i++) mkdir_in(home, sub[i]);
            RamNode *ad = mkdir_in(home, "AppData");
            RamNode *lo = ad ? mkdir_in(ad, "Local") : NULL;
            if (ad) { mkdir_in(ad, "Roaming"); mkdir_in(ad, "LocalLow"); }
            if (lo) mkdir_in(lo, "Temp");
        }
        FsUnlock();
        if (!home) return false;
        ksnprintf(a.profile, sizeof(a.profile), "C:\\Users\\%s", dir);
        ksnprintf(pk, sizeof(pk), PROFILE_LIST "\\%s", sid);
        um_registry_set_sz(pk, "ProfileImagePath", a.profile);
        um_registry_set_dword(pk, "State", 0);
        kprintf("[ACCT] Profile %s made for %s\n", a.profile, a.name);
    }
    if (path) ksnprintf(path, (size_t)cap, "%s", a.profile);
    return true;
}

/* -----------------------------------------------------------------------
 * The interactive session
 * ----------------------------------------------------------------------- */
UINT32 um_session_rid(void)
{
    UINT32 rid = g_session_rid;
    if (rid) return rid;
    char s[48];
    UmAccount a;
    rid = FIRST_RID;
    if (um_registry_get_sz(LOGONUI_KEY, "LastLoggedOnUserSID", s, sizeof(s))) {
        UINT32 r = 0;
        const char *p = strrchr(s, '-');
        for (p = p ? p + 1 : s; *p >= '0' && *p <= '9'; p++) r = r * 10 + (UINT32)(*p - '0');
        if (!strncmp(s, "S-1-5-21-1000-2000-3000-", 24) && um_account_get(r, &a) && !(a.flags & UM_ACCT_DISABLED)) rid = r;
    }
    UINT32 none = 0;
    __atomic_compare_exchange_n(&g_session_rid, &none, rid, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return g_session_rid;
}

void um_session_set(UINT32 rid)
{
    UmAccount a;
    if (!um_account_get(rid, &a)) return;
    __atomic_store_n(&g_session_rid, rid, __ATOMIC_RELEASE);
    char sid[48], last[32];
    um_account_sid_string(rid, sid, sizeof(sid));
    ksnprintf(last, sizeof(last), ".\\%s", a.name);
    um_registry_set_sz(LOGONUI_KEY, "LastLoggedOnUserSID", sid);
    um_registry_set_sz(LOGONUI_KEY, "LastLoggedOnSAMUser", last);
    um_account_load_profile(rid, NULL, 0);
    kprintf("[ACCT] %s signed in (%s)\n", a.name, sid);
}

/* Whether the sign-in screen must come before the desktop: the last
 * account to sign in has a password, or there are others to choose */
bool um_account_signin_required(void)
{
    UmAccount *all = kmalloc(sizeof(UmAccount) * MAX_ACCOUNTS);
    if (!all) return false;
    int n = um_accounts_list(all, MAX_ACCOUNTS), usable = 0;
    UINT32 me = um_session_rid();
    bool pw = false;
    for (int i = 0; i < n; i++) {
        if (all[i].flags & UM_ACCT_DISABLED) continue;
        usable++;
        if (all[i].rid == me) pw = all[i].password;
    }
    kfree(all);
    return pw || usable > 1;
}

/* -----------------------------------------------------------------------
 * NtNovaAccount(ULONG Op, PVOID In, ULONG InLength, PVOID Out, ULONG OutLength, PULONG Returned)
 *
 *   In: NOVA_ACCOUNT_REQUEST { ULONG Rid, Flags; CHAR Name[21], Password[257], OldPassword[257]; }
 *   Out: NOVA_ACCOUNT records { ULONG Rid, Flags, HasPassword; CHAR Name[21],
 *        FullName[64], Profile[96]; } (LIST: one per account), or a HANDLE
 * ----------------------------------------------------------------------- */
enum {
    ACCT_LIST, ACCT_GET, ACCT_FIND, ACCT_ADD, ACCT_DELETE, ACCT_SET_PASSWORD, ACCT_CHANGE_PASSWORD,
    ACCT_LOGON, ACCT_LOAD_PROFILE, ACCT_LOCK, ACCT_DELETE_PROFILE, ACCT_SESSION,
};

typedef struct { UINT32 rid, flags; char name[UM_ACCOUNT_NAME_MAX + 1], password[UM_PASSWORD_MAX + 1], old[UM_PASSWORD_MAX + 1]; } AcctRequest;
typedef struct { UINT32 rid, flags, password; char name[UM_ACCOUNT_NAME_MAX + 1], full[64], profile[96]; } AcctRecord;

static void to_record(const UmAccount *a, AcctRecord *r)
{
    memset(r, 0, sizeof(*r));
    r->rid = a->rid;
    r->flags = a->flags;
    r->password = a->password;
    memcpy(r->name, a->name, sizeof(r->name));
    memcpy(r->full, a->full, sizeof(r->full));
    memcpy(r->profile, a->profile, sizeof(r->profile));
}

static UINT32 give(UINT64 out, UINT64 cap, const void *data, UINT32 len, UINT64 ret)
{
    if (ret && !NT_SUCCESS(CopyToUser((void *)(uintptr_t)ret, &len, 4))) return ST_ACCESS_VIOLATION;
    if (cap < len) return ST_BUFFER_TOO_SMALL;
    if (len && !NT_SUCCESS(CopyToUser((void *)(uintptr_t)out, data, len))) return ST_ACCESS_VIOLATION;
    return ST_SUCCESS;
}

static UINT64 sys_nova_account(UINT64 op, UINT64 in_p, UINT64 in_len, UINT64 out_p)
{
    UINT64 out_len = um_stack_arg(5), ret = um_stack_arg(6);
    AcctRequest *rq = kzalloc(sizeof(*rq));
    if (!rq) return ST_NO_MEMORY;
    UINT32 st = ST_SUCCESS;
    if (in_p) {
        if (in_len != sizeof(*rq)) { kfree(rq); return ST_INVALID_PARAMETER; }
        if (!NT_SUCCESS(CopyFromUser(rq, (const void *)(uintptr_t)in_p, sizeof(*rq)))) { kfree(rq); return ST_ACCESS_VIOLATION; }
        rq->name[UM_ACCOUNT_NAME_MAX] = 0;
        rq->password[UM_PASSWORD_MAX] = 0;
        rq->old[UM_PASSWORD_MAX] = 0;
    } else if (op != ACCT_LIST && op != ACCT_LOCK && op != ACCT_SESSION) {
        kfree(rq);
        return ST_INVALID_PARAMETER;
    }
    UmAccount a;
    AcctRecord r;
    bool admin = um_caller_is_admin();
    switch (op) {
    case ACCT_LIST: {
        UmAccount *all = kmalloc(sizeof(UmAccount) * MAX_ACCOUNTS);
        AcctRecord *recs = kmalloc(sizeof(AcctRecord) * MAX_ACCOUNTS);
        if (!all || !recs) { kfree(all); kfree(recs); st = ST_NO_MEMORY; break; }
        int n = um_accounts_list(all, MAX_ACCOUNTS);
        for (int i = 0; i < n; i++) to_record(&all[i], &recs[i]);
        st = give(out_p, out_len, recs, (UINT32)(n * sizeof(AcctRecord)), ret);
        kfree(all);
        kfree(recs);
        break;
    }
    case ACCT_GET: case ACCT_FIND:
        if (op == ACCT_GET ? !um_account_get(rq->rid, &a) : !um_account_find(rq->name, &a)) { st = ST_NO_SUCH_USER; break; }
        to_record(&a, &r);
        st = give(out_p, out_len, &r, sizeof(r), ret);
        break;
    case ACCT_ADD: {
        if (!admin) { st = ST_ACCESS_DENIED; break; }
        UINT32 rid = 0;
        st = um_account_add(rq->name, rq->password, rq->flags, &rid);
        if (!st && out_p && um_account_get(rid, &a)) { to_record(&a, &r); st = give(out_p, out_len, &r, sizeof(r), ret); }
        break;
    }
    case ACCT_DELETE:
        st = !admin ? ST_ACCESS_DENIED : um_account_delete(rq->rid);
        break;
    case ACCT_DELETE_PROFILE:
        st = !admin ? ST_ACCESS_DENIED : um_account_delete_profile(rq->rid);
        break;
    case ACCT_SET_PASSWORD:                                         /* an administrator resets it */
        st = !admin ? ST_ACCESS_DENIED : um_account_set_password(rq->rid, rq->password);
        break;
    case ACCT_CHANGE_PASSWORD: {                                    /* the user changes it, with the old one */
        if (!um_account_get(rq->rid, &a)) { st = ST_NO_SUCH_USER; break; }
        if (!um_account_check(rq->rid, rq->old)) { st = ST_WRONG_PASSWORD; break; }
        st = um_account_set_password(rq->rid, rq->password);
        break;
    }
    case ACCT_LOGON: {                                              /* LogonUser: a primary token */
        st = um_account_logon(rq->name, rq->password, &a);
        if (st) break;
        UmObject *t = um_token_new_user(a.rid, false);
        if (!t) { st = ST_NO_MEMORY; break; }
        UINT64 h = um_handle_new_object(UmCurrent(), t);
        um_ob_unref(t);
        if (!h) { st = ST_TOO_MANY_HANDLES; break; }
        UmProcess *me = UmCurrent();
        st = me && me->wow ? give(out_p, out_len, &(UINT32){ (UINT32)h }, 4, ret) : give(out_p, out_len, &h, 8, ret);
        if (st) um_close_handle(h);
        else kprintf("[ACCT] %s signed in with LogonUser\n", a.name);
        break;
    }
    case ACCT_LOAD_PROFILE: {                                       /* LoadUserProfile: own, or any for an administrator */
        UINT32 mine = um_caller_rid();
        if (rq->rid != mine && !admin) { st = ST_ACCESS_DENIED; break; }
        if (!um_account_load_profile(rq->rid, NULL, 0) || !um_account_get(rq->rid, &a)) { st = ST_NO_SUCH_USER; break; }
        to_record(&a, &r);
        st = give(out_p, out_len, &r, sizeof(r), ret);
        break;
    }
    case ACCT_LOCK:                                                 /* LockWorkStation */
        LogonLock();
        break;
    case ACCT_SESSION: {                                            /* who is signed in, and whether the screen is locked */
        UINT32 s[2] = { um_session_rid(), LogonActive() ? 1u : 0u };
        st = give(out_p, out_len, s, sizeof(s), ret);
        break;
    }
    default:
        st = ST_INVALID_INFO_CLASS;
    }
    crypto_wipe(rq, sizeof(*rq));
    kfree(rq);
    return st;
}

void um_accounts_syscalls_init(void)
{
    um_install(SYSCALL_NtNovaAccount, sys_nova_account);
    um_lock_free(SYSCALL_NtNovaAccount);           /* (hashing a password takes a moment) */
}
