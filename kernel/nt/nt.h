/*
 * nt.h — Windows kernel-mode drivers in NovaOS (the export layer's own API)
 *
 * kernel/nt/ hosts real Windows .sys drivers (docs/design/windows-drivers.md):
 *
 *   ldr.c       loads a driver's PE32+ image into the driver window, binds
 *               its imports against the export tables, calls DriverEntry,
 *               and unloads it again (NtLoadDriver, NtUnloadDriver)
 *   ci.c        code integrity: the signing policy a driver image must meet
 *               (an embedded or installed-catalog signature; Microsoft's
 *               unless test signing is on), checked before it loads
 *   exports.h   the ntoskrnl.exe and hal.dll exports a driver can link
 *               against: the one list the tables are made from
 *   exports.c   the tables made from that list
 *   ntoskrnl.c  those exports (Microsoft x64 calling convention): debug
 *               output, the run-time library, hal.dll
 *   ke.c        the kernel's: IRQL, DPCs, spin locks, events, semaphores,
 *               timers, fast mutexes, time, KeBugCheckEx
 *   ex.c        the pool (ExAllocatePool*)
 *   mm.c        MDLs, physical addresses, MmGetSystemRoutineAddress
 *   io.c        Windows devices and their names, device stacks, IRPs: what
 *               a program's NtCreateFile, NtReadFile, NtWriteFile,
 *               NtDeviceIoControlFile and NtClose on a Windows device turn into
 *   pnp.c       the Plug and Play manager: a PCI function's PDO, the
 *               driver installed on it (AddDevice, IRP_MN_START_DEVICE with
 *               its resources, removal), device interfaces and properties
 *   intr.c      device interrupts (IoConnectInterrupt(Ex)) at DIRQL
 *   dma.c       device registers (MmMapIoSpace), contiguous memory and DMA
 *               adapters (IoGetDmaAdapter)
 *   storport.c  storport.sys: the Storport host storage miniports run on,
 *               each disk it finds handed to the block layer (storport.h:
 *               the Storport structures)
 *   wdm.h       the structures drivers see, in Windows' x64 layout
 *
 * Program-facing glue (the system calls and handles) is in um/um_driver.c.
 */
#pragma once

#include "../include/types.h"
#include "wdm.h"

typedef struct NtDriver NtDriver;

struct NtDriver {
    NtDriver         *next;
    char              service[64];      /* its service key's name */
    char              image[64];        /* the image's file name (wdmtest.sys) */
    char              path[260];        /* where it was loaded from (C:\...) */
    UINT64            base;             /* where the image is mapped */
    UINT32            size;             /* SizeOfImage */
    int               slot, slots;      /* its place in the driver window */
    NT_DRIVER_OBJECT *obj;
    NT_DRIVER_EXTENSION ext;
    NT_UNICODE_STRING reg_path;         /* \Registry\Machine\System\CurrentControlSet\Services\<name> */
    int               opens;            /* file objects open on its devices */
    bool              unload_pending;   /* NtUnloadDriver came while files were open */
    bool              started;          /* DriverEntry succeeded */
    bool              dll;              /* an export driver, loaded for its exports (WdfLdr.sys): no driver object */
};

/* ke.c: set the processor-count and debugger data exports once the CPUs
 * are up; io.c: start the driver work-item worker */
void      NtKeInit(void);
void      NtIoWorkerInit(void);

/* ldr.c */
void      NtDrvInit(void);
/* The loaded driver of service @service, or NULL */
NtDriver *NtDrvFind(const char *service);
/* Read the boot options (test signing) once the registry is loaded */
void      NtDrvBootOptions(void);
/* Load the driver whose service key is @service (its name) and call its
 * DriverEntry: STATUS_IMAGE_ALREADY_LOADED, STATUS_OBJECT_NAME_NOT_FOUND
 * (no such service or file), STATUS_INVALID_IMAGE_FORMAT,
 * STATUS_PROCEDURE_NOT_FOUND (an import NovaOS lacks: logged by name), or
 * what DriverEntry returned */
NTSTATUS  NtDrvLoad(const char *service);
/* Call its DriverUnload and unmap it (later, when the last file open on
 * its devices closes): STATUS_OBJECT_NAME_NOT_FOUND when it is not loaded,
 * STATUS_INVALID_DEVICE_REQUEST when it has no DriverUnload */
NTSTATUS  NtDrvUnload(const char *service);
/* The loaded drivers, for SystemModuleInformation: up to @max, their
 * bases, sizes and files (C:\...) (count returned) */
