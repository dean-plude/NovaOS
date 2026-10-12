/*
 * wdmtest.sys — NovaOS's first Windows kernel-mode test driver (step 30.1)
 *
 * A WDM driver as a Windows driver is written: DriverEntry creates
 * \Device\WdmTest (buffered I/O) and its \DosDevices\WdmTest link, and
 * serves IRP_MJ_CREATE, CLOSE, CLEANUP, READ, WRITE and DEVICE_CONTROL:
 * a write keeps up to 256 bytes that the next reads return, and the
 * control codes in wdmtest.h reverse bytes (buffered), add one to each
 * (neither I/O, on the caller's own buffers) and report the objects as the
 * driver sees them.  DriverUnload removes the link and the device.
 *
 * Step 30.2's kernel API: direct-I/O control codes read and fill the
 * caller's buffer through its MDL (MmGetSystemAddressForMdlSafe, as the
 * WDK has it); one runs checks of the IRQL (CR8 written directly, as the
 * WDK's inline KeRaiseIrql does, and the exports), a DPC queued under a
 * spin lock, the pool, events, semaphores, a timer, a fast mutex and an
 * MDL for pool; another returns STATUS_PENDING and has a timer's DPC
 * complete the IRP.  DriverEntry leaves one pool block (tag 'Leak') for
 * the unload report.
 *
 * It is built by tools/drivertest/mksys.py with clang and lld-link
 * /driver against kernel/nt/exports.h's ntoskrnl.exe, and uses NovaOS's
 * own wdm.h for the structures (the WDK is not needed).  The wdmtest self-
 * test installs it with "sc create WdmTest type= kernel" and drives it
 * with userland/programs/wdmtest.c.
 */
#include "../../kernel/nt/wdm.h"
#include "wdmtest.h"

#define IMPORT __declspec(dllimport) NT_API
IMPORT NT_STATUS IoCreateDevice(NT_DRIVER_OBJECT *drv, unsigned int ext_size, NT_UNICODE_STRING *name, unsigned int type,
                                unsigned int characteristics, unsigned char exclusive, NT_DEVICE_OBJECT **out);
