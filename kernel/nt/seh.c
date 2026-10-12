/*
 * seh.c — structured exception handling for kernel-mode drivers
 *
 * A Windows driver's __try/__except/__finally compile to the same thing
 * as in user mode: an entry in the image's .pdata (a RUNTIME_FUNCTION per
 * function), unwind codes in .xdata, and a language handler
 * (__C_specific_handler) named there.  When a driver faults (an access
 * violation, say) or raises (ExRaiseStatus, a failed ProbeForRead), the
 * kernel walks the driver's frames with the x64 unwinder, calling each
 * frame's __C_specific_handler; a frame whose __except filter accepts the
 * exception has its __finally blocks run and then resumes at the __except
 * body, exactly as ntdll does for user code (userland/ntdll/ntdll_exc.c).
 *
 * The walk only crosses frames that have unwind data — driver (and HAL,
 * and other .sys) frames.  The ELF kernel itself is built without .pdata,
 * so once the walk reaches a kernel frame with no RUNTIME_FUNCTION it
 * stops: a fault in the kernel under a driver call, or one no driver
 * handles, is a bug check, as on Windows.
 *
 * Two ways in:
 *   - a CPU exception taken in ring 0 at a driver address: the trap
 *     handler (arch/x86_64/idt.c) calls NtKernelException with its
 *     InterruptFrame; a handled exception resumes by IRET into the
 *     __except body (the frame is edited in place).
 *   - a driver calling ExRaiseStatus / RtlRaiseException, or a probe
 *     failing: the context is captured here and the same walk runs,
 *     resuming (seh_resume) into the handler without returning.
 */
#include "nt.h"
#include "../ldr/ldr.h"
#include "../ke/printf.h"
#include "../ke/irql.h"
#include "../lib/string.h"
#include "../arch/x86_64/idt.h"

/* The x64 register state the unwinder updates.  Rax..R15 are indexed by
 * the register numbers the unwind codes use (0 = RAX, 4 = RSP, 5 = RBP,
 * 8..15 = R8..R15); Rip and Rsp are kept apart for clarity. */
typedef struct {
    UINT64 Reg[16];                  /* by unwind register number */
    UINT64 Rip;
} SehContext;
enum { R_RAX, R_RCX, R_RDX, R_RBX, R_RSP, R_RBP, R_RSI, R_RDI,
       R_R8, R_R9, R_R10, R_R11, R_R12, R_R13, R_R14, R_R15 };

typedef struct {
    NTSTATUS ExceptionCode;
    UINT32   ExceptionFlags;
    void    *ExceptionRecordChain;
    void    *ExceptionAddress;
    UINT32   NumberParameters;
    UINT64   ExceptionInformation[15];
} SehRecord;

#define EXCEPTION_NONCONTINUABLE 0x01
#define EXCEPTION_UNWINDING      0x02
#define EXCEPTION_EXIT_UNWIND    0x04
#define EXCEPTION_TARGET_UNWIND  0x20

/* DISPATCHER_CONTEXT as __C_specific_handler reads it (the fields it
 * touches: the scope table in HandlerData, the image base, the faulting
 * PC, the scope index for a collided unwind, and the unwind target). */
typedef struct {
    UINT64        ControlPc;
    UINT64        ImageBase;
    void         *FunctionEntry;
    UINT64        EstablisherFrame;
    UINT64        TargetIp;
    SehContext   *ContextRecord;
    void         *LanguageHandler;
    void         *HandlerData;
    UINT64        ScopeIndex;
} SehDispatcher;

typedef enum { ExceptionContinueExecution, ExceptionContinueSearch,
               ExceptionNestedException, ExceptionCollidedUnwind } SehDisposition;

typedef SehDisposition (NT_API *SehHandler)(SehRecord *, void *frame, SehContext *, SehDispatcher *);

