/* volfiletest.exe — what drive C: relies on, on a volume mounted from disk
 *
 *   volfiletest DIR            the tests in DIR (on D:, E:, ...); leaves
 *                              behind what the restarted run checks
 *   volfiletest restarted DIR  after a restart: what was set is still there,
 *                              read back from the volume
 *
 * Attributes and times written through to the volume (SetFileAttributes,
 * SetFileTime, also on a file written in the same handle), change
 * notifications on its folders (FindFirstChangeNotification), files moved
 * between folders, folders renamed and moved, and on NTFS security
 * descriptors (SetFileSecurity, inheritance, access checks).  FAT keeps no
 * descriptors, so those checks are left out there, as on Windows.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

/* (not in our headers yet) */
#ifndef FILE_WRITE_ATTRIBUTES
#define FILE_WRITE_ATTRIBUTES 0x0100
#endif
#ifndef FILE_READ_ATTRIBUTES
#define FILE_READ_ATTRIBUTES  0x0080
#endif
#ifndef FILE_WRITE_DATA
#define FILE_WRITE_DATA       0x0002
#endif
#ifndef FILE_APPEND_DATA
#define FILE_APPEND_DATA      0x0004
#endif
#ifndef OBJECT_INHERIT_ACE
#define OBJECT_INHERIT_ACE    0x01
#endif
#ifndef CONTAINER_INHERIT_ACE
#define CONTAINER_INHERIT_ACE 0x02
#endif
#ifndef INHERITED_ACE
#define INHERITED_ACE         0x10
#endif
#ifndef ACCESS_DENIED_ACE_TYPE
#define ACCESS_ALLOWED_ACE_TYPE 0
#define ACCESS_DENIED_ACE_TYPE  1
#endif
#ifndef SECURITY_WORLD_RID
#define SECURITY_WORLD_RID    0
#endif
#ifndef SECURITY_WORLD_SID_AUTHORITY
#define SECURITY_WORLD_SID_AUTHORITY { 0, 0, 0, 0, 0, 1 }
#endif
#ifndef FILE_FLAG_BACKUP_SEMANTICS
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#endif
#ifndef FILE_NOTIFY_CHANGE_FILE_NAME
#define FILE_NOTIFY_CHANGE_FILE_NAME  0x01
#define FILE_NOTIFY_CHANGE_DIR_NAME   0x02
#define FILE_NOTIFY_CHANGE_ATTRIBUTES 0x04
#define FILE_NOTIFY_CHANGE_LAST_WRITE 0x10
#endif
HANDLE WINAPI FindFirstChangeNotificationA(LPCSTR path, BOOL sub, DWORD filter);
BOOL   WINAPI FindNextChangeNotification(HANDLE h);
BOOL   WINAPI FindCloseChangeNotification(HANDLE h);
WINADVAPI BOOL WINAPI AddAccessAllowedAceEx(PACL acl, DWORD rev, DWORD flags, DWORD mask, PSID sid);
WINADVAPI BOOL WINAPI GetFileSecurityA(LPCSTR name, SECURITY_INFORMATION si, PSECURITY_DESCRIPTOR sd, DWORD n, LPDWORD need);
WINADVAPI BOOL WINAPI SetFileSecurityA(LPCSTR name, SECURITY_INFORMATION si, PSECURITY_DESCRIPTOR sd);

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL %s (error %lu)\n", what, GetLastError()); } } while (0)

static char g_dir[MAX_PATH];
static BOOL g_ntfs;

static const char *at(const char *name)
{
    static char path[4][MAX_PATH];
    static int k;
    char *p = path[k++ & 3];
    snprintf(p, MAX_PATH, "%s\\%s", g_dir, name);
    return p;
}

static BOOL write_file(const char *path, const char *text)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD n;
    BOOL ok = WriteFile(h, text, (DWORD)strlen(text), &n, NULL) && n == strlen(text);
    CloseHandle(h);
    return ok;
}

static BOOL read_is(const char *path, const char *text)
{
    char buf[256];
    DWORD n = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &n, NULL);
    CloseHandle(h);
    buf[n] = 0;
    return ok && !strcmp(buf, text);
}

/* A FILETIME for the date and time given (UTC; even seconds, which FAT keeps) */
static FILETIME when(int y, int mo, int d, int h, int mi, int s)
{
    SYSTEMTIME st = { (WORD)y, (WORD)mo, 0, (WORD)d, (WORD)h, (WORD)mi, (WORD)s, 0 };
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    return ft;
}

