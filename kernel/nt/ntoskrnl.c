/*
 * ntoskrnl.c — the ntoskrnl.exe and hal.dll exports drivers call
 *
 * Every export is NT_API (the Microsoft x64 calling convention drivers
 * use) and named nt_<export>; exports.h lists them and NtExport finds
 * one for the loader.  The I/O manager's exports (Io*) are in io.c.
 */
#include "ntapi.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../arch/x86_64/apic.h"

/* -----------------------------------------------------------------------
 * Debug output: DbgPrint goes to the kernel log, as it goes to the
 * debugger on Windows
 * ----------------------------------------------------------------------- */
typedef struct { char *p; int n, cap; } Out;

static void out_c(Out *o, char c)
{
    if (o->n < o->cap - 1) o->p[o->n++] = c;
}

static void out_num(Out *o, UINT64 v, bool neg, int base, bool upper, int width, char pad)
{
    char tmp[24];
    int n = 0;
    do {
        int d = (int)(v % (UINT64)base);
        tmp[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
        v /= (UINT64)base;
    } while (v);
    if (neg) tmp[n++] = '-';
    while (width-- > n) out_c(o, pad);
    while (n) out_c(o, tmp[--n]);
}

static void out_wide(Out *o, const NT_WCHAR *w, int len)
{
    if (!w) { for (const char *s = "(null)"; *s; s++) out_c(o, *s); return; }
    for (int i = 0; len < 0 ? w[i] != 0 : i < len; i++) out_c(o, w[i] < 0x80 ? (char)w[i] : '?');
}

/* The printf a driver's format strings expect: %c %s %S %ws %wZ %Z %d %i
 * %u %x %X %o %p, with l, ll, h, I64, I32, z widths and 0 and width */
static void format(char *buf, int cap, const char *fmt, __builtin_ms_va_list ap)
{
    Out o = { buf, 0, cap };
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') { out_c(&o, *f); continue; }
        f++;
        char pad = ' ';
        int width = 0, size = 4;
        bool wide = false;
        while (*f == '-' || *f == '+' || *f == ' ' || *f == '#') f++;
        if (*f == '0') { pad = '0'; f++; }
        if (*f == '*') { width = __builtin_va_arg(ap, int); f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') { f++; if (*f == '*') { (void)__builtin_va_arg(ap, int); f++; } while (*f >= '0' && *f <= '9') f++; }
        for (;;) {
            if (*f == 'l' && f[1] == 'l') { size = 8; f += 2; }
            else if (*f == 'l') { f++; }                          /* (LLP64: long is 32 bits) */
            else if (*f == 'h') { f++; }
            else if (*f == 'w') { wide = true; f++; }
            else if (*f == 'z' || *f == 'j' || *f == 't') { size = 8; f++; }
            else if (f[0] == 'I' && f[1] == '6' && f[2] == '4') { size = 8; f += 3; }
            else if (f[0] == 'I' && f[1] == '3' && f[2] == '2') { size = 4; f += 3; }
            else if (*f == 'I') { size = 8; f++; }
            else break;
        }
        switch (*f) {
        case 0: f--; break;
        case '%': out_c(&o, '%'); break;
        case 'c': out_c(&o, (char)__builtin_va_arg(ap, int)); break;
        case 'C': out_c(&o, (char)__builtin_va_arg(ap, int)); break;
        case 's':
            if (wide) out_wide(&o, __builtin_va_arg(ap, const NT_WCHAR *), -1);
            else { const char *s = __builtin_va_arg(ap, const char *); for (s = s ? s : "(null)"; *s; s++) out_c(&o, *s); }
            break;
        case 'S': out_wide(&o, __builtin_va_arg(ap, const NT_WCHAR *), -1); break;
        case 'Z': {                                             /* %wZ: a UNICODE_STRING; %Z: an ANSI_STRING */
            const NT_UNICODE_STRING *u = __builtin_va_arg(ap, const NT_UNICODE_STRING *);
            if (!u) out_wide(&o, NULL, 0);
            else if (wide) out_wide(&o, u->Buffer, u->Length / 2);
            else for (int i = 0; i < u->Length; i++) out_c(&o, ((const char *)u->Buffer)[i]);
            break;
        }
        case 'd': case 'i': {
            INT64 v = size == 8 ? __builtin_va_arg(ap, INT64) : __builtin_va_arg(ap, INT32);
            out_num(&o, v < 0 ? (UINT64)-v : (UINT64)v, v < 0, 10, false, width, pad);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            UINT64 v = size == 8 ? __builtin_va_arg(ap, UINT64) : __builtin_va_arg(ap, UINT32);
            out_num(&o, v, false, *f == 'u' ? 10 : *f == 'o' ? 8 : 16, *f == 'X', width, pad);
            break;
        }
        case 'p': out_num(&o, (UINT64)(uintptr_t)__builtin_va_arg(ap, void *), false, 16, true, 16, '0'); break;
        default: out_c(&o, '%'); out_c(&o, *f); break;
        }
    }
    buf[o.n] = 0;
}

static void print(const char *fmt, __builtin_ms_va_list ap)
{
    char buf[512];
    format(buf, sizeof(buf), fmt, ap);
    int n = (int)strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    kprintf("[DbgPrint] %s\n", buf);
}

NT_API UINT32 nt_DbgPrint(const char *fmt, ...)
{
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    print(fmt, ap);
    __builtin_ms_va_end(ap);
    return 0;
}

/* Shown as Windows shows it with no debug filter set: errors only (a
 * level of 0, DPFLTR_ERROR_LEVEL, or a mask with that bit) */
NT_API UINT32 nt_DbgPrintEx(UINT32 component, UINT32 level, const char *fmt, ...)
{
    (void)component;
    UINT32 mask = level <= 31 ? 1u << level : level;
    if (!(mask & 1)) return 0;
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    print(fmt, ap);
    __builtin_ms_va_end(ap);
    return 0;
}

/* -----------------------------------------------------------------------
 * Run-time library
 * ----------------------------------------------------------------------- */
NT_API void nt_RtlInitUnicodeString(NT_UNICODE_STRING *u, const NT_WCHAR *s)
{
    UINT32 n = 0;
    if (s) while (s[n]) n++;
    if (n > 0x7FFE) n = 0x7FFE;
    u->Buffer = (NT_WCHAR *)s;
    u->Length = (unsigned short)(n * 2);
    u->MaximumLength = s ? (unsigned short)(n * 2 + 2) : 0;
}

NT_API void nt_RtlCopyUnicodeString(NT_UNICODE_STRING *dst, const NT_UNICODE_STRING *src)
{
    if (!src) { dst->Length = 0; return; }
    unsigned short n = src->Length < dst->MaximumLength ? src->Length : dst->MaximumLength;
    memmove(dst->Buffer, src->Buffer, n);
    dst->Length = n;
    if (n + 2 <= dst->MaximumLength) dst->Buffer[n / 2] = 0;
}

NT_API UINT64 nt_RtlCompareMemory(const void *a, const void *b, UINT64 n)
{
    UINT64 i = 0;
    while (i < n && ((const UINT8 *)a)[i] == ((const UINT8 *)b)[i]) i++;
    return i;
}

NT_API int nt_memcmp(const void *a, const void *b, UINT64 n) { return memcmp(a, b, n); }
NT_API void *nt_memcpy(void *d, const void *s, UINT64 n) { return memcpy(d, s, n); }
NT_API void *nt_memmove(void *d, const void *s, UINT64 n) { return memmove(d, s, n); }
NT_API void *nt_memset(void *d, int c, UINT64 n) { return memset(d, c, n); }
NT_API UINT64 nt_strlen(const char *s) { return strlen(s); }
NT_API UINT64 nt_wcslen(const NT_WCHAR *s)
{
    UINT64 n = 0;
    while (s[n]) n++;
    return n;
}

/* -----------------------------------------------------------------------
 * hal.dll
 * ----------------------------------------------------------------------- */
NT_API void nt_KeStallExecutionProcessor(UINT32 us)
{
    udelay(us);
}

/* -----------------------------------------------------------------------
 * ANSI and Unicode strings (drivers convert between the two)
 * ----------------------------------------------------------------------- */
typedef struct _NT_ANSI_STRING {
    unsigned short Length;
    unsigned short MaximumLength;
    char          *Buffer;
} NT_ANSI_STRING;

NT_API void nt_RtlInitAnsiString(NT_ANSI_STRING *a, const char *s)
{
    UINT32 n = 0;
    if (s) while (s[n]) n++;
    if (n > 0xFFFE) n = 0xFFFE;
    a->Buffer = (char *)s;
    a->Length = (unsigned short)n;
    a->MaximumLength = s ? (unsigned short)(n + 1) : 0;
}

NT_API NTSTATUS nt_RtlAnsiStringToUnicodeString(NT_UNICODE_STRING *u, const NT_ANSI_STRING *a, UINT8 alloc)
{
    unsigned short need = (unsigned short)(a->Length * 2);
    if (alloc) {
        u->Buffer = nt_ExAllocatePoolWithTag(0, (UINT64)need + 2, 0x6C747452 /* 'Rtl' */);
        if (!u->Buffer) return STATUS_NO_MEMORY;
        u->MaximumLength = (unsigned short)(need + 2);
    } else if (need + 2 > u->MaximumLength) {
        return STATUS_BUFFER_OVERFLOW;
    }
    for (int i = 0; i < a->Length; i++) u->Buffer[i] = (unsigned char)a->Buffer[i];
    u->Buffer[a->Length] = 0;
    u->Length = need;
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_RtlUnicodeStringToAnsiString(NT_ANSI_STRING *a, const NT_UNICODE_STRING *u, UINT8 alloc)
{
    unsigned short chars = u->Length / 2;
    if (alloc) {
        a->Buffer = nt_ExAllocatePoolWithTag(0, (UINT64)chars + 1, 0x6C747452 /* 'Rtl' */);
        if (!a->Buffer) return STATUS_NO_MEMORY;
        a->MaximumLength = (unsigned short)(chars + 1);
    } else if (chars + 1 > a->MaximumLength) {
        return STATUS_BUFFER_OVERFLOW;
    }
    for (int i = 0; i < chars; i++) a->Buffer[i] = u->Buffer[i] < 0x80 ? (char)u->Buffer[i] : '?';
    a->Buffer[chars] = 0;
    a->Length = chars;
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_RtlAnsiCharToUnicodeChar(UINT8 **src)
{
    UINT8 c = **src;
    (*src)++;
    return c;                                   /* returned in RAX as a WCHAR */
}

NT_API void nt_RtlFreeAnsiString(NT_ANSI_STRING *a)
{
    if (a && a->Buffer) nt_ExFreePool(a->Buffer);
    if (a) { a->Buffer = NULL; a->Length = a->MaximumLength = 0; }
}

NT_API NTSTATUS nt_RtlAppendUnicodeStringToString(NT_UNICODE_STRING *dst, const NT_UNICODE_STRING *src)
{
    if (!src->Length) return STATUS_SUCCESS;
    if (dst->Length + src->Length > dst->MaximumLength) return STATUS_BUFFER_TOO_SMALL;
    memmove((UINT8 *)dst->Buffer + dst->Length, src->Buffer, src->Length);
    dst->Length = (unsigned short)(dst->Length + src->Length);
    if (dst->Length + 2 <= dst->MaximumLength) dst->Buffer[dst->Length / 2] = 0;
    return STATUS_SUCCESS;
}

static int ustr_cmp(const NT_UNICODE_STRING *a, const NT_UNICODE_STRING *b, bool ci)
{
    int na = a->Length / 2, nb = b->Length / 2, n = na < nb ? na : nb;
    for (int i = 0; i < n; i++) {
        NT_WCHAR ca = a->Buffer[i], cb = b->Buffer[i];
        if (ci) { if (ca >= 'a' && ca <= 'z') ca -= 32; if (cb >= 'a' && cb <= 'z') cb -= 32; }
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return na == nb ? 0 : na < nb ? -1 : 1;
}

NT_API INT32 nt_RtlCompareUnicodeString(const NT_UNICODE_STRING *a, const NT_UNICODE_STRING *b, UINT8 ci)
{
    return ustr_cmp(a, b, ci != 0);
}

NT_API UINT8 nt_RtlEqualUnicodeString(const NT_UNICODE_STRING *a, const NT_UNICODE_STRING *b, UINT8 ci)
{
    return a->Length == b->Length && ustr_cmp(a, b, ci != 0) == 0;
}

NT_API NTSTATUS nt_RtlIntegerToUnicodeString(UINT32 value, UINT32 base, NT_UNICODE_STRING *dst)
{
    char tmp[36];
    int n = 0;
    if (!base) base = 10;
    do { int d = value % base; tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); value /= base; } while (value);
    if ((n + 1) * 2 > dst->MaximumLength) return STATUS_BUFFER_OVERFLOW;
    for (int i = 0; i < n; i++) dst->Buffer[i] = (NT_WCHAR)(unsigned char)tmp[n - 1 - i];
    dst->Buffer[n] = 0;
    dst->Length = (unsigned short)(n * 2);
    return STATUS_SUCCESS;
}

NT_API NTSTATUS nt_RtlUnicodeToMultiByteN(char *dst, UINT32 dstmax, UINT32 *written, const NT_WCHAR *src, UINT32 srcbytes)
{
    UINT32 chars = srcbytes / 2, n = chars < dstmax ? chars : dstmax;
    for (UINT32 i = 0; i < n; i++) dst[i] = src[i] < 0x80 ? (char)src[i] : '?';
    if (written) *written = n;
    return STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Version (KMDF checks the OS version it binds against): report the
 * Windows build NovaOS presents as, Windows 10 2004 (10.0.19041)
 * ----------------------------------------------------------------------- */
NT_API NTSTATUS nt_RtlGetVersion(void *info)
{
    UINT32 *v = info;                            /* RTL_OSVERSIONINFOW: size, major, minor, build, platform */
    if (!v || v[0] < 5 * 4) return STATUS_INVALID_PARAMETER;
    v[1] = 10;                                   /* dwMajorVersion */
    v[2] = 0;                                    /* dwMinorVersion */
    v[3] = 19041;                                /* dwBuildNumber */
    v[4] = 2;                                    /* VER_PLATFORM_WIN32_NT */
    return STATUS_SUCCESS;
}

/* PsGetVersion: the version RtlGetVersion reports (FALSE: a free build, not a checked one) */
NT_API UINT8 nt_PsGetVersion(UINT32 *major, UINT32 *minor, UINT32 *build, NT_UNICODE_STRING *csd)
{
    if (major) *major = 10;
    if (minor) *minor = 0;
    if (build) *build = 19041;
    if (csd && csd->Buffer && csd->MaximumLength >= 2) { csd->Length = 0; csd->Buffer[0] = 0; }
    return 0;
}

/* The debugger is not attached, so no frames are captured */
NT_API UINT16 nt_RtlCaptureStackBackTrace(UINT32 skip, UINT32 count, void **frames, UINT32 *hash)
{
    (void)skip; (void)count; (void)frames;
    if (hash) *hash = 0;
    return 0;
}

/* RtlQueryRegistryValues reads a driver's registry values through a table
 * of query routines and defaults.  The registry-backed driver store it
 * drives (per-value callbacks and defaults) arrives with the PnP driver
 * steps; until then it reports that it found nothing to query. */
NT_API NTSTATUS nt_RtlQueryRegistryValues(UINT32 relative, const NT_WCHAR *path, void *table,
                                          void *context, void *environment)
{
    (void)relative; (void)path; (void)table; (void)context; (void)environment;
    static bool told;
    if (!told) { told = true; kprintf("[NT] RtlQueryRegistryValues: driver registry queries are stubbed\n"); }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/* -----------------------------------------------------------------------
 * Bitmaps (RTL_BITMAP: ULONG SizeOfBitMap; ULONG *Buffer)
 * ----------------------------------------------------------------------- */
typedef struct _NT_RTL_BITMAP { UINT32 Size; UINT32 *Buffer; } NT_RTL_BITMAP;

NT_API void nt_RtlInitializeBitMap(NT_RTL_BITMAP *bm, UINT32 *buf, UINT32 size)
{
    bm->Size = size;
    bm->Buffer = buf;
}

NT_API void nt_RtlClearBit(NT_RTL_BITMAP *bm, UINT32 i)
{
    if (i < bm->Size) bm->Buffer[i >> 5] &= ~(1u << (i & 31));
}

NT_API void nt_RtlSetBit(NT_RTL_BITMAP *bm, UINT32 i)
{
    if (i < bm->Size) bm->Buffer[i >> 5] |= 1u << (i & 31);
}

NT_API UINT8 nt_RtlTestBit(NT_RTL_BITMAP *bm, UINT32 i)
{
    return i < bm->Size && (bm->Buffer[i >> 5] & (1u << (i & 31))) ? 1 : 0;
}

NT_API UINT32 nt_RtlFindClearBitsAndSet(NT_RTL_BITMAP *bm, UINT32 count, UINT32 hint)
{
    if (!count || count > bm->Size) return 0xFFFFFFFFu;
    for (UINT32 pass = 0; pass < 2; pass++) {
        UINT32 start = pass ? 0 : (hint < bm->Size ? hint : 0);
        UINT32 end = pass ? (hint < bm->Size ? hint : bm->Size) : bm->Size;
        UINT32 run = 0;
        for (UINT32 i = start; i < end; i++) {
            if (bm->Buffer[i >> 5] & (1u << (i & 31))) { run = 0; continue; }
            if (++run == count) {
                UINT32 begin = i - count + 1;
                for (UINT32 j = begin; j <= i; j++) bm->Buffer[j >> 5] |= 1u << (j & 31);
                return begin;
            }
        }
    }
    return 0xFFFFFFFFu;
}

/* -----------------------------------------------------------------------
 * C run-time the compiler's own output and drivers call
 * ----------------------------------------------------------------------- */
NT_API int nt__vsnprintf(char *buf, UINT64 count, const char *fmt, __builtin_ms_va_list ap)
{
    if (!count) return 0;
    format(buf, (int)(count > 0x7FFFFFFF ? 0x7FFFFFFF : count), fmt, ap);
    return (int)strlen(buf);
}

/* The secure form: at most @count characters (_TRUNCATE, ~0: as many as
 * fit) into @size bytes; -1 when the output was cut short */
NT_API int nt__vsnprintf_s(char *buf, UINT64 size, UINT64 count, const char *fmt, __builtin_ms_va_list ap)
{
    if (!buf || !size) return -1;
    UINT64 cap = count < size ? count + 1 : size;
    char tmp[1024];
    format(tmp, sizeof(tmp), fmt, ap);
    UINT64 n = strlen(tmp);
    UINT64 put = n < cap ? n : cap - 1;
    memcpy(buf, tmp, put);
    buf[put] = 0;
    return put < n ? -1 : (int)n;
}

NT_API int nt__vsnwprintf(NT_WCHAR *buf, UINT64 count, const NT_WCHAR *fmt, __builtin_ms_va_list ap)
{
    char nfmt[512], out[512];
    int i = 0;
    for (; fmt[i] && i < (int)sizeof(nfmt) - 1; i++) nfmt[i] = fmt[i] < 0x80 ? (char)fmt[i] : '?';
    nfmt[i] = 0;
    format(out, sizeof(out), nfmt, ap);
    int n = (int)strlen(out), max = count ? (int)(count - 1) : 0;
    if (n > max) n = max;
    for (int j = 0; j < n; j++) buf[j] = (unsigned char)out[j];
    if (count) buf[n] = 0;
    return n;
}

/* A call through an uninitialised C++ virtual table: a fatal driver bug */
NT_API void nt__purecall(void)
{
    KPANIC("*** STOP: pure virtual function call in a driver");
}
