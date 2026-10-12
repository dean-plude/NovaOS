/*
 * wpp.c — the framework's trace messages
 *
 * The framework traces through WPP: the WDK's preprocessor turns each
 * DoTraceLevelMessage into an ETW event whose format string stays in the
 * symbol files.  NovaOS builds it with the trace macros of
 * tools/kmdf/inc/novaos_wpp.h instead, which call this with the message's
 * level, flags and format.  Errors and warnings go to the kernel log as
 * DbgPrintEx text, as the framework's messages read in a debugger; WPP's
 * own format extensions (%!STATUS!, %!FUNC!, ...) are rewritten to plain
 * printf conversions first.
 */
#include <ntddk.h>
#include <stdarg.h>
#include "novaos_wpp.h"

/* ntoskrnl.exe exports it; the WDK has it in stdio.h, which a driver does not get here */
int __cdecl _vsnprintf(char *buf, size_t count, const char *fmt, va_list ap);

#define DPFLTR_IHVDRIVER_ID 77

/* Rewrite @f's WPP extensions: %!FUNC!, %!LINE! and %!FILE! take no
 * argument and are dropped; every other %!NAME! takes one value, printed
 * in hex (a pointer for the ones that take an address).  "%I64" becomes
 * "%ll". */
static void rewrite(const char *f, char *out, size_t cap)
{
    size_t o = 0;
    while (*f && o + 4 < cap) {
        if (f[0] == '%' && f[1] == '!') {
            const char *e = f + 2;
            while (*e && *e != '!') e++;
            if (!*e) break;
            size_t n = (size_t)(e - (f + 2));
            const char *name = f + 2;
            if ((n == 4 && !strncmp(name, "FUNC", 4)) || (n == 4 && !strncmp(name, "LINE", 4)) ||
                (n == 4 && !strncmp(name, "FILE", 4))) {
                /* no argument */
            } else if ((n == 4 && !strncmp(name, "GUID", 4)) || (n == 2 && !strncmp(name, "wZ", 2))) {
                out[o++] = '%'; out[o++] = 'p';
            } else {
                out[o++] = '%'; out[o++] = 'x';
            }
            f = e + 1;
        } else if (f[0] == '%' && f[1] == 'I' && f[2] == '6' && f[3] == '4') {
            out[o++] = '%'; out[o++] = 'l'; out[o++] = 'l';
            f += 4;
        } else {
            out[o++] = *f++;
        }
    }
    out[o] = 0;
}

void __cdecl NovaOSWdfTrace(unsigned long Level, unsigned long Flags, const char *Format, ...)
{
    char fmt[256], msg[384];
    va_list ap;

    (void)Flags;
    if (Level > TRACE_LEVEL_WARNING) {
        return;
    }
    rewrite(Format, fmt, sizeof(fmt));
    va_start(ap, Format);
    _vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
    va_end(ap);
    msg[sizeof(msg) - 1] = 0;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, Level == TRACE_LEVEL_WARNING ? DPFLTR_WARNING_LEVEL : DPFLTR_ERROR_LEVEL,
               "Wdf01000: %s\n", msg);
}
