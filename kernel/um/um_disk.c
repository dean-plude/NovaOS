/*
 * um_disk.c — disk and volume handles: \\.\PhysicalDriveN, \\.\X:,
 * \\?\Volume{GUID}, \Device\HarddiskVolumeN
 *
 * What Disk Management, diskpart and the storage parts of other programs
 * open with CreateFile and ask with DeviceIoControl, answered from the
 * disks' partition tables and volumes (fs/drives.h, fs/partition.h):
 *
 *   a disk:    IOCTL_STORAGE_QUERY_PROPERTY (StorageDeviceProperty),
 *              IOCTL_STORAGE_GET_DEVICE_NUMBER, IOCTL_DISK_GET_DRIVE_GEOMETRY(_EX),
 *              IOCTL_DISK_GET_LENGTH_INFO, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
 *              IOCTL_DISK_SET_DRIVE_LAYOUT_EX, IOCTL_DISK_CREATE_DISK,
 *              IOCTL_DISK_UPDATE_PROPERTIES, IOCTL_DISK_IS_WRITABLE,
 *              IOCTL_STORAGE_CHECK_VERIFY
 *   a volume:  IOCTL_STORAGE_GET_DEVICE_NUMBER, IOCTL_DISK_GET_PARTITION_INFO(_EX),
 *              IOCTL_DISK_GET_LENGTH_INFO, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
 *              IOCTL_MOUNTDEV_QUERY_DEVICE_NAME, NtQueryVolumeInformationFile
 *              (its label, file system and size, with or without a letter),
 *              and NovaOS's own IOCTL_NOVA_VOLUME_INFO (its GUID, number and
 *              letter: kernel32's volume functions), IOCTL_NOVA_VOLUME_FORMAT (fmifs.dll's
 *              FormatEx) and IOCTL_NOVA_VOLUME_SET_LETTER (SetVolumeMountPoint,
 *              DeleteVolumeMountPoint)
 *
 * The structures are Windows' (winioctl.h), the same for 32- and 64-bit
 * programs.  Reading and writing sectors through these handles is not
 * offered.
 */

#include "um_internal.h"
#include "../fs/drives.h"
#include "../fs/partition.h"
#include "../ke/probe.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

#define ST_SUCCESS               0x00000000u
#define ST_BUFFER_OVERFLOW       0x80000005u
#define ST_DEVICE_BUSY           0x80000011u
#define ST_INFO_LENGTH_MISMATCH  0xC0000004u
#define ST_INVALID_PARAMETER     0xC000000Du
#define ST_NO_SUCH_DEVICE        0xC000000Eu
#define ST_INVALID_DEVICE_REQUEST 0xC0000010u
#define ST_NO_MEMORY             0xC0000017u
#define ST_ACCESS_DENIED         0xC0000022u
#define ST_BUFFER_TOO_SMALL      0xC0000023u
#define ST_OBJECT_NAME_NOT_FOUND 0xC0000034u
#define ST_OBJECT_NAME_COLLISION 0xC0000035u
#define ST_INVALID_INFO_CLASS    0xC0000003u
#define ST_UNRECOGNIZED_VOLUME   0xC000014Fu
#define ST_IO_DEVICE_ERROR       0xC0000185u
#define ST_NOT_SUPPORTED         0xC00000BBu
#define ST_MEDIA_WRITE_PROTECTED 0xC00000A2u

#define IOCTL_DISK_GET_DRIVE_GEOMETRY        0x00070000u
#define IOCTL_DISK_GET_PARTITION_INFO        0x00074004u
#define IOCTL_DISK_IS_WRITABLE               0x00070024u
#define IOCTL_DISK_GET_PARTITION_INFO_EX     0x00070048u
#define IOCTL_DISK_GET_DRIVE_LAYOUT_EX       0x00070050u
#define IOCTL_DISK_SET_DRIVE_LAYOUT_EX       0x0007C054u
#define IOCTL_DISK_CREATE_DISK               0x0007C058u
#define IOCTL_DISK_GET_LENGTH_INFO           0x0007405Cu
#define IOCTL_DISK_GET_DRIVE_GEOMETRY_EX     0x000700A0u
#define IOCTL_DISK_UPDATE_PROPERTIES         0x00070140u
#define IOCTL_STORAGE_CHECK_VERIFY           0x002D4800u
#define IOCTL_STORAGE_GET_DEVICE_NUMBER      0x002D1080u
#define IOCTL_STORAGE_QUERY_PROPERTY         0x002D1400u
#define IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS 0x00560000u
#define IOCTL_MOUNTDEV_QUERY_DEVICE_NAME     0x004D0008u
#define IOCTL_NOVA_VOLUME_INFO               0x8E562000u
#define IOCTL_NOVA_VOLUME_FORMAT             0x8E56A004u
#define IOCTL_NOVA_VOLUME_SET_LETTER         0x8E56A008u

