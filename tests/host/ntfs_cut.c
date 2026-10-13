/* NTFS writes cut off at every point (tests/tools/test_ntfs_cut.py): the
 * real kernel/fs/ntfs.c on a RAM disk.  A churn of rewrites, creates,
 * renames, links and deletes runs once to count its disk writes; then for
 * each cut point the disk as it was after that many writes is mounted
 * again, as the next boot would, and must come back writable (NovaOS
 * checks and repairs a volume it left marked dirty) with every file whole.
 * Each disk after the repair must pass scripts/ntfs-check.py (a
 * chkdsk-style check) with no errors.
 *
 *   ntfs_cut CHECKER IMAGE [STEP]   CHECKER IMAGE runs the check on the
 *                                   disk saved as IMAGE; every STEP-th cut
 *                                   (from cut $CUT0, if set) */
#include "../../kernel/fs/ntfs.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static int quiet;
void kprintf(const char *f, ...)
{
    if (quiet) return;
    va_list a; va_start(a, f); vprintf(f, a); va_end(a);
}
static UINT64 tick = 132000000000000000ull;
static UINT64 now(void) { return tick += 10000000; }

#define DISK_BYTES (32ull << 20)
#define CHURN_DIR  "Churn"
#define BIG_FILES  150                      /* (enough for an index in blocks) */
#define ROUNDS     24
#define KEPT       "Kept before the cut\r\n"

typedef struct { BlockDev d; UINT8 *data; UINT8 *snap; long writes, cut_at; } Ram;

static bool ram_read(BlockDev *d, UINT64 lba, UINT32 n, void *buf)
{
    Ram *r = d->ctx;
    memcpy(buf, r->data + lba * BLOCK_SECTOR, (size_t)n * BLOCK_SECTOR);
    return true;
}
static bool ram_write(BlockDev *d, UINT64 lba, UINT32 n, const void *buf)
{
    Ram *r = d->ctx;
    if (r->writes++ == r->cut_at) memcpy(r->snap, r->data, DISK_BYTES);   /* the power goes here */
    memcpy(r->data + lba * BLOCK_SECTOR, buf, (size_t)n * BLOCK_SECTOR);
    return true;
}
static bool ram_flush(BlockDev *d) { (void)d; return true; }

static void ram_init(Ram *r, UINT8 *data)
{
    memset(r, 0, sizeof(*r));
    strcpy(r->d.name, "ram0");
    r->d.sectors = DISK_BYTES / BLOCK_SECTOR;
    r->d.read = ram_read; r->d.write = ram_write; r->d.flush = ram_flush;
    r->d.ctx = r;
    r->data = data;
    r->cut_at = -1;
}

/* Generation @g of churned file c(g % 4): its size, and word i is g * K + i */
static UINT32 gen_size(UINT32 g) { return g % 5 == 0 ? 300 : (20000 + (g * 7919u) % 150000) & ~3u; }
static void gen_fill(UINT32 *w, UINT32 bytes, UINT32 g)
{
    for (UINT32 i = 0; i < bytes / 4; i++) w[i] = g * 2654435761u + i;
}
static UINT8 g_buf[256 * 1024], g_back[256 * 1024];

static UINT64 lookup(NtfsVol *v, UINT64 dir, const char *name)
{
    UINT64 m = 0; bool d;
    return NtfsLookup(v, dir, name, &m, &d) ? m : 0;
}
static UINT64 make(NtfsVol *v, UINT64 dir, const char *name, const void *data, UINT32 len)
{
    UINT64 m = lookup(v, dir, name);
    if (!m && !NtfsCreate(v, dir, name, false, &m)) return 0;
    return NtfsWriteFile(v, m, data, len) ? m : 0;
}
static UINT32 big_text(char *out, UINT32 i)
{
    UINT32 n = 0;
    for (UINT32 k = 0; k < 40 + i % 900; k++) out[n++] = (char)('a' + (i + k) % 26);
    return n;
}

