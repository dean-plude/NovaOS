/*
 * win11.c — kernel routines vendor drivers for Windows 11 import beyond
 * what the frameworks need (step 30.9: Intel's AX211 Wi-Fi driver,
 * Netwtw14.sys, imports every one of these)
 *
 *   C run-time  the secure string routines (strcpy_s and the like, with
 *               the C run-time's checks: a bad argument or a buffer too
 *               small returns EINVAL or ERANGE and leaves "" behind),
 *               strstr, strtok_s, atoi, rand
 *   Rtl         RtlTimeToTimeFields, RtlCaptureContext (the caller's
 *               registers, for its own crash records)
 *   Ex          ExSystemTimeToLocalTime (the time-zone bias now),
 *               ExGetFirmwareEnvironmentVariable (NovaOS keeps no UEFI
 *               runtime services: no variable is found)
 *   Ke          KeQueryLogicalProcessorRelationship (one group, one
 *               package, one NUMA node, a core per processor),
 *               KeCapturePersistentThreadState (no dump to capture into)
 *   Ps          PsGetCurrentThreadId
 *   Po          power-setting callbacks (registered, never called: NovaOS
 *               changes no power setting under a driver)
 *   Pcw         performance counters (registered; nothing reads them)
 *   IoWMI       a WMI consumer's calls (no WMI provider answers: the
 *               block is not found, as on a machine without that provider)
 *   WER         live kernel reports (ext-ms-win-ntos-werkernel-l1-1-1.dll,
 *               the API set Windows resolves to ntoskrnl): not supported,
 *               which a driver takes as "no report"
 *   __chkstk    the stack probe: NovaOS's kernel stacks are committed
 *               whole, so there is nothing to touch
 */
#include "nt.h"
#include "ntapi.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/timezone.h"
#include "../hal/rtc.h"
#include "../lib/string.h"
#include "../um/um.h"

#define EINVAL 22
#define ERANGE 34
#define STRUNCATE 80
#define TRUNCATE ((UINT64)-1)

#define ST_VARIABLE_NOT_FOUND ((NTSTATUS)0xC0000100)
#define ST_WMI_GUID_NOT_FOUND ((NTSTATUS)0xC0000295)

NT_API int nt__vsnprintf_s(char *buf, UINT64 size, UINT64 count, const char *fmt, __builtin_ms_va_list ap);

/* -----------------------------------------------------------------------
 * C run-time
 * ----------------------------------------------------------------------- */
NT_API int nt_strcmp(const char *a, const char *b) { return strcmp(a, b); }

NT_API UINT64 nt_strnlen(const char *s, UINT64 max)
{
    UINT64 n = 0;
    while (n < max && s[n]) n++;
    return n;
}

NT_API char *nt_strstr(const char *hay, const char *needle)
{
    UINT64 n = strlen(needle);
    if (!n) return (char *)hay;
    for (; *hay; hay++)
        if (*hay == *needle && !strncmp(hay, needle, n)) return (char *)hay;
    return NULL;
}

NT_API int nt_strcpy_s(char *dst, UINT64 size, const char *src)
{
    if (!dst || !size) return EINVAL;
    if (!src) { dst[0] = 0; return EINVAL; }
    UINT64 n = strlen(src);
    if (n >= size) { dst[0] = 0; return ERANGE; }
    memcpy(dst, src, n + 1);
    return 0;
}

NT_API int nt_strcat_s(char *dst, UINT64 size, const char *src)
{
    if (!dst || !size) return EINVAL;
    if (!src) { dst[0] = 0; return EINVAL; }
    UINT64 at = nt_strnlen(dst, size);
    if (at == size) { dst[0] = 0; return EINVAL; }   /* (not terminated) */
    UINT64 n = strlen(src);
    if (at + n >= size) { dst[0] = 0; return ERANGE; }
    memcpy(dst + at, src, n + 1);
    return 0;
}

