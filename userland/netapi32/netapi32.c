/*
 * netapi32.dll — accounts and groups (Net*), domain controllers (Ds*).
 *
 * A standalone machine: its local accounts are the kernel's
 * (kernel/um/um_accounts.c, through NtNovaAccount), administrators in the
 * local Administrators and Users groups, standard users in Users; no
 * domain, no network shares.
 */
#include <winternl.h>
#include <windows.h>

#define NETAPI __declspec(dllexport)
#define NERR_Success         0
#define NERR_UserNotFound    2221
#define NERR_UserExists      2224
#define NERR_LastAdmin       2452
#define NERR_PasswordTooShort 2245
#define NERR_BadUsername     2202
#define ERROR_INVALID_PASSWORD_ 86
#define NERR_GroupNotFound   2220
#define NERR_UseNotFound     2250
#define NERR_InvalidComputer 2351
#define ERROR_NO_SUCH_DOMAIN_ 1355
#ifndef ERROR_INVALID_LEVEL
#define ERROR_INVALID_LEVEL 124
#endif
typedef DWORD NET_API_STATUS;

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

static int wlen(const WCHAR *s) { int n = 0; if (s) while (s[n]) n++; return n; }
static int weq(const WCHAR *a, const WCHAR *b)
{
    for (; *a && *b; a++, b++) {
        WCHAR x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return !*a && !*b;
}

NETAPI NET_API_STATUS WINAPI NetApiBufferAllocate(DWORD n, LPVOID *buf)
{
    *buf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1);
    return *buf ? NERR_Success : ERROR_NOT_ENOUGH_MEMORY;
}
NETAPI NET_API_STATUS WINAPI NetApiBufferFree(LPVOID buf) { if (buf) HeapFree(GetProcessHeap(), 0, buf); return NERR_Success; }

/* -----------------------------------------------------------------------
 * The kernel's accounts
 * ----------------------------------------------------------------------- */
static void to_utf8(const WCHAR *w, char *out, int cap)
{
    out[0] = 0;
    if (w && !WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, 0, 0)) out[0] = 0;
}
static void to_wide(const char *s, WCHAR *out, int cap)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap)) out[0] = 0;
}

static NTSTATUS acct_call_old(ULONG op, ULONG rid, const WCHAR *name, const WCHAR *pw, const WCHAR *old, ULONG flags,
                              NOVA_ACCOUNT *out)
{
    NOVA_ACCOUNT_REQUEST rq;
    memset(&rq, 0, sizeof(rq));
    rq.Rid = rid;
    rq.Flags = flags;
    if (name && wlen(name) > 20) return (NTSTATUS)0xC0000062;      /* STATUS_INVALID_ACCOUNT_NAME */
    if ((pw && wlen(pw) > 256) || (old && wlen(old) > 256)) return (NTSTATUS)0xC000006C;   /* STATUS_PASSWORD_RESTRICTION */
    to_utf8(name, rq.Name, sizeof(rq.Name));
    to_utf8(pw, rq.Password, sizeof(rq.Password));
    to_utf8(old, rq.OldPassword, sizeof(rq.OldPassword));
    ULONG got;
    NTSTATUS st = NtNovaAccount(op, &rq, sizeof(rq), out, out ? sizeof(*out) : 0, &got);
    SecureZeroMemory(&rq, sizeof(rq));
    return st;
}

static NTSTATUS acct_call(ULONG op, ULONG rid, const WCHAR *name, const WCHAR *pw, ULONG flags, NOVA_ACCOUNT *out)
{
    return acct_call_old(op, rid, name, pw, 0, flags, out);
}

static NET_API_STATUS net_status(NTSTATUS st)
{
    switch ((DWORD)st) {
    case 0: return NERR_Success;
    case 0xC0000064: return NERR_UserNotFound;
    case 0xC0000063: return NERR_UserExists;
    case 0xC0000062: return NERR_BadUsername;
    case 0xC0000069: return NERR_LastAdmin;
    case 0xC000006A: return ERROR_INVALID_PASSWORD_;
    case 0xC000006C: return NERR_PasswordTooShort;
    case 0xC0000022: return ERROR_ACCESS_DENIED;
    }
    return RtlNtStatusToDosError(st);
}

