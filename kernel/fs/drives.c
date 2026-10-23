/*
 * drives.c — the disks' volumes, and D:, E:, ... (see drives.h)
 *
 * A disk holds a volume of its own (no partition table) or partitions in
 * an MBR or a GPT (partition.c); each is a volume.  NTFS volumes are
 * mounted into drive C:'s tree of nodes (ramfs.h), writable when Windows
 * left them clean (ntfs.c).  FAT volumes are mounted too: read-write on
 * fixed disks, read-only on removable ones (USB sticks, the live stick's
 * boot log is written in place there).  NovaOS's own FAT volumes (the EFI
 * System Partition, the one drive C: is saved to) get no letter.
 */

#include "drives.h"
#include "ntfs.h"
#include "fat.h"
#include "ramfs.h"
#include "block.h"
#include "persist.h"
#include "bootlog.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../um/um.h"
#include "../wm/wm.h"
#include "../ke/scheduler.h"
#include "../ke/smp.h"
#include "../arch/x86_64/cpu.h"

/* ---- NTFS ---- */

static UINT32 g_ops;                       /* creates, deletes, renames, links: changes too (DrivesPoll) */

static bool ntfs_list(void *vol, UINT64 ref, bool (*add)(const RamfsExtEntry *e, void *ctx), void *ctx);
static bool ntfs_size(void *vol, UINT64 ref, UINT64 *size) { return NtfsSize(vol, ref, size); }
static bool ntfs_read(void *vol, UINT64 ref, UINT64 off, void *buf, UINT64 len) { return NtfsRead(vol, ref, off, buf, len); }

/* A record whose descriptor is the root's inherits, as drive C: saves them
 * (fs/persist.c): its own descriptor is any other one */
static bool ntfs_security(void *vol, UINT64 ref, void *buf, UINT32 cap, UINT32 *len)
{
    UINT32 id = NtfsSecurityId(vol, ref);
    if (!id || (ref != NTFS_ROOT && id == NtfsSecurityId(vol, NTFS_ROOT))) return false;
    return NtfsSecurityById(vol, id, buf, cap, len);
}
static bool ntfs_set_security(void *vol, UINT64 ref, const void *sd, UINT32 len)
{
    UINT32 id = sd ? NtfsAddSecurity(vol, sd, len) : NtfsSecurityId(vol, NTFS_ROOT);   /* (the root's may be 0) */
    return (id || !sd) && NtfsSetSecurityId(vol, ref, id) && NtfsSync(vol);
}
static bool ntfs_set_info(void *vol, UINT64 ref, UINT64 ctime, UINT64 mtime, UINT32 attrs)
{
    if (ref == NTFS_ROOT) return true;            /* (the root's are the volume's own) */
    return NtfsSetInfo(vol, ref, ctime, mtime, attrs) && NtfsSync(vol);
}

static bool ntfs_create(void *vol, UINT64 dir, const char *name, bool is_dir, UINT64 *ref)
{
    g_ops++;
    if (!NtfsCreate(vol, dir, name, is_dir, ref)) return false;
    /* (NTFS gives it its folder's descriptor: a new node inherits instead) */
    UINT32 root = NtfsSecurityId(vol, NTFS_ROOT);         /* (0 on a volume whose root keeps its own, as mkntfs makes) */
    if (NtfsSecurityId(vol, *ref) != root) NtfsSetSecurityId(vol, *ref, root);
    return NtfsSync(vol);
}
static bool ntfs_remove(void *vol, UINT64 dir, UINT64 ref, const char *name)
{
    g_ops++;
    return NtfsDelete(vol, dir, ref, name) && NtfsSync(vol);
}
static bool ntfs_rename(void *vol, UINT64 dir, UINT64 ref, const char *old_name, UINT64 to, const char *name)
{
    g_ops++;
    return NtfsRename(vol, dir, ref, old_name, to, name) && NtfsSync(vol);
}
static bool ntfs_link(void *vol, UINT64 ref, UINT64 dir, const char *name)
{
    g_ops++;
    return NtfsLink(vol, ref, dir, name) && NtfsSync(vol);
}
static UINT32 ntfs_links(void *vol, UINT64 ref) { return NtfsLinks(vol, ref); }
static bool ntfs_names(void *vol, UINT64 ref, bool (*add)(const char *path, void *ctx), void *ctx)
{
    return NtfsNames(vol, ref, add, ctx);
}
static bool ntfs_write(void *vol, UINT64 ref, const void *data, UINT64 len)
{
    return NtfsWriteFile(vol, ref, data, len) && NtfsSync(vol);
}

static UINT64 ntfs_free(void *vol) { return NtfsFreeBytes(vol); }
static bool ntfs_fits(void *vol, UINT64 len) { return NtfsFits(vol, len); }
static bool ntfs_can_write(void *vol, UINT64 ref) { return NtfsCanWrite(vol, ref); }

static const RamfsSource g_ntfs_source = { ntfs_list, ntfs_size, ntfs_read, NULL, NULL, NULL, NULL, NULL, NULL,
                                           NULL, ntfs_links, ntfs_names, NULL, NULL, ntfs_security, NULL };
static const RamfsSource g_ntfs_rw_source = { ntfs_list, ntfs_size, ntfs_read, ntfs_create, ntfs_remove,
                                              ntfs_rename, ntfs_write, ntfs_free, ntfs_can_write, ntfs_link, ntfs_links,
                                              ntfs_names, ntfs_fits, ntfs_set_info, ntfs_security, ntfs_set_security };

