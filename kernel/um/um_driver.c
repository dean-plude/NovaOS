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
 * On a handle opened for synchronous I/O the call waits for its IRP.  On
 * an overlapped one (CreateFile's FILE_FLAG_OVERLAPPED) an IRP the
 * driver leaves pending makes the call return STATUS_PENDING, as on
 * Windows: when it completes, the kernel's I/O completion thread writes
 * the data, the I/O status block (the OVERLAPPED's Internal and
 * InternalHigh) and sets the event, and NtCancelIoFile(Ex) cancels it.
 */
#include "um_internal.h"
#include "../nt/nt.h"
#include "../io/pnp.h"
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

/* An overlapped request left pending: who finishes it, and where */
typedef struct {
    UmProcess *proc;
    UINT64     iosb;                    /* bit 63: the 32-bit layout */
    UmObject  *event;                   /* referenced, or NULL */
} Pending;

static bool pending_copy(void *ctx, UINT64 dst, const void *src, UINT64 n)
{
    return um_write(((Pending *)ctx)->proc, dst, src, n);
}

static void pending_done(void *ctx, NTSTATUS st, UINT64 info, bool gone)
{
    Pending *pd = ctx;
    if (!gone && pd->iosb) {
        if (pd->iosb >> 63) {
            UINT32 b[2] = { (UINT32)st, (UINT32)info };
            um_write(pd->proc, pd->iosb & ~(UINT64_C(1) << 63), b, sizeof(b));
        } else {
            UINT64 b[2] = { (UINT32)st, info };
            um_write(pd->proc, pd->iosb, b, sizeof(b));
        }
    }
    if (pd->event) {
        if (!gone) {
            IrqState s = ob_lock();
            pd->event->signaled = true;
            um_ob_wake_boost(pd->event, BOOST_DISK);
            ob_unlock(s);
        }
        um_ob_unref(pd->event);
    }
    kfree(pd);
}

/* What an overlapped request on @o needs, or NULL (a synchronous handle,
 * or no memory: then it waits); the event is reset, as NtReadFile does */
static Pending *pending_new(UmObject *o, UINT64 event, UINT64 iosb, NtIoAsync *as)
{
    if (((DeviceFile *)o)->fo->Flags & NT_FO_SYNCHRONOUS_IO) return NULL;
    Pending *pd = kzalloc(sizeof(*pd));
    if (!pd) return NULL;
    pd->proc = UmCurrent();
    pd->iosb = iosb;
    pd->event = event ? um_handle_object(pd->proc, event, UO_EVENT) : NULL;
    if (pd->event) { IrqState s = ob_lock(); pd->event->signaled = false; ob_unlock(s); }
    *as = (NtIoAsync){ pending_copy, pending_done, pd, pd->proc, iosb & ~(UINT64_C(1) << 63) };
    return pd;
}

/* (finished at once: the caller sets the event and the status block) */
static UINT32 pending_end(Pending *pd, NTSTATUS st)
{
    if (st == STATUS_PENDING || !pd) return (UINT32)st;
    if (pd->event) um_ob_unref(pd->event);
    kfree(pd);
    return (UINT32)st;
}

/* NtReadFile / NtWriteFile on a device handle; @offset_ptr the caller's
 * PLARGE_INTEGER ByteOffset (or 0); STATUS_PENDING: see above */
UINT32 um_driver_rw(UmObject *o, bool write, UINT64 buf, UINT32 len, UINT64 offset_ptr, UINT64 event, UINT64 iosb,
                    UINT64 *info)
{
    NT_FILE_OBJECT *fo = ((DeviceFile *)o)->fo;
    INT64 off;
    const INT64 *at = NULL;
    *info = 0;
    if (offset_ptr) {
        if (!NT_SUCCESS(CopyFromUser(&off, (const void *)(uintptr_t)offset_ptr, 8))) return ST_ACCESS_VIOLATION;
        if (off != -2 && off != -1) at = &off;           /* (FILE_USE_FILE_POINTER_POSITION, FILE_WRITE_TO_END_OF_FILE) */
    }
    NtIoAsync as;
    Pending *pd = pending_new(o, event, iosb, &as);
    NTSTATUS st = write ? NtIoWrite(fo, buf, len, at, info, pd ? &as : NULL) : NtIoRead(fo, buf, len, at, info, pd ? &as : NULL);
    return pending_end(pd, st);
}

UINT32 um_driver_ioctl(UmObject *o, UINT32 code, UINT64 in, UINT32 in_len, UINT64 out, UINT32 out_len, UINT64 event,
                       UINT64 iosb, UINT64 *info)
{
    NtIoAsync as;
    Pending *pd = pending_new(o, event, iosb, &as);
    return pending_end(pd, NtIoControl(((DeviceFile *)o)->fo, code, in, in_len, out, out_len, info, pd ? &as : NULL));
}

/* NtCancelIoFile(Ex): the calling process's pending requests on the
 * handle (@iosb: only that one) */
UINT32 um_driver_cancel(UmObject *o, UINT64 iosb)
{
    int n = NtIoCancel(((DeviceFile *)o)->fo, UmCurrent(), iosb & ~(UINT64_C(1) << 63));
    return n || !iosb ? 0 : 0xC0000225u;                  /* STATUS_NOT_FOUND */
}

/* A process is going away: its pending requests finish without it */
void um_driver_process_gone(UmProcess *p)
{
    NtIoOwnerGone(p);
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
    return st ? st : (UINT32)NtDrvLoadQuiet(name);
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
 *   7  register a root-enumerated device (setupapi's DIF_REGISTERDEVICE):
 *      @ptr a node (its instance ROOT\<class>\NNNN, hardware and compatible
 *      IDs, description); its node ID, 0, or -1 without the privilege
 *   8  remove root-enumerated device @id (DIF_REMOVE): the NTSTATUS
 * 4, 5, 7 and 8 need SeLoadDriverPrivilege (an administrator), as on Windows. */
UINT64 UmPnpDriverCtl(UINT32 op, UINT32 id, UINT64 ptr)
{
    if (op == 3) {
        NT_GUID g[8];
        int n = NtPnpInterfaces(id, g, 8);
        if (n && !NT_SUCCESS(CopyToUser((void *)(uintptr_t)ptr, g, sizeof(NT_GUID) * (size_t)n))) return 0;
        return (UINT64)n;
    }
    if (!um_privilege_held(SE_LOAD_DRIVER_PRIVILEGE)) return op == 4 || op == 7 ? (UINT64)-1 : STATUS_PRIVILEGE_NOT_HELD;
    if (op == 4) return NtPnpStart(id);
    if (op == 5) return (UINT32)NtPnpStop(id);
    if (op == 7) {
        PnpNode *n = kmalloc(sizeof(PnpNode));
        UINT32 r = 0;
        if (n && NT_SUCCESS(CopyFromUser(n, (const void *)(uintptr_t)ptr, sizeof(*n)))) {
            n->instance[sizeof(n->instance) - 1] = n->desc[sizeof(n->desc) - 1] = 0;
            n->hwids[sizeof(n->hwids) - 2] = n->hwids[sizeof(n->hwids) - 1] = 0;
            n->compat[sizeof(n->compat) - 2] = n->compat[sizeof(n->compat) - 1] = 0;
            n->cls[sizeof(n->cls) - 1] = 0;
            r = NtPnpRootAdd(n->instance, n->cls, n->hwids, n->compat, n->desc);
        }
        kfree(n);
        return r;
    }
    if (op == 8) return (UINT32)NtPnpRootRemove(id);
    return 0;
}

void um_driver_syscalls_init(void)
{
    NtDrvInit();
    um_install(SYSCALL_NtLoadDriver,   sys_load_driver);     /* (under the big kernel lock) */
    um_install(SYSCALL_NtUnloadDriver, sys_unload_driver);
}
