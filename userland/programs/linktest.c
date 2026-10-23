/* linktest.exe — hard links (CreateHardLink, FindFirstFileNameW) self-test
 *
 *   linktest [DIR]             the tests, in DIR (C:\LinkTest); leaves kept-a.txt,
 *                              kept-b.txt and kept-sub\kept-c.txt there, three
 *                              names of one file
 *   linktest restarted [DIR]   after a restart: they are still one file, and
 *                              kept-times.txt keeps the times and the hidden
 *                              attribute it was given (on NTFS its creation
 *                              time too; FAT keeps write times to 2 seconds),
 *                              and on drive C: on NTFS (installed: C: is its
 *                              disk volume) the DACL it was given
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

#ifndef SECURITY_WORLD_RID
#define SECURITY_WORLD_RID 0
#endif
#ifndef ACCESS_ALLOWED_ACE_TYPE
#define ACCESS_ALLOWED_ACE_TYPE 0
#endif

#ifndef FILE_SUPPORTS_HARD_LINKS
#define FILE_SUPPORTS_HARD_LINKS 0x00400000
#endif

HANDLE WINAPI FindFirstFileNameW(LPCWSTR name, DWORD flags, LPDWORD len, PWSTR out);
BOOL   WINAPI FindNextFileNameW(HANDLE h, LPDWORD len, PWSTR out);

WINADVAPI BOOL WINAPI GetFileSecurityA(LPCSTR name, SECURITY_INFORMATION si, PSECURITY_DESCRIPTOR sd, DWORD n, LPDWORD need);
WINADVAPI BOOL WINAPI SetFileSecurityA(LPCSTR name, SECURITY_INFORMATION si, PSECURITY_DESCRIPTOR sd);
WINADVAPI BOOL WINAPI GetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR sd, LPBOOL present, PACL *acl, LPBOOL def);
WINADVAPI BOOL WINAPI GetAce(PACL acl, DWORD i, LPVOID *ace);

static int pass, fail;
#define CHECK(cond) do { if (cond) pass++; else { fail++; printf("FAIL line %d: %s (error %lu)\n", __LINE__, #cond, GetLastError()); } } while (0)

static const char *g_dir = "C:\\LinkTest";

/* The link count and identity of @name, by a fresh handle */
static BOOL info(const char *name, BY_HANDLE_FILE_INFORMATION *bi)
{
    HANDLE h = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    BOOL ok = GetFileInformationByHandle(h, bi);
    CloseHandle(h);
    return ok;
}

static int links(const char *name)
{
    BY_HANDLE_FILE_INFORMATION bi;
    return info(name, &bi) ? (int)bi.nNumberOfLinks : -1;
}

static int same_file(const char *a, const char *b)
{
    BY_HANDLE_FILE_INFORMATION x, y;
    if (!info(a, &x) || !info(b, &y)) return 0;
    return x.nFileIndexLow == y.nFileIndexLow && x.nFileIndexHigh == y.nFileIndexHigh &&
           x.dwVolumeSerialNumber == y.dwVolumeSerialNumber;
}

static int read_all(const char *name, char *buf, DWORD cap)
{
    HANDLE h = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD n = 0;
    BOOL ok = ReadFile(h, buf, cap - 1, &n, 0);
    CloseHandle(h);
    if (!ok) return -1;
    buf[n] = '\0';
    return (int)n;
}