/* .pdata / .xdata */
typedef struct { UINT32 Begin, End, UnwindData; } RuntimeFunction;
typedef struct {
    UINT8 VersionFlags;              /* Version:3 Flags:5 */
    UINT8 SizeOfProlog;
    UINT8 CountOfCodes;
    UINT8 FrameRegOff;               /* FrameReg:4 FrameRegOffset:4 (*16) */
    UINT16 Code[1];                  /* CountOfCodes of these, padded even */
} UnwindInfo;
#define UWOP_PUSH_NONVOL     0
#define UWOP_ALLOC_LARGE     1
#define UWOP_ALLOC_SMALL     2
#define UWOP_SET_FPREG       3
#define UWOP_SAVE_NONVOL     4
#define UWOP_SAVE_NONVOL_FAR 5
#define UWOP_SAVE_XMM128     8
#define UWOP_SAVE_XMM128_FAR 9
#define UWOP_PUSH_MACHFRAME  10
#define UNW_FLAG_EHANDLER    0x01
#define UNW_FLAG_UHANDLER    0x02
#define UNW_FLAG_CHAININFO   0x04

typedef struct { UINT32 Begin, End, Handler, Target; } ScopeRecord;
typedef struct { UINT32 Count; ScopeRecord Record[1]; } ScopeTable;

/* The driver image's .pdata for an address: its base and the sorted
 * RUNTIME_FUNCTION table, or NULL when @pc is not in a driver (a kernel
 * frame, which has no unwind data). */
static RuntimeFunction *pdata_for(UINT64 pc, UINT64 *base_out, UINT32 *count_out)
{
    NtDriver *drv = NtDrvByAddress(pc);
    if (!drv) return NULL;
    UINT64 base = drv->base;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)(uintptr_t)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(uintptr_t)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY *d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!d->VirtualAddress || !d->Size) return NULL;
    *base_out = base;
    *count_out = d->Size / sizeof(RuntimeFunction);
    return (RuntimeFunction *)(uintptr_t)(base + d->VirtualAddress);
}

/* The RUNTIME_FUNCTION covering RVA @rva, following chained entries to
 * the primary one, or NULL */
static RuntimeFunction *lookup(UINT64 base, RuntimeFunction *t, UINT32 n, UINT32 rva)
{
    int lo = 0, hi = (int)n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (rva < t[mid].Begin) hi = mid - 1;
        else if (rva >= t[mid].End) lo = mid + 1;
        else {
            RuntimeFunction *f = &t[mid];
            while (f->UnwindData & 1) f = (RuntimeFunction *)(uintptr_t)(base + (f->UnwindData & ~1u));
            return f;
        }
    }
    return NULL;
}

/* Undo one frame's prologue at @ctx (the registers the prologue saved are
 * restored to the caller's), and return the frame's language handler of
 * kind @type (UNW_FLAG_EHANDLER or UNW_FLAG_UHANDLER), with its data and
 * the establisher frame base. */