#define PIE_SIZE   144u                    /* PARTITION_INFORMATION_EX */
#define DLI_HEADER 48u                     /* DRIVE_LAYOUT_INFORMATION_EX up to PartitionEntry */

typedef struct {
    UmObject  ob;
    bool      volume;
    BlockDev *dev;                         /* a disk's */
    UINT8     guid[16];                    /* a volume's */
    bool      write;                       /* opened for writing */
} DiskHandle;

static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static bool iprefix(const char *s, const char *p)
{
    for (; *p; s++, p++) if (lc(*s) != lc(*p)) return false;
    return true;
}

static int hexval(char c)
{
    c = lc(c);
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" at @s into @g; the characters used, or 0 */
static int parse_guid(const char *s, UINT8 g[16])
{
    static const int at[16] = { 7, 5, 3, 1, 12, 10, 17, 15, 20, 22, 25, 27, 29, 31, 33, 35 };
    if (s[0] != '{' || s[9] != '-' || s[14] != '-' || s[19] != '-' || s[24] != '-' || s[37] != '}') return 0;
    for (int i = 0; i < 16; i++) {
        int hi = hexval(s[at[i]]), lo = hexval(s[at[i] + 1]);
        if (hi < 0 || lo < 0) return 0;
        g[i] = (UINT8)(hi << 4 | lo);
    }
    return 38;
}

static bool number_after(const char *s, UINT32 *n)
{
    if (!*s) return false;
    UINT32 v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > 100000) return false;
        v = v * 10 + (UINT32)(*s - '0');
    }
    *n = v;
    return true;
}

/* What @path (NT prefix removed) opens: 1 a disk (*@disk), 2 a volume
 * (*@guid), 0 neither */
static int parse(const char *path, int *disk, UINT8 guid[16])
{
    UINT32 n;
    if (iprefix(path, "PhysicalDrive") && number_after(path + 13, &n)) { *disk = (int)n; return 1; }
    if (iprefix(path, "\\Device\\HarddiskVolume") && number_after(path + 22, &n))
        return DrivesGuidOfNumber(n, guid) ? 2 : -1;
    if (iprefix(path, "Volume{")) {
        int k = parse_guid(path + 6, guid);
        if (!k || (path[6 + k] && !(path[6 + k] == '\\' && !path[7 + k]))) return 0;
        return 2;
    }
    return 0;
}

bool um_disk_name(const char *path)
{
    int disk;
    UINT8 g[16];
    return parse(path, &disk, g) != 0;
}

/* get_path's names that are volumes: "\??\X:" (a prefix, then just a
 * drive) is the volume \Device\HarddiskVolumeN; "Volume{GUID}\path" is
 * the path on the volume's drive.  @out is rewritten in place. */
UINT32 um_disk_path(char *out, int cap, bool prefixed)
{
    if (prefixed && ((out[0] | 0x20) >= 'a' && (out[0] | 0x20) <= 'z') && out[1] == ':' && !out[2]) {
        UINT32 n = DrivesNumberOfLetter(out[0]);
        if (!n) return 0xC000003Au;                                          /* OBJECT_PATH_NOT_FOUND */
        ksnprintf(out, (size_t)cap, "\\Device\\HarddiskVolume%u", n);
        return ST_SUCCESS;
    }
    if (!iprefix(out, "Volume{")) return ST_SUCCESS;
    UINT8 g[16];
    int k = parse_guid(out + 6, g);
    if (!k || out[6 + k] != '\\' || !out[7 + k]) return ST_SUCCESS;          /* (the volume itself: um_disk_open) */
    bool known;
    char letter = DrivesLetterOfGuid(g, &known);
    if (!known) return ST_OBJECT_NAME_NOT_FOUND;
    if (!letter) return 0xC000003Au;                                         /* (no drive: its files are not reachable) */
    char *rest = out + 6 + k;                                                /* "\..." */
    int rl = (int)strlen(rest);
    if (rl + 3 > cap) return 0xC0000033u;                                    /* OBJECT_NAME_INVALID */
    memmove(out + 2, rest, (size_t)rl + 1);
    out[0] = letter;
    out[1] = ':';
    return ST_SUCCESS;
}

