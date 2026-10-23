/*
 * setup.c — installing NovaOS on a disk (see setup.h)
 */

#include "setup.h"
#include "fat.h"
#include "ntfs.h"
#include "persist.h"
#include "drives.h"
#include "bootlog.h"
#include "bootfiles.h"
#include "ramfs.h"
#include "../um/um.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../hal/rtc.h"
#include "../hal/efi.h"
#include "../arch/x86_64/cpu.h"

#define PHYSMAP(p) ((const void *)(uintptr_t)(PHYSMAP_BASE + (p)))

static bool          g_live, g_live_usb;
static const UINT8  *g_media_kernel, *g_media_loader;     /* from the bootloader */
static UINT64        g_media_kernel_size, g_media_loader_size;

void SetupBootInfo(const BootInfo *info)
{
    if (!info || info->version < 3) return;
    g_live = (info->boot_flags & BOOT_FLAG_LIVE_MEDIA) != 0;
    g_live_usb = g_live && (info->boot_flags & BOOT_FLAG_LIVE_USB);
    if (info->media_kernel_base && info->media_kernel_size && info->media_loader_base && info->media_loader_size) {
        g_media_kernel = PHYSMAP(info->media_kernel_base);
        g_media_kernel_size = info->media_kernel_size;
        g_media_loader = PHYSMAP(info->media_loader_base);
        g_media_loader_size = info->media_loader_size;
    }
    if (g_live)
        kprintf("[SETUP] Running from the installation %s (kernel %u KB, loader %u KB in memory)\n",
                SetupMediaName(), (unsigned)(g_media_kernel_size >> 10), (unsigned)(g_media_loader_size >> 10));
    else if (info->boot_flags & BOOT_FLAG_ENTRY_ADDED)
        kprintf("[SETUP] Added the firmware boot entry \"NovaOS\" for this disk\n");
    else if (info->boot_flags & BOOT_FLAG_BOOT_ENTRY)
        kprintf("[SETUP] The firmware has a \"NovaOS\" boot entry for this disk\n");
}

bool SetupIsLive(void) { return g_live; }
const char *SetupMediaName(void) { return g_live_usb ? "USB stick" : "disc"; }
bool SetupLiveFromUsb(void) { return g_live_usb; }

/* ---------------------------------------------------------------------------
 * Disks
 * ------------------------------------------------------------------------- */
#define MIB_SECTORS 2048ull
static UINT64 align_up(UINT64 v)   { return (v + MIB_SECTORS - 1) & ~(MIB_SECTORS - 1); }
static UINT64 align_down(UINT64 v) { return v & ~(MIB_SECTORS - 1); }

/* What Setup calls partition @p: its volume's label and file system, or
 * its kind ("Windows (NTFS)", "SYSTEM (EFI System)", "Microsoft reserved") */
static void part_name(BlockDev *d, const DiskLayout *l, const DiskPart *p, const DriveVolume *vols, int nvols,
                      char *out, int cap)
{
    const DriveVolume *v = NULL;
    for (int i = 0; i < nvols && !v; i++)
        if (vols[i].dev == d && vols[i].lba == p->lba) v = &vols[i];
    const char *kind = DiskIsEsp(l, p) ? "EFI System"
                     : l->style == DISK_GPT && !memcmp(p->type, DISK_TYPE_MSR, 16) ? "Microsoft reserved"
                     : l->style == DISK_GPT && !memcmp(p->type, DISK_TYPE_RECOVERY, 16) ? "Windows recovery"
                     : v && v->fs[0] ? v->fs : NULL;
    if (v && v->label[0]) ksnprintf(out, (size_t)cap, "%s (%s)", v->label, kind ? kind : "unknown");
    else if (kind)        ksnprintf(out, (size_t)cap, "%s", kind);
    else                  ksnprintf(out, (size_t)cap, "Partition %u", p->number);
}

/* @l's partitions and free spaces in disk order into @s, and its largest
 * free space (whole MiB) */
static void map_disk(SetupDisk *s, const DiskLayout *l, const DriveVolume *vols, int nvols)
{
    int order[DISK_MAX_PARTS], n = 0;
    for (int i = 0; i < l->count; i++) {                  /* (sorted by where they start) */
        int k = n++;
        while (k > 0 && l->part[order[k - 1]].lba > l->part[i].lba) { order[k] = order[k - 1]; k--; }
        order[k] = i;
    }
    UINT64 at = align_up(l->first);
    for (int i = 0; i <= n; i++) {
        const DiskPart *p = i < n ? &l->part[order[i]] : NULL;
        UINT64 end = p ? align_down(p->lba) : align_down(l->last + 1);
        if (end > at && end - at >= 16 * MIB_SECTORS) {   /* free space worth showing */
            if (end - at > s->free_sectors) { s->free_lba = at; s->free_sectors = end - at; }
            if (s->nparts < SETUP_MAX_PARTS) {
                SetupPart *f = &s->parts[s->nparts++];
                f->lba = at; f->sectors = end - at; f->free = true;
                ksnprintf(f->name, sizeof(f->name), "Free space");
            }
        }
        if (!p) break;
        if (s->nparts < SETUP_MAX_PARTS) {
            SetupPart *q = &s->parts[s->nparts++];
            q->lba = p->lba; q->sectors = p->sectors;
            part_name(s->dev, l, p, vols, nvols, q->name, sizeof(q->name));
        }
        if (align_up(p->lba + p->sectors) > at) at = align_up(p->lba + p->sectors);
    }
}

/* Room for NovaOS by shrinking an NTFS volume (Windows' C:), when the free
 * space is too small: NovaOS gets half the volume's free space (at least
 * what it needs, at most 64 GiB, less if the volume can't give that much),
 * taken from its end, so the free space after it grows; of the NTFS
 * volumes on the disk, the one that leaves the most room.  Read-only. */