static SehHandler virtual_unwind(UINT32 type, UINT64 base, UINT64 pc, RuntimeFunction *f,
                                 SehContext *ctx, void **handler_data, UINT64 *frame_out)
{
    while (f->UnwindData & 1) f = (RuntimeFunction *)(uintptr_t)(base + (f->UnwindData & ~1u));
    if (handler_data) *handler_data = NULL;
    UnwindInfo *ui;
    UINT64 frame_base = ctx->Reg[R_RSP];
    int in_prolog = 0, first = 1;
    for (;;) {
        ui = (UnwindInfo *)(uintptr_t)(base + f->UnwindData);
        int n = ui->CountOfCodes;
        UINT8 framereg = ui->FrameRegOff & 0xF;
        UINT32 off = (UINT32)(pc - (base + f->Begin));
        int prolog = pc >= base + f->Begin && off < ui->SizeOfProlog;
        if (first) in_prolog = prolog;

        frame_base = ctx->Reg[R_RSP];
        if (framereg) {
            int set = !prolog;
            for (int i = 0; prolog && i < n; ) {
                UINT16 c = ui->Code[i];
                int op = (c >> 8) & 0xF, info = (c >> 12) & 0xF;
                if (op == UWOP_SET_FPREG && (c & 0xFF) <= off) set = 1;
                i += op == UWOP_ALLOC_LARGE ? (info ? 3 : 2)
                   : (op == UWOP_SAVE_NONVOL || op == UWOP_SAVE_XMM128) ? 2
                   : (op == UWOP_SAVE_NONVOL_FAR || op == UWOP_SAVE_XMM128_FAR) ? 3 : 1;
            }
            if (set) frame_base = ctx->Reg[framereg] - (ui->FrameRegOff >> 4) * 16ull;
        }
        first = 0;

        for (int i = 0; i < n; ) {
            UINT16 c = ui->Code[i];
            int codeoff = c & 0xFF, op = (c >> 8) & 0xF, info = (c >> 12) & 0xF;
            int slots = 1, applied = !prolog || codeoff <= (int)off;
            switch (op) {
            case UWOP_PUSH_NONVOL:
                if (applied) { ctx->Reg[info] = *(UINT64 *)(uintptr_t)ctx->Reg[R_RSP]; ctx->Reg[R_RSP] += 8; }
                break;
            case UWOP_ALLOC_LARGE:
                if (info == 0) { slots = 2; if (applied) ctx->Reg[R_RSP] += (ui->Code[i + 1]) * 8ull; }
                else { slots = 3; if (applied) ctx->Reg[R_RSP] += ui->Code[i + 1] | ((UINT32)ui->Code[i + 2] << 16); }
                break;
            case UWOP_ALLOC_SMALL:
                if (applied) ctx->Reg[R_RSP] += info * 8ull + 8;
                break;
            case UWOP_SET_FPREG:
                if (applied) ctx->Reg[R_RSP] = frame_base;
                break;
            case UWOP_SAVE_NONVOL:
                slots = 2;
                if (applied) ctx->Reg[info] = *(UINT64 *)(uintptr_t)(frame_base + ui->Code[i + 1] * 8ull);
                break;
            case UWOP_SAVE_NONVOL_FAR:
                slots = 3;
                if (applied) ctx->Reg[info] = *(UINT64 *)(uintptr_t)(frame_base + (ui->Code[i + 1] | ((UINT32)ui->Code[i + 2] << 16)));
                break;
            case UWOP_SAVE_XMM128: slots = 2; break;      /* (NovaOS does not restore XMM while unwinding) */
            case UWOP_SAVE_XMM128_FAR: slots = 3; break;
            case UWOP_PUSH_MACHFRAME:
                if (applied) {
                    UINT64 sp = ctx->Reg[R_RSP] + (info ? 8 : 0);
                    ctx->Rip = *(UINT64 *)(uintptr_t)(sp + 0);
                    ctx->Reg[R_RSP] = *(UINT64 *)(uintptr_t)(sp + 24);
                }
                break;
            default: break;
            }
            i += slots;
        }
        if (!((ui->VersionFlags >> 3) & UNW_FLAG_CHAININFO)) break;
        /* chained: the parent's prologue (all run) unwinds next */
        f = (RuntimeFunction *)&ui->Code[(n + 1) & ~1];
        while (f->UnwindData & 1) f = (RuntimeFunction *)(uintptr_t)(base + (f->UnwindData & ~1u));
        pc = base + f->End;
    }
    if (frame_out) *frame_out = frame_base;
    ctx->Rip = *(UINT64 *)(uintptr_t)ctx->Reg[R_RSP];
    ctx->Reg[R_RSP] += 8;

    UINT8 flags = ui->VersionFlags >> 3;
    if ((flags & type & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) && !in_prolog) {
        UINT32 *p = (UINT32 *)&ui->Code[(ui->CountOfCodes + 1) & ~1];
        if (handler_data) *handler_data = p + 1;
        return (SehHandler)(uintptr_t)(base + *p);
    }
    return NULL;
}

/* Resume execution at @ctx (loads the general registers and jumps to
 * Rip on the unwound stack); does not return. */