typedef struct { bool (*add)(const RamfsExtEntry *e, void *ctx); void *ctx; } ListCtx;

static bool ntfs_one(const NtfsEntry *e, void *ctx)
{
    ListCtx *l = ctx;
    RamfsExtEntry x;
    memcpy(x.name, e->name, sizeof(x.name));
    x.dir = e->dir;
    x.ref = e->mft;
    x.size = e->size;
    x.ctime = e->ctime;
    x.mtime = e->mtime;
    x.attrs = e->attrs;
    return l->add(&x, l->ctx);
}

static bool ntfs_list(void *vol, UINT64 ref, bool (*add)(const RamfsExtEntry *e, void *ctx), void *ctx)
{
    ListCtx l = { add, ctx };
    return NtfsList(vol, ref, ntfs_one, &l);
}

/* ---- FAT ----
 * FAT names files by their directory entry, not by a number, so the
 * entries listed so far are kept in a table and a node's ref is its index
 * there plus one (0 is the root directory); each remembers the directory
 * it was found in, which writing it again needs. */

typedef struct {
    FatVol   *v;
    FatEntry *ents;
    UINT32   *parent;                      /* each entry's directory (its cluster, or FAT_ROOT) */
    UINT32    n, cap;
} FatSrc;

typedef struct { FatSrc *s; UINT32 dir; ListCtx l; } FatListCtx;

static UINT32 fat_add(FatSrc *s, const FatEntry *e, UINT32 dir)
{
    if (s->n == s->cap) {
        UINT32 cap = s->cap ? s->cap * 2 : 256;
        FatEntry *ne = kmalloc(sizeof(FatEntry) * cap);
        UINT32 *np = kmalloc(sizeof(UINT32) * cap);
        if (!ne || !np) { kfree(ne); kfree(np); return 0; }
        if (s->n) { memcpy(ne, s->ents, sizeof(FatEntry) * s->n); memcpy(np, s->parent, sizeof(UINT32) * s->n); }
        kfree(s->ents);
        kfree(s->parent);
        s->ents = ne;
        s->parent = np;
        s->cap = cap;
    }
    s->ents[s->n] = *e;
    s->parent[s->n] = dir;
    return ++s->n;
}

static bool fat_one(const FatEntry *e, void *ctx)
{
    FatListCtx *c = ctx;
    UINT32 ref = fat_add(c->s, e, c->dir);
    if (!ref) return false;
    RamfsExtEntry x;
    memset(&x, 0, sizeof(x));
    strncpy(x.name, e->name, sizeof(x.name) - 1);
    x.dir = e->dir;
    x.ref = ref;
    x.size = e->size;
    x.mtime = PersistDosToFiletime(e->wtime);
    x.ctime = e->ctime ? PersistDosToFiletime(e->ctime) : x.mtime;
    x.attrs = e->attr & 0x27u;
    return c->l.add(&x, c->l.ctx);
}

/* The directory @ref names (its cluster; FAT_ROOT for the root) */
static bool fat_dir(FatSrc *s, UINT64 ref, UINT32 *dir)
{
    if (ref > s->n) return false;
    *dir = ref ? s->ents[ref - 1].cluster : FAT_ROOT;
    return true;
}

static bool fat_list(void *vol, UINT64 ref, bool (*add)(const RamfsExtEntry *e, void *ctx), void *ctx)
{
    FatSrc *s = vol;
    UINT32 dir;
    if (!fat_dir(s, ref, &dir)) return false;
    FatListCtx c = { s, dir, { add, ctx } };
    return FatList(s->v, dir, fat_one, &c);
}

static bool fat_size(void *vol, UINT64 ref, UINT64 *size)
{
    FatSrc *s = vol;
    if (!ref || ref > s->n) return false;
    *size = s->ents[ref - 1].size;
    return true;
}

static bool fat_read(void *vol, UINT64 ref, UINT64 off, void *buf, UINT64 len)
{
    FatSrc *s = vol;
    if (!ref || ref > s->n) return false;
    const FatEntry *e = &s->ents[ref - 1];
    if (off + len > e->size) return false;
    if (off == 0 && len == e->size) return FatRead(s->v, e, buf);
    UINT8 *all = kmalloc(e->size ? e->size : 1);
    if (!all) return false;
    bool ok = FatRead(s->v, e, all);
    if (ok) memcpy(buf, all + off, (size_t)len);
    kfree(all);
    return ok;
}

/* Entry @ref read again from its directory (after it was written or renamed) */
static bool fat_refresh(FatSrc *s, UINT64 ref, const char *name)
{
    FatEntry e;
    if (!FatLookup(s->v, s->parent[ref - 1], name, &e)) return false;
    s->ents[ref - 1] = e;
    return true;
}

static bool fat_create(void *vol, UINT64 dir, const char *name, bool is_dir, UINT64 *ref)
{
    FatSrc *s = vol;
    UINT32 dc, out;
    FatEntry e;
    g_ops++;
    if (!fat_dir(s, dir, &dc)) return false;
    FatSetStamp(s->v, 0);
    if (!(is_dir ? FatMkdir(s->v, dc, name, &out) : FatWriteFile(s->v, dc, name, "", 0)) ||
        !FatLookup(s->v, dc, name, &e) || !FatSync(s->v))
        return false;
    UINT32 r = fat_add(s, &e, dc);
    if (!r) return false;
    *ref = r;
    return true;
}

