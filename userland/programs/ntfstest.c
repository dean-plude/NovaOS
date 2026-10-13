/* ntfstest.exe — a full NTFS volume and a write cut off half-way (the
 * devices suite's "ntfs" boot, on drive D:)
 *
 *   ntfstest fill DIR    fills the volume: a file grows until WriteFile fails
 *                        with ERROR_DISK_FULL, keeps what was written, and the
 *                        space comes back when it is deleted
 *   ntfstest churn DIR   leaves DIR\kept.txt, then rewrites DIR\c0.bin .. c3.bin
 *                        (4 MB each) without end, for the test to reset the
 *                        machine meanwhile
 *   ntfstest cut DIR     after that reset (NovaOS found the volume it had
 *                        marked dirty while writing, checked and repaired it):
 *                        kept.txt is whole, each c*.bin holds one whole
 *                        generation, old or new, and new files can be written
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int pass, fail;
#define CHECK(cond) do { if (cond) pass++; else { fail++; printf("FAIL line %d: %s (error %lu)\n", __LINE__, #cond, GetLastError()); } } while (0)

#define MB          (1u << 20)
#define CHURN_BYTES (4u * MB)
#define KEPT_TEXT   "Kept by ntfstest before the cut\r\n"

static char g_path[MAX_PATH];

static const char *at(const char *dir, const char *name)
{
    snprintf(g_path, sizeof(g_path), "%s\\%s", dir, name);
    return g_path;
}

static ULONGLONG free_bytes(const char *dir)
{
    ULARGE_INTEGER avail, total, freeb;
    if (!GetDiskFreeSpaceExA(dir, &avail, &total, &freeb)) return 0;
    return freeb.QuadPart;
}

/* Generation @g of a churned file: each 32-bit word is g * K + its index */
static void pattern(DWORD *w, DWORD words, DWORD g)
{
    for (DWORD i = 0; i < words; i++) w[i] = g * 2654435761u + i;
}

static BOOL write_file(const char *name, const void *data, DWORD len)
{
    HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD n = 0;
    BOOL ok = WriteFile(h, data, len, &n, 0) && n == len;
    return CloseHandle(h) && ok;
}

static int read_file(const char *name, void *buf, DWORD cap)
{
    HANDLE h = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD n = 0;
    BOOL ok = ReadFile(h, buf, cap, &n, 0);
    CloseHandle(h);
    return ok ? (int)n : -1;
}

