/*
 * setup.h — installing NovaOS on a disk
 *
 * The installation files are the bootloader (\EFI\BOOT\BOOTX64.EFI), the
 * kernel (\EFI\NOVA\kernel.elf, which carries the programs and DLLs) and
 * the files beside it (\EFI\NOVA\Files: wallpapers, icon sets, pictures;
 * fs/bootfiles.h), which are copied from drive C:.
 * Booted from the installation disc, the bootloader hands them over in
 * memory; on an installed system they are read from the disk NovaOS
 * booted from.  Installing erases the chosen disk and gives it a GPT with
 * two partitions:
 *
 *   1. EFI System Partition, FAT32 "NOVA_EFI", at least 128 MiB: boot files and a staged update
 *   2. Basic data, NTFS (or FAT32) "NOVADATA", the rest: drive C: is saved
 *      here (on NTFS with its files' security descriptors)
 *
 * The firmware finds \EFI\BOOT\BOOTX64.EFI on the ESP by itself, so no
 * boot entry needs to be written.
 *
 * Or, beside the systems already on a GPT disk (Windows), the same two
 * partitions go into the disk's largest free space and every partition
 * already there is left as it is: NovaOS gets an ESP of its own rather
 * than writing into Windows' (docs/design/installer.md).
 */

#pragma once

#include "../include/types.h"
#include "../include/boot_protocol.h"
#include "block.h"

/* Record the installation media the bootloader passed (early boot) */
void SetupBootInfo(const BootInfo *info);
/* NovaOS is running from its installation media (the ISO on a disc, or
 * written to a USB stick) */
bool SetupIsLive(void);
/* ... and which: "disc" or "USB stick" */
const char *SetupMediaName(void);
bool SetupLiveFromUsb(void);

#define SETUP_MIN_BYTES  (256ull << 20)   /* smallest disk to install on */

#define SETUP_MAX_PARTS 8

/* A partition on a disk, or its free space, as Setup shows them */
typedef struct {
    UINT64 lba, sectors;
    bool   free;               /* unallocated space, not a partition */
    char   name[48];           /* "Windows (NTFS)", "EFI System", "Microsoft reserved", ... */
} SetupPart;

typedef struct {
    BlockDev *dev;
    UINT64    bytes;
    bool      too_small;
    bool      boot;            /* NovaOS booted from it */
    bool      holds_c;         /* drive C: is saved on it */
    bool      gpt;             /* it has a GUID partition table */
    bool      windows;         /* a Windows volume or boot manager is on it */
    bool      nova;            /* NovaOS is installed on it */
    char      contents[64];    /* "Empty", "NovaOS (NOVA_EFI, NOVADATA)", ... */
    int       nparts;          /* its partitions and free spaces, in disk order */
    SetupPart parts[SETUP_MAX_PARTS];
    UINT64    free_lba, free_sectors;      /* its largest free space (1 MiB aligned) */
    bool      can_beside;      /* NovaOS fits in that free space beside the rest */
} SetupDisk;

/* The free space NovaOS needs beside other systems: its ESP (the system
 * twice, for a staged update, and the boot files) and 512 MiB for drive C: */
UINT64 SetupBesideBytes(void);

/* The disks NovaOS could be installed on; returns how many (<= max) */
int SetupListDisks(SetupDisk *out, int max);

/* Start installing on @dev in the background, with drive C: on NTFS (or
 * FAT32): erasing it, or (@beside) in its largest free space with every
 * partition on it kept; false if one is running */
bool SetupStart(BlockDev *dev, bool ntfs, bool beside);

typedef enum { SETUP_IDLE, SETUP_RUNNING, SETUP_DONE, SETUP_FAILED } SetupState;
typedef struct {
    SetupState state;
    int        percent;        /* 0-100 */
    char       step[96];       /* what it is doing now */
    char       error[128];     /* when SETUP_FAILED */
    bool       moved_c;        /* this session's files were copied to the new disk */
} SetupStatus;

void SetupGetStatus(SetupStatus *out);