IMPORT void      IoDeleteDevice(NT_DEVICE_OBJECT *dev);
IMPORT NT_STATUS IoCreateSymbolicLink(NT_UNICODE_STRING *link, NT_UNICODE_STRING *target);
IMPORT NT_STATUS IoDeleteSymbolicLink(NT_UNICODE_STRING *link);
IMPORT void      IofCompleteRequest(NT_IRP *irp, char boost);
IMPORT void      RtlInitUnicodeString(NT_UNICODE_STRING *u, const NT_WCHAR *s);
IMPORT void      RtlCopyUnicodeString(NT_UNICODE_STRING *dst, const NT_UNICODE_STRING *src);
IMPORT unsigned int DbgPrint(const char *fmt, ...);
IMPORT unsigned char KeGetCurrentIrql(void);
IMPORT unsigned char KeRaiseIrqlToDpcLevel(void);
IMPORT void      KeLowerIrql(unsigned char irql);
IMPORT unsigned char KeAcquireSpinLockRaiseToDpc(NT_KSPIN_LOCK *l);
IMPORT void      KeReleaseSpinLock(NT_KSPIN_LOCK *l, unsigned char old);
IMPORT void      KeAcquireInStackQueuedSpinLock(NT_KSPIN_LOCK *l, NT_KLOCK_QUEUE_HANDLE *h);
IMPORT void      KeReleaseInStackQueuedSpinLock(NT_KLOCK_QUEUE_HANDLE *h);
IMPORT void      KeInitializeDpc(NT_KDPC *dpc, NT_KDEFERRED_ROUTINE fn, void *ctx);
IMPORT unsigned char KeInsertQueueDpc(NT_KDPC *dpc, void *a1, void *a2);
IMPORT void      KeFlushQueuedDpcs(void);
IMPORT void      KeStallExecutionProcessor(unsigned int us);
IMPORT void     *ExAllocatePoolWithTag(int type, unsigned long long size, unsigned int tag);
IMPORT void     *ExAllocatePool2(unsigned long long flags, unsigned long long size, unsigned int tag);
IMPORT void      ExFreePoolWithTag(void *p, unsigned int tag);
IMPORT void      KeInitializeEvent(NT_KEVENT *ev, int type, unsigned char state);
IMPORT int       KeSetEvent(NT_KEVENT *ev, int increment, unsigned char wait);
IMPORT NT_STATUS KeWaitForSingleObject(void *obj, int reason, char mode, unsigned char alertable, long long *timeout);
IMPORT void      KeInitializeSemaphore(NT_KSEMAPHORE *s, int count, int limit);
IMPORT int       KeReleaseSemaphore(NT_KSEMAPHORE *s, int increment, int adjustment, unsigned char wait);
IMPORT void      KeInitializeTimer(NT_KTIMER *t);
IMPORT unsigned char KeSetTimer(NT_KTIMER *t, long long due, NT_KDPC *dpc);
IMPORT unsigned char KeCancelTimer(NT_KTIMER *t);
IMPORT long long KeQueryPerformanceCounter(long long *freq);
IMPORT void      ExAcquireFastMutex(NT_FAST_MUTEX *m);
IMPORT void      ExReleaseFastMutex(NT_FAST_MUTEX *m);
IMPORT NT_MDL   *IoAllocateMdl(void *va, unsigned int len, unsigned char secondary, unsigned char quota, NT_IRP *irp);
IMPORT void      IoFreeMdl(NT_MDL *m);
IMPORT void      MmBuildMdlForNonPagedPool(NT_MDL *m);
IMPORT void     *MmMapLockedPagesSpecifyCache(NT_MDL *m, char mode, int cache, void *at, unsigned int bugcheck,
                                              unsigned int priority);
IMPORT long long MmGetPhysicalAddress(void *va);
IMPORT void     *MmGetSystemRoutineAddress(NT_UNICODE_STRING *name);
/* step 30.2 part 2: work items, system threads, registry (Zw) */
typedef void (NT_API *IO_WORKITEM_ROUTINE)(NT_DEVICE_OBJECT *dev, void *ctx);
IMPORT void     *IoAllocateWorkItem(NT_DEVICE_OBJECT *dev);
IMPORT void      IoQueueWorkItem(void *w, IO_WORKITEM_ROUTINE routine, int queue_type, void *ctx);
IMPORT void      IoFreeWorkItem(void *w);
typedef void (NT_API *THREAD_START)(void *ctx);
IMPORT NT_STATUS PsCreateSystemThread(void *handle, unsigned int access, void *attr, void *process, void *cid,
                                      THREAD_START start, void *ctx);
IMPORT NT_STATUS PsTerminateSystemThread(NT_STATUS status);
IMPORT NT_STATUS ObReferenceObjectByHandle(void *h, unsigned int access, void *type, char mode, void **obj, void *info);
IMPORT void      ObfDereferenceObject(void *obj);
IMPORT NT_STATUS ZwCreateKey(void *handle, unsigned int access, void *oa, unsigned int title, NT_UNICODE_STRING *cls,
                             unsigned int options, unsigned int *disposition);
IMPORT NT_STATUS ZwSetValueKey(void *key, NT_UNICODE_STRING *name, unsigned int title, unsigned int type,
                               void *data, unsigned int size);
IMPORT NT_STATUS ZwQueryValueKey(void *key, NT_UNICODE_STRING *name, unsigned int cls, void *out, unsigned int cap,
                                 unsigned int *result_len);
IMPORT NT_STATUS ZwDeleteValueKey(void *key, NT_UNICODE_STRING *name);
IMPORT NT_STATUS ZwDeleteKey(void *key);
IMPORT NT_STATUS ZwClose(void *handle);