static __attribute__((noreturn)) void seh_resume(SehContext *ctx)
{
    __asm__ volatile(
        "movq %0, %%rsp\n\t"           /* the caller is done with its frame */
        "pushq %1\n\t"                 /* return address = Rip */
        "movq %2, %%rax\n\t"
        "movq 0x08(%%rax), %%rcx\n\t" "movq 0x10(%%rax), %%rdx\n\t" "movq 0x18(%%rax), %%rbx\n\t"
        "movq 0x28(%%rax), %%rbp\n\t" "movq 0x30(%%rax), %%rsi\n\t" "movq 0x38(%%rax), %%rdi\n\t"
        "movq 0x40(%%rax), %%r8\n\t"  "movq 0x48(%%rax), %%r9\n\t"  "movq 0x50(%%rax), %%r10\n\t"
        "movq 0x58(%%rax), %%r11\n\t" "movq 0x60(%%rax), %%r12\n\t" "movq 0x68(%%rax), %%r13\n\t"
        "movq 0x70(%%rax), %%r14\n\t" "movq 0x78(%%rax), %%r15\n\t"
        "movq 0x00(%%rax), %%rax\n\t"
        "ret\n\t"
        : : "r"(ctx->Reg[R_RSP] - 8), "r"(ctx->Rip), "r"(ctx) : "memory");
    __builtin_unreachable();
}

/* Run the frames' handlers from the fault/raise down.  When a frame's
 * __except accepts the exception, its __finally blocks are run (an unwind
 * pass) and *@target receives where and with what stack to resume; 1 is
 * returned.  0 when nothing handled it. */
static int dispatch(SehRecord *rec, SehContext *ctx, SehContext *target)
{
    SehContext cur = *ctx;
    for (;;) {
        UINT64 base = 0, frame = 0;
        UINT32 count = 0;
        RuntimeFunction *tab = pdata_for(cur.Rip, &base, &count);
        if (!tab) break;                              /* a kernel frame: no unwind data */
        RuntimeFunction *f = lookup(base, tab, count, (UINT32)(cur.Rip - base));
        if (!f) {                                     /* a leaf: return address on top of stack */
            cur.Rip = *(UINT64 *)(uintptr_t)cur.Reg[R_RSP];
            cur.Reg[R_RSP] += 8;
            if (!cur.Rip) break;
            continue;
        }
        SehContext before = cur;
        void *hdata = NULL;
        SehHandler handler = virtual_unwind(UNW_FLAG_EHANDLER, base, before.Rip, f, &cur, &hdata, &frame);
        if (handler) {
            SehDispatcher dc = { before.Rip, base, f, frame, 0, &before, (void *)handler, hdata, 0 };
            SehDisposition d = handler(rec, (void *)(uintptr_t)frame, &before, &dc);
            if (d == ExceptionContinueExecution) return 0;   /* (continue is not used by drivers) */
            /* __C_specific_handler runs RtlUnwindEx itself for EXECUTE_HANDLER,
             * which resumes — it does not return ExceptionContinueExecution
             * here.  Reaching this point means ContinueSearch. */
        }
        if (cur.Rip == before.Rip && cur.Reg[R_RSP] == before.Reg[R_RSP]) break;
        if (!cur.Rip) break;
    }
    (void)target;
    return 0;
}

/* The unwind half: run __finally handlers from the raise/fault frame down
 * to @target_frame, then resume at @target_ip with @ctx's register state
 * as unwound to that frame.  Called by __C_specific_handler. */