#define SHRINK_MAX_SECTORS ((64ull << 30) / BLOCK_SECTOR)

static void shrink_plan(SetupDisk *s, const DiskLayout *l)
{
    UINT64 need = (SetupBesideBytes() + BLOCK_SECTOR - 1) / BLOCK_SECTOR, best = 0;
    for (int i = 0; i < l->count; i++) {
        const DiskPart *p = &l->part[i];
        if (memcmp(p->type, DISK_TYPE_BASIC, 16)) continue;
        NtfsVol *v = NtfsMount(s->dev, p->lba);
        if (!v) continue;
        UINT64 min = 0, used = 0, total = NtfsTotalBytes(v);
        const char *why = NULL;
        bool ok = NtfsShrinkLimit(v, &min, &used, &why);
        NtfsUnmount(v);
        if (!ok) {
            ksnprintf(s->shrink_why, sizeof(s->shrink_why), "Windows' volume can't be shrunk: %s.", why);
            continue;
        }
        UINT64 next = l->last + 1;                        /* (the room ends where the next partition starts) */
        for (int k = 0; k < l->count; k++)
            if (l->part[k].lba > p->lba && l->part[k].lba < next) next = l->part[k].lba;
        UINT64 end = align_down(next), to = 0;
        UINT64 want = total > used ? (total - used) / 2 / BLOCK_SECTOR : 0;
        if (want > SHRINK_MAX_SECTORS) want = SHRINK_MAX_SECTORS;
        if (want < need) want = need;
        for (int pass = 0; pass < 2 && !to; pass++, want = need) {
            UINT64 at = end > want ? align_down(end - want) : 0;
            if (at > p->lba && (at - p->lba) * BLOCK_SECTOR >= min && at - p->lba < p->sectors) to = at - p->lba;
        }
        if (!to) {
            ksnprintf(s->shrink_why, sizeof(s->shrink_why), "Windows' volume is too full to make room for NovaOS.");
            continue;
        }
        UINT64 room = end - align_up(p->lba + to);
        if (room <= best) continue;
        best = room;
        s->can_shrink = true;
        s->shrink_lba = p->lba;
        s->shrink_from = p->sectors;
        s->shrink_to = to;
        s->shrink_used = used;
    }
}

int SetupListDisks(SetupDisk *out, int max)
{
    int n = 0;
    BlockDev *cdev = PersistDevice();
    DiskLayout *l = kmalloc(sizeof(DiskLayout));
    DriveVolume *vols = kmalloc(32 * sizeof(DriveVolume));
    int nvols = vols ? DrivesVolumes(vols, 32) : 0;
    for (int i = 0; i < BlockCount() && n < max; i++) {
        BlockDev *d = BlockGet(i);
        SetupDisk *s = &out[n++];
        memset(s, 0, sizeof(*s));
        s->dev = d;
        s->bytes = d->sectors * BLOCK_SECTOR;
        s->too_small = s->bytes < SETUP_MIN_BYTES;
        s->holds_c = d == cdev;
        s->boot = d == BootLogDevice();              /* the USB stick NovaOS is running from */
        FatVol *v[8];
        bool blank = false;
        int nf = PersistFindVolumes(d, v, 8, &blank);
        for (int k = 0; k < nf; k++) {
            FatEntry e;
            /* (from the disc, a disk with NovaOS on it is not what booted) */
            if (!g_live && FatLookupPath(v[k], "\\EFI\\NOVA\\kernel.elf", &e) && !e.dir) s->boot = true;
            if (FatLookupPath(v[k], "\\EFI\\Microsoft\\Boot\\bootmgfw.efi", &e) && !e.dir) s->windows = true;
            FatUnmount(v[k]);
        }
        char labels[48];
        int nv = PersistDiskLabels(d, labels, sizeof(labels), &blank);
        s->nova = strstr(labels, "NOVA_EFI") && strstr(labels, "NOVADATA");
        if (blank)             ksnprintf(s->contents, sizeof(s->contents), "Empty");
        else if (s->nova)      ksnprintf(s->contents, sizeof(s->contents), "NovaOS is installed on it");
        else if (nv)           ksnprintf(s->contents, sizeof(s->contents), "Volumes: %s", labels[0] ? labels : "no label");
        else                   ksnprintf(s->contents, sizeof(s->contents), "Unknown contents");
        if (l && !blank && DiskReadLayout(d, l) && l->style == DISK_GPT) {
            s->gpt = true;
            for (int k = 0; k < l->count; k++)
                if (!memcmp(l->part[k].type, DISK_TYPE_MSR, 16)) s->windows = true;
            map_disk(s, l, vols, nvols);
            s->can_beside = !s->boot && !s->holds_c && !s->nova &&
                            s->free_sectors * BLOCK_SECTOR >= SetupBesideBytes();
            if (!s->boot && !s->holds_c && !s->nova && !s->can_beside) shrink_plan(s, l);
            s->can_reinstall = s->nova && !s->boot;          /* (drive C: may be kept there: it stays) */
        }
    }
    kfree(vols);
    kfree(l);
    return n;
}

/* ---------------------------------------------------------------------------
 * The installation
 * ------------------------------------------------------------------------- */
static volatile SetupStatus g_status;
static BlockDev *g_target;
static bool      g_ntfs;                  /* drive C: on NTFS */
static SetupHow  g_how;
static bool      g_beside;                /* in the free space, keeping the disk's partitions */

void SetupGetStatus(SetupStatus *out)
{
    memcpy(out, (const void *)&g_status, sizeof(*out));
}

static void step(int percent, const char *text)
{
    g_status.percent = percent;
    strncpy((char *)g_status.step, text, sizeof(g_status.step) - 1);
    kprintf("[SETUP] %d%% %s\n", percent, text);
}