UINT32 um_disk_open(const char *path, UINT32 access, UmObject **out)
{
    int disk = -1;
    UINT8 g[16];
    int kind = parse(path, &disk, g);
    if (kind <= 0) return ST_OBJECT_NAME_NOT_FOUND;
    BlockDev *d = NULL;
    bool known = true;
    if (kind == 1 && !(d = BlockGet(disk))) return ST_OBJECT_NAME_NOT_FOUND;
    if (kind == 2) DrivesLetterOfGuid(g, &known);
    if (!known) return ST_OBJECT_NAME_NOT_FOUND;
    DiskHandle *h = kzalloc(sizeof(*h));
    if (!h) return ST_NO_MEMORY;
    h->ob.type = UO_DISK;
    h->ob.refs = 1;
    h->ob.signaled = true;                       /* (waiting on it returns at once, as on a file) */
    h->ob.free_unlocked = true;
    h->volume = kind == 2;
    h->dev = d;
    memcpy(h->guid, g, 16);
    h->write = access & (0x40000000u | 0x10000000u | 0x0002u | 0x0004u | 0x02000000u);   /* GENERIC_WRITE/ALL, FILE_WRITE/APPEND_DATA, MAXIMUM_ALLOWED */
    *out = &h->ob;
    return ST_SUCCESS;
}

/* ---- the answers ---- */

typedef struct { UINT8 *b; UINT32 cap, len; } Out;

static void put(Out *o, UINT32 at, const void *v, UINT32 n)
{
    if (at + n <= o->cap) memcpy(o->b + at, v, n);
    if (at + n > o->len) o->len = at + n;
}
static void put32(Out *o, UINT32 at, UINT32 v) { put(o, at, &v, 4); }
static void put64(Out *o, UINT32 at, UINT64 v) { put(o, at, &v, 8); }

static UINT32 bus_type(BlockDev *d)
{
    if (!strncmp(d->name, "sata", 4)) return 0x0B;          /* BusTypeSata */
    if (!strncmp(d->name, "nvme", 4)) return 0x11;          /* BusTypeNvme */
    if (!strncmp(d->name, "usb", 3)) return 7;              /* BusTypeUsb */
    if (!strncmp(d->name, "stor", 4)) return 1;             /* BusTypeScsi (a Storport miniport's) */
    return 0;
}

/* PARTITION_INFORMATION_EX of @p (on a disk with layout @l) at @at */
static void put_partition(Out *o, UINT32 at, const DiskLayout *l, const DiskPart *p)
{
    put32(o, at, (UINT32)l->style);
    put64(o, at + 8, p->lba * BLOCK_SECTOR);
    put64(o, at + 16, p->sectors * BLOCK_SECTOR);
    put32(o, at + 24, p->number);
    UINT8 svc = l->style == DISK_GPT && (!memcmp(p->type, DISK_TYPE_RECOVERY, 16)) ? 1 : 0;
    put(o, at + 29, &svc, 1);
    if (l->style == DISK_GPT) {
        put(o, at + 32, p->type, 16);
        put(o, at + 48, p->id, 16);
        put64(o, at + 64, p->attrs);
        put(o, at + 72, p->name, 72);
    } else {
        UINT8 t = p->mbr_type, boot = p->active, rec = t == 0x07 || t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B ||
                                                      t == 0x0C || t == 0x0E;
        put(o, at + 32, &t, 1);
        put(o, at + 33, &boot, 1);
        put(o, at + 34, &rec, 1);
        put32(o, at + 36, (UINT32)p->lba);                   /* HiddenSectors */
    }
}

/* The partition volume @v is, in its disk's layout (read into *@l) */
static const DiskPart *volume_part(const DriveVolume *v, DiskLayout *l)
{
    if (!v->dev || !DiskReadLayout(v->dev, l)) return NULL;
    for (int i = 0; i < l->count; i++)
        if (l->part[i].lba == v->lba && l->part[i].sectors == v->sectors) return &l->part[i];
    return NULL;
}

static UINT32 status_of(int r)
{
    return r == DRIVES_OK ? ST_SUCCESS : r == DRIVES_NOT_FOUND ? ST_NO_SUCH_DEVICE : r == DRIVES_IN_USE ? ST_ACCESS_DENIED :
           r == DRIVES_LETTER_TAKEN ? ST_OBJECT_NAME_COLLISION : r == DRIVES_INVALID ? ST_INVALID_PARAMETER :
           ST_IO_DEVICE_ERROR;
}