static UINT64 g_dummy_target;
NT_API void nt_RtlUnwindEx(void *target_frame, void *target_ip, SehRecord *rec, void *retval,
                           SehContext *start, void *hist)
{
    (void)hist;
    SehRecord local;
    if (!rec) { memset(&local, 0, sizeof(local)); rec = &local; }
    rec->ExceptionFlags |= EXCEPTION_UNWINDING;
    SehContext cur = *start;
    for (;;) {
        UINT64 base = 0, frame = 0;
        UINT32 count = 0;
        RuntimeFunction *tab = pdata_for(cur.Rip, &base, &count);
        if (!tab) break;
        RuntimeFunction *f = lookup(base, tab, count, (UINT32)(cur.Rip - base));
        if (!f) { cur.Rip = *(UINT64 *)(uintptr_t)cur.Reg[R_RSP]; cur.Reg[R_RSP] += 8; if (!cur.Rip) break; continue; }
        SehContext before = cur;
        void *hdata = NULL;
        SehHandler handler = virtual_unwind(UNW_FLAG_UHANDLER, base, before.Rip, f, &cur, &hdata, &frame);
        int is_target = target_frame && frame == (UINT64)(uintptr_t)target_frame;
        if (handler) {
            SehDispatcher dc = { before.Rip, base, f, frame, (UINT64)(uintptr_t)target_ip, &before, (void *)handler, hdata, 0 };
            UINT32 saved = rec->ExceptionFlags;
            if (is_target) rec->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;
            handler(rec, (void *)(uintptr_t)frame, &before, &dc);   /* runs __finally cleanup */
            rec->ExceptionFlags = saved;
        }
        if (is_target) {
            before.Rip = (UINT64)(uintptr_t)target_ip;
            before.Reg[R_RAX] = (UINT64)(uintptr_t)retval;
            seh_resume(&before);
        }
        if (target_frame && frame > (UINT64)(uintptr_t)target_frame) break;
        if (cur.Rip == before.Rip && cur.Reg[R_RSP] == before.Reg[R_RSP]) break;
        if (!cur.Rip) break;
    }
    /* target not found (or exit unwind): resume at target_ip on target_frame */
    SehContext end = *start;
    end.Rip = (UINT64)(uintptr_t)target_ip;
    end.Reg[R_RSP] = target_frame ? (UINT64)(uintptr_t)target_frame : end.Reg[R_RSP];
    end.Reg[R_RAX] = (UINT64)(uintptr_t)retval;
    g_dummy_target = end.Rip;
    seh_resume(&end);
}

/* __C_specific_handler: the language handler for __try/__except/__finally
 * that clang and MSVC name in a driver's .xdata (ntdll's own, in the
 * kernel). */
#define EXCEPTION_EXECUTE_HANDLER    1
#define EXCEPTION_CONTINUE_SEARCH    0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
typedef LONG (NT_API *FilterFn)(void *info, void *frame);
typedef void (NT_API *FinallyFn)(UINT8 abnormal, void *frame);
typedef struct { SehRecord *ExceptionRecord; SehContext *ContextRecord; } SehPointers;

NT_API SehDisposition nt___C_specific_handler(SehRecord *rec, void *frame, SehContext *ctx, SehDispatcher *dc)
{
    ScopeTable *scope = dc->HandlerData;
    UINT64 base = dc->ImageBase;
    UINT32 control = (UINT32)(dc->ControlPc - base);
    int unwinding = (rec->ExceptionFlags & EXCEPTION_UNWINDING) != 0;

    for (UINT32 i = (UINT32)dc->ScopeIndex; i < scope->Count; i++) {
        UINT32 begin = scope->Record[i].Begin, end = scope->Record[i].End;
        UINT32 hdl = scope->Record[i].Handler, target = scope->Record[i].Target;
        if (control < begin || control >= end) continue;
        if (!unwinding) {
            if (target == 0) continue;                /* __finally: nothing on the search pass */
            LONG r;
            if (hdl == 1) r = EXCEPTION_EXECUTE_HANDLER;
            else { SehPointers ep = { rec, ctx }; r = ((FilterFn)(uintptr_t)(base + hdl))(&ep, frame); }
            if (r == EXCEPTION_CONTINUE_EXECUTION) return ExceptionContinueExecution;
            if (r == EXCEPTION_CONTINUE_SEARCH) continue;
            nt_RtlUnwindEx(frame, (void *)(uintptr_t)(base + target), rec,
                           (void *)(uintptr_t)(ULONG_PTR)rec->ExceptionCode, ctx, NULL);
            return ExceptionContinueExecution;        /* (unreached) */
        }
        if (rec->ExceptionFlags & EXCEPTION_TARGET_UNWIND) {
            UINT64 tip = dc->TargetIp - base;
            if (target ? tip == target : (tip >= begin && tip < end)) break;
        }
        if (target == 0 && hdl) {
            dc->ScopeIndex = i + 1;
            ((FinallyFn)(uintptr_t)(base + hdl))(1, frame);
        }
    }
    return ExceptionContinueSearch;
}

/* -----------------------------------------------------------------------
 * A CPU exception taken in ring 0 at a driver address (arch/x86_64/idt.c)
 * ----------------------------------------------------------------------- */