static BOOL write_all(const char *name, const char *text, DWORD how)
{
    HANDLE h = CreateFileA(name, GENERIC_WRITE, FILE_SHARE_READ, 0, how, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD n;
    if (how == OPEN_EXISTING) SetFilePointer(h, 0, 0, FILE_END);
    BOOL ok = WriteFile(h, text, (DWORD)strlen(text), &n, 0) && n == strlen(text);
    CloseHandle(h);
    return ok;
}

/* The names FindFirstFileNameW lists for @name, as "|\\dir\\a.txt|\\dir\\b.txt|"
 * (UTF-8, sorted, any case folded to lower) in @out; how many there are */
static int names_of(const char *name, char *out, int cap)
{
    WCHAR w[MAX_PATH], got[16][MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, w, MAX_PATH);
    int n = 0;
    DWORD len = MAX_PATH;
    HANDLE f = FindFirstFileNameW(w, 0, &len, got[0]);
    if (f == INVALID_HANDLE_VALUE) { out[0] = '\0'; return -1; }
    do {
        n++;
        len = MAX_PATH;
    } while (n < 16 && FindNextFileNameW(f, &len, got[n]));
    FindClose(f);
    for (int i = 1; i < n; i++)                                 /* (sorted: the order is the volume's) */
        for (int j = i; j > 0 && lstrcmpiW(got[j - 1], got[j]) > 0; j--) {
            WCHAR t[MAX_PATH];
            lstrcpyW(t, got[j]); lstrcpyW(got[j], got[j - 1]); lstrcpyW(got[j - 1], t);
        }
    int o = 0;
    out[o++] = '|';
    for (int i = 0; i < n && o < cap - 1; i++) {
        o += WideCharToMultiByte(CP_UTF8, 0, got[i], -1, out + o, cap - o - 1, 0, 0) - 1;
        out[o++] = '|';
    }
    out[o] = '\0';
    CharLowerA(out);
    return n;
}

/* "|\\dir\\@a|\\dir\\@b|..." for g_dir (lower case; @b, @c may be NULL), to
 * compare names_of with */
static const char *expect_names(const char *a, const char *b, const char *c)
{
    static char e[512];
    const char *d = g_dir[1] == ':' ? g_dir + 2 : g_dir, *n[3] = { a, b, c };
    int o = sprintf(e, "|");
    for (int i = 0; i < 3 && n[i]; i++) o += sprintf(e + o, "%s\\%s|", d, n[i]);
    CharLowerA(e);
    return e;
}

static void clean(void)
{
    const char *names[] = { "a.txt", "b.txt", "sub\\c.txt", "sub\\d.txt", "e.txt", "kept-a.txt", "kept-b.txt",
                            "kept-sub\\kept-c.txt" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        SetFileAttributesA(names[i], FILE_ATTRIBUTE_NORMAL);
        DeleteFileA(names[i]);
    }
    RemoveDirectoryA("sub");
    RemoveDirectoryA("kept-sub");
}

/* kept-times.txt's times: 2001-02-03 04:05:06 written, 1999-12-31 23:59:58 created (UTC) */
static FILETIME when(WORD y, WORD mo, WORD d, WORD h, WORD mi, WORD sec)
{
    SYSTEMTIME st = { y, mo, 0, d, h, mi, sec, 0 };
    FILETIME ft = { 0, 0 };
    SystemTimeToFileTime(&st, &ft);
    return ft;
}

static BOOL same_time(FILETIME a, FILETIME b) { return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime; }

/* Is the volume @g_dir is on NTFS? */
static BOOL on_ntfs(void)
{
    char root[4] = "C:\\", fs[16] = "";
    if (g_dir[1] == ':') root[0] = g_dir[0];
    return GetVolumeInformationA(root, 0, 0, 0, 0, 0, fs, sizeof(fs)) && !strcmp(fs, "NTFS");
}

/* kept-times.txt's DACL: everyone may do anything, then read (two ACEs of its own) */
static BOOL set_kept_dacl(void)
{
    static SID_IDENTIFIER_AUTHORITY world = { SECURITY_WORLD_SID_AUTHORITY };
    PSID everyone = NULL;
    BYTE buf[256];
    PACL acl = (PACL)buf;
    SECURITY_DESCRIPTOR sd;
    BOOL ok = AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone) &&
              InitializeAcl(acl, sizeof(buf), ACL_REVISION) &&
              AddAccessAllowedAce(acl, ACL_REVISION, 0x1F01FF, everyone) &&
              AddAccessAllowedAce(acl, ACL_REVISION, 0x120089, everyone) &&
              InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
              SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE) &&
              SetFileSecurityA("kept-times.txt", DACL_SECURITY_INFORMATION, &sd);
    if (everyone) FreeSid(everyone);
    return ok;
}

/* ... still there: two ACEs, not inherited, the first allowing everything */
static BOOL kept_dacl(void)
{
    static BYTE sd[1024];
    DWORD need;
    BOOL present, def;
    PACL acl;
    ACE_HEADER *a;
    return GetFileSecurityA("kept-times.txt", DACL_SECURITY_INFORMATION, sd, sizeof(sd), &need) &&
           GetSecurityDescriptorDacl(sd, &present, &acl, &def) && present && acl && acl->AceCount == 2 &&
           GetAce(acl, 0, (LPVOID *)&a) && a->AceType == ACCESS_ALLOWED_ACE_TYPE && !(a->AceFlags & 0x10) &&
           ((ACCESS_ALLOWED_ACE *)a)->Mask == 0x1F01FF;
}

