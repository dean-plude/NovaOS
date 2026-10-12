/*
 * io.c — Windows devices, their names, and the IRPs programs' I/O becomes
 *
 * A driver names its devices in \Device (IoCreateDevice) and gives
 * programs a name for one in \DosDevices (IoCreateSymbolicLink), which
 * is what CreateFile("\\\\.\\Name") opens.  Both live here, apart from
 * NovaOS's own devices (io/io.c): a name is either a device of a loaded
 * Windows driver or a link to one.
 *
 * Each open is a Windows FILE_OBJECT; NtCreateFile, NtReadFile,
 * NtWriteFile, NtDeviceIoControlFile and the handle's last close build
 * the IRP Windows builds for them (IRP_MJ_CREATE, READ, WRITE,
 * DEVICE_CONTROL, CLEANUP then CLOSE) with a stack location for each
 * device in the stack, and wait for it to complete: buffered I/O through
 * a kernel copy of the caller's buffer (AssociatedIrp.SystemBuffer),
 * neither I/O on the caller's own buffers.  Direct I/O needs MDLs, which
 * come with step 30.2; until then such a request fails with
 * STATUS_NOT_SUPPORTED and a log line.
 *
 * IofCompleteRequest runs the stack's completion routines from the
 * bottom up, as Windows does, then copies the status to the requester's
 * block and signals its event.
 */
#include "nt.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../ke/probe.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

#define NAME_MAX_  128
#define MAX_DRIVERS 32

/* IRP.Flags */
#define IRP_SYNCHRONOUS_API     0x00000004
#define IRP_BUFFERED_IO         0x00000010
#define IRP_DEALLOCATE_BUFFER   0x00000020
#define IRP_INPUT_OPERATION     0x00000040
#define IRP_CREATE_OPERATION    0x00000080
#define IRP_READ_OPERATION      0x00000100
#define IRP_WRITE_OPERATION     0x00000200
#define IRP_CLOSE_OPERATION     0x00000400
/* DEVOBJ_EXTENSION.ExtensionFlags */
#define DOE_DELETE_PENDING      0x00000002

#define ST_NO_SUCH_DEVICE       ((NTSTATUS)0xC000000E)
#define ST_DELETE_PENDING       ((NTSTATUS)0xC0000056)
#define ST_ACCESS_DENIED        ((NTSTATUS)0xC0000022)

/* DEVOBJ_EXTENSION, as far as Windows' layout goes that drivers look at */
typedef struct {
    short             Type, Size;
    NT_DEVICE_OBJECT *DeviceObject;
    unsigned int      PowerFlags;
    void             *Dope;
    unsigned int      ExtensionFlags;
    void             *DeviceNode, *AttachedTo;
    UINT8             reserved[0x40];
} NtDevObjExt;

/* A name in \Device (dev set) or \DosDevices (target set) */
typedef struct NtName {
    struct NtName    *next;
    char              name[NAME_MAX_];   /* "\Device\WdmTest", or a link's bare name "WdmTest" */
    NT_DEVICE_OBJECT *dev;
    char              target[NAME_MAX_];
} NtName;

static KSpinLock g_lock = KSPINLOCK_INIT;
static NtName   *g_names;
static NtDriver *g_drv[MAX_DRIVERS];

void NtIoInit(void) {}

static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static bool eq_ci(const char *a, const char *b)
{
    while (*a && *b && lc(*a) == lc(*b)) { a++; b++; }
    return !*a && !*b;
}

/* @s starts with @pre (case-insensitively): what follows it, else NULL */
static const char *after(const char *s, const char *pre)
{
    while (*pre && lc(*s) == lc(*pre)) { s++; pre++; }
    return *pre ? NULL : s;
}

/* An UNICODE_STRING as ASCII; false if it does not fit */
static bool ascii(const NT_UNICODE_STRING *u, char *out, int cap)
{
    if (!u || !u->Buffer || u->Length / 2 >= (unsigned)cap) return false;
    int n = u->Length / 2;
    for (int i = 0; i < n; i++) out[i] = u->Buffer[i] < 0x80 && u->Buffer[i] ? (char)u->Buffer[i] : '?';
    out[n] = 0;
    return true;
}

/* A link's name without its namespace: \DosDevices\X, \??\X,
 * \GLOBAL??\X, each also with Global\ */
