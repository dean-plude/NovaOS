/*
 * efi.h — the firmware's UEFI runtime services (variables)
 *
 * The bootloader hands over the physical address of the firmware's
 * EFI_RUNTIME_SERVICES table (boot protocol 6).  NovaOS never calls
 * SetVirtualAddressMap, so the services run as the UEFI specification
 * allows in that case, in physical mode: each call switches to page tables
 * that map the low physical memory (and every runtime region) one to one,
 * with the kernel's half of the address space as it is, so the arguments
 * may be ordinary kernel pointers.  Interrupts are off for the call, one
 * CPU at a time, and the caller's x87/SSE/AVX state is saved around it.
 *
 * Setup adds the firmware boot entry for a disk it installed with these
 * (kernel/fs/setup.c), as Windows' SetFirmwareEnvironmentVariable and
 * `bcdedit /set {fwbootmgr}` do.
 */

#pragma once

#include "../include/types.h"
#include "../../include/boot_protocol.h"

#define EFI_OK              0ull
#define EFI_ERR(n)          ((1ull << 63) | (n))
#define EFI_BUFFER_SMALL    EFI_ERR(5)
#define EFI_NOT_FOUND_ERR   EFI_ERR(14)
#define EFI_UNSUPPORTED_ERR EFI_ERR(3)

#define EFI_VAR_NV_BS_RT    7u           /* non-volatile, boot service and runtime access */

/* EFI_GLOBAL_VARIABLE (8BE4DF61-93CA-11D2-AA0D-00E098032B8C), as stored */
extern const UINT8 EFI_GLOBAL_GUID[16];

/* Remember the runtime services table and the regions it needs.  Call
 * once, at boot, after the memory manager is up. */
void EfiBootInfo(const BootInfo *info);

/* Whether the firmware's variable services can be called (a UEFI boot
 * with a bootloader that hands them over) */
bool EfiRuntimeAvailable(void);

/* GetVariable / SetVariable: an EFI_STATUS (EFI_OK, or EFI_ERR(n));
 * @name is a NUL-terminated UTF-16 string, @vendor a GUID as stored */
UINT64 EfiGetVariable(const UINT16 *name, const UINT8 vendor[16], UINT32 *attrs, UINT64 *size, void *data);
UINT64 EfiSetVariable(const UINT16 *name, const UINT8 vendor[16], UINT32 attrs, UINT64 size, const void *data);