/* OBJECT_ATTRIBUTES, as far as this driver fills it (kernel/nt/zw.c) */
typedef struct {
    unsigned int       Length, pad;
    void              *RootDirectory;
    NT_UNICODE_STRING *ObjectName;
    unsigned int       Attributes;
    void              *SecurityDescriptor, *SecurityQualityOfService;
} OBJ_ATTR;
#define DelayedWorkQueue      1
#define OBJ_CASE_INSENSITIVE  0x40u
#define KEY_ALL_ACCESS        0xF003Fu
#define REG_DWORD             4u
#define KEY_VALUE_PARTIAL     2u
#define ObjectThreadType      0        /* NULL expected-type: any object */

#define DISPATCH_LEVEL        2
#define APC_LEVEL             1
#define NonPagedPoolNx        512
#define POOL_FLAG_NON_PAGED   0x40ull
#define TAG_WDMT              0x746D6457u        /* 'Wdmt' */
#define TAG_LEAK              0x6B61654Cu        /* 'Leak' */

/* The WDK's inline KeGetCurrentIrql and KeRaiseIrql/KeLowerIrql on x64 */
static inline unsigned char read_cr8(void)
{
    unsigned long long v;
    __asm__ volatile ("mov %%cr8, %0" : "=r"(v));
    return (unsigned char)v;
}
static inline void write_cr8(unsigned char irql)
{
    __asm__ volatile ("mov %0, %%cr8" : : "r"((unsigned long long)irql) : "memory");
}

/* The WDK's MmGetSystemAddressForMdlSafe (a macro) */
static void *mdl_address(NT_MDL *m)
{
    if (m->MdlFlags & (NT_MDL_MAPPED_TO_SYSTEM_VA | NT_MDL_SOURCE_IS_NONPAGED_POOL)) return m->MappedSystemVa;
    return MmMapLockedPagesSpecifyCache(m, 0, 1 /* MmCached */, 0, 0, 16 /* NormalPagePriority */);
}

/* The WDK's ExInitializeFastMutex (inline) */
static void init_fast_mutex(NT_FAST_MUTEX *m)
{
    m->Count = 1;
    m->Owner = 0;
    m->Contention = 0;
    KeInitializeEvent(&m->Event, NT_SynchronizationEvent, 0);
}

typedef struct {
    unsigned int  opens;
    unsigned int  len;
    unsigned char data[256];
    NT_UNICODE_STRING registry_path;
    NT_WCHAR      registry_buf[128];
    NT_KTIMER     timer;                         /* IOCTL_WDMTEST_PENDING */
    NT_KDPC       dpc;
    NT_IRP       *pending;
} Ext;

static NT_STATUS complete(NT_IRP *irp, NT_STATUS st, unsigned long long info)
{
    irp->IoStatus.Status = st;
    irp->IoStatus.Information = info;
    IofCompleteRequest(irp, 0);
    return st;
}

static void wcopy(unsigned short *dst, int cap, const NT_UNICODE_STRING *u)
{
    int n = u && u->Buffer ? u->Length / 2 : 0;
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) dst[i] = u->Buffer[i];
    dst[n] = 0;
}

static NT_API NT_STATUS create_close(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    if (sl->MajorFunction == NT_IRP_MJ_CREATE) {
        ((Ext *)dev->DeviceExtension)->opens++;
        DbgPrint("wdmtest: open \"%wZ\"", &sl->FileObject->FileName);
    }
    return complete(irp, NT_STATUS_SUCCESS, 0);
}

static NT_API NT_STATUS read_write(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    Ext *x = dev->DeviceExtension;
    unsigned char *buf = irp->AssociatedIrp.SystemBuffer;
    unsigned int n = sl->Parameters.Read.Length;
    if (n && !buf) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
    if (sl->MajorFunction == NT_IRP_MJ_WRITE) {
        if (n > sizeof(x->data)) n = sizeof(x->data);
        for (unsigned int i = 0; i < n; i++) x->data[i] = buf[i];
        x->len = n;
        return complete(irp, NT_STATUS_SUCCESS, n);
    }
    if (!x->len) return complete(irp, NT_STATUS_END_OF_FILE, 0);
    if (n > x->len) n = x->len;
    for (unsigned int i = 0; i < n; i++) buf[i] = x->data[i];
    return complete(irp, NT_STATUS_SUCCESS, n);
}

