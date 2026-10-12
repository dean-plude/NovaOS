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
#include "../mm/vmm.h"
#include "../arch/x86_64/apic.h"
#include "../um/um.h"
#include "ntapi.h"

/* zw.c: the registry path (without "\Registry\") a Zw key handle names */
bool NtZwKeyPath(void *handle, char *out, int cap);

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
        u->Buffer = kmalloc((UINT32)need + 2);
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
        a->Buffer = kmalloc((UINT32)chars + 1);
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

NT_API void nt_RtlFreeUnicodeString(NT_UNICODE_STRING *u)
{
    if (u && u->Buffer) kfree(u->Buffer);
    if (u) { u->Buffer = NULL; u->Length = u->MaximumLength = 0; }
}

NT_API void nt_RtlFreeAnsiString(NT_ANSI_STRING *a)
{
    if (a && a->Buffer) kfree(a->Buffer);
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

/* The debugger is not attached, so no frames are captured */
NT_API UINT16 nt_RtlCaptureStackBackTrace(UINT32 skip, UINT32 count, void **frames, UINT32 *hash)
{
    (void)skip; (void)count; (void)frames;
    if (hash) *hash = 0;
    return 0;
}

/* -----------------------------------------------------------------------
 * Registry run-time library: RtlQueryRegistryValues, RtlWriteRegistryValue,
 * RtlDeleteRegistryValue, over the kernel registry (um/um_registry.c)
 * ----------------------------------------------------------------------- */
#define RTL_REGISTRY_ABSOLUTE    0
#define RTL_REGISTRY_SERVICES    1
#define RTL_REGISTRY_CONTROL     2
#define RTL_REGISTRY_WINDOWS_NT  3
#define RTL_REGISTRY_DEVICEMAP   4
#define RTL_REGISTRY_USER        5
#define RTL_REGISTRY_MAXIMUM     6
#define RTL_REGISTRY_HANDLE      0x40000000u
#define RTL_REGISTRY_OPTIONAL    0x80000000u

#define RTL_QUERY_REGISTRY_SUBKEY     0x00000001u
#define RTL_QUERY_REGISTRY_TOPKEY     0x00000002u
#define RTL_QUERY_REGISTRY_REQUIRED   0x00000004u
#define RTL_QUERY_REGISTRY_NOVALUE    0x00000008u
#define RTL_QUERY_REGISTRY_NOEXPAND   0x00000010u
#define RTL_QUERY_REGISTRY_DIRECT     0x00000020u
#define RTL_QUERY_REGISTRY_DELETE     0x00000040u
#define RTL_QUERY_REGISTRY_TYPECHECK  0x00000100u
#define RTL_QUERY_REGISTRY_TYPECHECK_SHIFT 24

#define REG_NONE       0
#define REG_SZ         1
#define REG_EXPAND_SZ  2
#define REG_MULTI_SZ   7

#define STATUS_OBJECT_TYPE_MISMATCH ((NTSTATUS)0xC0000024)

typedef NT_API NTSTATUS (*RtlQueryRoutine)(NT_WCHAR *name, UINT32 type, void *data, UINT32 len,
                                           void *context, void *entry_context);

/* RTL_QUERY_REGISTRY_TABLE (x64) */
typedef struct {
    RtlQueryRoutine QueryRoutine;
    UINT32          Flags;
    NT_WCHAR       *Name;
    void           *EntryContext;
    UINT32          DefaultType;
    void           *DefaultData;
    UINT32          DefaultLength;
} RtlQueryRegistryTable;
_Static_assert(sizeof(RtlQueryRegistryTable) == 56, "RTL_QUERY_REGISTRY_TABLE is 56 bytes on x64");

/* A NUL-terminated wide string as ASCII (truncated to fit) */
static void wide_ascii(const NT_WCHAR *w, char *out, int cap)
{
    int n = 0;
    for (; w && w[n] && n < cap - 1; n++) out[n] = w[n] < 0x80 ? (char)w[n] : '?';
    out[n] = 0;
}

/* The key RelativeTo and Path name, as a path without "\Registry\" */
static NTSTATUS rtl_reg_path(UINT32 relative, const NT_WCHAR *path, char *out, int cap)
{
    if (relative & RTL_REGISTRY_HANDLE)
        return NtZwKeyPath((void *)path, out, cap) ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
    static const char *const base[RTL_REGISTRY_MAXIMUM] = {
        [RTL_REGISTRY_SERVICES]   = "Machine\\System\\CurrentControlSet\\Services",
        [RTL_REGISTRY_CONTROL]    = "Machine\\System\\CurrentControlSet\\Control",
        [RTL_REGISTRY_WINDOWS_NT] = "Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion",
        [RTL_REGISTRY_DEVICEMAP]  = "Machine\\Hardware\\DeviceMap",
        [RTL_REGISTRY_USER]       = "User\\" UM_USER_SID,
    };
    UINT32 rel = relative & ~RTL_REGISTRY_OPTIONAL;
    if (rel >= RTL_REGISTRY_MAXIMUM) return STATUS_INVALID_PARAMETER;
    char p[256];
    wide_ascii(path, p, sizeof(p));
    if (rel == RTL_REGISTRY_ABSOLUTE) {
        static const char pre[] = "\\registry\\";
        bool same = true;
        for (int i = 0; pre[i] && same; i++) same = (pre[i] == '\\' ? p[i] : p[i] | 0x20) == pre[i];
        if (!same) return STATUS_OBJECT_NAME_NOT_FOUND;   /* (OBJECT_PATH_SYNTAX_BAD on Windows) */
        ksnprintf(out, cap, "%s", p + 10);
    } else {
        const char *rest = p;
        while (*rest == '\\') rest++;
        ksnprintf(out, cap, "%s%s%s", base[rel], rest[0] ? "\\" : "", rest);
    }
    int n = (int)strlen(out);
    while (n && out[n - 1] == '\\') out[--n] = 0;
    return out[0] ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static UINT32 wstr_bytes(const NT_WCHAR *w)                     /* with the terminator */
{
    UINT32 n = 0;
    while (w[n]) n++;
    return 2 * (n + 1);
}

static UINT32 multi_sz_bytes(const NT_WCHAR *w)                 /* through the final empty string */
{
    UINT32 n = 0;
    while (w[n]) { while (w[n]) n++; n++; }
    return 2 * (n + 1);
}

/* REG_EXPAND_SZ: %SystemRoot% and %windir% expanded (the variables a
 * kernel-mode value uses); other %names% are left as they are.  A new
 * pool buffer, terminated; *@len its size in bytes. */
static NT_WCHAR *expand_sz(const NT_WCHAR *s, UINT32 slen, UINT32 *len)
{
    static const char root[] = "C:\\Windows";
    UINT32 n = slen / 2, cap = n + 1, o = 0;
    for (UINT32 i = 0; i < n; i++) if (s[i] == '%') cap += sizeof(root);
    NT_WCHAR *d = kmalloc(2 * (size_t)cap);
    if (!d) return NULL;
    for (UINT32 i = 0; i < n && s[i]; ) {
        if (s[i] == '%') {
            UINT32 j = i + 1;
            while (j < n && s[j] && s[j] != '%') j++;
            if (j < n && s[j] == '%') {
                char var[16];
                UINT32 vl = j - i - 1, k = 0;
                for (; k < vl && k < sizeof(var) - 1; k++) var[k] = (char)(s[i + 1 + k] | 0x20);
                var[k] = 0;
                if (vl == k && (!strcmp(var, "systemroot") || !strcmp(var, "windir"))) {
                    for (const char *c = root; *c; c++) d[o++] = (UINT8)*c;
                    i = j + 1;
                    continue;
                }
            }
        }
        d[o++] = s[i++];
    }
    d[o++] = 0;
    *len = 2 * o;
    return d;
}

/* RTL_QUERY_REGISTRY_DIRECT: store one value where EntryContext points */
static NTSTATUS store_direct(void *ec, UINT32 type, const void *data, UINT32 len)
{
    if (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) {
        NT_UNICODE_STRING *us = ec;
        const NT_WCHAR *w = data;
        UINT32 chars = len / 2;
        if (type == REG_MULTI_SZ) { if (chars && !w[chars - 1]) chars--; }   /* all but the final NUL */
        else { UINT32 c = 0; while (c < chars && w[c]) c++; chars = c; }
        UINT32 bytes = 2 * chars, need = bytes + 2;
        if (need > 0xFFFF) return STATUS_BUFFER_TOO_SMALL;
        if (!us->Buffer) {
            us->Buffer = nt_ExAllocatePoolWithTag(1 /* PagedPool */, need, 0x20206C74 /* 'tl  ' */);
            if (!us->Buffer) return STATUS_NO_MEMORY;
            us->MaximumLength = (unsigned short)need;
        } else if (need > us->MaximumLength) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        memcpy(us->Buffer, w, bytes);
        us->Buffer[chars] = 0;
        us->Length = (unsigned short)bytes;
        return STATUS_SUCCESS;
    }
    if (len <= sizeof(UINT32)) {                       /* small non-string data: stored in place */
        memcpy(ec, data, len);
        return STATUS_SUCCESS;
    }
    /* Larger: EntryContext starts with a LONG giving the buffer's size; a
     * negative one wants the data alone, a positive one length and type
     * first (two ULONGs), then the data */
    INT32 size;
    memcpy(&size, ec, 4);
    if (size < 0) {
        if (len > (UINT32)-(INT64)size) return STATUS_BUFFER_TOO_SMALL;
        memcpy(ec, data, len);
    } else {
        if ((UINT64)len + 8 > (UINT64)size) return STATUS_BUFFER_TOO_SMALL;
        UINT32 *u = ec;
        u[0] = len;
        u[1] = type;
        memcpy(u + 2, data, len);
    }
    return STATUS_SUCCESS;
}

/* One value for a table entry: type-checked, expanded, then stored
 * (DIRECT) or handed to the query routine (a REG_MULTI_SZ one string at a
 * time, unless NOEXPAND) */
static NTSTATUS query_one(const RtlQueryRegistryTable *e, NT_WCHAR *name, UINT32 type, const void *data,
                          UINT32 len, void *context)
{
    if ((e->Flags & (RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK))
            == (RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK)
        && type != e->DefaultType >> RTL_QUERY_REGISTRY_TYPECHECK_SHIFT)
        return STATUS_OBJECT_TYPE_MISMATCH;
    NT_WCHAR *exp = NULL;
    if (type == REG_EXPAND_SZ && !(e->Flags & RTL_QUERY_REGISTRY_NOEXPAND)) {
        exp = expand_sz(data, len, &len);
        if (!exp) return STATUS_NO_MEMORY;
        data = exp;
        type = REG_SZ;
    }
    NTSTATUS st;
    if (e->Flags & RTL_QUERY_REGISTRY_DIRECT) {
        st = store_direct(e->EntryContext, type, data, len);
    } else if (type == REG_MULTI_SZ && !(e->Flags & RTL_QUERY_REGISTRY_NOEXPAND)) {
        st = STATUS_SUCCESS;
        NT_WCHAR *w = (NT_WCHAR *)data;
        UINT32 n = len / 2, i = 0;
        while (i < n && w[i] && NT_SUCCESS(st)) {
            UINT32 s = i;
            while (i < n && w[i]) i++;
            if (i >= n) break;                         /* (an unterminated last string: dropped) */
            st = e->QueryRoutine(name, REG_SZ, w + s, 2 * (i - s + 1), context, e->EntryContext);
            i++;
        }
    } else {
        st = e->QueryRoutine(name, type, (void *)data, len, context, e->EntryContext);
    }
    if (exp) kfree(exp);
    return st;
}

/* The entry's default, when its value is missing */
static NTSTATUS query_default(const RtlQueryRegistryTable *e, void *context)
{
    UINT32 type = e->DefaultType;
    if (e->Flags & RTL_QUERY_REGISTRY_TYPECHECK) type &= (1u << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) - 1;
    if (type == REG_NONE) return STATUS_SUCCESS;
    UINT32 len = e->DefaultLength;
    if (!len && e->DefaultData) {
        if (type == REG_SZ || type == REG_EXPAND_SZ) len = wstr_bytes(e->DefaultData);
        else if (type == REG_MULTI_SZ) len = multi_sz_bytes(e->DefaultData);
    }
    RtlQueryRegistryTable d = *e;                      /* (the default is not type-checked) */
    d.Flags &= ~RTL_QUERY_REGISTRY_TYPECHECK;
    return query_one(&d, e->Name, type, e->DefaultData, len, context);
}

/* Read value @name of @path for an entry: missing -> REQUIRED fails, else
 * the default */
static NTSTATUS query_named(const RtlQueryRegistryTable *e, const char *path, bool have_key, void *context)
{
    char nm[128];
    wide_ascii(e->Name, nm, sizeof(nm));
    UINT32 type = 0;
    int len = have_key ? um_registry_get_value(path, nm, &type, NULL, 0) : -1;
    UINT8 *data = NULL;
    if (len >= 0) {
        data = kzalloc((size_t)len + 4);               /* (terminated, for a string missing its NUL) */
        if (!data) return STATUS_NO_MEMORY;
        int got = um_registry_get_value(path, nm, &type, data, len);
        if (got < 0) { kfree(data); data = NULL; len = -1; }
        else if (got < len) len = got;
    }
    if (len < 0) {
        if (e->Flags & RTL_QUERY_REGISTRY_REQUIRED) return STATUS_OBJECT_NAME_NOT_FOUND;
        return query_default(e, context);
    }
    NTSTATUS st = query_one(e, e->Name, type, data, (UINT32)len, context);
    kfree(data);
    if (NT_SUCCESS(st) && (e->Flags & RTL_QUERY_REGISTRY_DELETE)) um_registry_delete_value(path, nm);
    return st;
}

/* Every value of @path to the entry's routine (none: REQUIRED fails, else
 * the default) */
static NTSTATUS query_all(const RtlQueryRegistryTable *e, const char *path, bool have_key, void *context)
{
    NT_WCHAR name[257];
    UINT32 i = 0;
    for (;; i++) {
        int nlen = 0;
        UINT32 type = 0;
        int len = have_key ? um_registry_enum_value(path, i, name, 256, &nlen, &type, NULL, 0) : -1;
        if (len < 0) break;
        UINT8 *data = kzalloc((size_t)len + 4);
        if (!data) return STATUS_NO_MEMORY;
        int got = um_registry_enum_value(path, i, name, 256, &nlen, &type, data, len);
        if (got < 0) { kfree(data); break; }
        name[nlen < 256 ? nlen : 256] = 0;
        NTSTATUS st = query_one(e, name, type, data, (UINT32)(got < len ? got : len), context);
        kfree(data);
        if (!NT_SUCCESS(st)) return st;
    }
    if (i) return STATUS_SUCCESS;
    if (e->Flags & RTL_QUERY_REGISTRY_REQUIRED) return STATUS_OBJECT_NAME_NOT_FOUND;
    return query_default(e, context);
}

/* RtlQueryRegistryValues(RelativeTo, Path, QueryTable, Context, Environment):
 * each entry of the table (to one whose QueryRoutine and Name are both
 * NULL) reads one value, every value, or moves to a subkey; values go to
 * the entry's QueryRoutine or, with DIRECT, straight to EntryContext */
NT_API NTSTATUS nt_RtlQueryRegistryValues(UINT32 relative, const NT_WCHAR *path, RtlQueryRegistryTable *table,
                                          void *context, void *environment)
{
    (void)environment;                               /* (%SystemRoot% is all NovaOS expands) */
    if (!table) return STATUS_INVALID_PARAMETER;
    char top[256], cur[256];
    NTSTATUS st = rtl_reg_path(relative, path, top, sizeof(top));
    if (!NT_SUCCESS(st)) return st;
    bool top_exists = um_registry_key_exists(top);
    if (!top_exists && !(relative & RTL_REGISTRY_OPTIONAL)) return STATUS_OBJECT_NAME_NOT_FOUND;
    ksnprintf(cur, sizeof(cur), "%s", top);
    bool have = top_exists;
    for (RtlQueryRegistryTable *e = table; e->QueryRoutine || e->Name; e++) {
        if ((e->Flags & RTL_QUERY_REGISTRY_DIRECT)
            && (!e->Name || !e->EntryContext || (e->Flags & RTL_QUERY_REGISTRY_SUBKEY)))
            return STATUS_INVALID_PARAMETER;
        if (e->Flags & (RTL_QUERY_REGISTRY_TOPKEY | RTL_QUERY_REGISTRY_SUBKEY)) {
            ksnprintf(cur, sizeof(cur), "%s", top);
            have = top_exists;
        }
        if (e->Flags & RTL_QUERY_REGISTRY_SUBKEY) {
            char sub[128];
            wide_ascii(e->Name, sub, sizeof(sub));
            const char *s = sub;
            while (*s == '\\') s++;
            ksnprintf(cur, sizeof(cur), "%s\\%s", top, s);
            have = um_registry_key_exists(cur);
            if (!have) return STATUS_OBJECT_NAME_NOT_FOUND;
            if (!e->QueryRoutine) continue;            /* only the focus moves */
            st = (e->Flags & RTL_QUERY_REGISTRY_NOVALUE)
                 ? e->QueryRoutine(NULL, REG_NONE, NULL, 0, context, e->EntryContext)
                 : query_all(e, cur, have, context);
        } else if (e->Name) {
            st = query_named(e, cur, have, context);
        } else if (e->Flags & RTL_QUERY_REGISTRY_NOVALUE) {
            st = e->QueryRoutine(NULL, REG_NONE, NULL, 0, context, e->EntryContext);
        } else {
            st = query_all(e, cur, have, context);
        }
        if (!NT_SUCCESS(st)) return st;
    }
    return STATUS_SUCCESS;
}

/* RtlWriteRegistryValue: the key is created if need be */
NT_API NTSTATUS nt_RtlWriteRegistryValue(UINT32 relative, const NT_WCHAR *path, const NT_WCHAR *name,
                                         UINT32 type, void *data, UINT32 len)
{
    char p[256], nm[128];
    NTSTATUS st = rtl_reg_path(relative, path, p, sizeof(p));
    if (!NT_SUCCESS(st)) return st;
    if (len && !data) return STATUS_INVALID_PARAMETER;
    wide_ascii(name, nm, sizeof(nm));
    if (!um_registry_create_key(p)) return STATUS_INSUFFICIENT_RESOURCES;
    um_registry_set_value(p, nm, type, data, len);
    return STATUS_SUCCESS;
}

/* RtlDeleteRegistryValue: the key must exist, and so must the value */
NT_API NTSTATUS nt_RtlDeleteRegistryValue(UINT32 relative, const NT_WCHAR *path, const NT_WCHAR *name)
{
    char p[256], nm[128];
    NTSTATUS st = rtl_reg_path(relative, path, p, sizeof(p));
    if (!NT_SUCCESS(st)) return st;
    if (!um_registry_key_exists(p)) return STATUS_OBJECT_NAME_NOT_FOUND;
    wide_ascii(name, nm, sizeof(nm));
    return um_registry_delete_value(p, nm) ? STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND;
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

NT_API int nt__vsnwprintf(NT_WCHAR *buf, UINT64 count, const NT_WCHAR *fmt, __builtin_ms_va_list ap)
{
    char nfmt[512], out[512];
    /* In a wide format %s and %c are wide and %S narrow: say so to format() */
    int k = 0;
    bool spec = false, h = false, l = false;
    for (int i = 0; fmt[i] && k < (int)sizeof(nfmt) - 3; i++) {
        char c = fmt[i] < 0x80 ? (char)fmt[i] : '?';
        if (!spec) { nfmt[k++] = c; if (c == '%') { spec = true; h = l = false; } continue; }
        if (c == 'h') { h = true; nfmt[k++] = c; continue; }
        if (c == 'l' || c == 'w') { l = true; nfmt[k++] = c; continue; }
        if (c == 's' && !h) nfmt[k++] = 'w';                 /* (%s, %ls, %ws: wide) */
        if (c == 'S') { nfmt[k++] = 'h'; c = 's'; }
        nfmt[k++] = c;
        if ((c >= 'a' && c <= 'z' && c != 'h' && c != 'l' && c != 'w' && c != 'z' && c != 'j' && c != 't') ||
            (c >= 'A' && c <= 'Z' && c != 'I') || c == '%')
            spec = false;
    }
    nfmt[k] = 0;
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

/* -----------------------------------------------------------------------
 * Step 30.8: strings FTDI's USB serial drivers use
 * ----------------------------------------------------------------------- */
NT_API int nt__snwprintf(NT_WCHAR *buf, UINT64 count, const NT_WCHAR *fmt, ...)
{
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    int n = nt__vsnwprintf(buf, count, fmt, ap);
    __builtin_ms_va_end(ap);
    return n;
}

static NT_WCHAR wupper(NT_WCHAR c) { return c >= 'a' && c <= 'z' ? (NT_WCHAR)(c - 32) : c; }

NT_API int nt__wcsnicmp(const NT_WCHAR *a, const NT_WCHAR *b, UINT64 n)
{
    for (; n; n--, a++, b++) {
        NT_WCHAR x = wupper(*a), y = wupper(*b);
        if (x != y) return x < y ? -1 : 1;
        if (!x) break;
    }
    return 0;
}

NT_API NT_WCHAR *nt_wcschr(const NT_WCHAR *s, NT_WCHAR c)
{
    for (;; s++) {
        if (*s == c) return (NT_WCHAR *)s;
        if (!*s) return NULL;
    }
}

NT_API NTSTATUS nt_RtlAppendUnicodeToString(NT_UNICODE_STRING *dst, const NT_WCHAR *src)
{
    if (!src) return STATUS_SUCCESS;
    UINT64 n = 2 * nt_wcslen(src);
    if (dst->Length + n > dst->MaximumLength) return STATUS_BUFFER_TOO_SMALL;
    memmove((UINT8 *)dst->Buffer + dst->Length, src, n);
    dst->Length = (unsigned short)(dst->Length + n);
    if (dst->Length + 2 <= dst->MaximumLength) dst->Buffer[dst->Length / 2] = 0;
    return STATUS_SUCCESS;
}

/* "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" in pool the caller frees with
 * RtlFreeUnicodeString */
NT_API NTSTATUS nt_RtlStringFromGUID(const NT_GUID *g, NT_UNICODE_STRING *out)
{
    char a[40];
    ksnprintf(a, sizeof(a), "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g->Data1, g->Data2, g->Data3,
              g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    NT_WCHAR *w = kmalloc(39 * 2);
    if (!w) return STATUS_INSUFFICIENT_RESOURCES;
    for (int i = 0; i < 39; i++) w[i] = (unsigned char)a[i];
    out->Buffer = w;
    out->Length = 38 * 2;
    out->MaximumLength = 39 * 2;
    return STATUS_SUCCESS;
}

/* Leading blanks, a sign, then digits in @base (0: a 0x, 0o or 0b prefix
 * picks it, else 10) up to the first that is not one */
NT_API NTSTATUS nt_RtlUnicodeStringToInteger(const NT_UNICODE_STRING *s, UINT32 base, UINT32 *value)
{
    if (!s || !value) return STATUS_INVALID_PARAMETER;
    const NT_WCHAR *p = s->Buffer;
    int n = s->Buffer ? s->Length / 2 : 0, i = 0;
    while (i < n && p[i] <= ' ') i++;
    bool neg = false;
    if (i < n && (p[i] == '-' || p[i] == '+')) neg = p[i++] == '-';
    if (!base) {
        base = 10;
        if (i + 1 < n && p[i] == '0') {
            NT_WCHAR c = (NT_WCHAR)(p[i + 1] | 0x20);
            if (c == 'x') { base = 16; i += 2; } else if (c == 'o') { base = 8; i += 2; } else if (c == 'b') { base = 2; i += 2; }
        }
    } else if (base != 2 && base != 8 && base != 10 && base != 16) {
        return STATUS_INVALID_PARAMETER;
    }
    UINT32 v = 0;
    for (; i < n; i++) {
        NT_WCHAR c = p[i];
        UINT32 d = c >= '0' && c <= '9' ? (UINT32)(c - '0') : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (UINT32)((c | 0x20) - 'a' + 10) : 99;
        if (d >= base) break;
        v = v * base + d;
    }
    *value = neg ? (UINT32)-(INT32)v : v;
    return STATUS_SUCCESS;
}

/* NovaOS presents as Windows 10 2004 (RtlGetVersion): NTDDI_WIN10_VB */
NT_API UINT8 nt_RtlIsNtDdiVersionAvailable(UINT32 version) { return version <= 0x0A000008u; }
