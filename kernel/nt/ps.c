/*
 * ps.c — threads and object references for drivers
 *
 * A driver's system thread (PsCreateSystemThread) is a real NovaOS kernel
 * thread.  Its handle is an NtSysThread the driver can wait on
 * (KeWaitForSingleObject), reference (ObReferenceObjectByHandle) and let
 * go of (ObDereferenceObject): it carries a notification event signalled
 * when the thread returns, and a reference count so the object outlives
 * both the thread and the handle until the driver is done with it.  The
 * start routine runs with the Microsoft x64 calling convention, like every
 * driver callback.
 *
 * At this step the only handle a driver can reference by value is such a
 * system-thread handle; referencing the Zw layer's file and key handles
 * (zw.c) by value arrives with the object manager in a later driver step,
 * so ObReferenceObjectByHandle returns STATUS_INVALID_HANDLE for anything
 * else.  ObfReferenceObject/ObfDereferenceObject count references on an
 * NtSysThread and are no-ops on any other pointer (drivers also hand them
 * device objects, which NovaOS does not reference-count yet).
 */
#include "nt.h"
#include "ntapi.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

#define NT_SYSTHREAD_MAGIC  0x53795468u   /* 'SyTh' */
#define NT_DRIVER_STACK     (24 * 1024)   /* driver threads run deeper than tests */

typedef void (NT_API *NtDriverThreadRoutine)(void *);

typedef struct NtSysThread {
    NT_KEVENT              done;      /* FIRST: the dispatcher object a driver waits on */
    struct NtSysThread    *next;
    UINT64                 tid;       /* the scheduler thread running it */
    UINT32                 magic;
    UINT32                 refs;
    bool                   exited;
    NtDriverThreadRoutine  routine;
    void                  *ctx;
} NtSysThread;

static NtSysThread *g_sys;            /* live system-thread objects */
static KSpinLock    g_sys_lock = KSPINLOCK_INIT;

static NtSysThread *find_locked(UINT64 tid)
{
    for (NtSysThread *t = g_sys; t; t = t->next)
        if (t->tid == tid) return t;
    return NULL;
}

/* Drop @st's last reference once the thread has exited and no driver still
 * holds the object (called with the lock held; it may unlink and free) */
static void maybe_free_locked(NtSysThread *st)
{
    if (st->refs || !st->exited) return;
    for (NtSysThread **p = &g_sys; *p; p = &(*p)->next)
        if (*p == st) { *p = st->next; break; }
    kfree(st);
}

/* The thread has run its course: signal the object and let it go */
static void finish(NtSysThread *st)
{
    IrqState s = spin_lock_irqsave(&g_sys_lock);
    st->exited = true;
    st->tid = 0;
    nt_KeSetEvent(&st->done, 0, 0);
    maybe_free_locked(st);
    spin_unlock_irqrestore(&g_sys_lock, s);
}

/* SysV entry the scheduler calls; it runs the driver's routine with the
 * Microsoft calling convention and ends the thread when it returns */
static void systhread_entry(void *arg)
{
    NtSysThread *st = arg;
    st->routine(st->ctx);
    finish(st);
    sched_exit_current();
}

NT_API NTSTATUS nt_PsCreateSystemThread(void *ThreadHandle, UINT32 DesiredAccess, void *ObjectAttributes,
                                        void *ProcessHandle, void *ClientId, void *StartRoutine, void *StartContext)
{
    (void)DesiredAccess; (void)ObjectAttributes; (void)ProcessHandle; (void)ClientId;
    if (!ThreadHandle || !StartRoutine) return STATUS_INVALID_PARAMETER;

    NtSysThread *st = kzalloc(sizeof(*st));
    if (!st) return STATUS_INSUFFICIENT_RESOURCES;
    nt_KeInitializeEvent(&st->done, NT_NotificationEvent, 0);
    st->magic   = NT_SYSTHREAD_MAGIC;
    st->refs    = 1;                  /* the handle itself holds one */
    st->routine = (NtDriverThreadRoutine)StartRoutine;
    st->ctx     = StartContext;

    IrqState s = spin_lock_irqsave(&g_sys_lock);
    st->next = g_sys;
    g_sys = st;
    spin_unlock_irqrestore(&g_sys_lock, s);

    Thread *t = sched_new_thread("nt-driver", systhread_entry, st, 9, NT_DRIVER_STACK);
    if (!t) {
        s = spin_lock_irqsave(&g_sys_lock);
        st->exited = true; st->refs = 0; maybe_free_locked(st);
        spin_unlock_irqrestore(&g_sys_lock, s);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    st->tid = t->tid;
    sched_start_thread(t);

    *(void **)ThreadHandle = st;
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_PsTerminateSystemThread(NTSTATUS ExitStatus)
{
    (void)ExitStatus;
    Thread *cur = sched_current();
    IrqState s = spin_lock_irqsave(&g_sys_lock);
    NtSysThread *st = cur ? find_locked(cur->tid) : NULL;
    spin_unlock_irqrestore(&g_sys_lock, s);
    if (st) finish(st);
    sched_exit_current();
    return STATUS_SUCCESS;            /* not reached */
}

/* -----------------------------------------------------------------------
 * Object references (see the file comment for what a driver may reference)
 * ----------------------------------------------------------------------- */
static NtSysThread *as_systhread(void *p)
{
    NtSysThread *st = p;
    return st && st->magic == NT_SYSTHREAD_MAGIC ? st : NULL;
}

NT_API NTSTATUS nt_ObReferenceObjectByHandle(void *Handle, UINT32 DesiredAccess, void *ObjectType, char AccessMode,
                                             void **Object, void *HandleInformation)
{
    (void)DesiredAccess; (void)ObjectType; (void)AccessMode; (void)HandleInformation;
    NtSysThread *st = as_systhread(Handle);
    if (!st) return STATUS_INVALID_HANDLE;
    IrqState s = spin_lock_irqsave(&g_sys_lock);
    st->refs++;
    spin_unlock_irqrestore(&g_sys_lock, s);
    if (Object) *Object = st;
    return STATUS_SUCCESS;
}

/* zw.c calls this first in ZwClose: a driver closes a system-thread handle
 * (PsCreateSystemThread) with ZwClose like any other.  It drops the
 * handle's own reference; true means the handle was one of ours. */
bool NtPsHandleClose(void *Handle)
{
    NtSysThread *st = as_systhread(Handle);
    if (!st) return false;
    IrqState s = spin_lock_irqsave(&g_sys_lock);
    if (st->refs) st->refs--;
    maybe_free_locked(st);
    spin_unlock_irqrestore(&g_sys_lock, s);
    return true;
}

NT_API void nt_ObfReferenceObject(void *Object)
{
    NtSysThread *st = as_systhread(Object);
    if (!st) return;
    IrqState s = spin_lock_irqsave(&g_sys_lock);
    st->refs++;
    spin_unlock_irqrestore(&g_sys_lock, s);
}

NT_API void nt_ObfDereferenceObject(void *Object)
{
    NtSysThread *st = as_systhread(Object);
    if (!st) return;
    IrqState s = spin_lock_irqsave(&g_sys_lock);
    if (st->refs) st->refs--;
    maybe_free_locked(st);
    spin_unlock_irqrestore(&g_sys_lock, s);
}