/* The churn: everything the cut points fall in */
static void churn(NtfsVol *v, UINT64 cd, UINT64 big)
{
    char name[32], text[1024];
    for (UINT32 g = 8; g < 8 + ROUNDS; g++) {
        snprintf(name, sizeof(name), "c%u.bin", g % 4);
        gen_fill((UINT32 *)g_buf, gen_size(g), g);
        assert(make(v, cd, name, g_buf, gen_size(g)));
        snprintf(name, sizeof(name), "t%u.tmp", g);                 /* made, written, moved */
        UINT32 n = big_text(text, 1000 + g);
        UINT64 t = make(v, cd, name, text, n);
        assert(t);
        char to[32];
        snprintf(to, sizeof(to), "n%u.txt", 1000 + g);
        assert(NtfsRename(v, cd, t, name, big, to));
        if (g % 3 == 0) {                                          /* a second name, then the first goes */
            snprintf(name, sizeof(name), "l%u.txt", 1000 + g);
            assert(NtfsLink(v, t, cd, name));
            assert(NtfsDelete(v, big, t, to));
        }
        snprintf(name, sizeof(name), "f%u.txt", g * 5 % BIG_FILES);  /* one of the many goes */
        UINT64 m = lookup(v, big, name);
        if (m) assert(NtfsDelete(v, big, m, name));
        if (g % 4 == 0) NtfsMarkClean(v);                          /* (a quiet second) */
    }
}