/* @user's account (name, any case); false when there is none */
static BOOL find_user(const WCHAR *user, NOVA_ACCOUNT *a) { return user && !acct_call(NOVA_ACCOUNT_FIND, 0, user, 0, 0, a); }

/* Every account; the caller frees the list */
static NOVA_ACCOUNT *all_users(int *n)
{
    NOVA_ACCOUNT *list = HeapAlloc(GetProcessHeap(), 0, 32 * sizeof(NOVA_ACCOUNT));
    ULONG got = 0;
    *n = 0;
    if (!list) return 0;
    if (!NtNovaAccount(NOVA_ACCOUNT_LIST, 0, 0, list, 32 * sizeof(NOVA_ACCOUNT), &got)) *n = (int)(got / sizeof(NOVA_ACCOUNT));
    return list;
}

#define UF_SCRIPT              0x0001
#define UF_ACCOUNTDISABLE      0x0002
#define UF_PASSWD_NOTREQD      0x0020
#define UF_NORMAL_ACCOUNT      0x0200
#define UF_DONT_EXPIRE_PASSWD  0x10000
static DWORD user_flags(const NOVA_ACCOUNT *a)
{
    return UF_SCRIPT | UF_NORMAL_ACCOUNT | UF_DONT_EXPIRE_PASSWD | ((a->Flags & NOVA_ACCOUNT_DISABLED) ? UF_ACCOUNTDISABLE : 0);
}

/* A buffer of @head bytes of structure followed by room for strings */
typedef struct { BYTE *base, *tail; } Pack;
static BOOL pack_new(Pack *p, DWORD head, DWORD strings)
{
    if (NetApiBufferAllocate(head + strings, (LPVOID *)&p->base)) return FALSE;
    p->tail = p->base + ((head + 7) & ~7u);
    return TRUE;
}
static LPWSTR pack_str(Pack *p, const WCHAR *s)
{
    int n = wlen(s);
    LPWSTR w = (LPWSTR)p->tail;
    memcpy(w, s, 2 * (size_t)(n + 1));
    p->tail += 2 * (n + 1);
    return w;
}

static const WCHAR *const g_local_groups[] = { L"Administrators", L"Users" };
static const WCHAR *const g_local_group_comments[] = {
    L"Administrators have complete and unrestricted access to the computer/domain",
    L"Users are prevented from making accidental or intentional system-wide changes" };

typedef struct {                                         /* USER_INFO_3 */
    LPWSTR name, password; DWORD password_age, priv; LPWSTR home_dir, comment; DWORD flags; LPWSTR script_path;
    DWORD auth_flags; LPWSTR full_name, usr_comment, parms, workstations; DWORD last_logon, last_logoff,
    acct_expires, max_storage, units_per_week; PBYTE logon_hours; DWORD bad_pw_count, num_logons;
    LPWSTR logon_server; DWORD country_code, code_page, user_id, primary_group_id; LPWSTR profile, home_dir_drive;
    DWORD password_expired;
} UI3;

