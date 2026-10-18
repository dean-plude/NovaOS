/*
 * psnotify.c — what anti-cheat and security drivers watch the system
 * through (step 24.8), as Windows provides it to them
 *
 *   notify      PsSetCreateProcessNotifyRoutine(Ex, Ex2): a process starts
 *               (with PS_CREATE_NOTIFY_INFO, whose CreationStatus an Ex
 *               routine may set to refuse it) and ends;
 *               PsSetCreateThreadNotifyRoutine(Ex): a thread starts and
 *               ends; PsSetLoadImageNotifyRoutine(Ex): an image is mapped
 *               into a process, or a driver is loaded (ProcessId 0)
 *   Ob          ObRegisterCallbacks: pre-operation callbacks see every
 *               process and thread handle being opened or duplicated and
 *               may take rights away (an anti-cheat strips PROCESS_VM_READ
 *               from opens of its game); post-operation ones hear what
 *               was granted
 *   Ps          a PEPROCESS is the process's object (um/'s UO_PROCESS
 *               UmObject, the one its handles name), a PETHREAD the
 *               thread's: PsLookupProcessByProcessId, PsGetCurrentProcess
 *               and the getters on them, referenced and let go with
 *               ObReferenceObject/ObDereferenceObject
 *
 * As on Windows, PsSetCreateProcessNotifyRoutineEx(2) and
 * ObRegisterCallbacks take only a routine in a driver image linked with
 * /INTEGRITYCHECK (IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY), which a
 * driver's signature then covers (STATUS_ACCESS_DENIED otherwise); each
 * kind holds 64 routines.  Routines are called at PASSIVE_LEVEL in the
 * thread that caused the event (the creator's for a new process or
 * thread), never under a lock, and removing one waits for calls in
 * progress.  A driver that unloads with routines registered has them
 * removed (Windows would crash on the next call).
 */
#include "nt.h"
#include "ntapi.h"
#include "psnotify.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../um/um_internal.h"

#define ST_ACCESS_DENIED       ((NTSTATUS)0xC0000022)
#define ST_INVALID_CID         ((NTSTATUS)0xC000000B)
#define ST_PROCEDURE_NOT_FOUND ((NTSTATUS)0xC000007A)
#define ST_ALTITUDE_COLLISION  ((NTSTATUS)0xC01C0011)   /* STATUS_FLT_INSTANCE_ALTITUDE_COLLISION */
#define ST_OBJECT_TYPE_MISMATCH ((NTSTATUS)0xC0000024)

#define NOTIFY_MAX 64

/* -----------------------------------------------------------------------
 * The objects drivers are handed
 * ----------------------------------------------------------------------- */
/* An object type, as PsProcessType and PsThreadType point at one */
typedef struct { NT_WCHAR name[8]; UINT32 index; } NtObType;
static NtObType g_process_type = { { 'P', 'r', 'o', 'c', 'e', 's', 's', 0 }, 7 };
static NtObType g_thread_type  = { { 'T', 'h', 'r', 'e', 'a', 'd', 0 }, 8 };
/* (POBJECT_TYPE *PsProcessType: a driver imports the variable by address) */
void *nt_PsProcessType = &g_process_type;
void *nt_PsThreadType  = &g_thread_type;

/* The System process (PID 4) and its threads: what a kernel thread is in */
static UmObject g_system = { .nt_magic = UM_NT_OBJECT, .nt_id = 4, .nt_pid = 4, .type = UO_PROCESS, .refs = 1 << 30 };
static UmObject g_system_thread = { .nt_magic = UM_NT_OBJECT, .nt_pid = 4, .type = UO_THREAD, .refs = 1 << 30 };

/* @p as a process or thread object handed to a driver, or NULL */
static UmObject *as_ps(void *p, UmObType type)
{
    UmObject *o = p;
    return o && o->nt_magic == UM_NT_OBJECT && o->type == type ? o : NULL;
}

bool NtPsObject(void *p) { return as_ps(p, UO_PROCESS) || as_ps(p, UO_THREAD); }

/* The object of process @p drivers see, held until it exits (NULL if gone) */
static UmObject *self_of(UmProcess *p)
{
    IrqState s = ob_lock();
    UmObject *o = p->nt_self;
    ob_unlock(s);
    if (o) return o;
    o = um_open_process(p->pid);                    /* (referenced: the hold) */
    if (!o) return NULL;
    s = ob_lock();
    if (p->nt_self) { UmObject *mine = o; o = p->nt_self; ob_unlock(s); um_ob_unref(mine); return o; }
    p->nt_self = o;
    ob_unlock(s);
    return o;
}