static NTSTATUS status_for_vector(UINT64 vector)
{
    switch (vector) {
    case 0:  return (NTSTATUS)0xC0000094;             /* INTEGER_DIVIDE_BY_ZERO */
    case 6:  return (NTSTATUS)0xC000001D;             /* ILLEGAL_INSTRUCTION */
    case 13: return (NTSTATUS)0xC0000005;             /* general protection: ACCESS_VIOLATION */
    case 14: return NT_STATUS_ACCESS_VIOLATION;
    default: return (NTSTATUS)0xC0000096;             /* PRIVILEGED_INSTRUCTION / other */
    }
}

/* Try to let a driver's __try/__except handle a ring-0 fault.  On success
 * the frame is edited to resume in the __except body (the trap handler
 * then IRETs) and true is returned; false means no driver frame handled
 * it, and the caller bug-checks. */
bool NtKernelException(InterruptFrame *f, UINT64 cr2)
{
    if (!NtDrvByAddress(f->rip)) return false;        /* not in driver code */
    SehContext ctx;
    /* A fault taken in ring 0 does not change rings, so the CPU pushed only
     * RIP/CS/RFLAGS (and the error code): InterruptFrame.rsp holds the
     * interrupted code's stack data, not the faulting RSP.  The real one is
     * where the CPU frame sits, i.e. the address of that rsp field. */
    UINT64 fault_rsp = (UINT64)(uintptr_t)&f->rsp;
    ctx.Reg[R_RAX] = f->rax; ctx.Reg[R_RCX] = f->rcx; ctx.Reg[R_RDX] = f->rdx; ctx.Reg[R_RBX] = f->rbx;
    ctx.Reg[R_RSP] = fault_rsp; ctx.Reg[R_RBP] = f->rbp; ctx.Reg[R_RSI] = f->rsi; ctx.Reg[R_RDI] = f->rdi;
    ctx.Reg[R_R8] = f->r8; ctx.Reg[R_R9] = f->r9; ctx.Reg[R_R10] = f->r10; ctx.Reg[R_R11] = f->r11;
    ctx.Reg[R_R12] = f->r12; ctx.Reg[R_R13] = f->r13; ctx.Reg[R_R14] = f->r14; ctx.Reg[R_R15] = f->r15;
    ctx.Rip = f->rip;

    SehRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.ExceptionCode = status_for_vector(f->vector);
    rec.ExceptionAddress = (void *)(uintptr_t)f->rip;
    if (f->vector == 14) {                            /* page fault: the two extra parameters */
        rec.NumberParameters = 2;
        rec.ExceptionInformation[0] = (f->error_code & 2) ? 1 : 0;   /* write */
        rec.ExceptionInformation[1] = cr2;
    }
    /* dispatch() resumes through seh_resume on a handled __except, which
     * loads the unwound registers and jumps — it never returns here.  If
     * it returns, nothing handled the fault. */
    dispatch(&rec, &ctx, NULL);
    return false;
}

/* -----------------------------------------------------------------------
 * Raising from a driver: ExRaiseStatus, RtlRaiseException, ProbeFor*
 *
 * The walk must start in the driver frame that called the export, so the
 * context is captured at the export's own entry: its return address is
 * the driver's call site, and the non-volatile registers (rbx, rbp, rsi,
 * rdi, r12-r15) still hold the driver's values after the C prologue has
 * only saved — not reassigned — them.  (Volatile registers are not needed
 * to resume an __except body.)  A raise from deep inside a kernel export,
 * such as a failed MmProbeAndLockPages, is not resumable this way and that
 * export does not raise; it reports the error through its return value, as
 * mm.c describes.
 * ----------------------------------------------------------------------- */
#define SEH_CAPTURE_CALLER(ctx) do {                                                        \
    __asm__ volatile(                                                                       \
        "movq %%rbx, 0x18(%0)\n\t" "movq %%rsi, 0x30(%0)\n\t" "movq %%rdi, 0x38(%0)\n\t"    \
        "movq %%r12, 0x60(%0)\n\t" "movq %%r13, 0x68(%0)\n\t" "movq %%r14, 0x70(%0)\n\t"    \
        "movq %%r15, 0x78(%0)\n\t"                                                          \
        : : "r"(&(ctx)) : "memory");                                                        \
    (ctx).Reg[R_RBP] = *(UINT64 *)__builtin_frame_address(0);                               \
    (ctx).Reg[R_RSP] = (UINT64)(uintptr_t)__builtin_frame_address(0) + 16;                  \
    (ctx).Rip = (UINT64)(uintptr_t)__builtin_return_address(0);                             \
} while (0)