/* -----------------------------------------------------------------------
 * Step 30.2: the kernel API
 * ----------------------------------------------------------------------- */
typedef struct {
    volatile int   ran;
    unsigned char  irql;
    void          *a1, *a2;
} DpcState;

static NT_API void dpc_routine(NT_KDPC *dpc, void *ctx, void *a1, void *a2)
{
    DpcState *s = ctx;
    s->irql = read_cr8();
    s->a1 = a1;
    s->a2 = a2;
    s->ran = 1;
}

/* step 30.2 part 2: a work item runs on a system worker thread */
typedef struct { NT_KEVENT done; unsigned char irql; volatile int ran; } WorkState;
static NT_API void work_routine(NT_DEVICE_OBJECT *dev, void *ctx)
{
    WorkState *w = ctx;
    w->irql = read_cr8();
    w->ran = 1;
    KeSetEvent(&w->done, 0, 0);
}

/* a driver's own system thread signals an event and ends itself */
typedef struct { NT_KEVENT done; unsigned char irql; volatile int ran; } ThreadState;
static NT_API void thread_routine(void *ctx)
{
    ThreadState *t = ctx;
    t->irql = read_cr8();
    t->ran = 1;
    KeSetEvent(&t->done, 0, 0);
    PsTerminateSystemThread(NT_STATUS_SUCCESS);
}

static void part2_checks(NT_DEVICE_OBJECT *dev, WDMTEST_KERNEL *k)
{
    long long wait5 = -50000000;                 /* 5 s, in case a worker is slow */

    /* structured exception handling: a null write faults in kernel mode and
     * __except catches it (kernel/nt/seh.c dispatches over the driver's
     * .pdata through __C_specific_handler) */
    unsigned int code = 0;
    __try {
        volatile int *p = (volatile int *)0;
        *p = 1;
    } __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
        code = (unsigned int)_exception_code();
    }
    k->exception = code == 0xC0000005u;          /* STATUS_ACCESS_VIOLATION */
    k->exception_code = code;
    k->irql_after_seh = read_cr8() == 0;         /* back at PASSIVE_LEVEL after unwinding */

    /* a work item queued to a system worker thread (runs at PASSIVE_LEVEL) */
    void *wi = IoAllocateWorkItem(dev);
    if (wi) {
        WorkState ws;
        KeInitializeEvent(&ws.done, NT_NotificationEvent, 0);
        ws.ran = 0; ws.irql = 0xFF;
        IoQueueWorkItem(wi, work_routine, DelayedWorkQueue, &ws);
        NT_STATUS s = KeWaitForSingleObject(&ws.done, 0, 0, 0, &wait5);
        k->work_item = s == NT_STATUS_SUCCESS && ws.ran && ws.irql == 0;
        IoFreeWorkItem(wi);
    }

    /* a system thread: create it, wait for it, reference its object by
     * handle, wait on the object, close the handle and let the object go */
    void *th = 0, *obj = 0;
    ThreadState ts;
    KeInitializeEvent(&ts.done, NT_NotificationEvent, 0);
    ts.ran = 0; ts.irql = 0xFF;
    NT_STATUS cs = PsCreateSystemThread(&th, 0, 0, 0, 0, thread_routine, &ts);
    if (NT_SUCCESS_(cs) && th) {
        NT_STATUS ws = KeWaitForSingleObject(&ts.done, 0, 0, 0, &wait5);
        NT_STATUS rs = ObReferenceObjectByHandle(th, 0, (void *)ObjectThreadType, 0, &obj, 0);
        NT_STATUS os = NT_SUCCESS_(rs) ? KeWaitForSingleObject(obj, 0, 0, 0, &wait5) : rs;
        ZwClose(th);
        if (obj) ObfDereferenceObject(obj);
        k->system_thread = ws == NT_STATUS_SUCCESS && ts.ran && ts.irql == 0 &&
                           NT_SUCCESS_(rs) && obj == th && os == NT_STATUS_SUCCESS;
    }

    /* the registry through Zw: create a key, set a DWORD, read it back,
     * delete the value and the key */
    NT_UNICODE_STRING keyname, valname;
    RtlInitUnicodeString(&keyname, L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\NovaOS\\WdmTestZw");
    RtlInitUnicodeString(&valname, L"Answer");
    OBJ_ATTR oa = { sizeof(oa), 0, 0, &keyname, OBJ_CASE_INSENSITIVE, 0, 0 };
    void *key = 0;
    unsigned int disp = 0;
    if (NT_SUCCESS_(ZwCreateKey(&key, KEY_ALL_ACCESS, &oa, 0, 0, 0, &disp)) && key) {
        unsigned int answer = 42, type = 0, len = 0;
        unsigned char info[32];
        NT_STATUS ss = ZwSetValueKey(key, &valname, 0, REG_DWORD, &answer, sizeof(answer));
        NT_STATUS qs = ZwQueryValueKey(key, &valname, KEY_VALUE_PARTIAL, info, sizeof(info), &len);
        /* KEY_VALUE_PARTIAL_INFORMATION: TitleIndex(4), Type(4), DataLength(4), Data[] */
        if (NT_SUCCESS_(qs) && len >= 12) {
            type = *(unsigned int *)(info + 4);
            unsigned int dlen = *(unsigned int *)(info + 8), data = 0;
            if (dlen == 4) data = *(unsigned int *)(info + 12);
            k->zw_registry = NT_SUCCESS_(ss) && type == REG_DWORD && data == 42;
        }
        ZwDeleteValueKey(key, &valname);
        ZwDeleteKey(key);
        ZwClose(key);
    }
}