static UmProcess *proc_of(UmObject *o)
{
    return o && o != &g_system ? o->proc : NULL;
}

NT_API void *nt_PsGetCurrentProcess(void)
{
    UmProcess *p = UmCurrent();
    UmObject *o = p ? self_of(p) : NULL;
    return o ? o : &g_system;
}

NT_API void *nt_IoGetCurrentProcess(void) { return nt_PsGetCurrentProcess(); }

NT_API void *nt_PsGetCurrentProcessId(void)
{
    UmProcess *p = UmCurrent();
    return (void *)(uintptr_t)(p ? p->pid : 4);
}

NT_API void *nt_PsGetCurrentThread(void)
{
    UmThread *t = UmCurrentThread();
    if (t) return &t->ob;
    Thread *k = sched_current();
    g_system_thread.nt_id = k ? (UINT32)k->tid : 0;
    return &g_system_thread;
}

NT_API NTSTATUS nt_PsLookupProcessByProcessId(void *pid, void **process)
{
    UINT32 id = (UINT32)(uintptr_t)pid;
    if (!process) return STATUS_INVALID_PARAMETER;
    UmObject *o = id == 4 ? um_ob_ref(&g_system) : um_open_process(id);
    if (!o) return ST_INVALID_CID;
    if (o->nt_magic != UM_NT_OBJECT) um_nt_object(o, id, id);
    *process = o;
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_PsLookupThreadByThreadId(void *tid, void **thread)
{
    if (!thread) return STATUS_INVALID_PARAMETER;
    UmObject *o = um_open_thread((UINT32)(uintptr_t)tid);
    if (!o) return ST_INVALID_CID;
    *thread = o;
    return STATUS_SUCCESS;
}

NT_API void *nt_PsGetProcessId(void *process)
{
    UmObject *o = as_ps(process, UO_PROCESS);
    return o ? (void *)(uintptr_t)o->nt_id : NULL;
}

NT_API void *nt_PsGetThreadId(void *thread)
{
    UmObject *o = as_ps(thread, UO_THREAD);
    return o ? (void *)(uintptr_t)o->nt_id : NULL;
}

NT_API void *nt_PsGetThreadProcessId(void *thread)
{
    UmObject *o = as_ps(thread, UO_THREAD);
    return o ? (void *)(uintptr_t)o->nt_pid : NULL;
}

NT_API void *nt_PsGetProcessInheritedFromUniqueProcessId(void *process)
{
    UmProcess *p = proc_of(as_ps(process, UO_PROCESS));
    return p ? (void *)(uintptr_t)p->parent_pid : NULL;
}

/* EPROCESS.ImageFileName: up to 15 characters of its program's name */
NT_API const char *nt_PsGetProcessImageFileName(void *process)
{
    UmObject *o = as_ps(process, UO_PROCESS);
    UmProcess *p = proc_of(o);
    return p ? p->image_file_name : o == &g_system ? "System" : "";
}

NT_API INT64 nt_PsGetProcessCreateTimeQuadPart(void *process)
{
    UmProcess *p = proc_of(as_ps(process, UO_PROCESS));
    return p ? (INT64)p->create_time : 0;
}

/* Its PEB (the 64-bit one), and a 32-bit program's own (NULL for others) */
NT_API void *nt_PsGetProcessPeb(void *process)
{
    UmProcess *p = proc_of(as_ps(process, UO_PROCESS));
    return p && !p->wow ? (void *)(uintptr_t)p->lay.peb : NULL;
}

NT_API void *nt_PsGetProcessWow64Process(void *process)
{
    UmProcess *p = proc_of(as_ps(process, UO_PROCESS));
    return p && p->wow ? (void *)(uintptr_t)p->lay.peb : NULL;
}

NT_API NTSTATUS nt_PsGetProcessExitStatus(void *process)
{
    UmObject *o = as_ps(process, UO_PROCESS);
    UmProcess *p = proc_of(o);
    if (!o) return STATUS_INVALID_PARAMETER;
    if (!p) return o == &g_system ? 0x103 : o->count;     /* (an ended one's code is kept in the object) */
    return p->exited ? (NTSTATUS)p->exit_status : 0x103;   /* STATUS_PENDING while it runs */
}

/* Protected processes (PS_PROTECTION: Type 2 protected, 1 light) */
NT_API UINT8 nt_PsIsProtectedProcess(void *process)
{
    UmProcess *p = proc_of(as_ps(process, UO_PROCESS));
    return p && (p->protection & 7) != 0;
}

NT_API UINT8 nt_PsIsProtectedProcessLight(void *process)
{
    UmProcess *p = proc_of(as_ps(process, UO_PROCESS));
    return p && (p->protection & 7) == 1;
}

UINT32 NtPsMapAccess(UINT32 want, bool thread)
{
    const UINT32 all = 0x001FFFFFu;                           /* PROCESS_ALL_ACCESS, THREAD_ALL_ACCESS */
    UINT32 r = want & all;
    if (want & 0x12000000u) r |= all;                         /* GENERIC_ALL, MAXIMUM_ALLOWED */
    if (want & 0x80000000u) r |= thread ? 0x00020048u : 0x00020410u;   /* GENERIC_READ */
    if (want & 0x40000000u) r |= thread ? 0x00020037u : 0x00020BEAu;   /* GENERIC_WRITE */
    if (want & 0x20000000u) r |= thread ? 0x00121800u : 0x00121000u;   /* GENERIC_EXECUTE */
    return r;
}

/* ObReferenceObjectByHandle on a process or thread handle of the calling
 * process (NtCurrentProcess() and NtCurrentThread() too), checked against
 * the rights the handle grants; STATUS_INVALID_HANDLE when it is not one */
NTSTATUS NtPsReferenceByHandle(void *handle, UINT32 access, void *type, void **object)
{
    UmProcess *p = UmCurrent();
    bool thread = type == (void *)&g_thread_type, process = type == (void *)&g_process_type;
    UINT64 h = (UINT64)(uintptr_t)handle;
    if (!p || (type && !thread && !process)) return STATUS_INVALID_HANDLE;
    if (h == UINT64_C(0xFFFFFFFFFFFFFFFF)) {                  /* NtCurrentProcess() */
        if (thread) return ST_OBJECT_TYPE_MISMATCH;
        UmObject *o = self_of(p);
        if (!o) return STATUS_INVALID_HANDLE;
        *object = um_ob_ref(o);
        return STATUS_SUCCESS;
    }
    UINT32 st = 0;
    UmObject *o = NULL;
    UINT32 right = access ? NtPsMapAccess(access, thread) : 0;
    if (!type) {
        o = um_handle_object_right(p, h, UO_PROCESS, right, &st);
        if (!o) o = um_handle_object_right(p, h, UO_THREAD, right, &st);
    } else o = um_handle_object_right(p, h, thread ? UO_THREAD : UO_PROCESS, right, &st);
    if (!o) return st ? (NTSTATUS)st : STATUS_INVALID_HANDLE;
    if (o->nt_magic != UM_NT_OBJECT) {                         /* (made before drivers could see it) */
        UmProcess *q = o->type == UO_PROCESS ? o->proc : ((UmThread *)o)->proc;
        um_nt_object(o, o->type == UO_PROCESS ? (q ? q->pid : 0) : ((UmThread *)o)->tid, q ? q->pid : 0);
    }
    *object = o;
    return STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Registered routines
 * ----------------------------------------------------------------------- */
typedef struct {
    void          *fn;                /* the routine (NULL: a free slot) */
    UINT8          how;               /* process: 0 plain, 1 Ex, 2 Ex2 */
    volatile INT32 busy;              /* calls in progress (the slot is reused once none are) */
} Notify;

enum { K_PROCESS, K_THREAD, K_IMAGE, K_KINDS };
static Notify g_notify[K_KINDS][NOTIFY_MAX];
static const char *const g_kind_name[K_KINDS] = { "process", "thread", "image load" };

typedef struct ObOp { void *type; UINT32 ops; void *pre, *post; } ObOp;
typedef struct ObReg {
    struct ObReg  *next;
    UINT32         magic;
    void          *ctx;
    NT_WCHAR       altitude[32];
    int            nops;
    volatile INT32 busy;
    ObOp           op[4];
} ObReg;
#define OBREG_MAGIC 0x6745724Fu       /* 'OrEg' */
static ObReg *g_obregs;

static KSpinLock g_lock = KSPINLOCK_INIT;
static volatile UINT32 g_count;       /* routines and callbacks registered: none, nothing to do */

/* Whether @fn lies in a driver linked with /INTEGRITYCHECK (Windows'
 * MmVerifyCallbackFunction): its image header says so */
static bool integrity_checked(const void *fn, const char **image)
{
    NtDriver *d = NtDrvByAddress((UINT64)(uintptr_t)fn);
    *image = d ? d->image : "a routine outside any driver";
    if (!d || !d->base) return false;
    const UINT8 *b = (const UINT8 *)(uintptr_t)d->base;
    UINT32 nt = *(const UINT32 *)(b + 0x3C);
    UINT16 chars = *(const UINT16 *)(b + nt + 0x18 + 0x46);   /* OptionalHeader.DllCharacteristics */
    return (chars & 0x0080) != 0;                             /* IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY */
}

static const char *image_of(const void *fn)
{
    NtDriver *d = NtDrvByAddress((UINT64)(uintptr_t)fn);
    return d ? d->image : "the kernel";
}

static NTSTATUS notify_add(int kind, void *fn, UINT8 how)
{
    if (!fn) return STATUS_INVALID_PARAMETER;
    IrqState s = spin_lock_irqsave(&g_lock);
    int free = -1;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        if (g_notify[kind][i].fn == fn) { spin_unlock_irqrestore(&g_lock, s); return STATUS_INVALID_PARAMETER; }
        if (free < 0 && !g_notify[kind][i].fn && !g_notify[kind][i].busy) free = i;
    }
    if (free >= 0) {
        g_notify[kind][free].how = how;
        g_notify[kind][free].fn = fn;
        g_count++;
    }
    spin_unlock_irqrestore(&g_lock, s);
    if (free < 0) return STATUS_INSUFFICIENT_RESOURCES;
    kprintf("[NT] %s registered a %s notify routine%s\n", image_of(fn), g_kind_name[kind],
            how == 2 ? " (Ex2)" : how == 1 ? " (Ex)" : "");
    return STATUS_SUCCESS;
}

/* Take @fn away and wait until no call to it is running */
static NTSTATUS notify_remove(int kind, void *fn)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    Notify *n = NULL;
    for (int i = 0; i < NOTIFY_MAX && !n; i++)
        if (g_notify[kind][i].fn == fn) n = &g_notify[kind][i];
    if (n) { n->fn = NULL; g_count--; }
    spin_unlock_irqrestore(&g_lock, s);
    if (!n) return ST_PROCEDURE_NOT_FOUND;
    while (__atomic_load_n(&n->busy, __ATOMIC_ACQUIRE)) sched_yield();
    kprintf("[NT] %s removed its %s notify routine\n", image_of(fn), g_kind_name[kind]);
    return STATUS_SUCCESS;
}

