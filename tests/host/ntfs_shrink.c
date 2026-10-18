/* Shrinking an NTFS volume (tests/tools/test_ntfs_shrink.py): the real
 * kernel/fs/ntfs.c on a RAM disk, as Setup shrinks Windows' volume to make
 * room beside it (docs/design/installer.md).  NovaOS's own format puts
 * $MFTMirr and $LogFile in the middle of the volume; files are written
 * until it is about two thirds full, two in three deleted, and one large
 * file written into the holes (so it is fragmented across the volume).
 * Then the volume is shrunk to under half its size: everything past the
 * new end ($MFTMirr, $LogFile, files, a directory's index blocks) moves.
 * The shrunk volume, on a disk cut to its new size, must mount (checked,
 * as it is left marked dirty, with nothing to put right), hold every file
 * byte for byte, take a change, and pass scripts/ntfs-check.py.  Then the
 * shrink is cut off at points through its writes: each disk must mount,
 * old size or new, with every file whole, and pass the check.
 *
 *   ntfs_shrink CHECKER IMAGE [STEP]   every STEP-th cut */
#include "../../kernel/fs/ntfs.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static int quiet;
static char g_log[16384];                                         /* (what it said since the last reset) */
void kprintf(const char *f, ...)
{
    char line[1024];
    va_list a;
    va_start(a, f);
    vsnprintf(line, sizeof(line), f, a);
    va_end(a);
    if (strlen(g_log) + strlen(line) < sizeof(g_log)) strcat(g_log, line);
    if (!quiet) fputs(line, stdout);
}
static UINT64 tick = 132000000000000000ull;
static UINT64 now(void) { return tick += 10000000; }

#define DISK_BYTES   (96ull << 20)
#define SHRUNK_BYTES (44ull << 20)          /* the new partition: under half */
#define FILES        145
#define BIG_BYTES    (4u << 20)

typedef struct { BlockDev d; UINT8 *data; UINT8 *snap; UINT64 bytes; long writes, cut_at; } Ram;

static bool ram_read(BlockDev *d, UINT64 lba, UINT32 n, void *buf)
{
    Ram *r = d->ctx;
    if ((lba + n) * BLOCK_SECTOR > r->bytes) return false;
    memcpy(buf, r->data + lba * BLOCK_SECTOR, (size_t)n * BLOCK_SECTOR);
    return true;
}
static bool ram_write(BlockDev *d, UINT64 lba, UINT32 n, const void *buf)
{
    Ram *r = d->ctx;
    if ((lba + n) * BLOCK_SECTOR > r->bytes) return false;
    if (r->writes++ == r->cut_at) memcpy(r->snap, r->data, r->bytes);   /* the power goes here */
    memcpy(r->data + lba * BLOCK_SECTOR, buf, (size_t)n * BLOCK_SECTOR);
    return true;
}
static bool ram_flush(BlockDev *d) { (void)d; return true; }

static void ram_init(Ram *r, UINT8 *data, UINT64 bytes)
{
    memset(r, 0, sizeof(*r));
    strcpy(r->d.name, "ram0");
    r->bytes = bytes;
    r->d.sectors = bytes / BLOCK_SECTOR;
    r->d.read = ram_read; r->d.write = ram_write; r->d.flush = ram_flush;
    r->d.ctx = r;
    r->data = data;
    r->cut_at = -1;
}

/* File i's size and contents (word k is i * K + k); FILES is the big one */
static UINT32 file_size(UINT32 i) { return i == FILES ? BIG_BYTES : i % 7 == 0 ? 600 : (100000 + i * 7919u % 900000) & ~3u; }
static void file_fill(UINT32 *w, UINT32 i)
{
    for (UINT32 k = 0; k < file_size(i) / 4; k++) w[k] = i * 2654435761u + k;
}
static UINT8 *g_buf, *g_back;