static BOOL same_time(const FILETIME *a, const FILETIME *b)
{
    return a->dwLowDateTime == b->dwLowDateTime && a->dwHighDateTime == b->dwHighDateTime;
}

/* @path's created and last-written times, by a fresh handle */
static BOOL times_of(const char *path, FILETIME *c, FILETIME *w)
{
    HANDLE h = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    FILETIME a;
    BOOL ok = GetFileTime(h, c, &a, w);
    CloseHandle(h);
    return ok;
}

/* The times the tests give files */
#define CREATED  when(2001, 2, 3, 4, 5, 6)
#define WRITTEN  when(2002, 3, 4, 5, 6, 8)

static BOOL set_dacl(const char *path, PACL acl)
{
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE);
    return SetFileSecurityA(path, DACL_SECURITY_INFORMATION, &sd);
}

/* @path's DACL: how many ACEs, and the first one's type, flags and mask */
static int dacl_of(const char *path, BYTE *type, BYTE *flags, DWORD *mask)
{
    static BYTE sd[1024];
    DWORD need;
    BOOL present, def;
    PACL acl;
    if (!GetFileSecurityA(path, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION, sd, sizeof(sd), &need)) return -1;
    if (!GetSecurityDescriptorDacl(sd, &present, &acl, &def) || !present || !acl) return -2;
    ACE_HEADER *a;
    if (acl->AceCount && GetAce(acl, 0, (LPVOID *)&a)) {
        *type = a->AceType;
        *flags = a->AceFlags;
        *mask = ((ACCESS_ALLOWED_ACE *)a)->Mask;
    }
    return acl->AceCount;
}

static BOOL write_denied(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return FALSE; }
    return GetLastError() == ERROR_ACCESS_DENIED;
}

static PSID user_sid(void)
{
    static BYTE buf[256];
    HANDLE tok;
    DWORD n;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return NULL;
    BOOL ok = GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n);
    CloseHandle(tok);
    return ok ? ((TOKEN_USER *)buf)->User.Sid : NULL;
}

#define DENIED_WRITE (FILE_WRITE_DATA | FILE_APPEND_DATA)

/* Waits for a change notification on @h; ready for the next afterwards */
static BOOL notified(HANDLE h)
{
    BOOL ok = WaitForSingleObject(h, 3000) == WAIT_OBJECT_0;
    FindNextChangeNotification(h);
    return ok;
}