/* The routine in slot @i, marked busy (NULL if none) */
static void *notify_take(int kind, int i, UINT8 *how)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    Notify *n = &g_notify[kind][i];
    void *fn = n->fn;
    if (fn) { n->busy++; *how = n->how; }
    spin_unlock_irqrestore(&g_lock, s);
    return fn;
}

static void notify_done(int kind, int i)
{
    __atomic_sub_fetch(&g_notify[kind][i].busy, 1, __ATOMIC_RELEASE);
}

NT_API NTSTATUS nt_PsSetCreateProcessNotifyRoutine(void *fn, UINT8 remove)
{
    return remove ? notify_remove(K_PROCESS, fn) : notify_add(K_PROCESS, fn, 0);
}

static NTSTATUS process_ex(void *fn, UINT8 remove, UINT8 how)
{
    if (remove) return notify_remove(K_PROCESS, fn);
    const char *image;
    if (!integrity_checked(fn, &image)) {
        kprintf("[NT] %s: PsSetCreateProcessNotifyRoutine%s refused: the image is not linked with /INTEGRITYCHECK\n",
                image, how == 2 ? "Ex2" : "Ex");
        return ST_ACCESS_DENIED;
    }
    return notify_add(K_PROCESS, fn, how);
}

NT_API NTSTATUS nt_PsSetCreateProcessNotifyRoutineEx(void *fn, UINT8 remove) { return process_ex(fn, remove, 1); }