static const char *link_name(const char *s)
{
    static const char *const pre[] = { "\\DosDevices\\", "\\??\\", "\\GLOBAL??\\" };
    for (unsigned i = 0; i < sizeof(pre) / sizeof(pre[0]); i++) {
        const char *r = after(s, pre[i]);
        if (r) {
            const char *g = after(r, "Global\\");
            return g ? g : r;
        }
    }
    return NULL;
}

/* (g_lock held) */
static NtName *find_name(const char *name, bool link)
{
    for (NtName *n = g_names; n; n = n->next)
        if (!n->dev == link && eq_ci(n->name, name)) return n;
    return NULL;
}

/* The device @path names (g_lock held); *@rest what follows its name */
static NT_DEVICE_OBJECT *resolve(const char *path, const char **rest)
{
    char tmp[2 * NAME_MAX_];
    if (!after(path, "\\Device\\")) {                      /* a link: <name>[\...] */
        const char *slash = path;
        while (*slash && *slash != '\\') slash++;
        int n = (int)(slash - path);
        if (!n || n >= NAME_MAX_) return NULL;
        memcpy(tmp, path, (size_t)n);
        tmp[n] = 0;
        NtName *l = find_name(tmp, true);
        if (!l) return NULL;
        ksnprintf(tmp, sizeof(tmp), "%s%s", l->target, slash);
        path = tmp;
        if (!after(path, "\\Device\\")) return NULL;
    }
    NtName *best = NULL;
    size_t blen = 0;
    for (NtName *n = g_names; n; n = n->next) {
        if (!n->dev) continue;
        const char *r = after(path, n->name);
        size_t len = strlen(n->name);
        if (r && (!*r || *r == '\\') && len > blen) { best = n; blen = len; }
    }
    if (!best) return NULL;
    if (rest) *rest = path + blen;
    return best->dev;
}

bool NtIoIsDevice(const char *path)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    bool yes = resolve(path, NULL) != NULL;
    spin_unlock_irqrestore(&g_lock, s);
    return yes;
}

/* -----------------------------------------------------------------------
 * Drivers
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS nt_IopInvalidDeviceRequest(NT_DEVICE_OBJECT *dev, NT_IRP *irp);

void NtIoAddDriver(NtDriver *drv)
{
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) drv->obj->MajorFunction[i] = nt_IopInvalidDeviceRequest;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < MAX_DRIVERS; i++) if (!g_drv[i]) { g_drv[i] = drv; break; }
    spin_unlock_irqrestore(&g_lock, s);
}

void NtIoRemoveDriver(NtDriver *drv)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < MAX_DRIVERS; i++) if (g_drv[i] == drv) g_drv[i] = NULL;
    spin_unlock_irqrestore(&g_lock, s);
}

NtDriver *NtIoDriverOf(NT_DRIVER_OBJECT *obj)
{
    for (int i = 0; i < MAX_DRIVERS; i++) if (g_drv[i] && g_drv[i]->obj == obj) return g_drv[i];
    return NULL;
}

/* -----------------------------------------------------------------------
 * Devices and links (exports)
 * ----------------------------------------------------------------------- */