static void notify_tests(void)
{
    HANDLE h = FindFirstChangeNotificationA(g_dir, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                            FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE);
    CHECK("FindFirstChangeNotification on the folder", h != INVALID_HANDLE_VALUE);
    if (h == INVALID_HANDLE_VALUE) return;
    CHECK("nothing changed yet", WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
    CHECK("creating a file notifies", write_file(at("notify.txt"), "one") && notified(h));
    CHECK("changing its attributes notifies", SetFileAttributesA(at("notify.txt"), FILE_ATTRIBUTE_HIDDEN) && notified(h));
    CHECK("renaming it notifies", MoveFileA(at("notify.txt"), at("notified.txt")) && notified(h));
    CHECK("deleting it notifies", SetFileAttributesA(at("notified.txt"), FILE_ATTRIBUTE_NORMAL) &&
          DeleteFileA(at("notified.txt")) && notified(h));
    FindCloseChangeNotification(h);

    CreateDirectoryA(at("Watched"), NULL);
    h = FindFirstChangeNotificationA(g_dir, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
    CHECK("FindFirstChangeNotification on the tree", h != INVALID_HANDLE_VALUE);
    if (h == INVALID_HANDLE_VALUE) return;
    CHECK("a file made in a subfolder notifies the tree's watch", write_file(at("Watched\\deep.txt"), "deep") && notified(h));
    FindCloseChangeNotification(h);
}

static void attribute_tests(void)
{
    CHECK("make attrs.txt", write_file(at("kept-attrs.txt"), "attributes"));
    CHECK("SetFileAttributes(hidden, system)", SetFileAttributesA(at("kept-attrs.txt"), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
    DWORD a = GetFileAttributesA(at("kept-attrs.txt"));
    CHECK("GetFileAttributes has them", a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_HIDDEN) && (a & FILE_ATTRIBUTE_SYSTEM));
    CHECK("a hidden folder", CreateDirectoryA(at("KeptHidden"), NULL) && SetFileAttributesA(at("KeptHidden"), FILE_ATTRIBUTE_HIDDEN) &&
          (GetFileAttributesA(at("KeptHidden")) & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_DIRECTORY)) ==
          (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_DIRECTORY));
}

static void time_tests(void)
{
    FILETIME c = CREATED, w = WRITTEN, gc, gw;
    CHECK("make kept-times.txt", write_file(at("kept-times.txt"), "times"));
    HANDLE h = CreateFileA(at("kept-times.txt"), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK("SetFileTime", h != INVALID_HANDLE_VALUE && SetFileTime(h, &c, NULL, &w));
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK("GetFileTime returns them", times_of(at("kept-times.txt"), &gc, &gw) && same_time(&gc, &c) && same_time(&gw, &w));

    /* An extractor's way: write, set the archive's times, close */
    h = CreateFileA(at("kept-extracted.txt"), GENERIC_WRITE | FILE_WRITE_ATTRIBUTES, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    CHECK("write a file and set its times in one handle", h != INVALID_HANDLE_VALUE && WriteFile(h, "extracted", 9, &n, NULL) &&
          SetFileTime(h, &c, NULL, &w));
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK("its times stay once it is written", times_of(at("kept-extracted.txt"), &gc, &gw) && same_time(&gw, &w));
    CHECK("its contents too", read_is(at("kept-extracted.txt"), "extracted"));
}

static void move_tests(void)
{
    CHECK("make Sub and Other", CreateDirectoryA(at("Sub"), NULL) && CreateDirectoryA(at("Other"), NULL));
    CHECK("write Sub\\inner.txt", write_file(at("Sub\\inner.txt"), "inner"));
    CHECK("write moving.txt", write_file(at("moving.txt"), "moving"));
    CHECK("move a file into another folder", MoveFileA(at("moving.txt"), at("Sub\\moved.txt")) &&
          GetFileAttributesA(at("moving.txt")) == INVALID_FILE_ATTRIBUTES && read_is(at("Sub\\moved.txt"), "moving"));
    CHECK("move it back out under a new name", MoveFileA(at("Sub\\moved.txt"), at("back.txt")) && read_is(at("back.txt"), "moving"));
    CHECK("rename a folder", MoveFileA(at("Sub"), at("Renamed")) && GetFileAttributesA(at("Sub")) == INVALID_FILE_ATTRIBUTES &&
          read_is(at("Renamed\\inner.txt"), "inner"));
    CHECK("change a folder name's case", MoveFileA(at("Renamed"), at("RENAMED")) && read_is(at("RENAMED\\inner.txt"), "inner"));
    CHECK("move a folder into another", MoveFileA(at("RENAMED"), at("Other\\KeptMoved")) &&
          read_is(at("Other\\KeptMoved\\inner.txt"), "inner"));
    CHECK("make a file in the moved folder", write_file(at("Other\\KeptMoved\\after.txt"), "after the move") &&
          read_is(at("Other\\KeptMoved\\after.txt"), "after the move"));
    CHECK("a folder can't move into itself", !MoveFileA(at("Other"), at("Other\\KeptMoved\\Loop")));
}

static void security_tests(void)
{
    BYTE buf[512], t = 0, f = 0;
    PACL acl = (PACL)buf;
    DWORD m = 0;
    PSID me = user_sid();
    SID_IDENTIFIER_AUTHORITY world = { SECURITY_WORLD_SID_AUTHORITY };
    PSID everyone = NULL;
    AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone);
    CHECK("the user's SID", me && everyone);
    if (!me || !everyone) return;

    /* A folder whose files inherit full control for the user, only */
    CHECK("make KeptAcl", CreateDirectoryA(at("KeptAcl"), NULL));
    InitializeAcl(acl, sizeof(buf), ACL_REVISION);
    AddAccessAllowedAceEx(acl, ACL_REVISION, CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE, GENERIC_ALL, me);
    CHECK("KeptAcl: set its DACL", set_dacl(at("KeptAcl"), acl));
    CHECK("KeptAcl: GetFileSecurity returns it", dacl_of(at("KeptAcl"), &t, &f, &m) == 1 && t == ACCESS_ALLOWED_ACE_TYPE);
    CHECK("write KeptAcl\\inherits.txt", write_file(at("KeptAcl\\inherits.txt"), "inherits"));
    int n = dacl_of(at("KeptAcl\\inherits.txt"), &t, &f, &m);
    CHECK("inherits.txt: inherits the folder's ACE", n == 1 && t == ACCESS_ALLOWED_ACE_TYPE && (f & INHERITED_ACE));

    /* A file nobody may write */
    CHECK("write KeptAcl\\locked.txt", write_file(at("KeptAcl\\locked.txt"), "locked"));
    InitializeAcl(acl, sizeof(buf), ACL_REVISION);
    AddAccessDeniedAce(acl, ACL_REVISION, DENIED_WRITE, everyone);
    AddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, me);
    CHECK("locked.txt: set a DACL that denies writing", set_dacl(at("KeptAcl\\locked.txt"), acl));
    n = dacl_of(at("KeptAcl\\locked.txt"), &t, &f, &m);
    CHECK("locked.txt: GetFileSecurity returns it", n == 2 && t == ACCESS_DENIED_ACE_TYPE && m == DENIED_WRITE);
    CHECK("locked.txt: writing is denied", write_denied(at("KeptAcl\\locked.txt")));
    CHECK("locked.txt: reading works", read_is(at("KeptAcl\\locked.txt"), "locked"));
    FreeSid(everyone);
}

/* What a run before the restart left; read from the volume now */
static void restarted_tests(void)
{
    FILETIME c = CREATED, w = WRITTEN, gc, gw;
    BYTE t = 0, f = 0;
    DWORD m = 0;
    DWORD a = GetFileAttributesA(at("kept-attrs.txt"));
    CHECK("kept-attrs.txt is still hidden and system", a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_HIDDEN) &&
          (a & FILE_ATTRIBUTE_SYSTEM));
    CHECK("KeptHidden is still hidden", (GetFileAttributesA(at("KeptHidden")) & FILE_ATTRIBUTE_HIDDEN) != 0 &&
          GetFileAttributesA(at("KeptHidden")) != INVALID_FILE_ATTRIBUTES);
    CHECK("kept-times.txt kept its times", times_of(at("kept-times.txt"), &gc, &gw) && same_time(&gc, &c) && same_time(&gw, &w));
    CHECK("kept-extracted.txt kept its time", times_of(at("kept-extracted.txt"), &gc, &gw) && same_time(&gw, &w));
    CHECK("the moved folder's files are there", read_is(at("Other\\KeptMoved\\inner.txt"), "inner") &&
          read_is(at("Other\\KeptMoved\\after.txt"), "after the move"));
    CHECK("and it takes new ones", write_file(at("Other\\KeptMoved\\later.txt"), "later") &&
          read_is(at("Other\\KeptMoved\\later.txt"), "later"));
    if (!g_ntfs) return;
    CHECK("KeptAcl kept its DACL", dacl_of(at("KeptAcl"), &t, &f, &m) == 1 && t == ACCESS_ALLOWED_ACE_TYPE);
    int n = dacl_of(at("KeptAcl\\inherits.txt"), &t, &f, &m);
    CHECK("inherits.txt still inherits", n == 1 && t == ACCESS_ALLOWED_ACE_TYPE && (f & INHERITED_ACE));
    n = dacl_of(at("KeptAcl\\locked.txt"), &t, &f, &m);
    CHECK("locked.txt kept its DACL", n == 2 && t == ACCESS_DENIED_ACE_TYPE && m == DENIED_WRITE);
    CHECK("locked.txt still can't be written", write_denied(at("KeptAcl\\locked.txt")));
    CHECK("a new file in KeptAcl inherits", write_file(at("KeptAcl\\new.txt"), "new") &&
          dacl_of(at("KeptAcl\\new.txt"), &t, &f, &m) == 1 && (f & INHERITED_ACE));
}

int main(int argc, char **argv)
{
    BOOL restarted = argc > 1 && !strcmp(argv[1], "restarted");
    const char *dir = argc > (restarted ? 2 : 1) ? argv[restarted ? 2 : 1] : "D:\\VolFiles";
    snprintf(g_dir, sizeof(g_dir), "%s", dir);
    char root[4] = { dir[0], ':', '\\', 0 }, fs[32] = "";
    GetVolumeInformationA(root, NULL, 0, NULL, NULL, NULL, fs, sizeof(fs));
    g_ntfs = !strcmp(fs, "NTFS");
    printf("volfiletest: %s is %s\n", g_dir, fs);
    if (restarted) {
        restarted_tests();
        printf("volfiletest restarted: %d passed, %d failed\n", pass, fail);
        return fail ? 1 : 0;
    }
    CHECK("make the test folder", CreateDirectoryA(g_dir, NULL));
    notify_tests();
    attribute_tests();
    time_tests();
    move_tests();
    if (g_ntfs) security_tests();
    printf("volfiletest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
