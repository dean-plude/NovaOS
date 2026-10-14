/*
 * partition.c — MBR and GPT partition tables (see partition.h)
 */

#include "partition.h"
#include "../mm/vmm.h"
#include "../lib/string.h"

const UINT8 DISK_TYPE_ESP[16]      = { 0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                                       0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B };
const UINT8 DISK_TYPE_BASIC[16]    = { 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
                                       0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 };
const UINT8 DISK_TYPE_MSR[16]      = { 0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D,
                                       0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE };
const UINT8 DISK_TYPE_RECOVERY[16] = { 0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D,
                                       0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC };

static const UINT8 g_zero16[16];

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

bool DiskIsEsp(const DiskLayout *l, const DiskPart *p)
{
    return l->style == DISK_GPT ? !memcmp(p->type, DISK_TYPE_ESP, 16) : p->mbr_type == 0xEF;
}

/* A GPT header at @lba (in @h) that is sane and matches its CRC */
static bool gpt_header_ok(UINT8 *h)
{
    UINT32 size, crc, zero = 0;
    if (memcmp(h, "EFI PART", 8)) return false;
    memcpy(&size, h + 12, 4);
    memcpy(&crc, h + 16, 4);
    if (size < 92 || size > BLOCK_SECTOR) return false;
    memcpy(h + 16, &zero, 4);
    bool ok = crc32(h, size) == crc;
    memcpy(h + 16, &crc, 4);
    return ok;
}

static bool read_gpt(BlockDev *d, const UINT8 *h, DiskLayout *l)
{
    UINT64 table;
    UINT32 count, esize, ecrc;
    memcpy(&l->first, h + 40, 8);
    memcpy(&l->last, h + 48, 8);
    memcpy(l->guid, h + 56, 16);
    memcpy(&table, h + 72, 8);
    memcpy(&count, h + 80, 4);
    memcpy(&esize, h + 84, 4);
    memcpy(&ecrc, h + 88, 4);
    if (esize < 128 || esize > BLOCK_SECTOR || BLOCK_SECTOR % esize || !count || count > 1024) return false;
    l->max = count;
    UINT32 sectors = (count * esize + BLOCK_SECTOR - 1) / BLOCK_SECTOR;
    UINT8 *ent = kmalloc((UINT64)sectors * BLOCK_SECTOR);
    if (!ent) return false;
    if (!d->read(d, table, sectors, ent)) { kfree(ent); return false; }
    /* (a table whose CRC does not match is still read, as NovaOS always has:
     * xorriso's hybrid ISOs carry 248 entries) */
    (void)ecrc;
    for (UINT32 i = 0; i < count && l->count < DISK_MAX_PARTS; i++) {
        const UINT8 *e = ent + i * esize;
        if (!memcmp(e, g_zero16, 16)) continue;
        DiskPart *p = &l->part[l->count++];
        memset(p, 0, sizeof(*p));
        UINT64 first, last;
        memcpy(p->type, e, 16);
        memcpy(p->id, e + 16, 16);
        memcpy(&first, e + 32, 8);
        memcpy(&last, e + 40, 8);
        memcpy(&p->attrs, e + 48, 8);
        memcpy(p->name, e + 56, 72);
        p->lba = first;
        p->sectors = last >= first ? last - first + 1 : 0;
        p->number = i + 1;
    }
    kfree(ent);
    return true;
}

bool DiskReadLayout(BlockDev *d, DiskLayout *l)
{
    memset(l, 0, sizeof(*l));
    l->style = DISK_RAW;
    l->first = 1;
    l->last = d->sectors ? d->sectors - 1 : 0;
    UINT8 *s = kmalloc(2 * BLOCK_SECTOR);
    if (!s) return false;
    if (!d->read(d, 0, 2, s)) { kfree(s); return false; }
    bool mbr = s[510] == 0x55 && s[511] == 0xAA;
    /* A volume of its own: NTFS, or a FAT boot sector (no partition entries
     * with a sane type, but a BPB with 512-byte sectors) */
    bool whole_fat = mbr && (s[0] == 0xEB || s[0] == 0xE9) && s[11] == 0x00 && s[12] == 0x02 && s[13] &&
                     (!memcmp(s + 54, "FAT", 3) || !memcmp(s + 82, "FAT", 3));
    if (!memcmp(s + 3, "NTFS    ", 8) || whole_fat) {
        l->style = DISK_MBR;
        l->superfloppy = true;
        l->first = 0;
        l->count = 1;
        DiskPart *p = &l->part[0];
        p->lba = 0;
        p->sectors = d->sectors;
        p->number = 1;
        p->mbr_type = whole_fat ? 0x0C : 0x07;
        kfree(s);
        return true;
    }
    if (!mbr) { kfree(s); return true; }
    l->style = DISK_MBR;
    memcpy(&l->signature, s + 440, 4);
    bool gpt = false;
    for (int i = 0; i < 4; i++) {
        const UINT8 *e = s + 446 + 16 * i;
        UINT32 lba, n;
        memcpy(&lba, e + 8, 4);
        memcpy(&n, e + 12, 4);
        if (e[4] == 0xEE) gpt = true;
        if (!e[4] || !n) continue;
        DiskPart *p = &l->part[l->count++];
        memset(p, 0, sizeof(*p));
        p->lba = lba;
        p->sectors = n;
        p->number = (UINT32)i + 1;
        p->mbr_type = e[4];
        p->active = e[0] == 0x80;
    }
    if (gpt) {
        l->style = DISK_GPT;
        l->count = 0;
        l->signature = 0;
        UINT8 *h = s + BLOCK_SECTOR;
        bool ok = gpt_header_ok(h) || !memcmp(h, "EFI PART", 8);   /* (as before: the CRC is not required) */
        if (!ok || !read_gpt(d, h, l)) {
            /* the primary is damaged: the backup at the end of the disk */
            if (!d->read(d, d->sectors - 1, 1, h) || !gpt_header_ok(h) || !read_gpt(d, h, l)) {
                l->style = DISK_RAW;                         /* (no usable table) */
                l->count = 0;
            }
        }
    }
    kfree(s);
    return true;
}