NT_API NT_STATUS nt_IoCreateDevice(NT_DRIVER_OBJECT *drv, unsigned int ext_size, NT_UNICODE_STRING *name,
                                   unsigned int type, unsigned int characteristics, unsigned char exclusive,
                                   NT_DEVICE_OBJECT **out)
{
    char nm[NAME_MAX_] = "";
    if (name && name->Length && (!ascii(name, nm, sizeof(nm)) || !after(nm, "\\Device\\")))
        return NT_STATUS_OBJECT_NAME_INVALID;
    size_t head = ALIGN_UP(sizeof(NT_DEVICE_OBJECT), 16);
    NT_DEVICE_OBJECT *d = kzalloc(head + ext_size);
    NtDevObjExt *x = kzalloc(sizeof(*x));
    NtName *n = nm[0] ? kzalloc(sizeof(*n)) : NULL;
    if (!d || !x || (nm[0] && !n)) { kfree(d); kfree(x); kfree(n); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    d->Type = 3;
    d->Size = (unsigned short)(head + ext_size);
    d->DriverObject = drv;
    d->Flags = NT_DO_DEVICE_INITIALIZING | (exclusive ? NT_DO_EXCLUSIVE : 0);
    d->Characteristics = characteristics;
    d->DeviceType = type;
    d->StackSize = 1;
    d->SectorSize = 512;
    d->DeviceExtension = ext_size ? (UINT8 *)d + head : NULL;
    d->DeviceLock.Header.Type = 1;                       /* SynchronizationEvent, signaled */
    d->DeviceLock.Header.Size = 6;
    d->DeviceLock.Header.SignalState = 1;
    d->DeviceLock.Header.WaitListHead.Flink = d->DeviceLock.Header.WaitListHead.Blink = &d->DeviceLock.Header.WaitListHead;
    d->Queue.ListEntry.Flink = d->Queue.ListEntry.Blink = &d->Queue.ListEntry;
    x->Type = 13;
    x->Size = (short)sizeof(*x);
    x->DeviceObject = d;
    d->DeviceObjectExtension = x;
    IrqState s = spin_lock_irqsave(&g_lock);
    if (n && find_name(nm, false)) {
        spin_unlock_irqrestore(&g_lock, s);
        kfree(d); kfree(x); kfree(n);
        return NT_STATUS_OBJECT_NAME_COLLISION;
    }
    if (n) {
        strcpy(n->name, nm);
        n->dev = d;
        n->next = g_names;
        g_names = n;
    }
    d->NextDevice = drv->DeviceObject;
    drv->DeviceObject = d;
    spin_unlock_irqrestore(&g_lock, s);
    *out = d;
    return NT_STATUS_SUCCESS;
}

static void free_device(NT_DEVICE_OBJECT *d)
{
    kfree(d->DeviceObjectExtension);
    kfree(d);
}

NT_API void nt_IoDeleteDevice(NT_DEVICE_OBJECT *d)
{
    if (!d) return;
    IrqState s = spin_lock_irqsave(&g_lock);
    for (NtName **pp = &g_names; *pp;) {
        NtName *n = *pp;
        if (n->dev == d) { *pp = n->next; kfree(n); }
        else pp = &n->next;
    }
    for (NT_DEVICE_OBJECT **pp = &d->DriverObject->DeviceObject; *pp; pp = &(*pp)->NextDevice)
        if (*pp == d) { *pp = d->NextDevice; break; }
    bool open = d->ReferenceCount > 0;
    if (open) ((NtDevObjExt *)d->DeviceObjectExtension)->ExtensionFlags |= DOE_DELETE_PENDING;   /* (freed at the last close) */
    spin_unlock_irqrestore(&g_lock, s);
    if (!open) free_device(d);
}

NT_API NT_STATUS nt_IoCreateSymbolicLink(NT_UNICODE_STRING *link, NT_UNICODE_STRING *target)
{
    char l[NAME_MAX_], t[NAME_MAX_];
    if (!ascii(link, l, sizeof(l)) || !ascii(target, t, sizeof(t))) return NT_STATUS_OBJECT_NAME_INVALID;
    const char *bare = link_name(l);
    if (!bare || !*bare) return NT_STATUS_OBJECT_NAME_INVALID;   /* (only \DosDevices links so far) */
    NtName *n = kzalloc(sizeof(*n));
    if (!n) return NT_STATUS_INSUFFICIENT_RESOURCES;
    strcpy(n->name, bare);
    strcpy(n->target, t);
    IrqState s = spin_lock_irqsave(&g_lock);
    if (find_name(bare, true)) {
        spin_unlock_irqrestore(&g_lock, s);
        kfree(n);
        return NT_STATUS_OBJECT_NAME_COLLISION;
    }
    n->next = g_names;
    g_names = n;
    spin_unlock_irqrestore(&g_lock, s);
    return NT_STATUS_SUCCESS;
}

NT_API NT_STATUS nt_IoDeleteSymbolicLink(NT_UNICODE_STRING *link)
{
    char l[NAME_MAX_];
    const char *bare = ascii(link, l, sizeof(l)) ? link_name(l) : NULL;
    if (!bare) return NT_STATUS_OBJECT_NAME_INVALID;
    IrqState s = spin_lock_irqsave(&g_lock);
    NtName *found = NULL;
    for (NtName **pp = &g_names; *pp; pp = &(*pp)->next)
        if (!(*pp)->dev && eq_ci((*pp)->name, bare)) { found = *pp; *pp = found->next; break; }
    spin_unlock_irqrestore(&g_lock, s);
    kfree(found);
    return found ? NT_STATUS_SUCCESS : NT_STATUS_OBJECT_NAME_NOT_FOUND;
}

void NtIoDropDevices(NtDriver *drv)
{
    /* links to its devices, then the devices themselves */
    IrqState s = spin_lock_irqsave(&g_lock);
    for (NtName **pp = &g_names; *pp;) {
        NtName *n = *pp;
        NT_DEVICE_OBJECT *d = n->dev ? NULL : resolve(n->target, NULL);
        if (d && d->DriverObject == drv->obj) {
            kprintf("[NT] %s: link %s left behind, removed\n", drv->image, n->name);
            *pp = n->next;
            kfree(n);
        } else pp = &n->next;
    }
    spin_unlock_irqrestore(&g_lock, s);
    while (drv->obj->DeviceObject) {
        kprintf("[NT] %s: device left behind, deleted\n", drv->image);
        nt_IoDeleteDevice(drv->obj->DeviceObject);
    }
}

/* -----------------------------------------------------------------------
 * IRPs (exports)
 * ----------------------------------------------------------------------- */

NT_API NT_STATUS nt_IofCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    if (--irp->CurrentLocation <= 0)
        KPANIC("NO_MORE_IRP_STACK_LOCATIONS (0x35): IRP %p passed below its last stack location", (void *)irp);
    NT_IO_STACK_LOCATION *sl = --irp->Tail.Overlay.CurrentStackLocation;
    sl->DeviceObject = dev;
    return dev->DriverObject->MajorFunction[sl->MajorFunction](dev, irp);
}