static int restarted(void)
{
    CHECK(SetCurrentDirectoryA(g_dir));
    char buf[256], names[512];
    /* (first: on a mounted volume kept-sub is not read yet, its name counts all the same) */
    CHECK(links("kept-a.txt") == 3 && links("kept-b.txt") == 3);
    CHECK(names_of("kept-b.txt", names, sizeof(names)) == 3 &&
          !strcmp(names, expect_names("kept-a.txt", "kept-b.txt", "kept-sub\\kept-c.txt")));
    CHECK(same_file("kept-a.txt", "kept-b.txt") && same_file("kept-a.txt", "kept-sub\\kept-c.txt"));
    CHECK(read_all("kept-sub\\kept-c.txt", buf, sizeof(buf)) > 0 && !strcmp(buf, "Kept by linktest\r\n"));
    CHECK(read_all("kept-b.txt", buf, sizeof(buf)) > 0 && !strcmp(buf, "Kept by linktest\r\n"));
    CHECK(write_all("kept-a.txt", "and again\r\n", OPEN_EXISTING));
    CHECK(read_all("kept-b.txt", buf, sizeof(buf)) > 0 && !strcmp(buf, "Kept by linktest\r\nand again\r\n"));
    WIN32_FILE_ATTRIBUTE_DATA ad;
    CHECK(GetFileAttributesExA("kept-times.txt", GetFileExInfoStandard, &ad));
    CHECK(ad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
    CHECK(same_time(ad.ftLastWriteTime, when(2001, 2, 3, 4, 5, 6)));
    CHECK(!on_ntfs() || same_time(ad.ftCreationTime, when(1999, 12, 31, 23, 59, 58)));
    CHECK((g_dir[0] & ~0x20) != 'C' || !on_ntfs() || kept_dacl());
    printf("linktest restarted: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "restarted")) {
        if (argc > 2) g_dir = argv[2];
        return restarted();
    }
    if (argc > 1) g_dir = argv[1];
    char buf[256], root[4] = "C:\\";
    DWORD n, flags = 0;
    if (g_dir[1] == ':') root[0] = g_dir[0];

    CHECK(GetVolumeInformationA(root, 0, 0, 0, 0, &flags, 0, 0) && (flags & FILE_SUPPORTS_HARD_LINKS));

    CreateDirectoryA(g_dir, 0);
    CHECK(SetCurrentDirectoryA(g_dir));
    clean();

    /* two names, one file */
    CHECK(write_all("a.txt", "hello", CREATE_ALWAYS));
    CHECK(links("a.txt") == 1);
    CHECK(CreateHardLinkA("b.txt", "a.txt", 0));
    CHECK(links("a.txt") == 2 && links("b.txt") == 2);
    CHECK(same_file("a.txt", "b.txt"));
    CHECK(read_all("b.txt", buf, sizeof(buf)) == 5 && !strcmp(buf, "hello"));

    /* a write by one name is read by the other, at once */
    CHECK(write_all("b.txt", " world", OPEN_EXISTING));
    CHECK(read_all("a.txt", buf, sizeof(buf)) == 11 && !strcmp(buf, "hello world"));
    HANDLE ha = CreateFileA("a.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    CHECK(ha != INVALID_HANDLE_VALUE);
    CHECK(GetFileSize(ha, 0) == 11);
    CHECK(write_all("b.txt", "!", OPEN_EXISTING));
    CHECK(GetFileSize(ha, 0) == 12);                            /* (an open handle sees the other name's write) */
    CHECK(SetFilePointer(ha, 11, 0, FILE_BEGIN) == 11 && ReadFile(ha, buf, 1, &n, 0) && n == 1 && buf[0] == '!');
    CloseHandle(ha);

    /* attributes are the file's, not the name's */
    CHECK(SetFileAttributesA("a.txt", FILE_ATTRIBUTE_READONLY));
    CHECK(GetFileAttributesA("b.txt") & FILE_ATTRIBUTE_READONLY);
    CHECK(SetFileAttributesA("b.txt", FILE_ATTRIBUTE_NORMAL));
    CHECK(!(GetFileAttributesA("a.txt") & FILE_ATTRIBUTE_READONLY));

    /* both names are listed */
    WIN32_FIND_DATAA fd;
    int seen_a = 0, seen_b = 0, count = 0;
    HANDLE fh = FindFirstFileA("*", &fd);
    CHECK(fh != INVALID_HANDLE_VALUE);
    if (fh != INVALID_HANDLE_VALUE) {
        do {
            count++;
            if (!strcmp(fd.cFileName, "a.txt") && fd.nFileSizeLow == 12) seen_a = 1;
            if (!strcmp(fd.cFileName, "b.txt") && fd.nFileSizeLow == 12) seen_b = 1;
        } while (FindNextFileA(fh, &fd));
        FindClose(fh);
    }
    CHECK(seen_a && seen_b && count == 4);                      /* ".", "..", a.txt, b.txt */

    /* what is refused: a taken name, a directory, a name on another drive */
    CHECK(!CreateHardLinkA("b.txt", "a.txt", 0) && GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(CreateDirectoryA("sub", 0));
    CHECK(!CreateHardLinkA("e.txt", "sub", 0));
    CHECK(!CreateHardLinkA("e.txt", "missing.txt", 0) && GetLastError() == ERROR_FILE_NOT_FOUND);
    CHECK(!CreateHardLinkA(root[0] == 'C' ? "Z:\\e.txt" : "C:\\LinkTest\\e.txt", "a.txt", 0));
    CHECK(GetFileAttributesA("e.txt") == INVALID_FILE_ATTRIBUTES);

    /* a name in another folder, renamed there */
    CHECK(CreateHardLinkA("sub\\c.txt", "a.txt", 0));
    CHECK(links("a.txt") == 3);
    CHECK(MoveFileA("sub\\c.txt", "sub\\d.txt"));
    CHECK(links("b.txt") == 3 && same_file("sub\\d.txt", "b.txt"));
    CHECK(read_all("sub\\d.txt", buf, sizeof(buf)) == 12 && !strcmp(buf, "hello world!"));

    /* FindFirstFileNameW lists every name, by any of them; a file with one name has just it */
    char names[512];
    CHECK(names_of("b.txt", names, sizeof(names)) == 3 && !strcmp(names, expect_names("a.txt", "b.txt", "sub\\d.txt")));
    CHECK(names_of("sub\\d.txt", names, sizeof(names)) == 3 && !strcmp(names, expect_names("a.txt", "b.txt", "sub\\d.txt")));
    CHECK(write_all("e.txt", "alone", CREATE_ALWAYS));
    CHECK(names_of("e.txt", names, sizeof(names)) == 1 && !strcmp(names, expect_names("e.txt", NULL, NULL)));
    CHECK(DeleteFileA("e.txt"));

    /* deleting a name leaves the file to the others; the last name takes it */
    CHECK(DeleteFileA("a.txt"));
    CHECK(GetFileAttributesA("a.txt") == INVALID_FILE_ATTRIBUTES);
    CHECK(links("b.txt") == 2);
    CHECK(read_all("sub\\d.txt", buf, sizeof(buf)) == 12 && !strcmp(buf, "hello world!"));
    HANDLE hb = CreateFileA("b.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
    CHECK(hb != INVALID_HANDLE_VALUE);
    CHECK(DeleteFileA("sub\\d.txt"));                            /* (while the file is open by another name) */
    BY_HANDLE_FILE_INFORMATION bi;
    CHECK(GetFileInformationByHandle(hb, &bi) && bi.nNumberOfLinks == 1);
    CHECK(SetFilePointer(hb, 0, 0, FILE_BEGIN) == 0 && ReadFile(hb, buf, 12, &n, 0) && n == 12 && !memcmp(buf, "hello world!", 12));
    CloseHandle(hb);
    CHECK(RemoveDirectoryA("sub"));
    CHECK(DeleteFileA("b.txt"));

    /* a new file over a name that is a link, and a link replacing a file */
    CHECK(write_all("a.txt", "one", CREATE_ALWAYS) && CreateHardLinkA("b.txt", "a.txt", 0));
    CHECK(write_all("b.txt", "two", CREATE_ALWAYS));            /* (truncates the file, by either name) */
    CHECK(read_all("a.txt", buf, sizeof(buf)) == 3 && !strcmp(buf, "two"));
    CHECK(write_all("e.txt", "three", CREATE_ALWAYS));
    CHECK(MoveFileExA("e.txt", "a.txt", MOVEFILE_REPLACE_EXISTING));   /* a.txt is e's file now; b.txt keeps "two" */
    CHECK(read_all("a.txt", buf, sizeof(buf)) == 5 && !strcmp(buf, "three"));
    CHECK(read_all("b.txt", buf, sizeof(buf)) == 3 && !strcmp(buf, "two") && links("b.txt") == 1);
    CHECK(DeleteFileA("a.txt") && DeleteFileA("b.txt"));

    /* kept for "linktest restarted" */
    CHECK(write_all("kept-a.txt", "Kept by linktest\r\n", CREATE_ALWAYS));
    CHECK(CreateHardLinkA("kept-b.txt", "kept-a.txt", 0));
    CHECK(CreateDirectoryA("kept-sub", 0) && CreateHardLinkA("kept-sub\\kept-c.txt", "kept-a.txt", 0));
    CHECK(links("kept-b.txt") == 3);
    /* given times while open after writing (the contents reach the disk
     * later, and must not take the times back) and a hidden attribute */
    HANDLE h = CreateFileA("kept-times.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        FILETIME ct = when(1999, 12, 31, 23, 59, 58), wt = when(2001, 2, 3, 4, 5, 6);
        CHECK(WriteFile(h, "Times kept\r\n", 12, &w, 0) && w == 12);
        CHECK(SetFileTime(h, &ct, 0, &wt));
        CloseHandle(h);
    }
    CHECK(SetFileAttributesA("kept-times.txt", FILE_ATTRIBUTE_HIDDEN));
    CHECK(set_kept_dacl() && ((g_dir[0] & ~0x20) != 'C' || kept_dacl()));   /* (other drives keep no ACLs) */

    printf("linktest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