bool DiskCheckLayout(BlockDev *d, const DiskLayout *l)
{
    if (l->style == DISK_RAW) return l->count == 0;
    if (l->style != DISK_MBR && l->style != DISK_GPT) return false;
    if (l->count < 0 || l->count > (l->style == DISK_MBR ? 4 : (int)l->max) || l->count > DISK_MAX_PARTS) return false;
    UINT64 first = l->style == DISK_GPT ? l->first : 1, last = l->style == DISK_GPT ? l->last : d->sectors - 1;
    if (l->style == DISK_GPT && (first < 2 + 32 || last > d->sectors - 2 - 32 || first > last)) return false;
    for (int i = 0; i < l->count; i++) {
        const DiskPart *p = &l->part[i];
        if (!p->sectors || p->lba < first || p->lba + p->sectors - 1 > last) return false;
        if (l->style == DISK_MBR && (p->lba + p->sectors > 0xFFFFFFFFull || !p->mbr_type || p->mbr_type == 0xEE ||
                                     p->number < 1 || p->number > 4))
            return false;
        if (l->style == DISK_GPT && (!memcmp(p->type, g_zero16, 16) || p->number < 1 || p->number > l->max)) return false;
        for (int k = 0; k < i; k++) {
            const DiskPart *q = &l->part[k];
            if (p->number == q->number) return false;
            if (p->lba < q->lba + q->sectors && q->lba < p->lba + p->sectors) return false;   /* overlapping */
        }
    }
    return true;
}

void DiskEmptyLayout(BlockDev *d, int style, DiskLayout *l)
{
    memset(l, 0, sizeof(*l));
    l->style = style;
    if (style == DISK_GPT) {
        l->max = 128;
        l->first = 34;                                       /* (after the table; partitions start at 1 MiB) */
        l->last = d->sectors - 34;
    } else {
        l->first = d->sectors > 4096 ? 2048 : 63;
        l->last = d->sectors - 1;
    }
}

/* An MBR partition entry's CHS address for @lba (past 1024 cylinders: the
 * usual 1023/254/63) */
static void chs(UINT8 *out, UINT64 lba)
{
    UINT64 c = lba / (255 * 63);
    if (c > 1023) { out[0] = 0xFE; out[1] = 0xFF; out[2] = 0xFF; return; }
    UINT32 h = (UINT32)(lba / 63 % 255), s = (UINT32)(lba % 63 + 1);
    out[0] = (UINT8)h;
    out[1] = (UINT8)(s | ((c >> 2) & 0xC0));
    out[2] = (UINT8)c;
}

static bool write_mbr(BlockDev *d, const DiskLayout *l)
{
    UINT8 *s = kzalloc(BLOCK_SECTOR);
    if (!s) return false;
    UINT8 old[BLOCK_SECTOR];
    if (d->read(d, 0, 1, old) && old[510] == 0x55 && old[511] == 0xAA && memcmp(old + 3, "NTFS    ", 8))
        memcpy(s, old, 440);                                 /* (its boot code stays) */
    memcpy(s + 440, &l->signature, 4);
    for (int i = 0; i < l->count; i++) {
        const DiskPart *p = &l->part[i];
        UINT8 *e = s + 446 + 16 * (p->number - 1);
        UINT32 lba = (UINT32)p->lba, n = (UINT32)p->sectors;
        e[0] = p->active ? 0x80 : 0;
        chs(e + 1, p->lba);
        e[4] = p->mbr_type;
        chs(e + 5, p->lba + p->sectors - 1);
        memcpy(e + 8, &lba, 4);
        memcpy(e + 12, &n, 4);
    }
    s[510] = 0x55; s[511] = 0xAA;
    bool ok = d->write(d, 0, 1, s);
    kfree(s);
    return ok;
}