NETAPI NET_API_STATUS WINAPI NetUserGetInfo(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE *buf)
{
    (void)server;
    NOVA_ACCOUNT a;
    *buf = 0;
    if (!find_user(user, &a)) return NERR_UserNotFound;
    WCHAR me[32], full[64], profile[96];
    to_wide(a.Name, me, 32);
    to_wide(a.FullName[0] ? a.FullName : a.Name, full, 64);
    to_wide(a.Profile, profile, 96);
    Pack p;
    switch (level) {
    case 0: case 10: case 1: case 2: case 3: case 4: case 11: case 20: case 23: {
        /* every level starts with the name; the larger ones are given as USER_INFO_3/4 */
        if (!pack_new(&p, sizeof(UI3) + 64, 1024)) return ERROR_NOT_ENOUGH_MEMORY;
        UI3 *u = (UI3 *)p.base;
        if (level == 0) { *(LPWSTR *)p.base = pack_str(&p, me); break; }
        if (level == 10 || level == 20 || level == 23) {    /* name, comment/full name... */
            LPWSTR *s = (LPWSTR *)p.base;
            s[0] = pack_str(&p, me);
            s[1] = pack_str(&p, level == 10 ? L"" : full);  /* 10: comment; 20/23: full_name */
            s[2] = pack_str(&p, level == 10 ? L"" : L""); /* 10: usr_comment; 20/23: comment */
            s[3] = pack_str(&p, full);                     /* 10: full_name */
            if (level == 20 || level == 23) { ((DWORD *)(s + 3))[0] = user_flags(&a); ((DWORD *)(s + 3))[1] = a.Rid; }
            break;
        }
        u->name = pack_str(&p, me);
        u->password = 0;
        u->priv = (a.Flags & NOVA_ACCOUNT_ADMIN) ? 2 : 1;   /* USER_PRIV_ADMIN, USER_PRIV_USER */
        u->home_dir = pack_str(&p, L"");
        u->comment = pack_str(&p, L"");
        u->flags = user_flags(&a);
        u->script_path = pack_str(&p, L"");
        u->full_name = pack_str(&p, full);
        u->usr_comment = pack_str(&p, L"");
        u->parms = pack_str(&p, L"");
        u->workstations = pack_str(&p, L"");
        u->acct_expires = 0xFFFFFFFF;                       /* TIMEQ_FOREVER */
        u->max_storage = 0xFFFFFFFF;
        u->units_per_week = 168;
        u->logon_server = pack_str(&p, L"\\\\*");
        u->user_id = a.Rid;
        u->primary_group_id = 513;
        u->profile = pack_str(&p, profile);
        u->home_dir_drive = pack_str(&p, L"");
        break;
    }
    case 24: {                                              /* USER_INFO_24: not a Microsoft account */
        if (!pack_new(&p, 48, 16)) return ERROR_NOT_ENOUGH_MEMORY;
        break;
    }
    default:
        return ERROR_INVALID_LEVEL;
    }
    *buf = p.base;
    return NERR_Success;
}

/* Lists of names (level 0 of the various enumerations) */
static NET_API_STATUS name_list(const WCHAR *const *names, const WCHAR *const *comments, int n, DWORD level,
                                LPBYTE *buf, LPDWORD read, LPDWORD total)
{
    DWORD per = level ? 2 : 1;
    Pack p;
    if (!pack_new(&p, (DWORD)(n * per * sizeof(LPWSTR)), 2048)) return ERROR_NOT_ENOUGH_MEMORY;
    LPWSTR *s = (LPWSTR *)p.base;
    for (int i = 0; i < n; i++) {
        s[i * per] = pack_str(&p, names[i]);
        if (per == 2) s[i * per + 1] = pack_str(&p, comments ? comments[i] : L"");
    }
    *buf = p.base;
    if (read) *read = (DWORD)n;
    if (total) *total = (DWORD)n;
    return NERR_Success;
}