/* (PsCreateProcessNotifySubsystems, 0, the only type: NovaOS has no
 * subsystem processes beside Win32 ones, so it is the Ex routine) */
NT_API NTSTATUS nt_PsSetCreateProcessNotifyRoutineEx2(int type, void *fn, UINT8 remove)
{
    if (type != 0) return STATUS_INVALID_PARAMETER;
    return process_ex(fn, remove, 2);
}

NT_API NTSTATUS nt_PsSetCreateThreadNotifyRoutine(void *fn) { return notify_add(K_THREAD, fn, 0); }

/* (PsCreateThreadNotifyNonSystem 0, PsCreateThreadNotifySubsystems 1) */
NT_API NTSTATUS nt_PsSetCreateThreadNotifyRoutineEx(int type, void *fn)
{
    if (type != 0 && type != 1) return STATUS_INVALID_PARAMETER;
    return notify_add(K_THREAD, fn, 1);
}

NT_API NTSTATUS nt_PsRemoveCreateThreadNotifyRoutine(void *fn) { return notify_remove(K_THREAD, fn); }
NT_API NTSTATUS nt_PsSetLoadImageNotifyRoutine(void *fn) { return notify_add(K_IMAGE, fn, 0); }

/* (PS_IMAGE_NOTIFY_CONFLICTING_ARCHITECTURE, 1: 32-bit images too, which
 * every routine is told of here) */