/* A GUID nobody has (version 4: the time stamp counter, mixed) */
static void fresh_guid(UINT8 g[16])
{
    static UINT64 s;
    for (int i = 0; i < 2; i++) {
        UINT32 lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        UINT64 z = (s += 0x9E3779B97F4A7C15ull + (((UINT64)hi << 32) | lo));
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        memcpy(g + i * 8, &z, 8);
    }
    g[7] = (UINT8)((g[7] & 0x0F) | 0x40);
    g[8] = (UINT8)((g[8] & 0x3F) | 0x80);
}

/* A new table's disk GUID or signature and its partitions' GUIDs, where the
 * program left them zero (as Windows makes them for IOCTL_DISK_CREATE_DISK) */
static void fill_ids(DiskLayout *l)
{
    static const UINT8 zero[16];
    if (l->style == DISK_GPT && !memcmp(l->guid, zero, 16)) fresh_guid(l->guid);
    if (l->style == DISK_MBR && !l->signature) {
        UINT8 g[16];
        fresh_guid(g);
        memcpy(&l->signature, g, 4);
        if (!l->signature) l->signature = 0x4E564D42u;
    }
    for (int i = 0; l->style == DISK_GPT && i < l->count; i++)
        if (!memcmp(l->part[i].id, zero, 16)) fresh_guid(l->part[i].id);
}

/* A DRIVE_LAYOUT_INFORMATION_EX (@in, @n bytes) as a DiskLayout for @d */
static bool read_layout_in(BlockDev *d, const UINT8 *in, UINT32 n, DiskLayout *l)
{
    UINT32 style, count;
    if (n < DLI_HEADER) return false;
    memcpy(&style, in, 4);
    memcpy(&count, in + 4, 4);
    if (count > DISK_MAX_PARTS || n < DLI_HEADER + count * PIE_SIZE) return false;
    DiskEmptyLayout(d, (int)style, l);
    if (style == DISK_MBR) memcpy(&l->signature, in + 8, 4);
    else if (style == DISK_GPT) {
        UINT64 start, len;
        memcpy(l->guid, in + 8, 16);
        memcpy(&start, in + 24, 8);
        memcpy(&len, in + 32, 8);
        memcpy(&l->max, in + 40, 4);
        if (start) l->first = start / BLOCK_SECTOR;
        if (len) l->last = (start + len) / BLOCK_SECTOR - 1;
        if (l->max < 128) l->max = 128;
    } else if (style != DISK_RAW) return false;
    for (UINT32 i = 0; i < count; i++) {
        const UINT8 *e = in + DLI_HEADER + i * PIE_SIZE;
        UINT64 off, len;
        UINT32 num;
        memcpy(&off, e + 8, 8);
        memcpy(&len, e + 16, 8);
        memcpy(&num, e + 24, 4);
        if (style == DISK_MBR && !e[32]) continue;           /* (an unused entry) */
        if (style == DISK_GPT && !len) continue;
        if (off % BLOCK_SECTOR || len % BLOCK_SECTOR) return false;
        DiskPart *p = &l->part[l->count++];
        memset(p, 0, sizeof(*p));
        p->lba = off / BLOCK_SECTOR;
        p->sectors = len / BLOCK_SECTOR;
        if (style == DISK_GPT) {
            memcpy(p->type, e + 32, 16);
            memcpy(p->id, e + 48, 16);
            memcpy(&p->attrs, e + 64, 8);
            memcpy(p->name, e + 72, 72);
            p->number = (UINT32)l->count;                    /* (Windows numbers GPT entries in order) */
        } else {
            p->mbr_type = e[32];
            p->active = e[33] != 0;
            p->number = num >= 1 && num <= 4 ? num : (UINT32)l->count;
        }
    }
    if (style == DISK_MBR)                                   /* (numbers must differ: the entries in order) */
        for (int i = 0; i < l->count; i++) l->part[i].number = (UINT32)i + 1;
    return true;
}

