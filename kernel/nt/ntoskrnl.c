/*
 * ntoskrnl.c — the ntoskrnl.exe and hal.dll exports drivers call
 *
 * Every export is NT_API (the Microsoft x64 calling convention drivers
 * use) and named nt_<export>; exports.h lists them and NtExport finds
 * one for the loader.  The I/O manager's exports (Io*) are in io.c.
 */
#include "nt.h"
#include "../ke/printf.h"
#include "../lib/string.h"
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