NETAPI NET_API_STATUS WINAPI NetUserGetLocalGroups(LPCWSTR server, LPCWSTR user, DWORD level, DWORD flags, LPBYTE *buf,
                                                   DWORD pref, LPDWORD read, LPDWORD total)
{
    (void)server; (void)flags; (void)pref;
    if (level) return ERROR_INVALID_LEVEL;
    NOVA_ACCOUNT a;
    if (!find_user(user, &a)) { *buf = 0; return NERR_UserNotFound; }
    BOOL admin = (a.Flags & NOVA_ACCOUNT_ADMIN) != 0;               /* (Administrators, Users; or Users) */
    return name_list(g_local_groups + !admin, 0, admin ? 2 : 1, 0, buf, read, total);
}
NETAPI NET_API_STATUS WINAPI NetUserGetGroups(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE *buf, DWORD pref,
                                              LPDWORD read, LPDWORD total)
{
    (void)server; (void)user; (void)pref;
    static const WCHAR *const none[] = { L"None" };
    if (level > 1) return ERROR_INVALID_LEVEL;
    return name_list(none, 0, 1, 0, buf, read, total);
}
NETAPI NET_API_STATUS WINAPI NetLocalGroupEnum(LPCWSTR server, DWORD level, LPBYTE *buf, DWORD pref, LPDWORD read,
                                               LPDWORD total, PDWORD_PTR resume)
{
    (void)server; (void)pref;
    if (level > 1) return ERROR_INVALID_LEVEL;
    if (resume && *resume) { *buf = 0; *read = *total = 0; return NERR_Success; }
    if (resume) *resume = 0;
    return name_list(g_local_groups, g_local_group_comments, 2, level, buf, read, total);
}
NETAPI NET_API_STATUS WINAPI NetLocalGroupGetInfo(LPCWSTR server, LPCWSTR group, DWORD level, LPBYTE *buf)
{
    (void)server;
    for (int i = 0; i < 2; i++)
        if (group && weq(group, g_local_groups[i])) {
            DWORD rd, tot;
            return name_list(&g_local_groups[i], &g_local_group_comments[i], 1, level ? 1 : 0, buf, &rd, &tot);
        }
    *buf = 0;
    return NERR_GroupNotFound;
}
NETAPI NET_API_STATUS WINAPI NetGroupEnum(LPCWSTR server, DWORD level, LPBYTE *buf, DWORD pref, LPDWORD read,
                                          LPDWORD total, PDWORD_PTR resume)
{
    (void)server; (void)level; (void)pref; (void)resume;
    *buf = 0; *read = 0; *total = 0;                       /* no global groups on a workstation */
    return NERR_Success;
}
/* Shares: NovaOS runs no file server, so this machine shares nothing, and
 * with no SMB client another machine's shares cannot be listed */
NETAPI NET_API_STATUS WINAPI NetShareEnum(LPWSTR server, DWORD level, LPBYTE *buf, DWORD pref, LPDWORD read,
                                          LPDWORD total, LPDWORD resume)
{
    (void)pref;
    if (!buf || !read || !total) return ERROR_INVALID_PARAMETER;
    *buf = 0; *read = *total = 0;
    if (resume) *resume = 0;
    if (level > 2 && level != 502 && level != 503) return ERROR_INVALID_LEVEL;
    if (server && server[0]) {
        WCHAR me[64];
        DWORD n = 64;
        const WCHAR *s = server;
        while (*s == '\\') s++;
        if (!GetComputerNameW(me, &n) || !weq(s, me)) return 53;    /* ERROR_BAD_NETPATH */
    }
    return NERR_Success;
}
/* Levels 0 (names), 1 and 20 of every account; the others one at a time
 * (NetUserGetInfo) */
NETAPI NET_API_STATUS WINAPI NetUserEnum(LPCWSTR server, DWORD level, DWORD filter, LPBYTE *buf, DWORD pref,
                                         LPDWORD read, LPDWORD total, PDWORD resume)
{
    (void)server; (void)filter; (void)pref;
    *buf = 0;
    if (read) *read = 0;
    if (total) *total = 0;
    if (resume && *resume) return NERR_Success;
    if (level != 0 && level != 1 && level != 20) return ERROR_INVALID_LEVEL;
    int n;
    NOVA_ACCOUNT *list = all_users(&n);
    if (!list) return ERROR_NOT_ENOUGH_MEMORY;
    DWORD each = level == 0 ? sizeof(LPWSTR) : level == 1 ? 8 * sizeof(LPWSTR) : 5 * sizeof(LPWSTR);
    Pack p;
    if (!pack_new(&p, each * (DWORD)n, (DWORD)n * 2 * (21 + 64 + 96 + 8))) { HeapFree(GetProcessHeap(), 0, list); return ERROR_NOT_ENOUGH_MEMORY; }
    for (int i = 0; i < n; i++) {
        WCHAR name[32], full[64];
        to_wide(list[i].Name, name, 32);
        to_wide(list[i].FullName, full, 64);
        BYTE *e = p.base + each * (DWORD)i;
        LPWSTR *s = (LPWSTR *)e;
        s[0] = pack_str(&p, name);
        if (level == 1) {                                   /* USER_INFO_1 */
            s[1] = 0;
            ((DWORD *)(s + 2))[0] = 0;                      /* password_age */
            ((DWORD *)(s + 2))[1] = (list[i].Flags & NOVA_ACCOUNT_ADMIN) ? 2 : 1;
            s[4] = pack_str(&p, L"");                       /* home_dir */
            s[5] = pack_str(&p, L"");                       /* comment */
            *(DWORD *)(s + 6) = user_flags(&list[i]);
            s[7] = pack_str(&p, L"");                       /* script_path */
        } else if (level == 20) {                           /* USER_INFO_20 */
            s[1] = pack_str(&p, full);
            s[2] = pack_str(&p, L"");
            ((DWORD *)(s + 3))[0] = user_flags(&list[i]);
            ((DWORD *)(s + 3))[1] = list[i].Rid;
        }
    }
    HeapFree(GetProcessHeap(), 0, list);
    *buf = p.base;
    if (read) *read = (DWORD)n;
    if (total) *total = (DWORD)n;
    return NERR_Success;
}