static UINT64 lookup(NtfsVol *v, UINT64 dir, const char *name)
{
    UINT64 m = 0; bool d;
    return NtfsLookup(v, dir, name, &m, &d) ? m : 0;
}
static bool kept(UINT32 i) { return i == FILES || i % 3 == 0; }

/* Every kept file whole, byte for byte; the deleted ones gone */
static int check_files(NtfsVol *v, const char *when)
{
    int bad = 0;
    UINT64 dir = lookup(v, NTFS_ROOT, "Data");
    if (!dir) { printf("%s: \\Data is missing\n", when); return 1; }
    for (UINT32 i = 0; i <= FILES; i++) {
        char name[32];
        snprintf(name, sizeof(name), "f%u.bin", i);
        UINT64 m = lookup(v, dir, name), size = 0;
        if (!kept(i)) { if (m) { printf("%s: %s is back\n", when, name); bad++; } continue; }
        file_fill((UINT32 *)g_buf, i);
        if (!m || !NtfsSize(v, m, &size) || size != file_size(i) || !NtfsRead(v, m, 0, g_back, size) ||
            memcmp(g_back, g_buf, size)) {
            printf("%s: %s is not whole\n", when, name);
            bad++;
        }
    }
    return bad;
}

static int run_check(const char *checker, const char *image, const UINT8 *disk, UINT64 bytes, const char *when)
{
    FILE *f = fopen(image, "wb");
    assert(f && fwrite(disk, 1, bytes, f) == bytes);
    fclose(f);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s %s > %s.log 2>&1", checker, image, image);
    if (!system(cmd)) return 0;
    printf("%s: the check found errors:\n", when);
    snprintf(cmd, sizeof(cmd), "grep -m 12 ERROR %s.log", image);
    if (system(cmd)) {}
    return 1;
}

/* Mount the disk again as the next boot would: writable (checked: it is
 * left dirty), every file whole, takes a change, passes the check */