static bool write_gpt(BlockDev *d, const DiskLayout *l)
{
    UINT32 table_sec = (l->max * 128 + BLOCK_SECTOR - 1) / BLOCK_SECTOR;
    UINT64 last = d->sectors - 1;
    UINT8 *buf = kzalloc((UINT64)(2 + table_sec) * BLOCK_SECTOR), *bh = kzalloc(BLOCK_SECTOR);
    if (!buf || !bh) { kfree(buf); kfree(bh); return false; }
    UINT8 *mbr = buf, *h = buf + BLOCK_SECTOR, *ent = buf + 2 * BLOCK_SECTOR;
    /* protective MBR: one 0xEE partition over the disk */
    UINT32 v32 = 1, n32 = last > 0xFFFFFFFFull ? 0xFFFFFFFFu : (UINT32)last;
    mbr[446 + 1] = 0x00; mbr[446 + 2] = 0x02; mbr[446 + 3] = 0x00;
    mbr[446 + 4] = 0xEE;
    mbr[446 + 5] = 0xFE; mbr[446 + 6] = 0xFF; mbr[446 + 7] = 0xFF;
    memcpy(mbr + 446 + 8, &v32, 4);
    memcpy(mbr + 446 + 12, &n32, 4);
    mbr[510] = 0x55; mbr[511] = 0xAA;
    for (int i = 0; i < l->count; i++) {
        const DiskPart *p = &l->part[i];
        UINT8 *e = ent + (p->number - 1) * 128;
        UINT64 end = p->lba + p->sectors - 1;
        memcpy(e, p->type, 16);
        memcpy(e + 16, p->id, 16);
        memcpy(e + 32, &p->lba, 8);
        memcpy(e + 40, &end, 8);
        memcpy(e + 48, &p->attrs, 8);
        memcpy(e + 56, p->name, 72);
    }
    UINT32 ecrc = crc32(ent, l->max * 128);
    UINT64 v64;
    memcpy(h, "EFI PART", 8);
    v32 = 0x00010000; memcpy(h + 8, &v32, 4);                 /* revision 1.0 */
    v32 = 92;         memcpy(h + 12, &v32, 4);
    v64 = 1;          memcpy(h + 24, &v64, 8);                /* this header */
    memcpy(h + 32, &last, 8);                                 /* the backup */
    memcpy(h + 40, &l->first, 8);
    memcpy(h + 48, &l->last, 8);
    memcpy(h + 56, l->guid, 16);
    v64 = 2;          memcpy(h + 72, &v64, 8);
    memcpy(h + 80, &l->max, 4);
    v32 = 128;        memcpy(h + 84, &v32, 4);
    memcpy(h + 88, &ecrc, 4);
    v32 = crc32(h, 92); memcpy(h + 16, &v32, 4);
    /* the backup: header at the last sector, its table just before */
    memcpy(bh, h, 92);
    memcpy(bh + 24, &last, 8);
    v64 = 1;                  memcpy(bh + 32, &v64, 8);
    v64 = last - table_sec;   memcpy(bh + 72, &v64, 8);
    v32 = 0; memcpy(bh + 16, &v32, 4);
    v32 = crc32(bh, 92); memcpy(bh + 16, &v32, 4);
    bool ok = d->write(d, last - table_sec, table_sec, ent) && d->write(d, last, 1, bh) &&
              d->write(d, 0, 2 + table_sec, buf);
    kfree(bh);
    kfree(buf);
    return ok;
}

bool DiskWriteLayout(BlockDev *d, const DiskLayout *l)
{
    if (!DiskCheckLayout(d, l)) return false;
    bool ok;
    if (l->style == DISK_RAW) {                              /* no table: sector 0 and the GPT headers cleared */
        UINT8 *z = kzalloc(BLOCK_SECTOR * 34);
        ok = z && d->write(d, 0, 34, z) && d->write(d, d->sectors - 34, 34, z);
        kfree(z);
    } else {
        ok = l->style == DISK_GPT ? write_gpt(d, l) : write_mbr(d, l);
        if (ok && l->style == DISK_MBR) {                    /* a GPT that was there is gone (its headers) */
            UINT8 *b = kmalloc(BLOCK_SECTOR);
            UINT64 at[2] = { 1, d->sectors - 1 };
            for (int i = 0; ok && b && i < 2; i++)
                if (d->read(d, at[i], 1, b) && !memcmp(b, "EFI PART", 8)) {
                    memset(b, 0, BLOCK_SECTOR);
                    ok = d->write(d, at[i], 1, b);
                }
            kfree(b);
        }
    }
    if (ok && d->flush) d->flush(d);
    return ok;
}