/* NetUserAdd: levels 1-4 (all begin as USER_INFO_1: name, password,
 * password_age, priv, ..., flags); USER_PRIV_ADMIN makes an
 * administrator, USER_PRIV_USER a standard user.  Administrators only */
NETAPI NET_API_STATUS WINAPI NetUserAdd(LPCWSTR server, DWORD level, LPBYTE buf, LPDWORD parm_err)
{
    (void)server;
    if (parm_err) *parm_err = 0;
    if (level < 1 || level > 4) return ERROR_INVALID_LEVEL;
    if (!buf) return ERROR_INVALID_PARAMETER;
    LPWSTR *s = (LPWSTR *)buf;
    DWORD priv = ((DWORD *)(s + 2))[1], flags = *(DWORD *)(s + 6);
    if (priv > 2) { if (parm_err) *parm_err = 5; return ERROR_INVALID_PARAMETER; }   /* USER_PRIV_PARMNUM */
    if (!s[0] || !s[0][0]) { if (parm_err) *parm_err = 1; return NERR_BadUsername; }
    ULONG f = (priv == 2 ? NOVA_ACCOUNT_ADMIN : 0) | ((flags & UF_ACCOUNTDISABLE) ? NOVA_ACCOUNT_DISABLED : 0);
    return net_status(acct_call(NOVA_ACCOUNT_ADD, 0, s[0], s[1] ? s[1] : L"", f, 0));
}

NETAPI NET_API_STATUS WINAPI NetUserDel(LPCWSTR server, LPCWSTR user)
{
    (void)server;
    NOVA_ACCOUNT a;
    if (!find_user(user, &a)) return NERR_UserNotFound;
    return net_status(acct_call(NOVA_ACCOUNT_DELETE, a.Rid, 0, 0, 0, 0));
}

/* NetUserSetInfo: level 1003 (USER_INFO_1003: a new password, an
 * administrator's reset); the others are accepted and change nothing */
NETAPI NET_API_STATUS WINAPI NetUserSetInfo(LPCWSTR server, LPCWSTR user, DWORD level, LPBYTE buf, LPDWORD parm_err)
{
    (void)server;
    if (parm_err) *parm_err = 0;
    NOVA_ACCOUNT a;
    if (!find_user(user, &a)) return NERR_UserNotFound;
    if (!buf) return ERROR_INVALID_PARAMETER;
    if (level == 1003) return net_status(acct_call(NOVA_ACCOUNT_SET_PASSWORD, a.Rid, 0, *(LPWSTR *)buf, 0, 0));
    return NERR_Success;
}