static UINT32 disk_ioctl(DiskHandle *h, UINT32 code, const UINT8 *in, UINT32 in_n, Out *o)
{
    BlockDev *d = h->dev;
    if (DrivesDiskNumber(d) < 0 || d->gone) return ST_NO_SUCH_DEVICE;
    UINT64 bytes = d->sectors * BLOCK_SECTOR;
    switch (code) {
    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
        put32(o, 0, 7); put32(o, 4, (UINT32)DrivesDiskNumber(d)); put32(o, 8, 0);
        return o->cap < 12 ? ST_BUFFER_TOO_SMALL : ST_SUCCESS;
    case IOCTL_STORAGE_QUERY_PROPERTY: {
        UINT32 id, type;
        if (in_n < 8) return ST_INVALID_PARAMETER;
        memcpy(&id, in, 4);
        memcpy(&type, in + 4, 4);
        if (id != 0 && id != 7) return ST_NOT_SUPPORTED;
        if (type == 1) return ST_SUCCESS;                    /* PropertyExistsQuery */
        if (type != 0) return ST_INVALID_PARAMETER;
        if (id == 7) {                                       /* DEVICE_SEEK_PENALTY_DESCRIPTOR: solid state? */
            put32(o, 0, 12); put32(o, 4, 12);
            UINT8 pen = 0;                                   /* (NovaOS can't tell: as a flash disk) */
            put(o, 8, &pen, 1);
            return o->cap < 8 ? ST_INVALID_PARAMETER : ST_SUCCESS;
        }
        char model[41];
        strncpy(model, d->model, sizeof(model) - 1);
        model[sizeof(model) - 1] = 0;
        UINT32 ml = (UINT32)strlen(model);
        while (ml && model[ml - 1] == ' ') model[--ml] = 0;
        UINT32 size = 40 + ml + 1;
        put32(o, 0, 1);                                      /* Version: sizeof */
        put32(o, 4, size);
        UINT8 hdr[4] = { 0 /* DIRECT_ACCESS_DEVICE */, 0, d->removable, 0 };
        put(o, 8, hdr, 4);
        put32(o, 12, 0);                                     /* VendorIdOffset: none (ATA) */
        put32(o, 16, 40);                                    /* ProductIdOffset */
        put32(o, 20, 0);
        put32(o, 24, 0);
        put32(o, 28, bus_type(d));
        put32(o, 32, 0);
        put(o, 40, model, ml + 1);
        if (o->cap < 8) return ST_INVALID_PARAMETER;
        if (o->len > o->cap) o->len = o->cap;                /* (what fits: Size says how much there is) */
        return ST_SUCCESS;
    }
    case IOCTL_DISK_GET_DRIVE_GEOMETRY:
    case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX: {
        put64(o, 0, d->sectors / (255 * 63));
        put32(o, 8, d->removable ? 11 : 12);                 /* RemovableMedia, FixedMedia */
        put32(o, 12, 255); put32(o, 16, 63); put32(o, 20, BLOCK_SECTOR);
        if (code == IOCTL_DISK_GET_DRIVE_GEOMETRY) return o->cap < 24 ? ST_BUFFER_TOO_SMALL : ST_SUCCESS;
        put64(o, 24, bytes);
        if (o->cap < 32) return ST_BUFFER_TOO_SMALL;
        DiskLayout *l = kmalloc(sizeof(DiskLayout));
        if (l && o->cap >= 32 + 24 && DiskReadLayout(d, l)) {   /* DISK_PARTITION_INFO, when it fits */
            put32(o, 32, 24);
            put32(o, 36, (UINT32)l->style);
            if (l->style == DISK_MBR) put32(o, 40, l->signature);
            else if (l->style == DISK_GPT) put(o, 40, l->guid, 16);
        }
        kfree(l);
        return ST_SUCCESS;
    }
    case IOCTL_DISK_GET_LENGTH_INFO:
        put64(o, 0, bytes);
        return o->cap < 8 ? ST_BUFFER_TOO_SMALL : ST_SUCCESS;
    case IOCTL_DISK_IS_WRITABLE:
    case IOCTL_STORAGE_CHECK_VERIFY:
    case IOCTL_DISK_UPDATE_PROPERTIES:
        return ST_SUCCESS;
    case IOCTL_DISK_GET_DRIVE_LAYOUT_EX: {
        DiskLayout *l = kmalloc(sizeof(DiskLayout));
        if (!l) return ST_NO_MEMORY;
        if (!DiskReadLayout(d, l)) { kfree(l); return ST_IO_DEVICE_ERROR; }
        UINT32 count = l->style == DISK_MBR ? 4 : (UINT32)l->count;   /* (an MBR's four entries, used or not) */
        if (l->superfloppy) count = 1;
        put32(o, 0, (UINT32)l->style);
        put32(o, 4, count);
        if (l->style == DISK_MBR) put32(o, 8, l->signature);
        else if (l->style == DISK_GPT) {
            put(o, 8, l->guid, 16);
            put64(o, 24, l->first * BLOCK_SECTOR);
            put64(o, 32, (l->last - l->first + 1) * BLOCK_SECTOR);
            put32(o, 40, l->max);
        }
        for (UINT32 i = 0; i < count; i++) {
            UINT32 at = DLI_HEADER + i * PIE_SIZE;
            const DiskPart *p = NULL;
            if (l->style == DISK_MBR && !l->superfloppy) {
                for (int k = 0; k < l->count && !p; k++) if (l->part[k].number == i + 1) p = &l->part[k];
            } else p = &l->part[i];
            if (p) put_partition(o, at, l, p);
            else { put32(o, at, DISK_MBR); put32(o, at + 24, 0); put64(o, at + 136, 0); }   /* (unused: zeros) */
        }
        kfree(l);
        return o->len > o->cap ? ST_BUFFER_TOO_SMALL : ST_SUCCESS;
    }
    case IOCTL_DISK_SET_DRIVE_LAYOUT_EX:
    case IOCTL_DISK_CREATE_DISK: {
        if (!h->write) return ST_ACCESS_DENIED;
        DiskLayout *l = kmalloc(sizeof(DiskLayout));
        if (!l) return ST_NO_MEMORY;
        bool ok;
        if (code == IOCTL_DISK_CREATE_DISK) {
            UINT32 style;
            ok = in_n >= 8;
            if (ok) memcpy(&style, in, 4);
            ok = ok && (style == DISK_MBR || style == DISK_GPT || style == DISK_RAW);
            if (ok) {
                DiskEmptyLayout(d, (int)style, l);
                if (style == DISK_MBR) memcpy(&l->signature, in + 4, 4);
                if (style == DISK_GPT && in_n >= 20) memcpy(l->guid, in + 4, 16);
            }
        } else ok = read_layout_in(d, in, in_n, l);
        if (ok) fill_ids(l);
        UINT32 st = ok ? status_of(DrivesSetLayout(d, l)) : ST_INVALID_PARAMETER;
        kfree(l);
        return st;
    }
    default:
        return ST_INVALID_DEVICE_REQUEST;
    }
}