static bool fat_remove(void *vol, UINT64 dir, UINT64 ref, const char *name)
{
    FatSrc *s = vol;
    UINT32 dc;
    (void)ref;
    g_ops++;
    return fat_dir(s, dir, &dc) && FatDelete(s->v, dc, name) && FatSync(s->v);
}

/* (a directory keeps its cluster, so what it holds keeps its refs) */
static bool fat_rename(void *vol, UINT64 dir, UINT64 ref, const char *old_name, UINT64 to, const char *name)
{
    FatSrc *s = vol;
    UINT32 dc, tc;
    g_ops++;
    if (!ref || ref > s->n || !fat_dir(s, dir, &dc) || !fat_dir(s, to, &tc)) return false;
    if (!FatMove(s->v, dc, old_name, tc, name) || !FatSync(s->v)) return false;
    s->parent[ref - 1] = tc;
    return fat_refresh(s, ref, name);
}

static bool fat_set_info(void *vol, UINT64 ref, UINT64 ctime, UINT64 mtime, UINT32 attrs)
{
    FatSrc *s = vol;
    if (!ref) return true;                        /* (the root directory has no entry) */
    if (ref > s->n) return false;
    char name[FAT_NAME_MAX];
    strncpy(name, s->ents[ref - 1].name, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    return FatSetInfo(s->v, s->parent[ref - 1], name, (UINT8)attrs, PersistFiletimeToDos(ctime),
                      PersistFiletimeToDos(mtime)) && FatSync(s->v) && fat_refresh(s, ref, name);
}

static bool fat_write(void *vol, UINT64 ref, const void *data, UINT64 len)
{
    FatSrc *s = vol;
    if (!ref || ref > s->n || len > 0xFFFFFFFFull) return false;
    char name[FAT_NAME_MAX];
    strncpy(name, s->ents[ref - 1].name, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    FatSetStamp(s->v, 0);
    return FatWriteFile(s->v, s->parent[ref - 1], name, data, (UINT32)len) && FatSync(s->v) &&
           fat_refresh(s, ref, name);
}

static UINT64 fat_free(void *vol) { return FatFreeBytes(((FatSrc *)vol)->v); }
static bool fat_fits(void *vol, UINT64 len) { return len <= 0xFFFFFFFFull && len <= fat_free(vol); }

static const RamfsSource g_fat_source = { fat_list, fat_size, fat_read, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                          NULL, NULL, NULL };
static const RamfsSource g_fat_rw_source = { fat_list, fat_size, fat_read, fat_create, fat_remove, fat_rename, fat_write,
                                             fat_free, NULL, NULL, NULL, NULL, fat_fits, fat_set_info, NULL, NULL };

static void fat_src_free(FatSrc *s)
{
    FatUnmount(s->v);
    kfree(s->ents);
    kfree(s->parent);
    kfree(s);
}

/* ---- the volumes ----
 * g_vols lists every partition of every disk; g_mounts the ones mounted
 * as drive letters.  A volume keeps its number while its disk is there. */

#define MAX_MOUNTS 24
#define MAX_VOLS   48

typedef struct {
    BlockDev *dev;
    UINT64    lba;
    char      letter;
    bool      ntfs, rw;
    void     *vol;                         /* NtfsVol, or FatSrc */
} Mount;

typedef struct {
    bool      used;
    BlockDev *dev;                         /* NULL: drive C: in memory only */
    UINT32    part;
    UINT64    lba, sectors;
    UINT32    number;
    UINT8     guid[16];
    UINT8     uid[24];                     /* its MountedDevices value (what Windows writes there) */
    UINT32    uid_len;
    UINT8     mbr_type;                    /* MBR disks */
    bool      gpt, esp;
    char      fs[8], label[34];            /* from its boot sector */
    UINT64    fs_bytes;                    /* the file system's size, from its boot sector (0: the partition's) */
    bool      seen;                        /* (a rescan found it again) */
} Vol;

static Mount  g_mounts[MAX_MOUNTS];
static Vol    g_vols[MAX_VOLS];
static UINT32 g_next_number = 2;           /* (1 is drive C:'s) */
static bool   g_reg;                       /* the registry is there (DrivesRestoreLetters) */

#define MOUNTED_DEVICES "Machine\\SYSTEM\\MountedDevices"

static const UINT8 g_c_guid[16] = { 0x61, 0x76, 0x6F, 0x4E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01 };   /* {4e6f7661-0000-0000-0000-000000000001} */

static Mount *mount_of(BlockDev *d, UINT64 lba)
{
    for (int i = 0; i < MAX_MOUNTS; i++)
        if (g_mounts[i].dev && g_mounts[i].dev == d && g_mounts[i].lba == lba) return &g_mounts[i];
    return NULL;
}

static Vol *vol_by_guid(const UINT8 guid[16])
{
    for (int i = 0; i < MAX_VOLS; i++)
        if (g_vols[i].used && !memcmp(g_vols[i].guid, guid, 16)) return &g_vols[i];
    return NULL;
}

static bool is_c(const Vol *v) { return !v->dev || PersistOwns(v->dev, v->lba); }

/* NovaOS's own: drive C:'s volume, the ESP of a disk NovaOS is on, the
 * stick it is running from */
static bool is_nova(const Vol *v)
{
    if (is_c(v) || v->dev == BootLogDevice()) return true;
    return v->esp || !strcmp(v->label, "NOVA_EFI") || !strcmp(v->label, "NOVADATA");
}

static void hex_label(char *out, const UINT8 *bpb_label)
{
    int n = 11;
    memcpy(out, bpb_label, 11);
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    if (!strcmp(out, "NO NAME")) out[0] = 0;
}

/* What file system the boot sector @s shows (@fs "" for none), its label
 * and size, and a serial number for the volume's identity */
static void sniff(const UINT8 *s, Vol *v, UINT32 *serial)
{
    v->fs[0] = v->label[0] = 0;
    v->fs_bytes = 0;
    *serial = 0;
    if (!memcmp(s + 3, "NTFS    ", 8)) {
        strcpy(v->fs, "NTFS");
        UINT64 n;
        memcpy(&n, s + 40, 8);
        v->fs_bytes = (n + 1) * BLOCK_SECTOR;
        memcpy(serial, s + 72, 4);
        return;
    }
    if (!memcmp(s + 3, "EXFAT   ", 8)) { strcpy(v->fs, "exFAT"); return; }
    if ((s[0] == 0xEB || s[0] == 0xE9) && s[11] == 0x00 && s[12] == 0x02 && s[13] && s[510] == 0x55 && s[511] == 0xAA) {
        UINT32 total = (UINT32)(s[19] | s[20] << 8);
        if (!total) memcpy(&total, s + 32, 4);
        v->fs_bytes = (UINT64)total * BLOCK_SECTOR;
        if (!memcmp(s + 82, "FAT32", 5)) {
            strcpy(v->fs, "FAT32");
            hex_label(v->label, s + 71);
            memcpy(serial, s + 67, 4);
        } else if (!memcmp(s + 54, "FAT", 3)) {
            strcpy(v->fs, !memcmp(s + 54, "FAT12", 5) ? "FAT12" : "FAT16");
            hex_label(v->label, s + 43);
            memcpy(serial, s + 39, 4);
        }
    }
}

/* Record the partition @p of @d (layout @l) as a volume, new or seen again */
static void add_vol(BlockDev *d, const DiskLayout *l, const DiskPart *p, const UINT8 *boot)
{
    Vol *v = NULL;
    for (int i = 0; i < MAX_VOLS && !v; i++)
        if (g_vols[i].used && g_vols[i].dev == d && g_vols[i].lba == p->lba && g_vols[i].sectors == p->sectors) v = &g_vols[i];
    bool fresh = !v;
    for (int i = 0; i < MAX_VOLS && !v; i++)
        if (!g_vols[i].used) v = &g_vols[i];
    if (!v) return;
    if (fresh) {
        memset(v, 0, sizeof(*v));
        v->used = true;
        v->dev = d;
        v->lba = p->lba;
        v->sectors = p->sectors;
        v->number = PersistOwns(d, p->lba) ? 1 : g_next_number++;
    }
    v->seen = true;
    v->part = p->number;
    v->gpt = l->style == DISK_GPT;
    v->esp = DiskIsEsp(l, p);
    v->mbr_type = p->mbr_type;
    UINT32 serial;
    sniff(boot, v, &serial);
    /* Its identity, as Windows' mount manager keeps it: on a GPT disk
     * "DMIO:ID:" and the partition's GUID, else the disk signature and
     * the partition's offset in bytes (the volume serial for a disk with
     * no table, whose "signature" bytes are boot code) */
    UINT64 off = p->lba * BLOCK_SECTOR;
    if (v->gpt) {
        memcpy(v->uid, "DMIO:ID:", 8);
        memcpy(v->uid + 8, p->id, 16);
        v->uid_len = 24;
        memcpy(v->guid, p->id, 16);
    } else {
        UINT32 sig = l->superfloppy ? serial : l->signature;
        memcpy(v->uid, &sig, 4);
        memcpy(v->uid + 4, &off, 8);
        v->uid_len = 12;
        memcpy(v->guid, &sig, 4);
        v->guid[4] = 'N'; v->guid[5] = 'V';                  /* {SIGNATURE-564e-4d42-OFFSET} */
        v->guid[6] = 'B'; v->guid[7] = 'M';
        memcpy(v->guid + 8, &off, 8);
    }
}

/* Read @d's partitions into the volume list (and their boot sectors) */
static void scan_vols(BlockDev *d, DiskLayout *l)
{
    UINT8 *s = kmalloc(BLOCK_SECTOR);
    if (!s || !DiskReadLayout(d, l)) { kfree(s); l->count = 0; return; }
    for (int i = 0; i < MAX_VOLS; i++)
        if (g_vols[i].used && g_vols[i].dev == d) g_vols[i].seen = false;
    for (int i = 0; i < l->count; i++) {
        const DiskPart *p = &l->part[i];
        if (!d->read(d, p->lba, 1, s)) memset(s, 0, BLOCK_SECTOR);
        add_vol(d, l, p, s);
    }
    for (int i = 0; i < MAX_VOLS; i++)
        if (g_vols[i].used && g_vols[i].dev == d && !g_vols[i].seen) memset(&g_vols[i], 0, sizeof(Vol));
    kfree(s);
}

/* ---- drive letters ---- */

static char free_letter(void)
{
    for (char l = 'D'; l <= 'Z'; l++)
        if (!RamfsDriveRoot(l)) return l;
    return 0;
}

static void guid_text(const UINT8 g[16], char *out)
{
    UINT32 d1;
    UINT16 d2, d3;
    memcpy(&d1, g, 4); memcpy(&d2, g + 4, 2); memcpy(&d3, g + 6, 2);
    ksnprintf(out, 40, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", d1, d2, d3, g[8], g[9], g[10], g[11],
              g[12], g[13], g[14], g[15]);
}

/* The letter the registry keeps for @v: 'X', 0 when it was taken away
 * (the volume is known, with no letter), or -1 for a volume never seen */
static int saved_letter(const Vol *v)
{
    if (!g_reg) return -1;
    UINT8 b[32];
    char name[16];
    for (char c = 'D'; c <= 'Z'; c++) {
        ksnprintf(name, sizeof(name), "\\DosDevices\\%c:", c);
        int n = um_registry_get_bin(MOUNTED_DEVICES, name, b, sizeof(b));
        if (n == (int)v->uid_len && !memcmp(b, v->uid, v->uid_len)) return c;
    }
    char g[40], vn[56];
    guid_text(v->guid, g);
    ksnprintf(vn, sizeof(vn), "\\??\\Volume%s", g);
    return um_registry_get_bin(MOUNTED_DEVICES, vn, b, sizeof(b)) >= 0 ? 0 : -1;
}

/* Remember @v's letter @letter (0: none) */
static void save_letter(const Vol *v, char letter)
{
    if (!g_reg || is_c(v)) return;
    UINT8 b[32];
    char name[16], g[40], vn[56];
    for (char c = 'D'; c <= 'Z'; c++) {                     /* its old letter goes (and a letter's old volume) */
        ksnprintf(name, sizeof(name), "\\DosDevices\\%c:", c);
        int n = um_registry_get_bin(MOUNTED_DEVICES, name, b, sizeof(b));
        if (n < 0) continue;
        if ((n == (int)v->uid_len && !memcmp(b, v->uid, v->uid_len)) || c == letter)
            um_registry_delete_value(MOUNTED_DEVICES, name);
    }
    if (letter) {
        ksnprintf(name, sizeof(name), "\\DosDevices\\%c:", letter);
        um_registry_set_bin(MOUNTED_DEVICES, name, v->uid, v->uid_len);
    }
    guid_text(v->guid, g);
    ksnprintf(vn, sizeof(vn), "\\??\\Volume%s", g);
    um_registry_set_bin(MOUNTED_DEVICES, vn, v->uid, v->uid_len);
}

/* ---- mounting ---- */

static Mount *record(BlockDev *d, UINT64 lba, char letter, bool ntfs, bool rw, void *vol)
{
    for (int i = 0; i < MAX_MOUNTS; i++)
        if (!g_mounts[i].dev) {
            g_mounts[i] = (Mount){ d, lba, letter, ntfs, rw, vol };
            return &g_mounts[i];
        }
    return NULL;
}

/* Mount volume @v as drive @want (0: the saved or next free letter); false
 * if it has no file system NovaOS mounts or should stay without a letter */
static bool try_mount(Vol *v, char want)
{
    BlockDev *d = v->dev;
    if (!d || mount_of(d, v->lba) || PersistOwns(d, v->lba)) return false;  /* (drive C:'s volume is not D: too) */
    if (v->sectors < 16 || v->mbr_type == 0xEE) return false;
    char letter = want;
    if (!letter) {
        int saved = saved_letter(v);
        if (saved == 0) return false;                        /* (its letter was taken away) */
        letter = saved > 0 && !RamfsDriveRoot((char)saved) ? (char)saved : free_letter();
    }
    if (!letter || RamfsDriveRoot(letter)) return false;
    NtfsVol *nv = NtfsMount(d, v->lba);
    if (nv) {
        NtfsSetClock(RamfsNow);
        bool rw = NtfsEnableWrite(nv);
        if (rw) NtfsMarkWhileWriting(nv);                    /* (dirty while changes are being written) */
        if (!RamfsMountDrive(letter, rw ? &g_ntfs_rw_source : &g_ntfs_source, nv, NTFS_ROOT, NtfsLabel(nv), "NTFS",
                             NtfsTotalBytes(nv))) {
            NtfsUnmount(nv);
            return false;
        }
        record(d, v->lba, letter, true, rw, nv);
        kprintf("[DRIVES] %c: is NTFS volume \"%s\" on %s (%s)\n", letter, NtfsLabel(nv), d->name,
                rw ? "read-write" : "read-only");
        save_letter(v, letter);
        return true;
    }
    if (!d->removable && is_nova(v)) return false;           /* (NovaOS's own FAT volumes) */
    FatVol *f = FatMount(d, v->lba);
    if (!f) return false;
    bool rw = !d->removable;
    FatSrc *s = kzalloc(sizeof(FatSrc));
    const char *fs = FatType(f) == 32 ? "FAT32" : "FAT16";
    if (!s || !RamfsMountDrive(letter, rw ? &g_fat_rw_source : &g_fat_source, s, 0, FatLabel(f), fs, FatTotalBytes(f))) {
        kfree(s);
        FatUnmount(f);
        return false;
    }
    s->v = f;
    record(d, v->lba, letter, false, rw, s);
    kprintf("[DRIVES] %c: is %s volume \"%s\" on %s (%s)\n", letter, fs, FatLabel(f), d->name,
            rw ? "read-write" : "read-only");
    save_letter(v, letter);
    return true;
}

/* Drive @m goes: its changes written, its letter free */
static void unmount(Mount *m)
{
    RamfsUnmountDrive(m->letter);
    if (m->ntfs) {
        if (!m->dev->gone) NtfsMarkClean(m->vol);
        NtfsUnmount(m->vol);
    } else fat_src_free(m->vol);
    memset(m, 0, sizeof(*m));
}

/* Mount the volumes of @d (MBR: the NTFS and FAT types; GPT: every
 * partition, the mounts look at the boot sector) */
static void scan(BlockDev *d)
{
    DiskLayout *l = kmalloc(sizeof(DiskLayout));
    if (!l) return;
    scan_vols(d, l);
    for (int i = 0; i < MAX_VOLS; i++) {
        Vol *v = &g_vols[i];
        if (!v->used || v->dev != d) continue;
        UINT8 t = v->mbr_type;
        if (v->gpt || l->superfloppy || t == 0x07 || t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C ||
            t == 0x0E)
            try_mount(v, 0);
    }
    kfree(l);
}

void DrivesInit(void)
{
    for (int i = 0; i < BlockCount(); i++) scan(BlockGet(i));
}

void DrivesAttach(BlockDev *d)
{
    DesktopLock();
    scan(d);
    WmInvalidate();                          /* (File Explorer lists the drives) */
    DesktopUnlock();
}

/* The volumes get the letters chosen before; the rest have theirs recorded */
void DrivesRestoreLetters(void)
{
    DesktopLock();
    bkl_acquire();
    g_reg = true;
    um_registry_create_key(MOUNTED_DEVICES);
    for (int i = 0; i < MAX_VOLS; i++) {
        Vol *v = &g_vols[i];
        if (!v->used || is_c(v)) continue;
        Mount *m = mount_of(v->dev, v->lba);
        int saved = saved_letter(v);
        if (!m) continue;
        if (saved < 0 || saved == m->letter) { save_letter(v, m->letter); continue; }
        char from = m->letter;
        if (saved > 0 && RamfsDriveRoot((char)saved)) {      /* (taken: by a volume that may move off it) */
            save_letter(v, m->letter);
            continue;
        }
        unmount(m);
        if (saved > 0) try_mount(v, (char)saved);
        kprintf("[DRIVES] Volume %u: drive letter %c: -> %c: (as chosen before)\n", v->number, from,
                saved ? saved : '-');
    }
    bkl_release();
    DesktopUnlock();
}

void DrivesPoll(void)
{
    static UINT32 seen;
    static UINT64 since;
    UINT32 c = RamfsExtChanges() + g_ops;
    UINT64 t = sched_ticks();
    if (c != seen) { seen = c; since = t; }
    if (t - since < 100) return;                             /* wait for a quiet second */
    bool marked = false;
    for (int i = 0; i < MAX_MOUNTS && !marked; i++)
        marked = g_mounts[i].dev && g_mounts[i].ntfs && NtfsMarked(g_mounts[i].vol);
    if (RamfsExtDirty() || marked) DrivesSync();
}

/* The changed files written, then the NTFS volumes marked clean */
bool DrivesSync(void)
{
    DesktopLock();
    bool ok = RamfsFlush();
    bkl_acquire();                                           /* (the volume and disk drivers rely on it) */
    for (int i = 0; i < MAX_MOUNTS; i++)
        if (g_mounts[i].dev && g_mounts[i].ntfs && !g_mounts[i].dev->gone && !RamfsExtDirty())
            ok &= NtfsMarkClean(g_mounts[i].vol);
    bkl_release();
    DesktopUnlock();
    return ok;
}

void DrivesDetach(BlockDev *d)
{
    DesktopLock();
    for (int i = 0; i < MAX_MOUNTS; i++) {
        Mount *m = &g_mounts[i];
        if (m->dev != d) continue;
        kprintf("[DRIVES] %c: removed (%s went away)\n", m->letter, d->name);
        RamfsUnmountDrive(m->letter);
        if (m->ntfs) NtfsUnmount(m->vol);
        else fat_src_free(m->vol);
        memset(m, 0, sizeof(*m));
    }
    for (int i = 0; i < MAX_VOLS; i++)
        if (g_vols[i].used && g_vols[i].dev == d) memset(&g_vols[i], 0, sizeof(Vol));
    WmInvalidate();
    DesktopUnlock();
}

/* ---- Disk Management ---- */

int DrivesDiskNumber(BlockDev *d)
{
    for (int i = 0; i < BlockCount(); i++)
        if (BlockGet(i) == d) return i;
    return -1;
}

static void describe(const Vol *v, DriveVolume *o)
{
    memset(o, 0, sizeof(*o));
    o->dev = v->dev;
    o->partition = v->part;
    o->lba = v->lba;
    o->sectors = v->sectors;
    o->number = v->number;
    memcpy(o->guid, v->guid, 16);
    strncpy(o->fs, v->fs, sizeof(o->fs) - 1);
    strncpy(o->label, v->label, sizeof(o->label) - 1);
    o->total = v->fs_bytes ? v->fs_bytes : v->sectors * BLOCK_SECTOR;
    if (v->esp) o->flags |= DRIVE_VOL_SYSTEM;
    if (is_nova(v)) o->flags |= DRIVE_VOL_NOVA;
    if (v->dev && v->dev->removable) o->flags |= DRIVE_VOL_REMOVABLE;
    if (is_c(v)) {
        o->letter = 'C';
        o->flags |= DRIVE_VOL_BOOT | DRIVE_VOL_MOUNTED;
        UINT64 fr, tot;
        if (PersistSpace(&fr, &tot)) { o->free = fr; o->total = tot; }
        if (!v->dev) { strcpy(o->fs, "RAMFS"); strcpy(o->label, "NovaOS"); }
        return;
    }
    Mount *m = mount_of(v->dev, v->lba);
    if (!m) return;
    o->letter = m->letter;
    o->flags |= DRIVE_VOL_MOUNTED | (m->rw ? 0 : DRIVE_VOL_READONLY);
    if (m->ntfs) {
        strncpy(o->label, NtfsLabel(m->vol), sizeof(o->label) - 1);
        o->total = NtfsTotalBytes(m->vol);
        o->free = m->rw ? NtfsFreeBytes(m->vol) : 0;
    } else {
        FatVol *f = ((FatSrc *)m->vol)->v;
        strncpy(o->label, FatLabel(f), sizeof(o->label) - 1);
        o->total = FatTotalBytes(f);
        o->free = FatFreeBytes(f);
    }
}

/* Drive C:, in memory only (no volume saves it): a volume of its own */
static bool c_saved(void);
static bool c_in_memory(DriveVolume *o)
{
    if (c_saved()) return false;
    Vol v;
    memset(&v, 0, sizeof(v));
    v.number = 1;
    memcpy(v.guid, g_c_guid, 16);
    describe(&v, o);
    UINT64 total = 0, avail = 0, used = 0;
    pmm_stats(&total, &avail, &used);
    o->total = total << 12;
    o->free = avail << 12;
    return true;
}

int DrivesVolumes(DriveVolume *out, int max)
{
    int n = 0;
    DesktopLock();
    bkl_acquire();
    if (n < max && c_in_memory(&out[n])) n++;
    for (int pass = 0; pass < 2; pass++)                     /* (drive C:'s first) */
        for (int i = 0; i < MAX_VOLS && n < max; i++)
            if (g_vols[i].used && is_c(&g_vols[i]) == !pass) describe(&g_vols[i], &out[n++]);
    bkl_release();
    DesktopUnlock();
    return n;
}

/* The volume with GUID @guid, else number @number, else letter @letter */
static bool find_volume(const UINT8 *guid, UINT32 number, char letter, DriveVolume *out)
{
    DriveVolume *all = kmalloc(sizeof(DriveVolume) * (MAX_VOLS + 1));
    if (!all) return false;
    int n = DrivesVolumes(all, MAX_VOLS + 1);
    bool found = false;
    for (int i = 0; i < n && !found; i++)
        if (guid ? !memcmp(all[i].guid, guid, 16) : number ? all[i].number == number : all[i].letter == letter) {
            *out = all[i];
            found = true;
        }
    kfree(all);
    return found;
}

bool DrivesVolumeByGuid(const UINT8 guid[16], DriveVolume *out) { return find_volume(guid, 0, 0, out); }
bool DrivesVolumeByNumber(UINT32 number, DriveVolume *out) { return number && find_volume(NULL, number, 0, out); }
bool DrivesVolumeByLetter(char letter, DriveVolume *out)
{
    letter = (char)(letter & ~0x20);
    return letter >= 'A' && letter <= 'Z' && find_volume(NULL, 0, letter, out);
}

static bool c_saved(void)
{
    for (int i = 0; i < MAX_VOLS; i++)
        if (g_vols[i].used && is_c(&g_vols[i])) return true;
    return false;
}

UINT32 DrivesNumberOfLetter(char letter)
{
    letter = (char)(letter & ~0x20);
    if (letter == 'C') return 1;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        Mount *m = &g_mounts[i];
        if (!m->dev || m->letter != letter) continue;
        for (int k = 0; k < MAX_VOLS; k++)
            if (g_vols[k].used && g_vols[k].dev == m->dev && g_vols[k].lba == m->lba) return g_vols[k].number;
    }
    return 0;
}

char DrivesLetterOfGuid(const UINT8 guid[16], bool *known)
{
    *known = true;
    if (!memcmp(guid, g_c_guid, 16) && !c_saved()) return 'C';
    Vol *v = vol_by_guid(guid);
    if (!v) { *known = false; return 0; }
    if (is_c(v)) return 'C';
    Mount *m = mount_of(v->dev, v->lba);
    return m ? m->letter : 0;
}

bool DrivesGuidOfNumber(UINT32 number, UINT8 guid[16])
{
    if (number == 1 && !c_saved()) { memcpy(guid, g_c_guid, 16); return true; }
    for (int i = 0; i < MAX_VOLS; i++)
        if (g_vols[i].used && g_vols[i].number == number) { memcpy(guid, g_vols[i].guid, 16); return true; }
    return false;
}

/* (under DesktopLock and the big lock) the files changed on drive @m written */
static bool flush_mount(Mount *m)
{
    (void)m;
    return RamfsFlush();
}

int DrivesSetLetter(const UINT8 guid[16], char letter)
{
    letter = (char)(letter & ~0x20);
    if (letter && (letter < 'D' || letter > 'Z')) return DRIVES_INVALID;
    DesktopLock();
    bkl_acquire();
    int r = DRIVES_OK;
    Vol *v = vol_by_guid(guid);
    Mount *m = v && v->dev ? mount_of(v->dev, v->lba) : NULL;
    if (!v) r = DRIVES_NOT_FOUND;
    else if (is_nova(v)) r = DRIVES_IN_USE;
    else if (m && m->letter == letter) r = DRIVES_OK;
    else if (letter && RamfsDriveRoot(letter)) r = DRIVES_LETTER_TAKEN;
    else if (!m && !v->fs[0]) r = DRIVES_INVALID;            /* (nothing to mount: format it first) */
    else {
        char from = m ? m->letter : 0;
        if (m) { flush_mount(m); unmount(m); }
        if (letter) {
            if (!try_mount(v, letter)) r = DRIVES_FAILED;
        } else save_letter(v, 0);
        kprintf("[DRIVES] Volume %u: drive letter %c: -> %c:\n", v->number, from ? from : '-', letter ? letter : '-');
        WmInvalidate();
    }
    bkl_release();
    DesktopUnlock();
    return r;
}

static UINT64 serial64(void)
{
    UINT64 x = rdtsc() * 0x9E3779B97F4A7C15ull;
    return x ^ (x >> 29);
}

int DrivesFormat(const UINT8 guid[16], bool ntfs, const char *label)
{
    DesktopLock();
    bkl_acquire();
    int r = DRIVES_OK;
    Vol *v = vol_by_guid(guid);
    if (!v || !v->dev) r = v ? DRIVES_IN_USE : DRIVES_NOT_FOUND;
    else if (is_nova(v) || v->dev->gone) r = DRIVES_IN_USE;
    else if (v->sectors < (ntfs ? (32ull << 20) : (34ull << 20)) / BLOCK_SECTOR) r = DRIVES_INVALID;   /* (too small) */
    else {
        BlockDev *d = v->dev;
        Mount *m = mount_of(d, v->lba);
        char letter = m ? m->letter : 0;
        if (m) { flush_mount(m); unmount(m); }
        kprintf("[DRIVES] Formatting volume %u on %s as %s \"%s\" (%u MiB)\n", v->number, d->name,
                ntfs ? "NTFS" : "FAT32", label, (unsigned)(v->sectors >> 11));
        bool ok;
        if (ntfs) ok = NtfsFormat(d, v->lba, v->sectors, label, serial64());
        else {
            FatVol *f = FatFormat(d, v->lba, v->sectors, label);
            ok = f && FatSync(f);
            if (f) FatUnmount(f);
        }
        if (ok && d->flush) d->flush(d);
        /* An MBR entry gets the type for its file system, as Windows sets it */
        DiskLayout *l = kmalloc(sizeof(DiskLayout));
        if (ok && l && !v->gpt && DiskReadLayout(d, l) && !l->superfloppy)
            for (int i = 0; i < l->count; i++)
                if (l->part[i].lba == v->lba && l->part[i].mbr_type != (ntfs ? 0x07 : 0x0C)) {
                    l->part[i].mbr_type = ntfs ? 0x07 : 0x0C;
                    ok = DiskWriteLayout(d, l);
                }
        if (l) scan_vols(d, l);                              /* (its file system and label, read again) */
        kfree(l);
        v = vol_by_guid(guid);
        if (!ok || !v) r = DRIVES_FAILED;
        else if (!try_mount(v, letter && !RamfsDriveRoot(letter) ? letter : 0) && saved_letter(v) != 0)
            r = DRIVES_FAILED;
        kprintf("[DRIVES] Formatting volume %u: %s\n", v ? v->number : 0, r ? "failed" : "done");
        WmInvalidate();
    }
    bkl_release();
    DesktopUnlock();
    return r;
}

/* A volume of @d that is in use: mounted, or NovaOS's own */
static bool in_use(const Vol *v)
{
    return is_nova(v) || mount_of(v->dev, v->lba);
}

int DrivesSetLayout(BlockDev *d, const DiskLayout *l)
{
    if (!d || !DiskCheckLayout(d, l)) return DRIVES_INVALID;
    DesktopLock();
    bkl_acquire();
    int r = DRIVES_OK;
    if (d->gone || d == BootLogDevice()) r = DRIVES_IN_USE;
    for (int i = 0; i < MAX_VOLS && !r; i++) {
        Vol *v = &g_vols[i];
        if (!v->used || v->dev != d || !in_use(v)) continue;
        bool kept = false;
        for (int k = 0; k < l->count && !kept; k++)
            kept = l->part[k].lba == v->lba && l->part[k].sectors == v->sectors;
        if (!kept) r = DRIVES_IN_USE;
    }
    if (!r) {
        UINT8 *z = kzalloc(16 * BLOCK_SECTOR);
        bool ok = z != NULL;
        for (int k = 0; ok && k < l->count; k++) {           /* new partitions start blank */
            const DiskPart *p = &l->part[k];
            bool old = false;
            for (int i = 0; i < MAX_VOLS && !old; i++)
                old = g_vols[i].used && g_vols[i].dev == d && g_vols[i].lba == p->lba && g_vols[i].sectors == p->sectors;
            if (!old) ok = d->write(d, p->lba, p->sectors < 16 ? (UINT32)p->sectors : 16, z);
        }
        kfree(z);
        ok = ok && DiskWriteLayout(d, l);
        kprintf("[DRIVES] %s: new partition table (%s, %d partition%s)%s\n", d->name,
                l->style == DISK_GPT ? "GPT" : l->style == DISK_MBR ? "MBR" : "none", l->count, l->count == 1 ? "" : "s",
                ok ? "" : ": could not be written");
        if (!ok) r = DRIVES_FAILED;
        scan(d);                                              /* (its volumes as they are now) */
        WmInvalidate();
    }
    bkl_release();
    DesktopUnlock();
    return r;
}