static int fill(const char *dir)
{
    CreateDirectoryA(dir, 0);
    ULONGLONG before = free_bytes(dir);
    CHECK(before > 8 * MB);
    static DWORD chunk[MB / 4];
    /* (made first: a new file's record may need clusters for the MFT) */
    HANDLE more = CreateFileA(at(dir, "more.bin"), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    CHECK(more != INVALID_HANDLE_VALUE);
    HANDLE h = CreateFileA(at(dir, "fill.bin"), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    CHECK(h != INVALID_HANDLE_VALUE);
    ULONGLONG written = 0;
    DWORD err = 0;
    for (DWORD i = 0; h != INVALID_HANDLE_VALUE && written <= before; i++) {
        pattern(chunk, MB / 4, i);
        DWORD n = 0;
        if (!WriteFile(h, chunk, MB, &n, 0)) { err = GetLastError(); break; }
        written += n;
    }
    printf("ntfstest fill: %llu of %llu free bytes written before the volume was full\n", written, before);
    CHECK(err == ERROR_DISK_FULL);
    CHECK(written > 0 && written <= before);
    CHECK(h != INVALID_HANDLE_VALUE && CloseHandle(h));             /* (to the disk now) */
    ULONGLONG full = free_bytes(dir);
    printf("ntfstest fill: %llu bytes free once it was written\n", full);
    CHECK(full < 2 * MB);

    /* What was written is there: the first and the last megabyte */
    static DWORD back[MB / 4];
    h = CreateFileA(at(dir, "fill.bin"), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    CHECK(h != INVALID_HANDLE_VALUE);
    LARGE_INTEGER size = { 0 };
    CHECK(GetFileSizeEx(h, &size) && (ULONGLONG)size.QuadPart == written);
    DWORD n = 0, last = (DWORD)(written / MB) - 1;
    CHECK(ReadFile(h, back, MB, &n, 0) && n == MB);
    pattern(chunk, MB / 4, 0);
    CHECK(!memcmp(back, chunk, MB));
    LARGE_INTEGER to = { .QuadPart = (LONGLONG)last * MB };
    CHECK(SetFilePointerEx(h, to, 0, FILE_BEGIN) && ReadFile(h, back, MB, &n, 0) && n == MB);
    pattern(chunk, MB / 4, last);
    CHECK(!memcmp(back, chunk, MB));
    CloseHandle(h);

    /* Another file can't take more than is left, written or extended */
    n = 0;
    CHECK(!WriteFile(more, chunk, MB, &n, 0) && GetLastError() == ERROR_DISK_FULL && n == 0);
    LARGE_INTEGER grow = { .QuadPart = 4 * MB };
    CHECK(SetFilePointerEx(more, grow, 0, FILE_BEGIN) && !SetEndOfFile(more) && GetLastError() == ERROR_DISK_FULL);
    CloseHandle(more);
    CHECK(DeleteFileA(at(dir, "more.bin")));

    /* The space comes back */
    CHECK(DeleteFileA(at(dir, "fill.bin")));
    ULONGLONG after = free_bytes(dir);
    printf("ntfstest fill: %llu bytes free once it was deleted\n", after);
    CHECK(after + 256 * 1024 >= before);
    CHECK(write_file(at(dir, "after.txt"), "written after the volume was full\r\n", 35));
    char text[64];
    CHECK(read_file(at(dir, "after.txt"), text, sizeof(text)) == 35 && !memcmp(text, "written after", 13));
    printf("ntfstest fill: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}

static int churn(const char *dir)
{
    CreateDirectoryA(dir, 0);
    if (!write_file(at(dir, "kept.txt"), KEPT_TEXT, sizeof(KEPT_TEXT) - 1)) {
        printf("ntfstest churn: kept.txt could not be written (error %lu)\n", GetLastError());
        return 1;
    }
    DWORD *buf = VirtualAlloc(0, CHURN_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) return 1;
    for (DWORD g = 1; g < 5000; g++) {
        char name[16];
        snprintf(name, sizeof(name), "c%lu.bin", g % 4);
        pattern(buf, CHURN_BYTES / 4, g);
        if (!write_file(at(dir, name), buf, CHURN_BYTES)) {
            printf("ntfstest churn: %s could not be written (error %lu)\n", name, GetLastError());
            return 1;
        }
        if (g == 4) { printf("ntfstest churn: writing\n"); fflush(stdout); }
    }
    printf("ntfstest churn: not cut off\n");
    return 1;
}

static int cut(const char *dir)
{
    char text[64];
    CHECK(read_file(at(dir, "kept.txt"), text, sizeof(text)) == (int)sizeof(KEPT_TEXT) - 1 &&
          !memcmp(text, KEPT_TEXT, sizeof(KEPT_TEXT) - 1));
    DWORD *buf = VirtualAlloc(0, CHURN_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD *want = VirtualAlloc(0, CHURN_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf || !want) return 1;
    int found = 0;
    for (int i = 0; i < 4; i++) {
        char name[16];
        snprintf(name, sizeof(name), "c%d.bin", i);
        int n = read_file(at(dir, name), buf, CHURN_BYTES);
        if (n < 0) continue;                                         /* (not made before the cut) */
        found++;
        DWORD g = 0;                                                 /* (its generation: word 0 is g * K) */
        for (DWORD c = 1; c < 5000 && !g; c++) if ((DWORD)(c * 2654435761u) == buf[0]) g = c;
        pattern(want, CHURN_BYTES / 4, g);
        printf("ntfstest cut: %s holds generation %lu\n", name, g);
        CHECK(n == (int)CHURN_BYTES && g % 4 == (DWORD)i && !memcmp(buf, want, CHURN_BYTES));
    }
    CHECK(found == 4);
    /* and the repaired volume takes changes again */
    CHECK(write_file(at(dir, "after-cut.txt"), "written after the cut\r\n", 23));
    CHECK(read_file(at(dir, "after-cut.txt"), text, sizeof(text)) == 23 && !memcmp(text, "written after the cut", 21));
    printf("ntfstest cut: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: ntfstest fill|churn|cut DIR\n");
        return 2;
    }
    if (!strcmp(argv[1], "fill")) return fill(argv[2]);
    if (!strcmp(argv[1], "churn")) return churn(argv[2]);
    if (!strcmp(argv[1], "cut")) return cut(argv[2]);
    printf("ntfstest: unknown mode %s\n", argv[1]);
    return 2;
}