/* The disk after the cut, mounted again: writable, and every file whole */
static int check_after(UINT8 *disk, long cut, const char *checker, const char *out)
{
    Ram r;
    ram_init(&r, disk);
    NtfsVol *v = NtfsMount(&r.d, 0);
    int bad = 0;
#define EXPECT(c) do { if (!(c)) { printf("cut %ld: %s\n", cut, #c); bad++; } } while (0)
    EXPECT(v);
    if (!v) return 1;
    quiet = 0;
    printf("cut %ld: ", cut);
    EXPECT(NtfsEnableWrite(v));
    quiet = 1;
    UINT64 cd = lookup(v, NTFS_ROOT, CHURN_DIR), big = lookup(v, cd, "Big");
    EXPECT(cd && big);
    UINT64 k = lookup(v, cd, "kept.txt"), size = 0;
    EXPECT(k && NtfsSize(v, k, &size) && size == sizeof(KEPT) - 1 && NtfsRead(v, k, 0, g_back, size) &&
           !memcmp(g_back, KEPT, size));
    for (UINT32 i = 0; i < 4; i++) {                               /* one whole generation, old or new */
        char name[16];
        snprintf(name, sizeof(name), "c%u.bin", i);
        UINT64 m = lookup(v, cd, name);
        EXPECT(m && NtfsSize(v, m, &size) && size <= sizeof(g_back) && NtfsRead(v, m, 0, g_back, size));
        UINT32 g = 0, w0 = *(UINT32 *)g_back;
        for (UINT32 c = 4; c < 8 + ROUNDS && !g; c++) if (c * 2654435761u == w0) g = c;
        gen_fill((UINT32 *)g_buf, gen_size(g), g);
        if (!(g && g % 4 == i && size == gen_size(g) && !memcmp(g_back, g_buf, size)))
            printf("cut %ld: %s holds %llu bytes, generation %u\n", cut, name, (unsigned long long)size, g);
        EXPECT(g && g % 4 == i && size == gen_size(g) && !memcmp(g_back, g_buf, size));
    }
    for (UINT32 i = 0; i < BIG_FILES; i++) {                       /* the many: whole, or gone */
        char name[32], text[1024];
        snprintf(name, sizeof(name), "f%u.txt", i);
        UINT64 m = lookup(v, big, name);
        if (!m) continue;
        UINT32 n = big_text(text, i);
        EXPECT(NtfsSize(v, m, &size) && size == n && NtfsRead(v, m, 0, g_back, n) && !memcmp(g_back, text, n));
    }
    UINT64 m = 0;                                                  /* and it takes a change again */
    EXPECT(NtfsCreate(v, cd, "after.txt", false, &m) && NtfsWriteFile(v, m, "after", 5));
    NtfsUnmount(v);
    FILE *f = fopen(out, "wb");
    assert(f && fwrite(disk, 1, DISK_BYTES, f) == DISK_BYTES);
    fclose(f);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s %s > %s.log 2>&1", checker, out, out);
    if (system(cmd)) {
        printf("cut %ld: the check found errors:\n", cut);
        snprintf(cmd, sizeof(cmd), "grep -m 12 ERROR %s.log", out);
        if (system(cmd)) {}
        bad++;
    }
    return bad;
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: ntfs_cut CHECKER IMAGE [STEP]\n"); return 2; }
    const char *checker = argv[1], *image = argv[2];
    long step = argc > 3 ? atol(argv[3]) : 1;
    NtfsSetClock(now);
    UINT8 *base = calloc(1, DISK_BYTES), *disk = malloc(DISK_BYTES), *snap = malloc(DISK_BYTES);
    assert(base && disk && snap);

    /* The volume before the churn: kept.txt, c0-c3, and the many in Big */
    Ram r;
    ram_init(&r, base);
    assert(NtfsFormat(&r.d, 0, DISK_BYTES / BLOCK_SECTOR, "NOVATEST", 0x1234));
    NtfsVol *v = NtfsMount(&r.d, 0);
    assert(v && NtfsEnableWrite(v));
    UINT64 cd, big;
    assert(NtfsCreate(v, NTFS_ROOT, CHURN_DIR, true, &cd) && NtfsCreate(v, cd, "Big", true, &big));
    assert(make(v, cd, "kept.txt", KEPT, sizeof(KEPT) - 1));
    for (UINT32 g = 0; g < 4; g++) {
        char name[16];
        snprintf(name, sizeof(name), "c%u.bin", g);
        gen_fill((UINT32 *)g_buf, gen_size(g + 4), g + 4);
        assert(make(v, cd, name, g_buf, gen_size(g + 4)));
    }
    for (UINT32 i = 0; i < BIG_FILES; i++) {
        char name[32], text[1024];
        snprintf(name, sizeof(name), "f%u.txt", i);
        assert(make(v, big, name, text, big_text(text, i)));
    }
    NtfsUnmount(v);

    /* Once through, to count the writes */
    quiet = 1;
    memcpy(disk, base, DISK_BYTES);
    ram_init(&r, disk);
    v = NtfsMount(&r.d, 0);
    assert(v && NtfsEnableWrite(v));
    NtfsMarkWhileWriting(v);
    UINT64 cd2 = lookup(v, NTFS_ROOT, CHURN_DIR);
    churn(v, cd2, lookup(v, cd2, "Big"));
    NtfsUnmount(v);
    long total = r.writes;
    printf("the churn makes %ld disk writes\n", total);

    int bad = 0, cuts = 0;
    for (long cut = getenv("CUT0") ? atol(getenv("CUT0")) : 0; cut < total; cut += step) {
        memcpy(disk, base, DISK_BYTES);
        ram_init(&r, disk);
        r.snap = snap;
        r.cut_at = cut;
        v = NtfsMount(&r.d, 0);
        assert(v && NtfsEnableWrite(v));
        NtfsMarkWhileWriting(v);
        cd2 = lookup(v, NTFS_ROOT, CHURN_DIR);
        churn(v, cd2, lookup(v, cd2, "Big"));
        NtfsUnmount(v);
        bad += check_after(snap, cut, checker, image) != 0;
        cuts++;
    }
    /* What NovaOS did not leave is left for chkdsk: a torn record stays read-only, untouched */
    memcpy(disk, base, DISK_BYTES);
    ram_init(&r, disk);
    v = NtfsMount(&r.d, 0);
    assert(v && NtfsEnableWrite(v));
    NtfsMarkWhileWriting(v);
    cd2 = lookup(v, NTFS_ROOT, CHURN_DIR);
    UINT64 kept = lookup(v, cd2, "kept.txt"), m;
    assert(kept && kept < 64 && NtfsCreate(v, cd2, "new.txt", false, &m) && NtfsMarked(v));
    memcpy(snap, disk, DISK_BYTES);                                /* (cut off there, dirty) */
    NtfsUnmount(v);
    snap[4096ull * 4 + kept * 1024 + 510] ^= 0x5A;                /* (kept.txt's record, in the MFT's first extent
                                                                      at cluster 4: half of it never written) */
    memcpy(disk, snap, DISK_BYTES);
    ram_init(&r, disk);
    v = NtfsMount(&r.d, 0);
    quiet = 0;
    printf("torn: ");
    bool rw = v && NtfsEnableWrite(v);
    if (v) NtfsUnmount(v);
    if (rw || memcmp(disk, snap, DISK_BYTES)) { printf("torn record: the volume was written to\n"); bad++; }
    printf("ntfs_cut: %d cut points, %d bad\n", cuts, bad);
    return bad != 0;
}