static void kernel_checks(NT_DEVICE_OBJECT *dev, WDMTEST_KERNEL *k)
{
    k->irql_passive = read_cr8() == 0 && KeGetCurrentIrql() == 0;

    /* the IRQL as the WDK's inline functions keep it, then the exports */
    unsigned char old = read_cr8();
    write_cr8(DISPATCH_LEVEL);
    k->irql_cr8 = read_cr8() == DISPATCH_LEVEL && KeGetCurrentIrql() == DISPATCH_LEVEL;
    write_cr8(old);
    old = KeRaiseIrqlToDpcLevel();
    k->irql_export = old == 0 && read_cr8() == DISPATCH_LEVEL;
    KeLowerIrql(old);
    k->irql_export &= read_cr8() == 0;

    /* a DPC queued while a spin lock is held runs when it is let go */
    NT_KSPIN_LOCK lock = 0;
    NT_KDPC dpc;
    DpcState ds = { 0, 0, 0, 0 };
    KeInitializeDpc(&dpc, dpc_routine, &ds);
    unsigned char irql = KeAcquireSpinLockRaiseToDpc(&lock);
    unsigned char first = KeInsertQueueDpc(&dpc, (void *)0x1234, (void *)0x5678);
    k->dpc_once = first && !KeInsertQueueDpc(&dpc, 0, 0);
    KeStallExecutionProcessor(2000);
    k->dpc_held = first && !ds.ran;
    KeReleaseSpinLock(&lock, irql);
    for (int i = 0; i < 1000 && !ds.ran; i++) KeStallExecutionProcessor(10);
    k->dpc_ran = ds.ran && ds.irql == DISPATCH_LEVEL && ds.a1 == (void *)0x1234 && ds.a2 == (void *)0x5678 && read_cr8() == 0;

    /* an in-stack queued spin lock */
    NT_KLOCK_QUEUE_HANDLE h;
    KeAcquireInStackQueuedSpinLock(&lock, &h);
    k->queued_lock = read_cr8() == DISPATCH_LEVEL && lock != 0;
    KeReleaseInStackQueuedSpinLock(&h);
    k->queued_lock &= read_cr8() == 0 && lock == 0;

    /* the pool */
    unsigned char *a = ExAllocatePoolWithTag(NonPagedPoolNx, 100, TAG_WDMT);
    unsigned char *b = ExAllocatePool2(POOL_FLAG_NON_PAGED, 8192, TAG_WDMT);
    k->pool = a && b && !((unsigned long long)a & 15) && !((unsigned long long)b & 4095);
    for (int i = 0; b && i < 8192; i++) if (b[i]) k->pool = 0;

    /* an MDL for pool */
    if (a) {
        NT_MDL *m = IoAllocateMdl(a, 100, 0, 0, 0);
        if (m) {
            MmBuildMdlForNonPagedPool(m);
            k->pool_mdl = mdl_address(m) == a && (m->MdlFlags & NT_MDL_SOURCE_IS_NONPAGED_POOL) &&
                          MmGetPhysicalAddress(a) == (long long)(NtMdlPfns(m)[0] << 12 | ((unsigned long long)a & 4095));
            IoFreeMdl(m);
        }
    }
    if (a) ExFreePoolWithTag(a, TAG_WDMT);
    if (b) ExFreePoolWithTag(b, TAG_WDMT);

    /* a synchronization event: one wait takes it */
    NT_KEVENT ev;
    long long zero = 0;
    KeInitializeEvent(&ev, NT_SynchronizationEvent, 0);
    k->event = KeWaitForSingleObject(&ev, 0, 0, 0, &zero) == NT_STATUS_TIMEOUT && KeSetEvent(&ev, 0, 0) == 0 &&
               KeWaitForSingleObject(&ev, 0, 0, 0, &zero) == NT_STATUS_SUCCESS &&
               KeWaitForSingleObject(&ev, 0, 0, 0, &zero) == NT_STATUS_TIMEOUT;

    /* a semaphore */
    NT_KSEMAPHORE sem;
    KeInitializeSemaphore(&sem, 0, 5);
    k->semaphore = KeReleaseSemaphore(&sem, 0, 2, 0) == 0 && KeWaitForSingleObject(&sem, 0, 0, 0, &zero) == 0 &&
                   KeWaitForSingleObject(&sem, 0, 0, 0, &zero) == 0 &&
                   KeWaitForSingleObject(&sem, 0, 0, 0, &zero) == NT_STATUS_TIMEOUT;

    /* a 20 ms timer */
    NT_KTIMER t;
    long long freq, t0, t1;
    KeInitializeTimer(&t);
    t0 = KeQueryPerformanceCounter(&freq);
    KeSetTimer(&t, -200000, 0);
    NT_STATUS st = KeWaitForSingleObject(&t, 0, 0, 0, 0);
    t1 = KeQueryPerformanceCounter(0);
    k->timer_ms = freq ? (unsigned int)((t1 - t0) * 1000 / freq) : 0;
    k->timer = st == NT_STATUS_SUCCESS && k->timer_ms >= 20;

    /* a fast mutex */
    NT_FAST_MUTEX fm;
    init_fast_mutex(&fm);
    ExAcquireFastMutex(&fm);
    k->fast_mutex = read_cr8() == APC_LEVEL && !(fm.Count & 1);
    ExReleaseFastMutex(&fm);
    k->fast_mutex &= read_cr8() == 0 && (fm.Count & 1);

    NT_UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"KeSetEvent");
    k->routine = MmGetSystemRoutineAddress(&name) == (void *)KeSetEvent;

    part2_checks(dev, k);
}