int       NtDrvModules(UINT64 *base, UINT32 *size, char (*path)[260], int max);
/* The last file open on @drv's devices closed */
void      NtDrvLastClose(NtDriver *drv);
/* The loaded driver whose image holds @addr, or NULL */
NtDriver *NtDrvByAddress(UINT64 addr);

/* ex.c: @drv unloads: log the pool blocks it never freed */
void      NtPoolDriverGone(NtDriver *drv);
/* ex.c: a new random (version 4) UUID, as ExUuidCreate makes */
NTSTATUS  NtExUuid(UINT8 out[16]);

/* ps.c: ZwClose of a system-thread handle drops its reference (true when
 * @handle was one) */
bool      NtPsHandleClose(void *handle);

/* exports.c: an export by module and name, or 0 */
UINT64    NtExport(const char *module, const char *name);

/* io.c */
void      NtIoInit(void);
/* The driver that owns @obj (one NovaOS loaded) */
NtDriver *NtIoDriverOf(NT_DRIVER_OBJECT *obj);
void      NtIoAddDriver(NtDriver *drv);
void      NtIoRemoveDriver(NtDriver *drv);
/* Whether @path (an NT path without its \??\ or \\.\ prefix) names a
 * Windows device: \Device\<name>[\...], or a link a driver made in
 * \DosDevices (<link>[\...]) */
bool      NtIoIsDevice(const char *path);
/* The name in \Device of @d ("\Device\0000004a"); false if it has none */
bool      NtIoDeviceName(NT_DEVICE_OBJECT *d, char *out, int cap);
/* Open it: IRP_MJ_CREATE; *@out the file object */
NTSTATUS  NtIoOpen(const char *path, UINT32 access, UINT32 share, UINT32 disposition, UINT32 options,
                   NT_FILE_OBJECT **out);
/* Read, write or send a control code: the caller's buffers are user
 * addresses of the current process.  *@info the bytes transferred. */
NTSTATUS  NtIoRead(NT_FILE_OBJECT *fo, UINT64 buf, UINT32 len, const INT64 *offset, UINT64 *info);
NTSTATUS  NtIoWrite(NT_FILE_OBJECT *fo, UINT64 buf, UINT32 len, const INT64 *offset, UINT64 *info);
NTSTATUS  NtIoControl(NT_FILE_OBJECT *fo, UINT32 code, UINT64 in, UINT32 in_len, UINT64 out, UINT32 out_len,
                      UINT64 *info);
/* The handle's last close: IRP_MJ_CLEANUP, IRP_MJ_CLOSE, and the file object goes */
void      NtIoClose(NT_FILE_OBJECT *fo);
/* Remove @drv's devices and links that its DriverUnload left behind */
void      NtIoDropDevices(NtDriver *drv);

/* pnp.c: start the Windows driver installed on device node @node (its
 * Enum key's Service): 0, or the CM_PROB_* problem it has now (28: no
 * driver installed) */
UINT32    NtPnpStart(UINT32 node);
/* Remove a started device's driver (query-remove, remove): it is disabled
 * (problem 22) until NtPnpStart; a driver's veto comes back as its status */
NTSTATUS  NtPnpStop(UINT32 node);
/* At boot, once the registry is loaded: start the installed drivers */
void      NtPnpBoot(void);
/* The interfaces node @node's driver turned on (at most @max) */
int       NtPnpInterfaces(UINT32 node, NT_GUID *out, int max);
/* The translated interrupt the PnP manager gave PDO @pdo */
bool      NtPnpInterruptOf(NT_DEVICE_OBJECT *pdo, UINT32 *vector, UINT8 *irql);

/* PDO @pdo's PCI configuration space (bytes read or written), and its
 * device instance (PCI\VEN_...), for ndis.c and storport.c */
UINT32    NtPnpConfig(NT_DEVICE_OBJECT *pdo, bool write, void *buf, UINT32 off, UINT32 len);
const char *NtPnpInstanceOf(NT_DEVICE_OBJECT *pdo);

/* intr.c: a vector of its own for GSI @gsi, routed (masked until a driver
 * connects to it) to the boot CPU; 0 if it cannot be routed */
UINT8     NtIntrVector(UINT32 gsi, bool level, bool low);

/* mm.c: describe @m's pages (of the calling program's buffer when @user)
 * for an access by a driver (@op IoReadAccess 0, IoWriteAccess 1,
 * IoModifyAccess 2): STATUS_ACCESS_VIOLATION when a page cannot be used */
NTSTATUS  NtMdlLock(NT_MDL *m, bool user, int op);
