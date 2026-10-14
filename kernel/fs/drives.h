/*
 * drives.h — the disks' volumes: drive C:'s, and D:, E:, ... for the others
 *
 * Every partition of every disk is a volume, as on Windows, with a number
 * (\Device\HarddiskVolumeN; drive C: is 1) and a GUID (\\?\Volume{...}\).
 * The NTFS and FAT volumes are mounted as drive letters; Disk Management
 * (diskmgmt.exe, through kernel/um/um_disk.c) lists them, formats them,
 * changes their letters and the disks' partition tables.  Letters chosen
 * are kept in HKLM\SYSTEM\MountedDevices, as Windows keeps them.
 */

#pragma once

#include "block.h"
#include "partition.h"

/* Find the volumes on the fixed disks and mount each as the next free
 * drive letter from D:.  After PersistInit (which brings up the disks). */
void DrivesInit(void);
/* The registry is there (UmInit): give the volumes the letters chosen for
 * them before, and remember the ones they got */
void DrivesRestoreLetters(void);
/* A disk arrived (a USB stick): mount its NTFS and FAT volumes */
void DrivesAttach(BlockDev *d);
/* A disk went away: its drive letters go with it */
void DrivesDetach(BlockDev *d);
/* Write files changed on the drives to their disks: after a quiet second
 * (DrivesPoll, called often), or now (DrivesSync, before shutting down) */
void DrivesPoll(void);
bool DrivesSync(void);

#define DRIVE_VOL_SYSTEM   0x01         /* an EFI System Partition */
#define DRIVE_VOL_BOOT     0x02         /* drive C: */
#define DRIVE_VOL_NOVA     0x04         /* NovaOS's own (drive C:'s, its boot files): left alone */
#define DRIVE_VOL_READONLY 0x08
#define DRIVE_VOL_MOUNTED  0x10         /* its files are a drive (it has a letter) */
#define DRIVE_VOL_REMOVABLE 0x20

typedef struct {
    BlockDev *dev;                      /* NULL: drive C: kept in memory only */
    UINT32    partition;                /* its number on the disk (DiskPart.number) */
    UINT64    lba, sectors;
    UINT32    number;                   /* \Device\HarddiskVolumeN */
    UINT8     guid[16];                 /* \\?\Volume{...} */
    char      letter;                   /* 0: none */
    char      fs[8];                    /* "NTFS", "FAT32", "FAT16", "exFAT", or "" (RAW) */
    char      label[34];
    UINT64    total, free;              /* bytes (free: 0 when not known) */
    UINT32    flags;                    /* DRIVE_VOL_* */
} DriveVolume;

/* The volumes there are, drive C:'s first; how many (at most @max) */
int  DrivesVolumes(DriveVolume *out, int max);
/* The volume with GUID @guid, by number, or with drive letter @letter */
bool DrivesVolumeByGuid(const UINT8 guid[16], DriveVolume *out);
bool DrivesVolumeByNumber(UINT32 number, DriveVolume *out);
bool DrivesVolumeByLetter(char letter, DriveVolume *out);

/* Without locks, for path lookups (a snapshot: the volumes may change
 * meanwhile): the volume number of drive @letter (0: none); the letter of
 * volume @guid (0: none; *@known whether there is such a volume); the GUID
 * of volume @number */
UINT32 DrivesNumberOfLetter(char letter);
char   DrivesLetterOfGuid(const UINT8 guid[16], bool *known);
bool   DrivesGuidOfNumber(UINT32 number, UINT8 guid[16]);

/* What the changes below report */
enum { DRIVES_OK = 0, DRIVES_NOT_FOUND, DRIVES_IN_USE, DRIVES_LETTER_TAKEN, DRIVES_INVALID, DRIVES_FAILED };

/* Format the volume @guid as NTFS (or FAT32) labelled @label: its old
 * files are gone; it mounts again with its letter (a new one if it had
 * none and the registry does not say it should stay without) */
int  DrivesFormat(const UINT8 guid[16], bool ntfs, const char *label);
/* Give the volume @guid drive letter @letter (0: take its letter away) */
int  DrivesSetLetter(const UINT8 guid[16], char letter);
/* Write @l as @d's partition table (IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
 * IOCTL_DISK_CREATE_DISK): every volume that is in use (mounted, or
 * NovaOS's own) must keep its partition as it is.  A new partition's
 * first sectors are cleared, so no old volume shows up there. */
int  DrivesSetLayout(BlockDev *d, const DiskLayout *l);
/* @d's number (\\.\PhysicalDriveN), or -1 */
int  DrivesDiskNumber(BlockDev *d);