NT_API NTSTATUS nt_PsSetLoadImageNotifyRoutineEx(void *fn, UINT64 flags)
{
    if (flags & ~UINT64_C(1)) return STATUS_INVALID_PARAMETER;
    return notify_add(K_IMAGE, fn, 1);
}

NT_API NTSTATUS nt_PsRemoveLoadImageNotifyRoutine(void *fn) { return notify_remove(K_IMAGE, fn); }

/* -----------------------------------------------------------------------
 * Telling the routines
 * ----------------------------------------------------------------------- */
typedef void (NT_API *ProcessFn)(void *parent, void *pid, UINT8 create);
typedef void (NT_API *ProcessExFn)(void *process, void *pid, void *info);
typedef void (NT_API *ThreadFn)(void *pid, void *tid, UINT8 create);
typedef void (NT_API *ImageFn)(NT_UNICODE_STRING *name, void *pid, void *info);

typedef struct {                       /* PS_CREATE_NOTIFY_INFO (x64) */
    UINT64              Size;
    UINT32              Flags;         /* FileOpenNameAvailable: 1 */
    UINT32              pad;
    void               *ParentProcessId;
    void               *CreatingProcess, *CreatingThread;   /* CLIENT_ID */
    void               *FileObject;
    NT_UNICODE_STRING  *ImageFileName;
    NT_UNICODE_STRING  *CommandLine;
    NTSTATUS            CreationStatus;
    UINT32              pad2;
} PsCreateInfo;
_Static_assert(sizeof(PsCreateInfo) == 0x48, "PS_CREATE_NOTIFY_INFO");

typedef struct {                       /* IMAGE_INFO (x64) */
    UINT32  Properties;                /* ImageAddressingMode (3: 32-bit addressing) | SystemModeImage << 8 */
    UINT32  pad;
    void   *ImageBase;
    UINT32  ImageSelector, pad2;
    UINT64  ImageSize;
    UINT32  ImageSectionNumber, pad3;
} ImageInfo;
_Static_assert(sizeof(ImageInfo) == 0x28, "IMAGE_INFO");

/* @s (UTF-8) as a UNICODE_STRING in @buf (@cap units), @prefix before it */
static void ustr(NT_UNICODE_STRING *u, NT_WCHAR *buf, UINT32 cap, const char *prefix, const char *s)
{
    UINT32 n = 0;
    for (const char *c = prefix; c && *c && n < cap - 1; c++) buf[n++] = (UINT8)*c;
    UINT32 need = um_utf16_from_utf8(s, NULL);
    if (n + need < cap) n += um_utf16_from_utf8(s, (UINT8 *)(buf + n));
    buf[n] = 0;
    u->Buffer = buf;
    u->Length = (UINT16)(2 * n);
    u->MaximumLength = (UINT16)(2 * n + 2);
}