static UINT32 volume_ioctl(DiskHandle *h, UINT32 code, const UINT8 *in, UINT32 in_n, Out *o)
{
    DriveVolume v;
    if (!DrivesVolumeByGuid(h->guid, &v)) return ST_NO_SUCH_DEVICE;
    int disk = v.dev ? DrivesDiskNumber(v.dev) : -1;
    switch (code) {
    case IOCTL_STORAGE_GET_DEVICE_NUMBER:
        if (disk < 0) return ST_INVALID_DEVICE_REQUEST;
        put32(o, 0, 7); put32(o, 4, (UINT32)disk); put32(o, 8, v.partition);
        return o->cap < 12 ? ST_BUFFER_TOO_SMALL : ST_SUCCESS;
    case IOCTL_DISK_GET_LENGTH_INFO:
        put64(o, 0, v.sectors ? v.sectors * BLOCK_SECTOR : v.total);
        return o->cap < 8 ? ST_BUFFER_TOO_SMALL : ST_SUCCESS;
    case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
        if (disk < 0) return ST_INVALID_DEVICE_REQUEST;
        put32(o, 0, 1);
        put32(o, 8, (UINT32)disk);
        put64(o, 16, v.lba * BLOCK_SECTOR);
        put64(o, 24, v.sectors * BLOCK_SECTOR);
        return o->cap < 32 ? (o->cap >= 8 ? ST_BUFFER_OVERFLOW : ST_BUFFER_TOO_SMALL) : ST_SUCCESS;
    case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME: {
        char name[40];
        int n = ksnprintf(name, sizeof(name), "\\Device\\HarddiskVolume%u", v.number);
        UINT16 len = (UINT16)(2 * n);
        put(o, 0, &len, 2);
        for (int i = 0; i < n; i++) { UINT16 c = (UINT8)name[i]; put(o, 2 + 2 * (UINT32)i, &c, 2); }
        return o->cap < 2 ? ST_BUFFER_TOO_SMALL : o->len > o->cap ? ST_BUFFER_OVERFLOW : ST_SUCCESS;
    }
    case IOCTL_DISK_GET_PARTITION_INFO:
    case IOCTL_DISK_GET_PARTITION_INFO_EX: {
        DiskLayout *l = kmalloc(sizeof(DiskLayout));
        if (!l) return ST_NO_MEMORY;
        const DiskPart *p = volume_part(&v, l);
        UINT32 st = ST_SUCCESS;
        if (!p) st = ST_INVALID_DEVICE_REQUEST;
        else if (code == IOCTL_DISK_GET_PARTITION_INFO_EX) {
            put_partition(o, 0, l, p);
            if (o->cap < PIE_SIZE) st = ST_BUFFER_TOO_SMALL;
        } else {                                             /* PARTITION_INFORMATION (MBR partitions' view) */
            put64(o, 0, p->lba * BLOCK_SECTOR);
            put64(o, 8, p->sectors * BLOCK_SECTOR);
            put32(o, 16, (UINT32)p->lba);
            put32(o, 20, p->number);
            UINT8 b[4] = { l->style == DISK_GPT ? 0x07 : p->mbr_type, p->active, 1, 0 };
            put(o, 24, b, 4);
            if (o->cap < 32) st = ST_BUFFER_TOO_SMALL;
            o->len = 32;
        }
        kfree(l);
        return st;
    }
    case IOCTL_DISK_IS_WRITABLE:
        return v.flags & DRIVE_VOL_READONLY ? ST_MEDIA_WRITE_PROTECTED : ST_SUCCESS;
    case IOCTL_STORAGE_CHECK_VERIFY:
        return ST_SUCCESS;
    case IOCTL_NOVA_VOLUME_INFO: {                           /* NOVA_VOLUME_INFO (winioctl.h) */
        UINT16 letter = (UINT8)v.letter;
        put(o, 0, v.guid, 16);
        put32(o, 16, v.number);
        put32(o, 20, v.flags);
        put(o, 24, &letter, 2);
        put32(o, 28, disk < 0 ? 0xFFFFFFFFu : (UINT32)disk);
        put32(o, 32, v.partition);
        put64(o, 40, v.lba * BLOCK_SECTOR);
        put64(o, 48, v.sectors ? v.sectors * BLOCK_SECTOR : v.total);
        return o->cap < 56 ? ST_BUFFER_TOO_SMALL : ST_SUCCESS;
    }
    case IOCTL_NOVA_VOLUME_FORMAT: {
        if (!h->write) return ST_ACCESS_DENIED;
        if (in_n < 4) return ST_INVALID_PARAMETER;
        UINT32 fs;
        memcpy(&fs, in, 4);
        if (fs > 1) return ST_INVALID_PARAMETER;
        char label[34];
        int k = 0;
        for (UINT32 i = 4; i + 1 < in_n && k < 32; i += 2) {
            UINT16 c = (UINT16)(in[i] | in[i + 1] << 8);
            if (!c) break;
            label[k++] = c < 0x80 ? (char)c : '_';
        }
        label[k] = 0;
        return status_of(DrivesFormat(h->guid, fs == 0, label));
    }
    case IOCTL_NOVA_VOLUME_SET_LETTER: {
        if (!h->write) return ST_ACCESS_DENIED;
        if (in_n < 2) return ST_INVALID_PARAMETER;
        UINT16 c = (UINT16)(in[0] | in[1] << 8);
        if (c > 0x7F) return ST_INVALID_PARAMETER;
        return status_of(DrivesSetLetter(h->guid, (char)c));
    }
    default:
        return ST_INVALID_DEVICE_REQUEST;
    }
}