static __attribute__((noreturn)) void raise_from(SehContext *ctx, NTSTATUS code, UINT32 nparams, const UINT64 *params)
{
    SehRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.ExceptionCode = code;
    rec.ExceptionAddress = (void *)(uintptr_t)ctx->Rip;
    rec.NumberParameters = nparams > 15 ? 15 : nparams;
    for (UINT32 i = 0; i < rec.NumberParameters; i++) rec.ExceptionInformation[i] = params[i];
    dispatch(&rec, ctx, NULL);
    KPANIC("*** STOP: 0x0000001E (KMODE_EXCEPTION_NOT_HANDLED): 0x%08x raised by a driver, nothing handled it",
           (UINT32)code);
}

NT_API __attribute__((noreturn)) void nt_ExRaiseStatus(NTSTATUS code)
{
    SehContext ctx; SEH_CAPTURE_CALLER(ctx);
    raise_from(&ctx, code, 0, NULL);
}
NT_API __attribute__((noreturn)) void nt_ExRaiseAccessViolation(void)
{
    SehContext ctx; SEH_CAPTURE_CALLER(ctx);
    raise_from(&ctx, NT_STATUS_ACCESS_VIOLATION, 0, NULL);
}
NT_API __attribute__((noreturn)) void nt_ExRaiseDatatypeMisalignment(void)
{
    SehContext ctx; SEH_CAPTURE_CALLER(ctx);
    raise_from(&ctx, NT_STATUS_DATATYPE_MISALIGNMENT, 0, NULL);
}

/* RtlRaiseException(EXCEPTION_RECORD*): the record a driver built */
NT_API __attribute__((noreturn)) void nt_RtlRaiseException(SehRecord *rec)
{
    SehContext ctx; SEH_CAPTURE_CALLER(ctx);
    rec->ExceptionAddress = (void *)(uintptr_t)ctx.Rip;
    dispatch(rec, &ctx, NULL);
    KPANIC("*** STOP: 0x0000001E (KMODE_EXCEPTION_NOT_HANDLED): 0x%08x raised by a driver, nothing handled it",
           (UINT32)rec->ExceptionCode);
}

/* ProbeForRead / ProbeForWrite: a driver checks a user buffer before
 * touching it; a bad one raises (caught by the driver's __try, as on
 * Windows).  NovaOS's user range is below 0x0000800000000000. */
#define USER_LIMIT  UINT64_C(0x0000800000000000)
NT_API void nt_ProbeForRead(const void *addr, UINT64 len, UINT64 align)
{
    UINT64 a = (UINT64)(uintptr_t)addr;
    if (!len) return;
    if (align && (a & (align - 1))) { SehContext ctx; SEH_CAPTURE_CALLER(ctx); raise_from(&ctx, NT_STATUS_DATATYPE_MISALIGNMENT, 0, NULL); }
    if (a + len < a || a + len > USER_LIMIT) { SehContext ctx; SEH_CAPTURE_CALLER(ctx); UINT64 p[2] = { 0, a }; raise_from(&ctx, NT_STATUS_ACCESS_VIOLATION, 2, p); }
}
NT_API void nt_ProbeForWrite(void *addr, UINT64 len, UINT64 align)
{
    UINT64 a = (UINT64)(uintptr_t)addr;
    if (!len) return;
    if (align && (a & (align - 1))) { SehContext ctx; SEH_CAPTURE_CALLER(ctx); raise_from(&ctx, NT_STATUS_DATATYPE_MISALIGNMENT, 0, NULL); }
    if (a + len < a || a + len > USER_LIMIT) { SehContext ctx; SEH_CAPTURE_CALLER(ctx); UINT64 p[2] = { 1, a }; raise_from(&ctx, NT_STATUS_ACCESS_VIOLATION, 2, p); }
}