UINT32 NtPsNotifyProcessCreate(UmProcess *p, const char *image_path, const char *cmdline)
{
    if (!__atomic_load_n(&g_count, __ATOMIC_ACQUIRE)) return 0;
    UmObject *self = self_of(p);
    UINT32 refused = 0;
    UINT32 cap = 32768;
    NT_WCHAR *image = kmalloc(2 * 300), *cmd = kmalloc(2 * cap);
    NT_UNICODE_STRING uimage, ucmd;
    if (image) ustr(&uimage, image, 300, "\\??\\", image_path);
    if (cmd) ustr(&ucmd, cmd, cap, NULL, cmdline ? cmdline : "");
    UmProcess *me = UmCurrent();
    UmThread *mt = UmCurrentThread();
    for (int i = 0; i < NOTIFY_MAX; i++) {
        UINT8 how;
        void *fn = notify_take(K_PROCESS, i, &how);
        if (!fn) continue;
        if (!how) ((ProcessFn)fn)((void *)(uintptr_t)p->parent_pid, (void *)(uintptr_t)p->pid, 1);
        else if (self && image && cmd) {
            PsCreateInfo ci;
            memset(&ci, 0, sizeof(ci));
            ci.Size = sizeof(ci);
            ci.Flags = 1;                                    /* FileOpenNameAvailable */
            ci.ParentProcessId = (void *)(uintptr_t)p->parent_pid;
            ci.CreatingProcess = (void *)(uintptr_t)(me ? me->pid : 4);
            ci.CreatingThread = (void *)(uintptr_t)(mt ? mt->tid : 0);
            ci.ImageFileName = &uimage;
            ci.CommandLine = &ucmd;
            ((ProcessExFn)fn)(self, (void *)(uintptr_t)p->pid, &ci);
            if (ci.CreationStatus < 0 && !refused) {
                refused = (UINT32)ci.CreationStatus;
                kprintf("[NT] %s refused process %s (PID %u): 0x%08x\n", image_of(fn), p->name, p->pid, refused);
            }
        }
        notify_done(K_PROCESS, i);
    }
    kfree(image);
    kfree(cmd);
    return refused;
}

void NtPsNotifyProcessExit(UmProcess *p)
{
    if (__atomic_load_n(&g_count, __ATOMIC_ACQUIRE)) {
        UmObject *self = self_of(p);
        for (int i = 0; i < NOTIFY_MAX; i++) {
            UINT8 how;
            void *fn = notify_take(K_PROCESS, i, &how);
            if (!fn) continue;
            if (!how) ((ProcessFn)fn)((void *)(uintptr_t)p->parent_pid, (void *)(uintptr_t)p->pid, 0);
            else if (self) ((ProcessExFn)fn)(self, (void *)(uintptr_t)p->pid, NULL);
            notify_done(K_PROCESS, i);
        }
    }
    IrqState s = ob_lock();                                  /* (no more calls about it: let its object go) */
    UmObject *o = p->nt_self;
    p->nt_self = NULL;
    ob_unlock(s);
    if (o) um_ob_unref(o);
}

void NtPsNotifyThread(UINT32 pid, UINT32 tid, bool create)
{
    if (!__atomic_load_n(&g_count, __ATOMIC_ACQUIRE)) return;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        UINT8 how;
        void *fn = notify_take(K_THREAD, i, &how);
        if (!fn) continue;
        ((ThreadFn)fn)((void *)(uintptr_t)pid, (void *)(uintptr_t)tid, create);
        notify_done(K_THREAD, i);
    }
}

void NtPsNotifyImage(UINT32 pid, const char *path, UINT64 base, UINT64 size, bool system)
{
    if (!__atomic_load_n(&g_count, __ATOMIC_ACQUIRE)) return;
    NT_WCHAR *name = NULL;
    NT_UNICODE_STRING u;
    for (int i = 0; i < NOTIFY_MAX; i++) {
        UINT8 how;
        void *fn = notify_take(K_IMAGE, i, &how);
        if (!fn) continue;
        if (!name && (name = kmalloc(2 * 300)))             /* \Device\HarddiskVolume1\Windows\System32\ntdll.dll */
            ustr(&u, name, 300, "\\Device\\HarddiskVolume1", path[0] && path[1] == ':' ? path + 2 : path);
        ImageInfo ii;
        memset(&ii, 0, sizeof(ii));
        ii.Properties = 3 | (system ? 1u << 8 : 0);          /* IMAGE_ADDRESSING_MODE_32BIT, SystemModeImage */
        ii.ImageBase = (void *)(uintptr_t)base;
        ii.ImageSize = size;
        if (name) ((ImageFn)fn)(&u, (void *)(uintptr_t)pid, &ii);
        notify_done(K_IMAGE, i);
    }
    kfree(name);
}

/* -----------------------------------------------------------------------
 * Ob callbacks
 * ----------------------------------------------------------------------- */
typedef struct {                       /* OB_OPERATION_REGISTRATION */
    void  **ObjectType;                /* POBJECT_TYPE *: &PsProcessType or &PsThreadType */
    UINT32  Operations, pad;           /* OB_OPERATION_HANDLE_CREATE 1, _DUPLICATE 2 */
    void   *PreOperation, *PostOperation;
} ObOpReg;