/* NtDeviceIoControlFile on a disk or volume handle; *@info the bytes put out */
UINT32 um_disk_ioctl(UmObject *o, UINT32 code, UINT64 in, UINT32 in_n, UINT64 out, UINT32 out_n, UINT64 *info)
{
    DiskHandle *h = (DiskHandle *)o;
    *info = 0;
    if (in_n > 1u << 20 || out_n > 1u << 20) return ST_INVALID_PARAMETER;
    UINT8 *ib = in_n ? kmalloc(in_n) : NULL, *ob = out_n ? kzalloc(out_n) : NULL;
    if ((in_n && !ib) || (out_n && !ob)) { kfree(ib); kfree(ob); return ST_NO_MEMORY; }
    if (in_n && !NT_SUCCESS(CopyFromUser(ib, (const void *)(uintptr_t)in, in_n))) {
        kfree(ib); kfree(ob);
        return 0xC0000005u;                                  /* ACCESS_VIOLATION */
    }
    Out ot = { ob, out_n, 0 };
    UINT32 st = h->volume ? volume_ioctl(h, code, ib, in_n, &ot) : disk_ioctl(h, code, ib, in_n, &ot);
    UINT32 n = ot.len < out_n ? ot.len : out_n;
    if ((st == ST_SUCCESS || st == ST_BUFFER_OVERFLOW) && n) {
        if (!NT_SUCCESS(CopyToUser((void *)(uintptr_t)out, ob, n))) st = 0xC0000005u;
        else *info = n;
    }
    kfree(ib);
    kfree(ob);
    return st;
}