NT_API void nt_IofCompleteRequest(NT_IRP *irp, char boost)
{
    (void)boost;
    while (irp->CurrentLocation <= irp->StackCount) {
        NT_IO_STACK_LOCATION *sl = irp->Tail.Overlay.CurrentStackLocation;
        irp->CurrentLocation++;
        irp->Tail.Overlay.CurrentStackLocation++;
        irp->PendingReturned = sl->Control & NT_SL_PENDING_RETURNED;
        bool above = irp->CurrentLocation <= irp->StackCount;
        NT_DEVICE_OBJECT *up = above ? irp->Tail.Overlay.CurrentStackLocation->DeviceObject : NULL;
        NT_STATUS st = irp->IoStatus.Status;
        bool call = sl->CompletionRoutine &&
                    ((NT_SUCCESS_(st) && (sl->Control & NT_SL_INVOKE_ON_SUCCESS)) ||
                     (!NT_SUCCESS_(st) && (sl->Control & NT_SL_INVOKE_ON_ERROR)) ||
                     (irp->Cancel && (sl->Control & NT_SL_INVOKE_ON_CANCEL)));
        NT_IO_COMPLETION_ROUTINE routine = sl->CompletionRoutine;
        void *ctx = sl->Context;
        sl->CompletionRoutine = NULL;
        sl->Context = NULL;
        sl->Control = 0;
        if (call) {
            if (routine(up, irp, ctx) == NT_STATUS_MORE_PROCESSING_REQUIRED) return;   /* (its owner takes it back) */
        } else if (irp->PendingReturned && above) {
            irp->Tail.Overlay.CurrentStackLocation->Control |= NT_SL_PENDING_RETURNED;   /* (IoMarkIrpPending upward) */
        }
    }
    if (irp->UserIosb) *irp->UserIosb = irp->IoStatus;
    if (irp->UserEvent) __atomic_store_n(&irp->UserEvent->Header.SignalState, 1, __ATOMIC_RELEASE);
}

static NT_API NT_STATUS nt_IopInvalidDeviceRequest(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    (void)dev;
    irp->IoStatus.Status = NT_STATUS_INVALID_DEVICE_REQUEST;
    irp->IoStatus.Information = 0;
    nt_IofCompleteRequest(irp, 0);
    return NT_STATUS_INVALID_DEVICE_REQUEST;
}

/* -----------------------------------------------------------------------
 * The I/O manager's side: requests from programs
 * ----------------------------------------------------------------------- */
