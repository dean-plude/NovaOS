/*
 * bootfiles.h — the files beside the kernel
 *
 * Wallpapers, icon sets and sample pictures are not built into kernel.elf:
 * they are files under \EFI\NOVA\Files on the boot volume (the ISO's EFI
 * System Partition, or an installed disk's), which the bootloader reads
 * and lists (BootInfo version 5).  At boot they are installed on drive C:
 * at the same path below that folder (\EFI\NOVA\Files\Windows\Web\Wallpaper\
 * Beam.png is C:\Windows\Web\Wallpaper\Beam.png), as system files that are
 * never saved to the data disk, and the installer copies them to the disk
 * it installs on.
 */

#pragma once

#include "../include/types.h"
#include "../include/boot_protocol.h"

#define BOOT_FILES_DIR "\\EFI\\NOVA\\Files"

/* Record the files the bootloader passed (early boot, before the heap) */
void BootFilesInfo(const BootInfo *info);
/* Install them on drive C: (UmInit, while it installs the system files)
 * and give their memory back; how many */
int  BootFilesInstall(void);
/* What was installed: how many files, their C: paths and their total size */
int         BootFilesCount(void);
const char *BootFilesPath(int i);
UINT64      BootFilesBytes(void);
