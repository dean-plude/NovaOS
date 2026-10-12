/*
 * exports.c — the export tables drivers' imports are bound against
 *
 * Made from exports.h: each export is nt_<name>, defined in ntoskrnl.c
 * or io.c (declared here only by name: the loader stores its address).
 */
#include "nt.h"
#include "../lib/string.h"

#define NT_EXPORT(mod, name) NT_API void nt_##name(void);
#include "exports.h"
#undef NT_EXPORT

static const struct { const char *module, *name; void (NT_API *fn)(void); } g_exports[] = {
#define NT_EXPORT(mod, name) { #mod, #name, nt_##name },
#include "exports.h"
#undef NT_EXPORT
};

static bool same_ci(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

UINT64 NtExport(const char *module, const char *name)
{
    for (unsigned i = 0; i < sizeof(g_exports) / sizeof(g_exports[0]); i++)
        if (same_ci(g_exports[i].module, module) && !strcmp(g_exports[i].name, name))
            return (UINT64)(uintptr_t)g_exports[i].fn;
    return 0;
}