/* NetUserChangePassword: the user's own change, with the old password */
NETAPI NET_API_STATUS WINAPI NetUserChangePassword(LPCWSTR domain, LPCWSTR user, LPCWSTR old_pw, LPCWSTR new_pw)
{
    (void)domain;
    NOVA_ACCOUNT a;
    WCHAR me[64];
    DWORD n = 64;
    if (!user) { if (!GetUserNameW(me, &n)) return NERR_UserNotFound; user = me; }
    if (!find_user(user, &a)) return NERR_UserNotFound;
    return net_status(acct_call_old(NOVA_ACCOUNT_CHANGE_PASSWORD, a.Rid, 0, new_pw ? new_pw : L"", old_pw ? old_pw : L"", 0, 0));
}
NETAPI NET_API_STATUS WINAPI NetUseGetInfo(LPCWSTR server, LPCWSTR name, DWORD level, LPBYTE *buf)
{
    (void)server; (void)name; (void)level;
    *buf = 0;
    return NERR_UseNotFound;                                /* no network drives */
}
NETAPI NET_API_STATUS WINAPI NetWkstaGetInfo(LPCWSTR server, DWORD level, LPBYTE *buf)
{
    (void)server;
    if (level != 100) { *buf = 0; return ERROR_INVALID_LEVEL; }
    Pack p;                                                 /* WKSTA_INFO_100 */
    if (!pack_new(&p, 40, 256)) return ERROR_NOT_ENOUGH_MEMORY;
    DWORD *d = (DWORD *)p.base;
    d[0] = 500;                                             /* PLATFORM_ID_NT */
    WCHAR name[64];
    DWORD n = 64;
    if (!GetComputerNameW(name, &n)) lstrcpyW(name, L"NOVA-PC");
    *(LPWSTR *)(p.base + 8) = pack_str(&p, name);
    *(LPWSTR *)(p.base + 8 + sizeof(LPWSTR)) = pack_str(&p, L"WORKGROUP");
    ((DWORD *)(p.base + 8 + 2 * sizeof(LPWSTR)))[0] = 10;   /* ver_major */
    *buf = p.base;
    return NERR_Success;
}

/* No domain */
NETAPI DWORD WINAPI DsGetDcNameW(LPCWSTR comp, LPCWSTR dom, GUID *guid, LPCWSTR site, ULONG flags, PVOID *info)
{ (void)comp; (void)dom; (void)guid; (void)site; (void)flags; *info = 0; return ERROR_NO_SUCH_DOMAIN_; }
NETAPI DWORD WINAPI DsGetDcNameA(LPCSTR comp, LPCSTR dom, GUID *guid, LPCSTR site, ULONG flags, PVOID *info)
{ (void)comp; (void)dom; (void)guid; (void)site; (void)flags; *info = 0; return ERROR_NO_SUCH_DOMAIN_; }
NETAPI DWORD WINAPI DsEnumerateDomainTrustsW(LPWSTR server, ULONG flags, PVOID *doms, PULONG n)
{ (void)server; (void)flags; *doms = 0; *n = 0; return ERROR_NO_SUCH_DOMAIN_; }
NETAPI NET_API_STATUS WINAPI NetGetDCName(LPCWSTR server, LPCWSTR dom, LPBYTE *buf) { (void)server; (void)dom; *buf = 0; return 2453; /* NERR_DCNotFound */ }
/* not joined to Azure AD (Entra ID): no information, S_OK, as on a
 * workgroup PC */
NETAPI HRESULT WINAPI NetGetAadJoinInformation(LPCWSTR tenant, PVOID *info)
{
    (void)tenant;
    if (!info) return E_INVALIDARG;
    *info = 0;
    return S_OK;
}
NETAPI VOID WINAPI NetFreeAadJoinInformation(PVOID info) { (void)info; }

NETAPI NET_API_STATUS WINAPI NetGetJoinInformation(LPCWSTR server, LPWSTR *name, PDWORD status)
{
    (void)server;
    Pack p;
    if (!pack_new(&p, 0, 32)) return ERROR_NOT_ENOUGH_MEMORY;
    *name = pack_str(&p, L"WORKGROUP");
    *status = 2;                                            /* NetSetupWorkgroupName */
    return NERR_Success;
}
