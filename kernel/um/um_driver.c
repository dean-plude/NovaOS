/*
 * um_driver.c — Windows kernel-mode drivers as programs see them
 *
 * NtLoadDriver and NtUnloadDriver (what the service control manager calls
 * to start and stop a service of type SERVICE_KERNEL_DRIVER) take the
 * service's registry path, \Registry\Machine\System\CurrentControlSet\
 * Services\<name>, and need SeLoadDriverPrivilege, as on Windows.
 *
 * A handle a program opens on a Windows driver's device (\\.\<link> or
 * \Device\<name>, kernel/nt/io.c) is a UO_DEVICE object holding the
 * driver's FILE_OBJECT; NtReadFile, NtWriteFile and NtDeviceIoControlFile
 * on it become IRPs, and its last close IRP_MJ_CLEANUP and IRP_MJ_CLOSE.
 * The requests finish before the call returns (overlapped handles too,
 * whose event is then set at once).
 */
#include "um_internal.h"
#include "../nt/nt.h"
#include "../ke/probe.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

#define ST_OBJECT_NAME_INVALID  0xC0000033u
#define ST_ACCESS_VIOLATION     0xC0000005u
#define ST_NO_MEMORY            0xC0000017u
#define SE_LOAD_DRIVER_PRIVILEGE 10

typedef struct {
    UmObject        ob;
    NT_FILE_OBJECT *fo;
} DeviceFile;

static void device_destroy(UmObject *o)
{
    NtIoClose(((DeviceFile *)o)->fo);
}

bool um_driver_name(const char *path)
{
    return NtIoIsDevice(path);
}

UINT32 um_driver_open(const char *path, UINT32 access, UINT32 share, UINT32 disposition, UINT32 options, UmObject **out)
{
    DeviceFile *f = kzalloc(sizeof(*f));
    if (!f) return ST_NO_MEMORY;
    NTSTATUS st = NtIoOpen(path, access, share, disposition, options, &f->fo);
    if (!NT_SUCCESS(st)) { kfree(f); return (UINT32)st; }
    f->ob.type = UO_DEVICE;
    f->ob.refs = 1;
    f->ob.signaled = true;                      /* (waiting on it returns at once, as on a file) */
    f->ob.destroy = device_destroy;
    *out = &f->ob;
    return 0;
}

/* NtReadFile / NtWriteFile on a device handle; @offset_ptr the caller's
 * PLARGE_INTEGER ByteOffset (or 0) */
UINT32 um_driver_rw(UmObject *o, bool write, UINT64 buf, UINT32 len, UINT64 offset_ptr, UINT64 *info)
{
    NT_FILE_OBJECT *fo = ((DeviceFile *)o)->fo;
    INT64 off;
    const INT64 *at = NULL;
    *info = 0;
    if (offset_ptr) {
        if (!NT_SUCCESS(CopyFromUser(&off, (const void *)(uintptr_t)offset_ptr, 8))) return ST_ACCESS_VIOLATION;
        if (off != -2 && off != -1) at = &off;           /* (FILE_USE_FILE_POINTER_POSITION, FILE_WRITE_TO_END_OF_FILE) */
    }
    return (UINT32)(write ? NtIoWrite(fo, buf, len, at, info) : NtIoRead(fo, buf, len, at, info));
}

UINT32 um_driver_ioctl(UmObject *o, UINT32 code, UINT64 in, UINT32 in_len, UINT64 out, UINT32 out_len, UINT64 *info)
{
    return (UINT32)NtIoControl(((DeviceFile *)o)->fo, code, in, in_len, out, out_len, info);
}

/* The service name in a driver's registry path (a user UNICODE_STRING) */
static UINT32 service_of(UINT64 us, char *out, int cap)
{
    UINT64 u[2];
    if (!us || !NT_SUCCESS(CopyFromUser(u, (const void *)(uintptr_t)us, sizeof(u)))) return ST_ACCESS_VIOLATION;
    UINT32 n = (UINT32)(u[0] & 0xFFFF) / 2;
    UINT16 w[260];
    char path[260];
    if (!n || n >= 260) return ST_OBJECT_NAME_INVALID;
    if (!NT_SUCCESS(CopyFromUser(w, (const void *)(uintptr_t)u[1], 2 * (UINT64)n))) return ST_ACCESS_VIOLATION;
    for (UINT32 i = 0; i < n; i++) path[i] = w[i] < 0x80 && w[i] ? (char)w[i] : '?';
    path[n] = 0;
    static const char *const pre[] = { "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\",
                                       "\\Registry\\Machine\\System\\ControlSet001\\Services\\" };
    for (unsigned k = 0; k < sizeof(pre) / sizeof(pre[0]); k++) {
        size_t len = strlen(pre[k]), i = 0;
        while (i < len && (path[i] | 0x20) == (pre[k][i] | 0x20)) i++;
        if (i < len) continue;
        const char *s = path + len;
        if (!*s || strchr(s, '\\') || (int)strlen(s) >= cap) return ST_OBJECT_NAME_INVALID;
        strcpy(out, s);
        return 0;
    }
    return ST_OBJECT_NAME_INVALID;
}

/* NtLoadDriver(PUNICODE_STRING DriverServiceName) */
static UINT64 sys_load_driver(UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4)
{
    (void)a2; (void)a3; (void)a4;
    if (!um_privilege_held(SE_LOAD_DRIVER_PRIVILEGE)) return STATUS_PRIVILEGE_NOT_HELD;
    char name[64];
    UINT32 st = service_of(a1, name, sizeof(name));
    return st ? st : (UINT32)NtDrvLoad(name);
}

/* NtUnloadDriver(PUNICODE_STRING DriverServiceName) */
static UINT64 sys_unload_driver(UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4)
{
    (void)a2; (void)a3; (void)a4;
    if (!um_privilege_held(SE_LOAD_DRIVER_PRIVILEGE)) return STATUS_PRIVILEGE_NOT_HELD;
    char name[64];
    UINT32 st = service_of(a1, name, sizeof(name));
    return st ? st : (UINT32)NtDrvUnload(name);
}

/* CTL_PNP's operations on Windows drivers (um_gui.c's pnp_ctl hands them
 * here), what cfgmgr32's CM_Setup_DevNode, CM_Disable_DevNode and
 * CM_Get_Device_Interface_List do on Windows:
 *   3  the interfaces device node @id's driver turned on: GUIDs to @ptr
 *      (8 at most), how many
 *   4  start the driver installed on node @id: 0, or its problem code
 *   5  remove its driver (query-remove, remove): the NTSTATUS
 * 4 and 5 need SeLoadDriverPrivilege (an administrator), as on Windows. */
UINT64 UmPnpDriverCtl(UINT32 op, UINT32 id, UINT64 ptr)
{
    if (op == 3) {
        NT_GUID g[8];
        int n = NtPnpInterfaces(id, g, 8);
        if (n && !NT_SUCCESS(CopyToUser((void *)(uintptr_t)ptr, g, sizeof(NT_GUID) * (size_t)n))) return 0;
        return (UINT64)n;
    }
    if (!um_privilege_held(SE_LOAD_DRIVER_PRIVILEGE)) return op == 4 ? (UINT64)-1 : STATUS_PRIVILEGE_NOT_HELD;
    if (op == 4) return NtPnpStart(id);
    if (op == 5) return (UINT32)NtPnpStop(id);
    return 0;
}

void um_driver_syscalls_init(void)
{
    NtDrvInit();
    um_install(SYSCALL_NtLoadDriver,   sys_load_driver);     /* (under the big kernel lock) */
    um_install(SYSCALL_NtUnloadDriver, sys_unload_driver);
}