typedef struct {                       /* OB_CALLBACK_REGISTRATION */
    UINT16             Version, OperationRegistrationCount;
    UINT32             pad;
    NT_UNICODE_STRING  Altitude;
    void              *RegistrationContext;
    ObOpReg           *OperationRegistration;
} ObCallbackReg;

typedef struct {                       /* OB_PRE_CREATE_HANDLE_INFORMATION / _DUPLICATE_ */
    UINT32  DesiredAccess, OriginalDesiredAccess;
    void   *SourceProcess, *TargetProcess;   /* (duplicate only) */
} ObPreParams;

typedef struct {                       /* OB_PRE_OPERATION_INFORMATION */
    UINT32       Operation, Flags;     /* Flags: KernelHandle 1 */
    void        *Object, *ObjectType, *CallContext;
    ObPreParams *Parameters;
} ObPreInfo;

typedef struct {                       /* OB_POST_OPERATION_INFORMATION */
    UINT32    Operation, Flags;
    void     *Object, *ObjectType, *CallContext;
    NTSTATUS  ReturnStatus;
    UINT32    pad;
    UINT32   *Parameters;              /* OB_POST_CREATE_HANDLE_INFORMATION: GrantedAccess */
} ObPostInfo;
_Static_assert(sizeof(ObPreInfo) == 0x28 && sizeof(ObPostInfo) == 0x30, "OB_*_OPERATION_INFORMATION");

typedef int  (NT_API *ObPreFn)(void *ctx, ObPreInfo *info);
typedef void (NT_API *ObPostFn)(void *ctx, ObPostInfo *info);

NT_API UINT16 nt_ObGetFilterVersion(void) { return 0x0100; }   /* OB_FLT_REGISTRATION_VERSION */

static bool same_altitude(const NT_WCHAR *a, const NT_UNICODE_STRING *b)
{
    UINT32 n = b->Length / 2;
    for (UINT32 i = 0; i < n; i++) if (a[i] != b->Buffer[i]) return false;
    return a[n] == 0;
}

NT_API NTSTATUS nt_ObRegisterCallbacks(ObCallbackReg *r, void **handle)
{
    if (!r || !handle || (r->Version & 0xFF00) != 0x0100 || !r->OperationRegistrationCount ||
        r->OperationRegistrationCount > 4 || !r->Altitude.Length || r->Altitude.Length / 2 >= 32 || !r->Altitude.Buffer)
        return STATUS_INVALID_PARAMETER;
    ObReg *g = kzalloc(sizeof(*g));
    if (!g) return STATUS_INSUFFICIENT_RESOURCES;
    const char *image = "a driver";
    for (int i = 0; i < r->OperationRegistrationCount; i++) {
        ObOpReg *o = &r->OperationRegistration[i];
        void *type = o->ObjectType ? *o->ObjectType : NULL;
        bool ok = (type == &g_process_type || type == &g_thread_type) && (o->Operations & 3) &&
                  (o->PreOperation || o->PostOperation);
        if (!ok) { kfree(g); return STATUS_INVALID_PARAMETER; }
        if ((o->PreOperation && !integrity_checked(o->PreOperation, &image)) ||
            (o->PostOperation && !integrity_checked(o->PostOperation, &image))) {
            kprintf("[NT] %s: ObRegisterCallbacks refused: the image is not linked with /INTEGRITYCHECK\n", image);
            kfree(g);
            return ST_ACCESS_DENIED;
        }
        g->op[i] = (ObOp){ type, o->Operations & 3, o->PreOperation, o->PostOperation };
    }
    g->magic = OBREG_MAGIC;
    g->ctx = r->RegistrationContext;
    g->nops = r->OperationRegistrationCount;
    for (UINT32 i = 0; i < r->Altitude.Length / 2u; i++) g->altitude[i] = r->Altitude.Buffer[i];
    IrqState s = spin_lock_irqsave(&g_lock);
    for (ObReg *x = g_obregs; x; x = x->next)
        if (same_altitude(x->altitude, &r->Altitude)) {
            spin_unlock_irqrestore(&g_lock, s);
            kfree(g);
            return ST_ALTITUDE_COLLISION;
        }
    g->next = g_obregs;
    g_obregs = g;
    g_count++;
    spin_unlock_irqrestore(&g_lock, s);
    *handle = g;
    char alt[32];
    int n = 0;
    for (; g->altitude[n] && n < 31; n++) alt[n] = (char)g->altitude[n];
    alt[n] = 0;
    kprintf("[NT] %s registered Ob callbacks at altitude %s (%d operation%s)\n", image, alt, g->nops, g->nops == 1 ? "" : "s");
    return STATUS_SUCCESS;
}