static int check_disk(UINT8 *disk, UINT64 bytes, const char *checker, const char *image, const char *when, bool fixes_none)
{
    Ram r;
    ram_init(&r, disk, bytes);
    NtfsVol *v = NtfsMount(&r.d, 0);
    if (!v) { printf("%s: does not mount\n", when); return 1; }
    int bad = 0;
    g_log[0] = 0;
    if (!NtfsEnableWrite(v)) { printf("%s: stays read-only: %s\n", when, g_log); NtfsUnmount(v); return 1; }
    if (fixes_none && !strstr(g_log, "checked, 0 lost clusters freed, 0 clusters claimed, 0 MFT records put right, "
                                     "0 directory indexes rebuilt")) {
        printf("%s: not checked, or the check put things right: %s", when, g_log);
        bad++;
    }
    bad += check_files(v, when);
    UINT64 m = 0, dir = lookup(v, NTFS_ROOT, "Data");
    if (!dir || !NtfsCreate(v, dir, "after.txt", false, &m) || !NtfsWriteFile(v, m, "after", 5)) {
        printf("%s: does not take a change\n", when);
        bad++;
    }
    NtfsUnmount(v);
    return bad + run_check(checker, image, disk, bytes, when);
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: ntfs_shrink CHECKER IMAGE [STEP]\n"); return 2; }
    const char *checker = argv[1], *image = argv[2];
    long step = argc > 3 ? atol(argv[3]) : 1;
    NtfsSetClock(now);
    g_buf = malloc(BIG_BYTES);
    g_back = malloc(BIG_BYTES);
    UINT8 *base = calloc(1, DISK_BYTES), *disk = malloc(DISK_BYTES), *snap = malloc(DISK_BYTES);
    assert(g_buf && g_back && base && disk && snap);

    /* The volume: \Data with FILES files, three quarters full, every other
     * one deleted, then the big one written into the holes */
    quiet = 1;
    Ram r;
    ram_init(&r, base, DISK_BYTES);
    assert(NtfsFormat(&r.d, 0, DISK_BYTES / BLOCK_SECTOR, "Windows", 0x5678));
    NtfsVol *v = NtfsMount(&r.d, 0);
    assert(v && NtfsEnableWrite(v));
    UINT64 dir, m;
    assert(NtfsCreate(v, NTFS_ROOT, "Data", true, &dir));
    for (UINT32 i = 0; i < FILES; i++) {
        char name[32];
        snprintf(name, sizeof(name), "f%u.bin", i);
        file_fill((UINT32 *)g_buf, i);
        assert(NtfsCreate(v, dir, name, false, &m) && NtfsWriteFile(v, m, g_buf, file_size(i)));
    }
    UINT64 full = NtfsFreeBytes(v);
    for (UINT32 i = 0; i < FILES; i++) {
        if (kept(i)) continue;
        char name[32];
        snprintf(name, sizeof(name), "f%u.bin", i);
        assert(NtfsDelete(v, dir, lookup(v, dir, name), name));
    }
    char big[32];
    snprintf(big, sizeof(big), "f%u.bin", FILES);
    file_fill((UINT32 *)g_buf, FILES);
    assert(NtfsCreate(v, dir, big, false, &m) && NtfsWriteFile(v, m, g_buf, BIG_BYTES));
    UINT64 min = 0, used = 0;
    const char *why = NULL;
    assert(NtfsShrinkLimit(v, &min, &used, &why));
    printf("the volume: %llu MiB, %llu MiB free when full, %llu MiB used now; Setup would shrink it to %llu MiB at least\n",
           (unsigned long long)(NtfsTotalBytes(v) >> 20), (unsigned long long)(full >> 20),
           (unsigned long long)(used >> 20), (unsigned long long)(min >> 20));
    NtfsUnmount(v);
    int bad = 0;

    /* Shrunk, once through (counting its writes) */
    memcpy(disk, base, DISK_BYTES);
    ram_init(&r, disk, DISK_BYTES);
    v = NtfsMount(&r.d, 0);
    assert(v && NtfsEnableWrite(v));
    quiet = 0;
    long before = r.writes;
    if (!NtfsShrink(v, SHRUNK_BYTES, &why)) { printf("NtfsShrink: %s\n", why); return 1; }
    long writes = r.writes - before;
    quiet = 1;
    NtfsUnmount(v);
    printf("the shrink makes %ld disk writes\n", writes);
    UINT64 nsec = SHRUNK_BYTES / BLOCK_SECTOR - 1;
    if (rd64(disk + 0x28) != nsec || memcmp(disk, disk + nsec * BLOCK_SECTOR, BLOCK_SECTOR)) {
        printf("the boot sector or its backup in the last sector does not say the new size\n");
        bad++;
    }
    /* On a disk of the new size: nothing past it is needed */
    UINT8 *small = malloc(SHRUNK_BYTES);
    assert(small);
    memcpy(small, disk, SHRUNK_BYTES);
    quiet = 0;
    printf("shrunk: ");
    bad += check_disk(small, SHRUNK_BYTES, checker, image, "shrunk", true);
    quiet = 1;

    /* Cut off at points through it: old size or new, every file whole */
    int cuts = 0;
    for (long cut = 0; cut < writes; cut += step) {
        memcpy(disk, base, DISK_BYTES);
        ram_init(&r, disk, DISK_BYTES);
        v = NtfsMount(&r.d, 0);
        assert(v && NtfsEnableWrite(v));
        r.snap = snap;
        r.cut_at = r.writes + cut;
        assert(NtfsShrink(v, SHRUNK_BYTES, &why));
        NtfsUnmount(v);
        char when[32];
        snprintf(when, sizeof(when), "cut %ld", cut);
        bad += check_disk(snap, DISK_BYTES, checker, image, when, false) != 0;
        cuts++;
    }
    printf("ntfs_shrink: %d cut points, %d bad\n", cuts, bad);
    return bad != 0;
}