/* IOCTL_WDMTEST_PENDING: completed by the timer's DPC */
static NT_API void pending_dpc(NT_KDPC *dpc, void *ctx, void *a1, void *a2)
{
    Ext *x = ctx;
    NT_IRP *irp = x->pending;
    x->pending = 0;
    if (!irp) return;
    WDMTEST_PENDING *p = irp->AssociatedIrp.SystemBuffer;
    p->text[0] = 'l'; p->text[1] = 'a'; p->text[2] = 't'; p->text[3] = 'e';
    p->dpc_irql = read_cr8();
    p->pending_returned = (NtIrpCurrentStack(irp)->Control & NT_SL_PENDING_RETURNED) != 0;
    irp->IoStatus.Status = NT_STATUS_SUCCESS;
    irp->IoStatus.Information = sizeof(WDMTEST_PENDING);
    IofCompleteRequest(irp, 0);
}

static NT_API NT_STATUS control(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    Ext *x = dev->DeviceExtension;
    unsigned int in = sl->Parameters.DeviceIoControl.InputBufferLength, out = sl->Parameters.DeviceIoControl.OutputBufferLength;
    unsigned char *sys = irp->AssociatedIrp.SystemBuffer;
    switch (sl->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_WDMTEST_REVERSE: {
        if (out < in) return complete(irp, NT_STATUS_BUFFER_TOO_SMALL, 0);
        for (unsigned int i = 0; i < in / 2; i++) {
            unsigned char t = sys[i];
            sys[i] = sys[in - 1 - i];
            sys[in - 1 - i] = t;
        }
        return complete(irp, NT_STATUS_SUCCESS, in);
    }
    case IOCTL_WDMTEST_NEITHER: {                      /* (the caller's own buffers: this runs in its process) */
        const unsigned char *src = sl->Parameters.DeviceIoControl.Type3InputBuffer;
        unsigned char *dst = irp->UserBuffer;
        if (out < in || !src || !dst) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        for (unsigned int i = 0; i < in; i++) dst[i] = (unsigned char)(src[i] + 1);
        return complete(irp, NT_STATUS_SUCCESS, in);
    }
    case IOCTL_WDMTEST_DIRECT: {                       /* METHOD_IN_DIRECT: read the buffer through its MDL */
        NT_MDL *m = irp->MdlAddress;
        const unsigned char *p = m ? mdl_address(m) : 0;
        if (!p || m->ByteCount != out) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        unsigned int same = 0;
        for (unsigned int i = 0; i < out; i++) same += p[i] == (unsigned char)(i * 7);
        return complete(irp, NT_STATUS_SUCCESS, same);
    }
    case IOCTL_WDMTEST_DIRECT_OUT: {                   /* METHOD_OUT_DIRECT: fill it through its MDL */
        NT_MDL *m = irp->MdlAddress;
        unsigned char *p = m ? mdl_address(m) : 0;
        if (!p || in < 1 || out < sizeof(WDMTEST_MDL)) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        for (unsigned int i = 0; i < out; i++) p[i] = (unsigned char)(sys[0] + i);
        WDMTEST_MDL *d = (WDMTEST_MDL *)p;
        d->flags = (unsigned short)m->MdlFlags;
        d->byte_count = m->ByteCount;
        d->byte_offset = m->ByteOffset;
        d->pages = (unsigned int)((m->Size - sizeof(NT_MDL)) / 8);
        return complete(irp, NT_STATUS_SUCCESS, out);
    }
    case IOCTL_WDMTEST_KERNEL: {
        if (out < sizeof(WDMTEST_KERNEL)) return complete(irp, NT_STATUS_BUFFER_TOO_SMALL, 0);
        WDMTEST_KERNEL *k = (WDMTEST_KERNEL *)sys;
        for (unsigned int i = 0; i < sizeof(*k); i++) ((unsigned char *)k)[i] = 0;
        kernel_checks(dev, k);
        return complete(irp, NT_STATUS_SUCCESS, sizeof(WDMTEST_KERNEL));
    }
    case IOCTL_WDMTEST_PENDING: {
        if (out < sizeof(WDMTEST_PENDING) || x->pending) return complete(irp, NT_STATUS_INVALID_PARAMETER, 0);
        NtIrpMarkPending(irp);
        x->pending = irp;
        KeSetTimer(&x->timer, -300000, &x->dpc);       /* 30 ms */
        return NT_STATUS_PENDING;
    }
    case IOCTL_WDMTEST_INFO: {
        if (out < sizeof(WDMTEST_INFO)) return complete(irp, NT_STATUS_BUFFER_TOO_SMALL, 0);
        WDMTEST_INFO *i = (WDMTEST_INFO *)sys;
        NT_DRIVER_OBJECT *d = dev->DriverObject;
        i->driver_type = d->Type;
        i->driver_size = d->Size;
        i->device_type = dev->Type;
        i->device_size = (short)dev->Size;
        i->irp_type = irp->Type;
        i->irp_stack_count = irp->StackCount;
        i->irp_current_location = irp->CurrentLocation;
        i->file_type = sl->FileObject->Type;
        i->file_size = sl->FileObject->Size;
        i->requestor_mode = (unsigned char)irp->RequestorMode;
        i->buffered = sys != 0 && (dev->Flags & NT_DO_BUFFERED_IO) != 0;
        i->opens = x->opens;
        wcopy(i->driver_name, 64, &d->DriverName);
        wcopy(i->registry_path, 96, &x->registry_path);
        wcopy(i->service_key, 32, &d->DriverExtension->ServiceKeyName);
        wcopy(i->file_name, 32, &sl->FileObject->FileName);
        return complete(irp, NT_STATUS_SUCCESS, sizeof(WDMTEST_INFO));
    }
    default:
        return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

static NT_API void unload(NT_DRIVER_OBJECT *drv)
{
    Ext *x = drv->DeviceObject->DeviceExtension;
    KeCancelTimer(&x->timer);
    KeFlushQueuedDpcs();
    NT_UNICODE_STRING link;
    RtlInitUnicodeString(&link, WDMTEST_LINK);
    IoDeleteSymbolicLink(&link);
    IoDeleteDevice(drv->DeviceObject);
    DbgPrint("wdmtest: unloaded");
}

NT_API NT_STATUS DriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *registry_path)
{
    NT_UNICODE_STRING name, link;
    NT_DEVICE_OBJECT *dev;
    DbgPrint("wdmtest: DriverEntry %wZ", registry_path);
    RtlInitUnicodeString(&name, WDMTEST_DEVICE);
    NT_STATUS st = IoCreateDevice(drv, sizeof(Ext), &name, NT_FILE_DEVICE_UNKNOWN, 0, 0, &dev);
    if (!NT_SUCCESS_(st)) return st;
    dev->Flags |= NT_DO_BUFFERED_IO;
    Ext *x = dev->DeviceExtension;
    x->registry_path.Buffer = x->registry_buf;
    x->registry_path.MaximumLength = sizeof(x->registry_buf);
    RtlCopyUnicodeString(&x->registry_path, registry_path);   /* (RegistryPath is only lent for the call) */
    KeInitializeTimer(&x->timer);
    KeInitializeDpc(&x->dpc, pending_dpc, x);
    ExAllocatePoolWithTag(NonPagedPoolNx, 32, TAG_LEAK);       /* (left for the unload report) */
    RtlInitUnicodeString(&link, WDMTEST_LINK);
    st = IoCreateSymbolicLink(&link, &name);
    if (!NT_SUCCESS_(st)) { IoDeleteDevice(dev); return st; }
    drv->MajorFunction[NT_IRP_MJ_CREATE] = create_close;
    drv->MajorFunction[NT_IRP_MJ_CLOSE] = create_close;
    drv->MajorFunction[NT_IRP_MJ_CLEANUP] = create_close;
    drv->MajorFunction[NT_IRP_MJ_READ] = read_write;
    drv->MajorFunction[NT_IRP_MJ_WRITE] = read_write;
    drv->MajorFunction[NT_IRP_MJ_DEVICE_CONTROL] = control;
    drv->DriverUnload = unload;
    dev->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    return NT_STATUS_SUCCESS;
}