NT_API void nt_ObUnRegisterCallbacks(void *handle)
{
    ObReg *g = handle;
    IrqState s = spin_lock_irqsave(&g_lock);
    ObReg **pp = &g_obregs;
    while (*pp && *pp != g) pp = &(*pp)->next;
    bool found = *pp != NULL;
    if (found) { *pp = g->next; g_count--; }
    spin_unlock_irqrestore(&g_lock, s);
    if (!found) return;
    while (__atomic_load_n(&g->busy, __ATOMIC_ACQUIRE)) sched_yield();
    kprintf("[NT] %s removed its Ob callbacks\n", image_of(g->op[0].pre ? g->op[0].pre : g->op[0].post));
    g->magic = 0;
    kfree(g);
}

/* Each registration's operations for @o's type, pre or post, in turn */
static void ob_each(UmObject *o, bool dup, bool post, ObPreParams *pp, UINT32 granted, UINT32 status)
{
    void *type = o->type == UO_THREAD ? (void *)&g_thread_type : (void *)&g_process_type;
    UINT32 op = dup ? 2 : 1;
    for (int k = 0; ; k++) {
        IrqState s = spin_lock_irqsave(&g_lock);             /* the k-th registration, held busy */
        ObReg *g = g_obregs;
        for (int i = 0; g && i < k; i++) g = g->next;
        if (g) g->busy++;
        spin_unlock_irqrestore(&g_lock, s);
        if (!g) break;
        for (int i = 0; i < g->nops; i++) {
            ObOp *x = &g->op[i];
            if (x->type != type || !(x->ops & op)) continue;
            if (!post && x->pre) {
                ObPreInfo info = { op, 0, o, type, NULL, pp };
                ((ObPreFn)x->pre)(g->ctx, &info);
            } else if (post && x->post) {
                ObPostInfo info = { op, 0, o, type, NULL, (NTSTATUS)status, 0, &granted };
                ((ObPostFn)x->post)(g->ctx, &info);
            }
        }
        __atomic_sub_fetch(&g->busy, 1, __ATOMIC_RELEASE);
    }
}

UINT32 NtObPreOperation(UmObject *o, bool dup, UINT32 access)
{
    if (!__atomic_load_n(&g_count, __ATOMIC_ACQUIRE) || !g_obregs) return access;
    if (o->nt_magic != UM_NT_OBJECT) {
        UmProcess *q = o->type == UO_PROCESS ? o->proc : ((UmThread *)o)->proc;
        um_nt_object(o, o->type == UO_PROCESS ? (q ? q->pid : 0) : ((UmThread *)o)->tid, q ? q->pid : 0);
    }
    ObPreParams pp = { access, access, NULL, NULL };
    ob_each(o, dup, false, &pp, 0, 0);
    return access & pp.DesiredAccess;                       /* (a callback may take rights away, never add them) */
}

void NtObPostOperation(UmObject *o, bool dup, UINT32 granted, UINT32 status)
{
    if (!__atomic_load_n(&g_count, __ATOMIC_ACQUIRE) || !g_obregs) return;
    ob_each(o, dup, true, NULL, granted, status);
}

/* -----------------------------------------------------------------------
 * A driver unloads: its routines go with it
 * ----------------------------------------------------------------------- */
void NtPsDriverGone(NtDriver *drv)
{
    if (!drv->base) return;
    for (int kind = 0; kind < K_KINDS; kind++)
        for (int i = 0; i < NOTIFY_MAX; i++) {
            void *fn = g_notify[kind][i].fn;
            if (fn && (UINT64)(uintptr_t)fn - drv->base < drv->size) {
                kprintf("[NT] %s unloaded with a %s notify routine registered: removed\n", drv->image, g_kind_name[kind]);
                notify_remove(kind, fn);
            }
        }
    for (;;) {
        IrqState s = spin_lock_irqsave(&g_lock);
        ObReg *g = g_obregs;
        while (g && !(g->op[0].pre && (UINT64)(uintptr_t)g->op[0].pre - drv->base < drv->size) &&
               !(g->op[0].post && (UINT64)(uintptr_t)g->op[0].post - drv->base < drv->size)) g = g->next;
        spin_unlock_irqrestore(&g_lock, s);
        if (!g) break;
        kprintf("[NT] %s unloaded with Ob callbacks registered: removed\n", drv->image);
        nt_ObUnRegisterCallbacks(g);
    }
}