/* UTF-8 @s as UTF-16 at @out (BMP only); code units */
static UINT32 widen(const char *s, UINT8 *out, UINT32 cap)
{
    UINT32 n = 0;
    for (const unsigned char *c = (const unsigned char *)s; *c && n < cap; n++) {
        UINT32 ch = *c++;
        if (ch >= 0xC0) {
            int more = ch >= 0xE0 ? 2 : 1;
            ch &= 0x3F >> more;
            while (more-- && (*c & 0xC0) == 0x80) ch = ch << 6 | (*c++ & 0x3F);
        }
        out[2 * n] = (UINT8)ch;
        out[2 * n + 1] = (UINT8)(ch >> 8);
    }
    return n;
}

/* NtQueryVolumeInformationFile on a volume handle: class @cls into @b
 * (at least 128 bytes); *@need its size */
UINT32 um_disk_query_volume(UmObject *o, UINT32 cls, UINT8 *b, UINT32 *need)
{
    DiskHandle *h = (DiskHandle *)o;
    DriveVolume v;
    if (cls == 4) {                                          /* FileFsDeviceInformation */
        UINT32 dev[2] = { 7, 0 };
        memcpy(b, dev, 8);
        *need = 8;
        return ST_SUCCESS;
    }
    if (!h->volume) return ST_INVALID_DEVICE_REQUEST;
    if (!DrivesVolumeByGuid(h->guid, &v)) return ST_NO_SUCH_DEVICE;
    if (!v.fs[0] && cls != 11) return ST_UNRECOGNIZED_VOLUME;  /* (RAW: no file system answers) */
    UINT64 total = v.total / 4096, avail = v.free / 4096;
    switch (cls) {
    case 1: {                                                /* FileFsVolumeInformation */
        UINT32 serial = 0x4E560000u | v.number, ll = 2 * widen(v.label, b + 18, 32);
        memset(b, 0, 18);
        memcpy(b + 8, &serial, 4);
        memcpy(b + 12, &ll, 4);
        *need = 18 + ll;
        return ST_SUCCESS;
    }
    case 3:                                                  /* FileFsSizeInformation */
        memset(b, 0, 24);
        memcpy(b, &total, 8); memcpy(b + 8, &avail, 8);
        b[16] = 8; b[21] = 2;                                /* 8 sectors of 512 */
        *need = 24;
        return ST_SUCCESS;
    case 5: {                                                /* FileFsAttributeInformation */
        bool ntfs = !strcmp(v.fs, "NTFS");
        UINT32 attrs = ntfs ? 0x0002u | 0x0004u | 0x0008u | 0x400000u : 0x0002u | 0x0004u;   /* (+ PERSISTENT_ACLS, HARD_LINKS) */
        if (v.flags & DRIVE_VOL_READONLY) attrs |= 0x80000u;
        UINT32 maxc = 255, nl = 2 * widen(v.fs, b + 12, 8);
        memcpy(b, &attrs, 4); memcpy(b + 4, &maxc, 4); memcpy(b + 8, &nl, 4);
        *need = 12 + nl;
        return ST_SUCCESS;
    }
    case 7:                                                  /* FileFsFullSizeInformation */
        memset(b, 0, 32);
        memcpy(b, &total, 8); memcpy(b + 8, &avail, 8); memcpy(b + 16, &avail, 8);
        b[24] = 8; b[29] = 2;
        *need = 32;
        return ST_SUCCESS;
    case 11: {                                               /* FileFsSectorSizeInformation */
        UINT32 s[7] = { 512, 512, 512, 512, 0, 0, 0 };
        memcpy(b, s, 28);
        *need = 28;
        return ST_SUCCESS;
    }
    default:
        return ST_INVALID_INFO_CLASS;
    }
}