typedef struct {
    NT_IRP            *irp;
    NT_KEVENT          done;
    NT_IO_STATUS_BLOCK iosb;
} Req;

/* An IRP for @fo's device with its stack, the next location filled with @major */
static NT_IO_STACK_LOCATION *req_new(Req *r, NT_FILE_OBJECT *fo, UINT8 major, UINT32 flags)
{
    NT_DEVICE_OBJECT *dev = fo->DeviceObject;
    int n = dev->StackSize > 0 ? dev->StackSize : 1;
    memset(r, 0, sizeof(*r));
    r->irp = kzalloc(sizeof(NT_IRP) + (size_t)n * sizeof(NT_IO_STACK_LOCATION));
    if (!r->irp) return NULL;
    NT_IRP *irp = r->irp;
    irp->Type = 6;
    irp->Size = (unsigned short)(sizeof(NT_IRP) + (size_t)n * sizeof(NT_IO_STACK_LOCATION));
    irp->StackCount = (char)n;
    irp->CurrentLocation = (char)(n + 1);
    irp->Tail.Overlay.CurrentStackLocation = (NT_IO_STACK_LOCATION *)(irp + 1) + n;
    irp->Tail.Overlay.OriginalFileObject = fo;
    irp->RequestorMode = 1;                                      /* UserMode */
    irp->Flags = IRP_SYNCHRONOUS_API | flags;
    irp->UserIosb = &r->iosb;
    irp->UserEvent = &r->done;
    irp->ThreadListEntry.Flink = irp->ThreadListEntry.Blink = &irp->ThreadListEntry;
    NT_IO_STACK_LOCATION *sl = NtIrpNextStack(irp);
    sl->MajorFunction = major;
    sl->FileObject = fo;
    return sl;
}

/* Send it and wait for it to complete: its status */
static NTSTATUS req_send(Req *r, NT_FILE_OBJECT *fo)
{
    NT_STATUS st = nt_IofCallDriver(fo->DeviceObject, r->irp);
    if (st != NT_STATUS_PENDING && !__atomic_load_n(&r->done.Header.SignalState, __ATOMIC_ACQUIRE)) {
        NtDriver *drv = NtIoDriverOf(fo->DeviceObject->DriverObject);
        kprintf("[NT] %s: dispatch returned 0x%08x without completing its IRP\n", drv ? drv->image : "?", (UINT32)st);
        r->iosb.Status = st;
        r->irp = NULL;                                           /* (the driver may still hold it) */
        return st;
    }
    while (!__atomic_load_n(&r->done.Header.SignalState, __ATOMIC_ACQUIRE)) sched_yield();
    return r->iosb.Status;
}

static void req_free(Req *r)
{
    kfree(r->irp);
}

/* An open's device and driver references go; the last one lets a
 * driver whose unload waited go */
static void unref(NT_DEVICE_OBJECT *d)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    NtDriver *drv = NtIoDriverOf(d->DriverObject);
    bool gone = --d->ReferenceCount == 0 && (((NtDevObjExt *)d->DeviceObjectExtension)->ExtensionFlags & DOE_DELETE_PENDING);
    bool last = drv && --drv->opens == 0;
    spin_unlock_irqrestore(&g_lock, s);
    if (gone) free_device(d);
    if (last) NtDrvLastClose(drv);
}

static void release(NT_FILE_OBJECT *fo)
{
    NT_DEVICE_OBJECT *d = fo->DeviceObject;
    kfree(fo->FileName.Buffer);
    kfree(fo);
    unref(d);
}