static void failed(const char *why)
{
    strncpy((char *)g_status.error, why, sizeof(g_status.error) - 1);
    kprintf("[SETUP] Failed: %s\n", why);
    g_status.state = SETUP_FAILED;
}

/* The installation files, in memory: from the bootloader, or read from
 * the FAT volume NovaOS booted from */
static UINT8 *g_kernel_copy, *g_loader_copy;

static bool load_media(const UINT8 **kernel, UINT32 *ksize, const UINT8 **loader, UINT32 *lsize)
{
    if (g_media_kernel) {
        *kernel = g_media_kernel; *ksize = (UINT32)g_media_kernel_size;
        *loader = g_media_loader; *lsize = (UINT32)g_media_loader_size;
        return true;
    }
    for (int i = 0; i < BlockCount(); i++) {
        FatVol *v[8];
        bool blank;
        int nv = PersistFindVolumes(BlockGet(i), v, 8, &blank);
        bool found = false;
        for (int k = 0; k < nv; k++) {
            FatEntry ke, le;
            if (!found && FatLookupPath(v[k], "\\EFI\\NOVA\\kernel.elf", &ke) && !ke.dir &&
                FatLookupPath(v[k], "\\EFI\\BOOT\\BOOTX64.EFI", &le) && !le.dir) {
                g_kernel_copy = kmalloc(ke.size + 1);
                g_loader_copy = kmalloc(le.size + 1);
                if (g_kernel_copy && g_loader_copy && FatRead(v[k], &ke, g_kernel_copy) && FatRead(v[k], &le, g_loader_copy)) {
                    *kernel = g_kernel_copy; *ksize = ke.size;
                    *loader = g_loader_copy; *lsize = le.size;
                    found = true;
                }
            }
            FatUnmount(v[k]);
        }
        if (found) return true;
    }
    return false;
}

