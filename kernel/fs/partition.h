/*
 * partition.h — a disk's partition table (MBR or GPT), read and written
 *
 * What Windows' IOCTL_DISK_GET_DRIVE_LAYOUT_EX reports and
 * IOCTL_DISK_SET_DRIVE_LAYOUT_EX / IOCTL_DISK_CREATE_DISK write: the
 * partitions of an MBR (the four primary entries; extended partitions are
 * not followed) or of a GPT (the primary header and table, checked by
 * their CRCs, written back with the backup copy at the end of the disk).
 * A disk with a volume of its own at sector 0 (NTFS or FAT, no partition
 * table: a "superfloppy") is reported as Windows does, as an MBR disk with
 * one partition from sector 0.
 */

#pragma once

#include "../include/types.h"
#include "block.h"

#define DISK_RAW 2                      /* PARTITION_STYLE_RAW: no partition table */
#define DISK_MBR 0
#define DISK_GPT 1

#define DISK_MAX_PARTS 128

typedef struct {
    UINT64 lba, sectors;
    UINT32 number;                      /* its number on the disk, from 1 (MBR: the entry's, 1-4) */
    UINT8  mbr_type;                    /* MBR: the type byte (0x07 NTFS, 0x0C FAT32, 0xEF EFI...) */
    bool   active;                      /* MBR: the boot indicator */
    UINT8  type[16], id[16];            /* GPT: the type and unique GUIDs, as stored */
    UINT64 attrs;                       /* GPT: attribute bits */
    UINT16 name[36];                    /* GPT: its name (UTF-16) */
} DiskPart;

typedef struct {
    int      style;                     /* DISK_MBR, DISK_GPT or DISK_RAW */
    bool     superfloppy;               /* a volume at sector 0, no table */
    UINT32   signature;                 /* MBR: the disk signature */
    UINT8    guid[16];                  /* GPT: the disk GUID */
    UINT64   first, last;               /* the sectors partitions may use */
    UINT32   max;                       /* GPT: the table's entries */
    int      count;
    DiskPart part[DISK_MAX_PARTS];
} DiskLayout;

/* The GPT partition types NovaOS knows */
extern const UINT8 DISK_TYPE_ESP[16];        /* C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
extern const UINT8 DISK_TYPE_BASIC[16];      /* EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 (basic data) */
extern const UINT8 DISK_TYPE_MSR[16];        /* E3C9E316-0B5C-4DB8-817D-F92DF00215AE (Microsoft reserved) */
extern const UINT8 DISK_TYPE_RECOVERY[16];   /* DE94BBA4-06D1-4D40-A16A-BFD50179D6AC (Windows recovery) */

/* @d's partition table into *@l (kmalloc'd by the caller: it is large);
 * false on a read error */
bool DiskReadLayout(BlockDev *d, DiskLayout *l);
/* Write *@l as @d's partition table (an MBR keeps its boot code; a GPT
 * gets a protective MBR and its backup copy).  The partitions must lie
 * between l->first and l->last without overlapping (DiskCheckLayout). */
bool DiskWriteLayout(BlockDev *d, const DiskLayout *l);
/* Whether *@l can be written to @d: its partitions in the usable area, none
 * overlapping, at most four on an MBR disk */
bool DiskCheckLayout(BlockDev *d, const DiskLayout *l);
/* A fresh, empty table of @style for @d (what IOCTL_DISK_CREATE_DISK
 * writes): the usable area and, for a GPT, 128 entries */
void DiskEmptyLayout(BlockDev *d, int style, DiskLayout *l);
/* The partition that is an EFI System Partition (GPT type or MBR 0xEF) */
bool DiskIsEsp(const DiskLayout *l, const DiskPart *p);