NTSTATUS NtIoOpen(const char *path, UINT32 access, UINT32 share, UINT32 disposition, UINT32 options, NT_FILE_OBJECT **out)
{
    const char *rest = "";
    IrqState s = spin_lock_irqsave(&g_lock);
    NT_DEVICE_OBJECT *d = resolve(path, &rest);
    NtDriver *drv = d ? NtIoDriverOf(d->DriverObject) : NULL;
    NTSTATUS st = !d ? STATUS_OBJECT_NAME_NOT_FOUND : !drv || !drv->started ? ST_NO_SUCH_DEVICE :
                  drv->unload_pending || (((NtDevObjExt *)d->DeviceObjectExtension)->ExtensionFlags & DOE_DELETE_PENDING) ?
                      ST_DELETE_PENDING :
                  (d->Flags & NT_DO_EXCLUSIVE) && d->ReferenceCount ? ST_ACCESS_DENIED : STATUS_SUCCESS;
    if (!st) { d->ReferenceCount++; drv->opens++; }
    spin_unlock_irqrestore(&g_lock, s);
    if (st) return st;

    NT_FILE_OBJECT *fo = kzalloc(sizeof(*fo));
    size_t rl = strlen(rest);
    NT_WCHAR *fn = rl ? kzalloc(2 * (rl + 1)) : NULL;
    if (!fo || (rl && !fn)) {
        kfree(fn);
        kfree(fo);
        unref(d);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (size_t i = 0; i < rl; i++) fn[i] = (UINT8)rest[i];
    fo->Type = 5;
    fo->Size = (short)sizeof(*fo);
    fo->DeviceObject = d;
    fo->FileName.Buffer = fn;
    fo->FileName.Length = (unsigned short)(2 * rl);
    fo->FileName.MaximumLength = (unsigned short)(rl ? 2 * rl + 2 : 0);
    fo->ReadAccess = (access & (0x80000000u | 0x10000000u | 0x0001u)) != 0;   /* GENERIC_READ, GENERIC_ALL, FILE_READ_DATA */
    fo->WriteAccess = (access & (0x40000000u | 0x10000000u | 0x0002u | 0x0004u)) != 0;
    fo->SharedRead = (share & 1) != 0;
    fo->SharedWrite = (share & 2) != 0;
    fo->SharedDelete = (share & 4) != 0;
    if (options & 0x30) fo->Flags |= NT_FO_SYNCHRONOUS_IO;       /* FILE_SYNCHRONOUS_IO_(NON)ALERT */
    fo->Event.Header.Type = 0;                                   /* NotificationEvent */
    fo->Event.Header.Size = 6;
    fo->Event.Header.WaitListHead.Flink = fo->Event.Header.WaitListHead.Blink = &fo->Event.Header.WaitListHead;
    fo->IrpList.Flink = fo->IrpList.Blink = &fo->IrpList;

    struct { void *SecurityQos, *AccessState; unsigned int DesiredAccess, FullCreateOptions; } sec = { 0, 0, access, options };
    Req r;
    NT_IO_STACK_LOCATION *sl = req_new(&r, fo, NT_IRP_MJ_CREATE, IRP_CREATE_OPERATION);
    if (!sl) { release(fo); return STATUS_INSUFFICIENT_RESOURCES; }
    sl->Parameters.Create.SecurityContext = &sec;
    sl->Parameters.Create.Options = (disposition << 24) | (options & 0x00FFFFFF);
    sl->Parameters.Create.ShareAccess = (unsigned short)share;
    st = req_send(&r, fo);
    req_free(&r);
    if (!NT_SUCCESS(st)) { release(fo); return st; }
    *out = fo;
    return STATUS_SUCCESS;
}

/* A read or write: buffered through a kernel copy, or the caller's own
 * buffer (neither) */
static NTSTATUS transfer(NT_FILE_OBJECT *fo, bool write, UINT64 buf, UINT32 len, const INT64 *offset, UINT64 *info)
{
    NT_DEVICE_OBJECT *d = fo->DeviceObject;
    *info = 0;
    if (d->Flags & NT_DO_DIRECT_IO) {
        kprintf("[NT] %s on a direct-I/O device: MDLs come with step 30.2\n", write ? "write" : "read");
        return NT_STATUS_NOT_SUPPORTED;
    }
    bool buffered = d->Flags & NT_DO_BUFFERED_IO;
    void *sys = NULL;
    if (buffered) {
        sys = kmalloc(len ? len : 1);
        if (!sys) return STATUS_INSUFFICIENT_RESOURCES;
        if (write && len && !NT_SUCCESS(CopyFromUser(sys, (const void *)(uintptr_t)buf, len))) { kfree(sys); return STATUS_ACCESS_VIOLATION; }
    }
    Req r;
    NT_IO_STACK_LOCATION *sl = req_new(&r, fo, write ? NT_IRP_MJ_WRITE : NT_IRP_MJ_READ,
                                       (write ? IRP_WRITE_OPERATION : IRP_READ_OPERATION | IRP_INPUT_OPERATION) |
                                       (buffered ? IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER : 0));
    if (!sl) { kfree(sys); return STATUS_INSUFFICIENT_RESOURCES; }
    sl->Parameters.Read.Length = len;
    sl->Parameters.Read.ByteOffset = offset ? *offset : fo->CurrentByteOffset;
    r.irp->AssociatedIrp.SystemBuffer = sys;
    r.irp->UserBuffer = (void *)(uintptr_t)buf;
    NTSTATUS st = req_send(&r, fo);
    UINT64 n = r.iosb.Information < len ? r.iosb.Information : len;
    if (NT_SUCCESS(st) && buffered && !write && n && !NT_SUCCESS(CopyToUser((void *)(uintptr_t)buf, sys, n)))
        st = STATUS_ACCESS_VIOLATION;
    if (NT_SUCCESS(st)) {
        *info = n;
        if (!offset) fo->CurrentByteOffset += (long long)n;
    }
    if (r.irp) kfree(sys);
    req_free(&r);
    return st;
}

NTSTATUS NtIoRead(NT_FILE_OBJECT *fo, UINT64 buf, UINT32 len, const INT64 *offset, UINT64 *info)
{
    return transfer(fo, false, buf, len, offset, info);
}

NTSTATUS NtIoWrite(NT_FILE_OBJECT *fo, UINT64 buf, UINT32 len, const INT64 *offset, UINT64 *info)
{
    return transfer(fo, true, buf, len, offset, info);
}

NTSTATUS NtIoControl(NT_FILE_OBJECT *fo, UINT32 code, UINT64 in, UINT32 in_len, UINT64 out, UINT32 out_len, UINT64 *info)
{
    UINT32 method = code & 3;
    *info = 0;
    if (method == NT_METHOD_IN_DIRECT || method == NT_METHOD_OUT_DIRECT) {
        kprintf("[NT] control code 0x%08x uses direct I/O: MDLs come with step 30.2\n", code);
        return NT_STATUS_NOT_SUPPORTED;
    }
    void *sys = NULL;
    UINT32 sys_len = in_len > out_len ? in_len : out_len;
    if (method == NT_METHOD_BUFFERED && sys_len) {
        sys = kzalloc(sys_len);
        if (!sys) return STATUS_INSUFFICIENT_RESOURCES;
        if (in_len && !NT_SUCCESS(CopyFromUser(sys, (const void *)(uintptr_t)in, in_len))) { kfree(sys); return STATUS_ACCESS_VIOLATION; }
    }
    Req r;
    NT_IO_STACK_LOCATION *sl = req_new(&r, fo, NT_IRP_MJ_DEVICE_CONTROL, sys ? IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER : 0);
    if (!sl) { kfree(sys); return STATUS_INSUFFICIENT_RESOURCES; }
    sl->Parameters.DeviceIoControl.IoControlCode = code;
    sl->Parameters.DeviceIoControl.InputBufferLength = in_len;
    sl->Parameters.DeviceIoControl.OutputBufferLength = out_len;
    if (method == NT_METHOD_NEITHER) sl->Parameters.DeviceIoControl.Type3InputBuffer = (void *)(uintptr_t)in;
    r.irp->AssociatedIrp.SystemBuffer = sys;
    r.irp->UserBuffer = (void *)(uintptr_t)out;
    NTSTATUS st = req_send(&r, fo);
    UINT64 n = r.iosb.Information;
    if (method == NT_METHOD_BUFFERED) {
        if (n > out_len) n = out_len;
        if (NT_SUCCESS(st) && n && !NT_SUCCESS(CopyToUser((void *)(uintptr_t)out, sys, n))) st = STATUS_ACCESS_VIOLATION;
    }
    if (NT_SUCCESS(st)) *info = n;
    if (r.irp) kfree(sys);
    req_free(&r);
    return st;
}

void NtIoClose(NT_FILE_OBJECT *fo)
{
    static const UINT8 majors[2] = { NT_IRP_MJ_CLEANUP, NT_IRP_MJ_CLOSE };
    for (int i = 0; i < 2; i++) {
        Req r;
        if (!req_new(&r, fo, majors[i], i ? IRP_CLOSE_OPERATION : 0)) continue;
        req_send(&r, fo);
        req_free(&r);
    }
    release(fo);
}