/* CRC-32 (IEEE), as GPT uses */
static UINT32 crc32(const void *data, UINT32 len)
{
    const UINT8 *p = data;
    UINT32 c = 0xFFFFFFFFu;
    for (UINT32 i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static UINT64 g_rng;
static UINT64 rnd(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return g_rng;
}

/* A random (version 4) GUID in its on-disk byte order */
static void random_guid(UINT8 g[16])
{
    UINT64 a = rnd(), b = rnd();
    memcpy(g, &a, 8);
    memcpy(g + 8, &b, 8);
    g[7] = (UINT8)((g[7] & 0x0F) | 0x40);
    g[8] = (UINT8)((g[8] & 0x3F) | 0x80);
}

/* A GUID string's value in GPT (mixed-endian) byte order */
static void guid_bytes(UINT8 g[16], UINT32 d1, UINT16 d2, UINT16 d3, const UINT8 d4[8])
{
    memcpy(g, &d1, 4);
    memcpy(g + 4, &d2, 2);
    memcpy(g + 6, &d3, 2);
    memcpy(g + 8, d4, 8);
}

static void put_name(UINT8 *e, const char *name)
{
    for (int i = 0; name[i] && i < 36; i++) { e[56 + 2 * i] = (UINT8)name[i]; e[57 + 2 * i] = 0; }
}

#define GPT_ENTRIES   128
#define GPT_ESIZE     128
#define GPT_TABLE_SEC (GPT_ENTRIES * GPT_ESIZE / BLOCK_SECTOR)      /* 32 */

/* Reserve the installed payload and a complete staged update, the files
 * beside the kernel, plus FAT metadata/configuration. Match the
 * installation image's 32 MiB rounding. */
static UINT64 setup_esp_sectors(UINT32 kernel_size, UINT32 loader_size, UINT64 files_size)
{
    UINT64 payload_mb = ((UINT64)kernel_size + loader_size + 1048575) >> 20;
    UINT64 files_mb = (files_size + 1048575) >> 20;       /* \EFI\NOVA\Files: not staged twice */
    UINT64 mb = ((2 * payload_mb + files_mb + 32 + 31) / 32) * 32;
    if (mb < 128) mb = 128;
    return mb * 2048;
}

/* Validate geometry before detaching volumes or erasing any disk sectors. */
static bool setup_layout(UINT64 sectors, UINT64 esp_sectors,
                         UINT64 *esp_lba, UINT64 *data_lba, UINT64 *data_sectors)
{
    if (sectors < 4096 || esp_sectors > sectors - 2048) return false;
    *esp_lba = 2048;
    *data_lba = *esp_lba + esp_sectors;
    UINT64 end = (sectors - 1 - GPT_TABLE_SEC) & ~2047ull;
    if (end <= *data_lba || end - *data_lba <= 131072) return false;
    *data_sectors = end - *data_lba;
    if (!g_ntfs && *data_sectors > (1ull << 31)) *data_sectors = 1ull << 31;
    return true;
}

/* Write a protective MBR and primary + backup GPTs with the two partitions. */
static bool write_gpt(BlockDev *d, UINT64 *esp_lba, UINT64 *esp_sectors, UINT64 *data_lba, UINT64 *data_sectors,
                      UINT8 esp_id[16])
{
    UINT64 last = d->sectors - 1;
    UINT64 first_usable = 2 + GPT_TABLE_SEC, last_usable = last - 1 - GPT_TABLE_SEC;
    if (!setup_layout(d->sectors, *esp_sectors, esp_lba, data_lba, data_sectors)) return false;

    UINT8 *buf = kzalloc((2 + GPT_TABLE_SEC) * BLOCK_SECTOR);
    if (!buf) return false;
    /* protective MBR: one 0xEE partition covering the disk */
    UINT8 *mbr = buf;
    UINT8 *pe = mbr + 446;
    pe[1] = 0x00; pe[2] = 0x02; pe[3] = 0x00;
    pe[4] = 0xEE;
    pe[5] = 0xFF; pe[6] = 0xFF; pe[7] = 0xFF;
    UINT32 one = 1, cover = last > 0xFFFFFFFFull ? 0xFFFFFFFFu : (UINT32)last;
    memcpy(pe + 8, &one, 4);
    memcpy(pe + 12, &cover, 4);
    mbr[510] = 0x55; mbr[511] = 0xAA;

    /* the partition entries */
    UINT8 *ent = buf + 2 * BLOCK_SECTOR;
    static const UINT8 esp4[8]  = { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B };
    static const UINT8 data4[8] = { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 };
    UINT8 *e0 = ent, *e1 = ent + GPT_ESIZE;
    guid_bytes(e0, 0xC12A7328, 0xF81F, 0x11D2, esp4);            /* EFI System Partition */
    random_guid(e0 + 16);
    memcpy(esp_id, e0 + 16, 16);
    UINT64 a = *esp_lba, b = *esp_lba + *esp_sectors - 1;
    memcpy(e0 + 32, &a, 8);
    memcpy(e0 + 40, &b, 8);
    put_name(e0, "EFI System Partition");
    guid_bytes(e1, 0xEBD0A0A2, 0xB9E5, 0x4433, data4);           /* Basic data */
    random_guid(e1 + 16);
    a = *data_lba; b = *data_lba + *data_sectors - 1;
    memcpy(e1 + 32, &a, 8);
    memcpy(e1 + 40, &b, 8);
    put_name(e1, "NovaOS");
    UINT32 ecrc = crc32(ent, GPT_TABLE_SEC * BLOCK_SECTOR);

    /* the header (primary at LBA 1; the backup mirrors it at the end) */
    UINT8 disk_guid[16];
    random_guid(disk_guid);
    UINT8 *h = buf + BLOCK_SECTOR;
    UINT64 v64;
    UINT32 v32;
    memcpy(h, "EFI PART", 8);
    v32 = 0x00010000; memcpy(h + 8, &v32, 4);                     /* revision 1.0 */
    v32 = 92;         memcpy(h + 12, &v32, 4);                    /* header size */
    v64 = 1;          memcpy(h + 24, &v64, 8);                    /* this header */
    v64 = last;       memcpy(h + 32, &v64, 8);                    /* the other one */
    v64 = first_usable; memcpy(h + 40, &v64, 8);
    v64 = last_usable;  memcpy(h + 48, &v64, 8);
    memcpy(h + 56, disk_guid, 16);
    v64 = 2;          memcpy(h + 72, &v64, 8);                    /* entries at */
    v32 = GPT_ENTRIES; memcpy(h + 80, &v32, 4);
    v32 = GPT_ESIZE;   memcpy(h + 84, &v32, 4);
    memcpy(h + 88, &ecrc, 4);
    v32 = 0; memcpy(h + 16, &v32, 4);
    v32 = crc32(h, 92); memcpy(h + 16, &v32, 4);

    UINT8 *bh = kzalloc(BLOCK_SECTOR);
    if (!bh) { kfree(buf); return false; }
    memcpy(bh, h, 92);
    v64 = last;  memcpy(bh + 24, &v64, 8);
    v64 = 1;     memcpy(bh + 32, &v64, 8);
    v64 = last - GPT_TABLE_SEC; memcpy(bh + 72, &v64, 8);
    v32 = 0; memcpy(bh + 16, &v32, 4);
    v32 = crc32(bh, 92); memcpy(bh + 16, &v32, 4);

    bool ok = d->write(d, 0, 2 + GPT_TABLE_SEC, buf) &&
              d->write(d, last - GPT_TABLE_SEC, GPT_TABLE_SEC, ent) &&
              d->write(d, last, 1, bh);
    kfree(bh);
    kfree(buf);
    return ok;
}

UINT64 SetupBesideBytes(void)
{
    /* (installed, not live: the system's size as the disc's, about 256 MiB) */
    UINT64 k = g_media_kernel ? g_media_kernel_size : 256ull << 20, l = g_media_kernel ? g_media_loader_size : 1ull << 20;
    return (setup_esp_sectors((UINT32)k, (UINT32)l, BootFilesBytes()) + 512 * MIB_SECTORS) * BLOCK_SECTOR;
}

/* Beside the systems on @d: NovaOS's ESP and data partition in its largest
 * free space, added to its GPT in the first unused entries; every
 * partition already there keeps its entry, place and GUIDs.  Nothing is
 * written here: *@l is the new table. */
static bool beside_layout(BlockDev *d, DiskLayout *l, UINT64 esp_sectors,
                          UINT64 *esp_lba, UINT64 *data_lba, UINT64 *data_sectors)
{
    SetupDisk s;
    memset(&s, 0, sizeof(s));
    s.dev = d;
    if (!DiskReadLayout(d, l) || l->style != DISK_GPT) return false;
    map_disk(&s, l, NULL, 0);
    if (s.free_sectors < esp_sectors + 512 * MIB_SECTORS) return false;    /* (drive C: gets 512 MiB at least) */
    *esp_lba = s.free_lba;
    *data_lba = s.free_lba + esp_sectors;
    *data_sectors = s.free_lba + s.free_sectors - *data_lba;
    if (!g_ntfs && *data_sectors > (1ull << 31)) *data_sectors = 1ull << 31;
    UINT32 nums[2] = { 0, 0 };
    for (UINT32 num = 1, k = 0; num <= l->max && k < 2; num++) {
        bool used = false;
        for (int i = 0; i < l->count && !used; i++) used = l->part[i].number == num;
        if (!used) nums[k++] = num;
    }
    if (!nums[1] || l->count + 2 > DISK_MAX_PARTS) return false;
    const UINT8 *types[2] = { DISK_TYPE_ESP, DISK_TYPE_BASIC };
    const char *names[2] = { "EFI System Partition", "NovaOS" };
    UINT64 at[2] = { *esp_lba, *data_lba }, len[2] = { esp_sectors, *data_sectors };
    for (int k = 0; k < 2; k++) {
        DiskPart *p = &l->part[l->count++];
        memset(p, 0, sizeof(*p));
        p->lba = at[k];
        p->sectors = len[k];
        p->number = nums[k];
        memcpy(p->type, types[k], 16);
        random_guid(p->id);
        for (int i = 0; names[k][i]; i++) p->name[i] = (UINT16)names[k][i];
    }
    return DiskCheckLayout(d, l);
}

/* Zero the start of each partition's area and the old partition tables so
 * no stale volume or GPT is found there */
static bool wipe(BlockDev *d, UINT64 esp_lba, UINT64 data_lba)
{
    UINT8 *z = kzalloc(64 * BLOCK_SECTOR);
    if (!z) return false;
    bool ok = d->write(d, 0, 64, z) &&
              d->write(d, d->sectors - 64, 64, z) &&
              d->write(d, esp_lba, 64, z) &&
              d->write(d, data_lba, 64, z);
    kfree(z);
    return ok;
}

/* The files beside the kernel (fs/bootfiles.h), from drive C:, to
 * \EFI\NOVA\Files on the new ESP */
static bool copy_files(FatVol *esp)
{
    for (int i = 0; i < BootFilesCount(); i++) {
        const char *path = BootFilesPath(i);
        char dir[RAMFS_PATH_MAX + 16];
        ksnprintf(dir, sizeof(dir), "%s%s", BOOT_FILES_DIR, path);
        char *slash = strrchr(dir, '\\');
        if (!slash) continue;
        *slash = '\0';
        FsLock();
        RamNode *f = RamfsResolve(NULL, path);
        char *data = NULL;
        UINT32 len = 0;
        if (f && !f->dir && RamfsLoad(f) && (f->data || !f->size)) {
            len = f->size;
            data = kmalloc(len ? len : 1);
            if (data && len) memcpy(data, f->data, len);
        }
        FsUnlock();
        if (!data) continue;                    /* (deleted since boot: not copied) */
        UINT32 d;
        bool ok = FatMkdirPath(esp, dir, &d) && FatWriteFile(esp, d, slash + 1, data, len);
        kfree(data);
        if (!ok) return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * The firmware's boot entry.  As Windows Setup adds "Windows Boot Manager",
 * Setup adds "NovaOS" for \EFI\BOOT\BOOTX64.EFI on the new ESP and puts it
 * first in BootOrder, keeping every other entry ("Windows Boot Manager"
 * second), so the PC starts NovaOS, whose boot menu offers Windows.  The
 * path is the short form UEFI firmware expands itself (UEFI 2.10, 3.1.2):
 * the partition by its GPT entry, place and unique GUID, then the file.
 * Not fatal: without it the boot loader adds the entry on NovaOS's first
 * start, once it is started from the firmware's boot menu.
 * ------------------------------------------------------------------------- */
#define LOAD_OPTION_ACTIVE 1u

static void var16(UINT16 *out, const char *s)
{
    while ((*out++ = (UINT8)*s++)) {}
}

static UINT32 boot_option(UINT8 *o, UINT32 num, UINT64 lba, UINT64 sectors, const UINT8 id[16])
{
    static const char desc[] = "NovaOS", file[] = "\\EFI\\BOOT\\BOOTX64.EFI";
    UINT32 n = 6;
    UINT32 attrs = LOAD_OPTION_ACTIVE;
    memcpy(o, &attrs, 4);
    for (int i = 0; i <= (int)sizeof(desc) - 1; i++, n += 2) { o[n] = (UINT8)desc[i]; o[n + 1] = 0; }
    UINT32 path = n;
    UINT8 *hd = o + n;                                  /* HARDDRIVE_DEVICE_PATH */
    hd[0] = 4; hd[1] = 1; hd[2] = 42; hd[3] = 0;
    memcpy(hd + 4, &num, 4);
    memcpy(hd + 8, &lba, 8);
    memcpy(hd + 16, &sectors, 8);
    memcpy(hd + 24, id, 16);
    hd[40] = 2;                                         /* GPT */
    hd[41] = 2;                                         /* a GUID signature */
    n += 42;
    UINT8 *fp = o + n;                                  /* FILEPATH_DEVICE_PATH */
    UINT16 flen = (UINT16)(4 + 2 * sizeof(file));
    fp[0] = 4; fp[1] = 4; memcpy(fp + 2, &flen, 2);
    for (int i = 0; i < (int)sizeof(file); i++) { fp[4 + 2 * i] = (UINT8)file[i]; fp[5 + 2 * i] = 0; }
    n += flen;
    o[n] = 0x7F; o[n + 1] = 0xFF; o[n + 2] = 4; o[n + 3] = 0;   /* the end */
    n += 4;
    UINT16 plen = (UINT16)(n - path);
    memcpy(o + 4, &plen, 2);
    return n;
}

/* An entry's HARDDRIVE node, or NULL */
static const UINT8 *option_hd(const UINT8 *o, UINT64 len)
{
    if (len < 6) return NULL;
    UINT16 plen;
    memcpy(&plen, o + 4, 2);
    UINT64 at = 6;
    while (at + 1 < len && (o[at] | o[at + 1])) at += 2;      /* past the description */
    at += 2;
    for (UINT64 end = at + plen; at + 4 <= end && end <= len; ) {
        UINT16 nl;
        memcpy(&nl, o + at + 2, 2);
        if (nl < 4 || o[at] == 0x7F) break;
        if (o[at] == 4 && o[at + 1] == 1 && nl == 42 && at + 42 <= end) return o + at;
        at += nl;
    }
    return NULL;
}

static void add_boot_entry(BlockDev *d, UINT32 num, UINT64 lba, UINT64 sectors, const UINT8 id[16])
{
    if (!EfiRuntimeAvailable()) {
        kprintf("[SETUP] No firmware variable services: NovaOS adds its boot entry on its first start\n");
        return;
    }
    UINT8 *opt = kzalloc(256), *cur = kmalloc(1024);
    UINT16 *order = kmalloc(512 * sizeof(UINT16));
    if (!opt || !cur || !order) { kfree(opt); kfree(cur); kfree(order); return; }
    UINT32 len = boot_option(opt, num, lba, sectors, id);

    /* The entry for this partition already there (a reinstall), else the
     * first free Boot#### */
    UINT16 name[16];
    int use = -1, free_num = -1;
    for (int n = 0; n < 0x100 && use < 0; n++) {
        char a[16];
        ksnprintf(a, sizeof(a), "Boot%04X", n);
        var16(name, a);
        UINT64 sz = 1024;
        UINT64 st = EfiGetVariable(name, EFI_GLOBAL_GUID, NULL, &sz, cur);
        if (st == EFI_NOT_FOUND_ERR) { if (free_num < 0) free_num = n; continue; }
        const UINT8 *hd = st == EFI_OK ? option_hd(cur, sz) : NULL;
        if (hd && !memcmp(hd + 24, id, 16)) use = n;
    }
    if (use < 0) use = free_num;
    char a[16], entry[16];
    ksnprintf(entry, sizeof(entry), "Boot%04X", use);
    var16(name, entry);
    UINT64 st = use < 0 ? EFI_ERR(9) : EfiSetVariable(name, EFI_GLOBAL_GUID, EFI_VAR_NV_BS_RT, len, opt);
    if (st != EFI_OK) {
        kprintf("[SETUP] Could not add the firmware boot entry (EFI status %llx): NovaOS adds it on its first start\n",
                (unsigned long long)st);
        goto out;
    }

    /* First in BootOrder, every other entry kept after it */
    UINT16 bo[16];
    var16(bo, "BootOrder");
    UINT64 osz = 511 * sizeof(UINT16);
    if (EfiGetVariable(bo, EFI_GLOBAL_GUID, NULL, &osz, order + 1) != EFI_OK) osz = 0;
    UINT32 k = 1;
    for (UINT32 i = 1; i <= osz / 2; i++)
        if (order[i] != use) order[k++] = order[i];
    order[0] = (UINT16)use;
    st = EfiSetVariable(bo, EFI_GLOBAL_GUID, EFI_VAR_NV_BS_RT, k * sizeof(UINT16), order);

    /* Read it back, and say what comes next (Windows' entry, on a PC) */
    UINT64 back = 511 * sizeof(UINT16);
    bool first = st == EFI_OK && EfiGetVariable(bo, EFI_GLOBAL_GUID, NULL, &back, order) == EFI_OK &&
                 back >= 2 && order[0] == use;
    char next[64] = "";
    if (first && back >= 4) {
        ksnprintf(a, sizeof(a), "Boot%04X", order[1]);
        var16(name, a);
        UINT64 sz = 1024;
        if (EfiGetVariable(name, EFI_GLOBAL_GUID, NULL, &sz, cur) == EFI_OK && sz > 8) {
            int j = 0;
            for (UINT64 i = 6; i + 1 < sz && (cur[i] | cur[i + 1]) && j < 40; i += 2)
                next[j++] = cur[i + 1] || cur[i] < 0x20 ? '?' : (char)cur[i];
            next[j] = 0;
        }
    }
    if (first)
        kprintf("[SETUP] Added the firmware boot entry %s \"NovaOS\" for %s, first in BootOrder%s%s%s\n",
                entry, d->name, next[0] ? " (then \"" : "", next, next[0] ? "\")" : "");
    else
        kprintf("[SETUP] Added the firmware boot entry %s \"NovaOS\" for %s, but could not put it first in BootOrder\n",
                entry, d->name);
out:
    kfree(opt);
    kfree(cur);
    kfree(order);
}

/* Shrink the NTFS volume shrink_plan picks, then its partition, so the
 * free space after it takes NovaOS (SETUP_SHRINK).  Windows' volumes on
 * the disk are detached first.  Nothing but that volume and its GPT entry
 * is written. */
static bool make_room(BlockDev *d)
{
    SetupDisk s;
    memset(&s, 0, sizeof(s));
    s.dev = d;
    DiskLayout *l = kmalloc(sizeof(DiskLayout));
    if (!l || !DiskReadLayout(d, l) || l->style != DISK_GPT) {
        kfree(l);
        failed("The disk's partition table could not be read.");
        return false;
    }
    shrink_plan(&s, l);
    if (!s.can_shrink) {
        kfree(l);
        failed(s.shrink_why[0] ? s.shrink_why : "No volume on the disk can be shrunk to make room for NovaOS.");
        return false;
    }
    step(4, "Making room: shrinking Windows' volume (files past its new end are moved)");
    NtfsVol *v = NtfsMount(d, s.shrink_lba);
    const char *why = "it could not be opened";
    bool ok = v && NtfsEnableWrite(v);
    if (v && !ok) why = "Windows did not leave it closed cleanly (turn off Fast Startup, shut down fully)";
    ok = ok && NtfsShrink(v, s.shrink_to * BLOCK_SECTOR, &why);
    if (v) NtfsUnmount(v);
    if (ok) {
        for (int k = 0; k < l->count; k++)
            if (l->part[k].lba == s.shrink_lba) l->part[k].sectors = s.shrink_to;
        ok = DiskWriteLayout(d, l);
        if (!ok) why = "the partition table could not be written";
        if (d->flush) d->flush(d);
    }
    kfree(l);
    if (!ok) {
        char msg[128];
        ksnprintf(msg, sizeof(msg), "Windows' volume could not be shrunk: %s.", why);
        failed(msg);
        return false;
    }
    kprintf("[SETUP] Shrank the NTFS volume at %u MiB on %s from %u MiB to %u MiB to make room for NovaOS\n",
            (unsigned)(s.shrink_lba >> 11), d->name, (unsigned)(s.shrink_from >> 11), (unsigned)(s.shrink_to >> 11));
    return true;
}

/* A label of the volume at @lba ("" if none is recognised) */
static void volume_label(BlockDev *d, UINT64 lba, char *out, int cap)
{
    out[0] = 0;
    FatVol *f = FatMount(d, lba);
    if (f) { ksnprintf(out, (size_t)cap, "%s", FatLabel(f)); FatUnmount(f); return; }
    NtfsVol *n = NtfsMount(d, lba);
    if (n) { ksnprintf(out, (size_t)cap, "%s", NtfsLabel(n)); NtfsUnmount(n); }
}

/* Reinstalling (SETUP_REINSTALL): NovaOS's ESP (NOVA_EFI) and data
 * partition (NOVADATA) on @d; nothing is written */
static bool reinstall_layout(BlockDev *d, UINT64 *esp_lba, UINT64 *esp_sec, UINT32 *esp_num, UINT8 esp_id[16],
                             UINT64 *data_lba, UINT64 *data_sec)
{
    DiskLayout *l = kmalloc(sizeof(DiskLayout));
    bool esp = false, data = false;
    if (l && DiskReadLayout(d, l) && l->style == DISK_GPT)
        for (int i = 0; i < l->count; i++) {
            const DiskPart *p = &l->part[i];
            char label[40];
            volume_label(d, p->lba, label, sizeof(label));
            if (!esp && DiskIsEsp(l, p) && !strcmp(label, "NOVA_EFI")) {
                *esp_lba = p->lba; *esp_sec = p->sectors; *esp_num = p->number;
                memcpy(esp_id, p->id, 16);
                esp = true;
            } else if (!data && !strcmp(label, "NOVADATA")) {
                *data_lba = p->lba; *data_sec = p->sectors;
                data = true;
            }
        }
    kfree(l);
    return esp && data;
}

static void setup_thread(void *arg)
{
    (void)arg;
    BlockDev *d = g_target;
    RtcTime t;
    rtc_read(&t);
    g_rng = rdtsc() ^ ((UINT64)t.second << 40) ^ ((UINT64)t.minute << 48) ^ 0x9E3779B97F4A7C15ull;
    if (!g_rng) g_rng = 1;

    step(2, "Getting the installation files ready");
    const UINT8 *kernel, *loader;
    UINT32 ksize, lsize;
    if (!load_media(&kernel, &ksize, &loader, &lsize)) {
        failed("The installation files were not found (boot from the NovaOS disc).");
        return;
    }

    UINT64 esp_lba, esp_sec = setup_esp_sectors(ksize, lsize, BootFilesBytes()), data_lba, data_sec;
    UINT8 esp_id[16];
    UINT32 esp_num = 1;                                          /* (the ESP's GPT entry, for its boot entry) */
    DiskLayout *layout = NULL;
    bool keep = g_how == SETUP_REINSTALL;                        /* (drive C:'s partition as it is) */
    if (g_how == SETUP_SHRINK) {
        DrivesDetach(d);
        if (!make_room(d)) { DrivesAttach(d); return; }
    }
    if (keep) {
        UINT64 need = esp_sec;
        if (!reinstall_layout(d, &esp_lba, &esp_sec, &esp_num, esp_id, &data_lba, &data_sec)) {
            failed("NovaOS's partitions (NOVA_EFI and NOVADATA) were not found on the disk.");
            return;
        }
        if (esp_sec < need) {
            failed("NovaOS's EFI System Partition on the disk is too small for this version: erase the disk instead.");
            return;
        }
        kprintf("[SETUP] Reinstalling NovaOS on %s: EFI System Partition at %u MiB (%u MiB) formatted again, "
                "NOVADATA at %u MiB (%u MiB) kept\n", d->name, (unsigned)(esp_lba >> 11), (unsigned)(esp_sec >> 11),
                (unsigned)(data_lba >> 11), (unsigned)(data_sec >> 11));
    } else if (g_beside) {
        layout = kmalloc(sizeof(DiskLayout));
        if (!layout || !beside_layout(d, layout, esp_sec, &esp_lba, &data_lba, &data_sec)) {
            kfree(layout);
            if (g_how == SETUP_SHRINK) DrivesAttach(d);
            failed("There is not enough free space on the disk for NovaOS beside what is on it.");
            return;
        }
        esp_num = layout->part[layout->count - 2].number;
        memcpy(esp_id, layout->part[layout->count - 2].id, 16);
        kprintf("[SETUP] Beside the %d partition%s on %s: EFI System Partition at %u MiB (%u MiB), "
                "NovaOS at %u MiB (%u MiB)\n", layout->count - 2, layout->count == 3 ? "" : "s", d->name,
                (unsigned)(esp_lba >> 11), (unsigned)(esp_sec >> 11), (unsigned)(data_lba >> 11), (unsigned)(data_sec >> 11));
    } else if (!setup_layout(d->sectors, esp_sec, &esp_lba, &data_lba, &data_sec)) {
        failed("The disk is too small for NovaOS and a staged update.");
        return;
    }

    /* C: may be saved on this very disk: stop that before erasing it */
    bool had_c = PersistActive(), c_here = PersistDevice() == d && !keep;
    if (c_here && PersistDirect()) {                             /* (the running system's own C:, docs/design/disk-c.md) */
        kfree(layout);
        failed("NovaOS is running from this disk. Start from the installation media to install on it again.");
        return;
    }
    if (c_here) {
        step(6, "Saving this session's files for the move");
        PersistDetach();
    }

    DrivesDetach(d);                                             /* (its volumes are not D:, E:, ... any more) */
    step(10, keep ? "Keeping the disk's partitions as they are" : "Creating partitions");
    bool parted;
    if (keep)                         /* (the partitions stay as they are) */
        parted = true;
    else if (layout) {                /* (only the new partitions' first sectors are cleared) */
        UINT8 *z = kzalloc(64 * BLOCK_SECTOR);
        parted = z && d->write(d, esp_lba, 64, z) && d->write(d, data_lba, 64, z) && DiskWriteLayout(d, layout);
        kfree(z);
        kfree(layout);
    } else
        parted = wipe(d, esp_lba, data_lba) && write_gpt(d, &esp_lba, &esp_sec, &data_lba, &data_sec, esp_id);
    if (!parted) {
        if (g_beside || keep) DrivesAttach(d);
        failed("Could not write the partition table (a disk error, or the disk is too small).");
        return;
    }
    if (d->flush) d->flush(d);

    step(18, keep ? "Formatting NovaOS's EFI System Partition again" : "Formatting the EFI System Partition");
    FatVol *esp = FatFormat(d, esp_lba, esp_sec, "NOVA_EFI");
    if (!esp) { failed("Could not format the EFI System Partition."); return; }
    UINT32 dir;
    step(26, "Copying the boot loader");
    if (!FatMkdirPath(esp, "\\EFI\\BOOT", &dir) ||
        !FatWriteFile(esp, dir, "BOOTX64.EFI", loader, lsize)) {
        FatUnmount(esp);
        failed("Could not copy the boot loader.");
        return;
    }
    step(34, "Copying NovaOS (the kernel and its programs)");
    if (!FatMkdirPath(esp, "\\EFI\\NOVA", &dir) ||
        !FatWriteFile(esp, dir, "kernel.elf", kernel, ksize)) {
        FatUnmount(esp);
        failed("Could not copy the NovaOS system files (is the disk big enough?).");
        return;
    }
    step(52, "Copying the wallpapers, icons and pictures");
    if (!copy_files(esp)) {
        FatUnmount(esp);
        failed("Could not copy the wallpapers, icons and pictures (is the disk big enough?).");
        return;
    }
    step(70, "Checking the copied files");
    FatEntry e;
    bool good = FatSync(esp) && FatLookupPath(esp, "\\EFI\\NOVA\\kernel.elf", &e) && e.size == ksize;
    if (good) {
        /* read the kernel back and compare */
        UINT8 *back = kmalloc(ksize + 1);
        good = back && FatRead(esp, &e, back) && !memcmp(back, kernel, ksize);
        kfree(back);
    }
    FatUnmount(esp);
    if (!good) { failed("The copied system files did not read back correctly."); return; }

    if (keep) {
        step(80, "Keeping drive C: (NOVADATA) and your files as they are");
        g_status.kept_c = true;
    } else if (g_ntfs) {
        step(80, "Formatting the data partition for drive C: (NTFS)");
        if (!NtfsFormat(d, data_lba, data_sec, "NOVADATA", rnd())) { failed("Could not format the data partition."); return; }
    } else {
        step(80, "Formatting the data partition for drive C: (FAT32)");
        FatVol *data = FatFormat(d, data_lba, data_sec, "NOVADATA");
        if (!data) { failed("Could not format the data partition."); return; }
        UINT32 nova_dir;
        FatMkdirPath(data, "\\NOVA\\C", &nova_dir);
        FatSync(data);
        FatUnmount(data);
    }

    /* This session's files go to the new disk when C: has nowhere else to
     * be saved; a C: kept on another disk stays there */
    if (!keep && (c_here || !had_c)) {
        step(88, "Copying your files to the new disk");
        g_status.moved_c = PersistAdopt(d, data_lba);
    }
    if (d->flush) d->flush(d);
    if (g_kernel_copy) { kfree(g_kernel_copy); g_kernel_copy = NULL; }
    if (g_loader_copy) { kfree(g_loader_copy); g_loader_copy = NULL; }
    if (g_beside || keep) DrivesAttach(d);                       /* (the other systems' volumes again) */
    step(95, "Adding NovaOS to the firmware's boot menu");
    add_boot_entry(d, esp_num, esp_lba, esp_sec, esp_id);
    step(100, "NovaOS is installed");
    g_status.state = SETUP_DONE;
}

bool SetupStart(BlockDev *dev, bool ntfs, SetupHow how)
{
    static const char *const what[] = { "on", "beside the other systems on", "beside the other systems, making room, on",
                                        "again, keeping drive C:, on" };
    if (g_status.state == SETUP_RUNNING || !dev) return false;
    memset((void *)&g_status, 0, sizeof(g_status));
    g_status.state = SETUP_RUNNING;
    g_target = dev;
    g_ntfs = ntfs;
    g_how = how;
    g_beside = how == SETUP_BESIDE || how == SETUP_SHRINK;
    kprintf("[SETUP] Installing NovaOS %s %s (%s, %u MiB), drive C: on %s\n", what[how],
            dev->name, dev->model, (unsigned)(dev->sectors >> 11), how == SETUP_REINSTALL ? "NOVADATA, kept" : ntfs ? "NTFS" : "FAT32");
    if (!sched_create_thread("setup", setup_thread, NULL, PRIO_SERVICE)) {
        g_status.state = SETUP_FAILED;
        strncpy((char *)g_status.error, "Could not start the installer.", sizeof(g_status.error) - 1);
        return false;
    }
    return true;
}