/* At most @count characters of @src (_TRUNCATE: as many as fit, STRUNCATE) */
NT_API int nt_strncpy_s(char *dst, UINT64 size, const char *src, UINT64 count)
{
    if (!dst || !size) return EINVAL;
    if (!src) { dst[0] = 0; return count ? EINVAL : 0; }
    UINT64 n = nt_strnlen(src, count == TRUNCATE ? size : count);
    if (n >= size) {
        if (count == TRUNCATE) { memcpy(dst, src, size - 1); dst[size - 1] = 0; return STRUNCATE; }
        dst[0] = 0;
        return ERANGE;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}

static bool in_set(char c, const char *set)
{
    for (; *set; set++) if (*set == c) return true;
    return false;
}

NT_API char *nt_strtok_s(char *s, const char *delim, char **ctx)
{
    if (!delim || !ctx) return NULL;
    char *p = s ? s : *ctx;
    if (!p) return NULL;
    while (*p && in_set(*p, delim)) p++;
    if (!*p) { *ctx = p; return NULL; }
    char *tok = p;
    while (*p && !in_set(*p, delim)) p++;
    if (*p) *p++ = 0;
    *ctx = p;
    return tok;
}

NT_API int nt_vsprintf_s(char *buf, UINT64 size, const char *fmt, __builtin_ms_va_list ap)
{
    int n = nt__vsnprintf_s(buf, size, TRUNCATE, fmt, ap);
    if (n < 0 && buf && size) buf[0] = 0;               /* (too long: the C run-time's "" and -1) */
    return n;
}

NT_API int nt_sprintf_s(char *buf, UINT64 size, const char *fmt, ...)
{
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    int n = nt_vsprintf_s(buf, size, fmt, ap);
    __builtin_ms_va_end(ap);
    return n;
}

NT_API int nt__snprintf_s(char *buf, UINT64 size, UINT64 count, const char *fmt, ...)
{
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    int n = nt__vsnprintf_s(buf, size, count, fmt, ap);
    __builtin_ms_va_end(ap);
    return n;
}

NT_API long nt_atol(const char *s)
{
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    bool neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
    long v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    return neg ? -v : v;
}

NT_API int nt_atoi(const char *s) { return (int)nt_atol(s); }

/* The C run-time's own generator (MSVC's: the same sequence for a seed) */
static UINT32 g_rand = 1;
NT_API void nt_srand(UINT32 seed) { g_rand = seed; }
NT_API int nt_rand(void)
{
    g_rand = g_rand * 214013u + 2531011u;
    return (int)((g_rand >> 16) & 0x7FFF);
}

/* -----------------------------------------------------------------------
 * Time
 * ----------------------------------------------------------------------- */
typedef struct {
    INT16 Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday;
} NtTimeFields;

NT_API void nt_RtlTimeToTimeFields(const INT64 *time, NtTimeFields *tf)
{
    UINT64 t = (UINT64)*time;
    UINT64 days = t / 864000000000ull, rem = t % 864000000000ull;
    tf->Milliseconds = (INT16)(rem / 10000 % 1000);
    rem /= 10000000;
    tf->Second = (INT16)(rem % 60);
    tf->Minute = (INT16)(rem / 60 % 60);
    tf->Hour = (INT16)(rem / 3600);
    tf->Weekday = (INT16)((days + 1) % 7);               /* (1 January 1601 was a Monday) */
    /* days since 1601-01-01 to a civil date (H. Hinnant's algorithm, from 0000-03-01) */
    INT64 z = (INT64)days + 584694;                      /* (1601-01-01 is day 584694 of that count) */
    INT64 era = z / 146097, doe = z - era * 146097;
    INT64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    INT64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    INT64 d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    tf->Year = (INT16)(yoe + era * 400 + (m <= 2));
    tf->Month = (INT16)m;
    tf->Day = (INT16)d;
}

NT_API void nt_ExSystemTimeToLocalTime(const INT64 *sys, INT64 *local)
{
    NtTimeFields tf;
    nt_RtlTimeToTimeFields(sys, &tf);
    RtcTime utc = { (UINT8)tf.Second, (UINT8)tf.Minute, (UINT8)tf.Hour, (UINT8)tf.Day, (UINT8)tf.Month, (UINT16)tf.Year };
    bool dst;
    INT32 bias = TzBiasAt(&utc, &dst);                   /* (minutes: UTC = local + bias) */
    *local = *sys - (INT64)bias * 600000000;
}

/* -----------------------------------------------------------------------
 * Ex, Ke, Ps
 * ----------------------------------------------------------------------- */
NT_API NTSTATUS nt_ExGetFirmwareEnvironmentVariable(NT_UNICODE_STRING *name, NT_GUID *vendor, void *value, UINT32 *len,
                                                    UINT32 *attributes)
{
    (void)name; (void)vendor; (void)value; (void)attributes;
    if (len) *len = 0;
    return ST_VARIABLE_NOT_FOUND;
}

NT_API UINT32 nt_KeCapturePersistentThreadState(void *context, void *thread, UINT32 code, UINT64 p1, UINT64 p2,
                                                UINT64 p3, UINT64 p4, void *page)
{
    (void)context; (void)thread; (void)code; (void)p1; (void)p2; (void)p3; (void)p4; (void)page;
    return 0;
}

NT_API void *nt_PsGetCurrentThreadId(void)
{
    Thread *t = sched_current();
    return (void *)(uintptr_t)(t ? t->tid : 0);
}

/* SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX entries: the processor and
 * group relationships (RelationProcessorCore 0, NumaNode 1, Cache 2,
 * ProcessorPackage 3, Group 4, All 0xFFFF) */
#define REL_CORE    0
#define REL_NUMA    1
#define REL_PACKAGE 3
#define REL_GROUP   4
#define REL_ALL     0xFFFF

static UINT32 put_rel(UINT8 *out, UINT32 cap, UINT32 at, int rel, UINT64 mask, UINT32 ncpu)
{
    UINT32 size = rel == REL_GROUP ? 80 : 48;
    if (out && at + size <= cap) {
        UINT8 *e = out + at;
        memset(e, 0, size);
        *(UINT32 *)e = (UINT32)rel;
        *(UINT32 *)(e + 4) = size;
        if (rel == REL_GROUP) {
            *(UINT16 *)(e + 8) = 1;                       /* MaximumGroupCount */
            *(UINT16 *)(e + 10) = 1;                      /* ActiveGroupCount */
            e[32] = (UINT8)ncpu;                          /* GroupInfo[0].MaximumProcessorCount */
            e[33] = (UINT8)ncpu;                          /* .ActiveProcessorCount */
            *(UINT64 *)(e + 72) = mask;                   /* .ActiveProcessorMask */
        } else if (rel == REL_NUMA) {
            *(UINT16 *)(e + 30) = 1;                      /* GroupCount */
            *(UINT64 *)(e + 32) = mask;                   /* GroupMask[0].Mask (group 0) */
        } else {
            *(UINT16 *)(e + 30) = 1;                      /* GroupCount */
            *(UINT64 *)(e + 32) = mask;
        }
    }
    return at + size;
}

NT_API NTSTATUS nt_KeQueryLogicalProcessorRelationship(UINT32 *number, int rel, void *info, UINT32 *len)
{
    if (!len) return STATUS_INVALID_PARAMETER;
    UINT32 ncpu = nt_KeQueryActiveProcessorCountEx(0);
    if (ncpu > 64) ncpu = 64;
    UINT64 all = ncpu == 64 ? ~0ull : (1ull << ncpu) - 1;
    int first = 0, last = (int)ncpu - 1;
    if (number) {                                         /* (PROCESSOR_NUMBER: Group, Number) */
        int n = (int)((UINT8 *)number)[2];
        if ((*(UINT16 *)number) != 0 || n >= (int)ncpu) return STATUS_INVALID_PARAMETER;
        first = last = n;
    }
    UINT32 at = 0, cap = info ? *len : 0;
    UINT8 *out = info;
    if (rel == REL_CORE || rel == REL_ALL)
        for (int i = first; i <= last; i++) at = put_rel(out, cap, at, REL_CORE, 1ull << i, ncpu);
    if (rel == REL_NUMA || rel == REL_ALL) at = put_rel(out, cap, at, REL_NUMA, all, ncpu);
    if (rel == REL_PACKAGE || rel == REL_ALL) at = put_rel(out, cap, at, REL_PACKAGE, all, ncpu);
    if (rel == REL_GROUP || rel == REL_ALL) at = put_rel(out, cap, at, REL_GROUP, all, ncpu);
    bool fits = at <= cap;
    *len = at;
    return fits ? STATUS_SUCCESS : NT_STATUS_INFO_LENGTH_MISMATCH;
}

/* -----------------------------------------------------------------------
 * Po, Pcw, IoWMI, WER
 * ----------------------------------------------------------------------- */
NT_API NTSTATUS nt_PoRegisterPowerSettingCallback(NT_DEVICE_OBJECT *dev, const NT_GUID *setting, void *callback,
                                                  void *ctx, void **handle)
{
    (void)dev; (void)setting; (void)callback; (void)ctx;
    static UINT8 registration;
    if (handle) *handle = &registration;
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_PoUnregisterPowerSettingCallback(void *handle) { (void)handle; return STATUS_SUCCESS; }

static UINT8 g_pcw;                                       /* (what a performance-counter handle points at) */
NT_API NTSTATUS nt_PcwRegister(void **registration, const void *info)
{
    (void)info;
    if (registration) *registration = &g_pcw;
    return STATUS_SUCCESS;
}
NT_API void nt_PcwUnregister(void *registration) { (void)registration; }
NT_API NTSTATUS nt_PcwCreateInstance(void **instance, void *registration, const NT_UNICODE_STRING *name, UINT32 count,
                                     const void *data)
{
    (void)registration; (void)name; (void)count; (void)data;
    if (instance) *instance = &g_pcw;
    return STATUS_SUCCESS;
}
NT_API void nt_PcwCloseInstance(void *instance) { (void)instance; }

NT_API NTSTATUS nt_IoWMIOpenBlock(const NT_GUID *guid, UINT32 access, void **object)
{
    (void)guid; (void)access;
    if (object) *object = NULL;
    return ST_WMI_GUID_NOT_FOUND;
}

NT_API NTSTATUS nt_IoWMIQueryAllData(void *object, UINT32 *size, void *out)
{
    (void)object; (void)out;
    if (size) *size = 0;
    return ST_WMI_GUID_NOT_FOUND;
}

NT_API NTSTATUS nt_IoWMIExecuteMethod(void *object, const NT_UNICODE_STRING *instance, UINT32 method, UINT32 in_size,
                                      UINT32 *out_size, void *buf)
{
    (void)object; (void)instance; (void)method; (void)in_size; (void)buf;
    if (out_size) *out_size = 0;
    return ST_WMI_GUID_NOT_FOUND;
}

NT_API NTSTATUS nt_IoWMISetNotificationCallback(void *object, void *callback, void *ctx)
{
    (void)object; (void)callback; (void)ctx;
    return ST_WMI_GUID_NOT_FOUND;
}

NT_API void *nt_WerLiveKernelCreateReport(UINT32 code, UINT64 p1, UINT64 p2, UINT64 p3, UINT64 p4, void *flags)
{
    (void)code; (void)p1; (void)p2; (void)p3; (void)p4; (void)flags;
    return NULL;
}
NT_API void nt_WerLiveKernelCancelReport(void *report) { (void)report; }
NT_API void nt_WerLiveKernelCloseHandle(void *report) { (void)report; }
NT_API NTSTATUS nt_WerLiveKernelOpenDumpFile(void *report, void **handle)
{
    (void)report;
    if (handle) *handle = NULL;
    return NT_STATUS_NOT_SUPPORTED;
}
NT_API void nt_WerLiveKernelSubmitReport(void *report) { (void)report; }

/* -----------------------------------------------------------------------
 * RtlCaptureContext and __chkstk (assembly: the caller's registers as
 * they are at the call, the CONTEXT layout of x64 Windows)
 * ----------------------------------------------------------------------- */
__asm__(
    ".globl nt_RtlCaptureContext\n"
    "nt_RtlCaptureContext:\n"
    "    pushfq\n"
    "    movq %rax, 0x78(%rcx)\n"
    "    movq %rcx, 0x80(%rcx)\n"
    "    movq %rdx, 0x88(%rcx)\n"
    "    movq %rbx, 0x90(%rcx)\n"
    "    leaq 0x10(%rsp), %rax\n"                       /* (the caller's RSP after the return) */
    "    movq %rax, 0x98(%rcx)\n"
    "    movq %rbp, 0xA0(%rcx)\n"
    "    movq %rsi, 0xA8(%rcx)\n"
    "    movq %rdi, 0xB0(%rcx)\n"
    "    movq %r8, 0xB8(%rcx)\n"
    "    movq %r9, 0xC0(%rcx)\n"
    "    movq %r10, 0xC8(%rcx)\n"
    "    movq %r11, 0xD0(%rcx)\n"
    "    movq %r12, 0xD8(%rcx)\n"
    "    movq %r13, 0xE0(%rcx)\n"
    "    movq %r14, 0xE8(%rcx)\n"
    "    movq %r15, 0xF0(%rcx)\n"
    "    movq 8(%rsp), %rax\n"                          /* (the return address: the caller's RIP) */
    "    movq %rax, 0xF8(%rcx)\n"
    "    movw %cs, 0x38(%rcx)\n"
    "    movw %ds, 0x3A(%rcx)\n"
    "    movw %es, 0x3C(%rcx)\n"
    "    movw %fs, 0x3E(%rcx)\n"
    "    movw %gs, 0x40(%rcx)\n"
    "    movw %ss, 0x42(%rcx)\n"
    "    popq %rax\n"
    "    movl %eax, 0x44(%rcx)\n"                       /* EFlags */
    "    stmxcsr 0x34(%rcx)\n"
    "    fxsave64 0x100(%rcx)\n"
    "    movl $0x10000F, 0x30(%rcx)\n"                  /* CONTEXT_FULL | CONTEXT_SEGMENTS (CONTEXT_AMD64) */
    "    movq 0x78(%rcx), %rax\n"
    "    ret\n"
    ".globl nt___chkstk\n"
    "nt___chkstk:\n"
    "    ret\n");
NT_API void nt_RtlCaptureContext(void *context);
NT_API void nt___chkstk(void);

/* The build Windows reports (NtBuildNumber: 0xF000 in the top bits marks
 * a free build), as RtlGetVersion does */
UINT32 nt_NtBuildNumber = 0xF0000000u | 18362;
