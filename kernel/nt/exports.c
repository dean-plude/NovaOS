/*
 * exports.c — the export tables drivers' imports are bound against
 *
 * Made from exports.h: each export is nt_<name>, defined in ntoskrnl.c,
 * io.c, ke.c and the rest of kernel/nt/ (declared here only by name: the
 * loader stores its address).  NT_EXPORT is a function (the common case);
 * NT_EXPORT_DATA is a variable a driver imports by address (KeNumberProcessors,
 * the debugger flags, KiBugCheckData, IoFileObjectType).
 */
#include "nt.h"
#include "../lib/string.h"

#define NT_EXPORT(mod, name)      NT_API void nt_##name(void);
#define NT_EXPORT_DATA(mod, name) extern UINT8 nt_##name[];
#include "exports.h"
#undef NT_EXPORT
#undef NT_EXPORT_DATA

static const struct { const char *module, *name; void (NT_API *fn)(void); } g_exports[] = {
#define NT_EXPORT(mod, name)      { #mod, #name, nt_##name },
#define NT_EXPORT_DATA(mod, name)
#include "exports.h"
#undef NT_EXPORT
#undef NT_EXPORT_DATA
};

static const struct { const char *module, *name; const void *addr; } g_data[] = {
#define NT_EXPORT(mod, name)
#define NT_EXPORT_DATA(mod, name) { #mod, #name, (const void *)nt_##name },
#include "exports.h"
#undef NT_EXPORT
#undef NT_EXPORT_DATA
};

static bool same_ci(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

bool NtExportModule(const char *module)
{
    for (unsigned i = 0; i < sizeof(g_exports) / sizeof(g_exports[0]); i++)
        if (same_ci(g_exports[i].module, module)) return true;
    for (unsigned i = 0; i < sizeof(g_data) / sizeof(g_data[0]); i++)
        if (same_ci(g_data[i].module, module)) return true;
    return false;
}

UINT64 NtExport(const char *module, const char *name)
{
    for (unsigned i = 0; i < sizeof(g_exports) / sizeof(g_exports[0]); i++)
        if (same_ci(g_exports[i].module, module) && !strcmp(g_exports[i].name, name))
            return (UINT64)(uintptr_t)g_exports[i].fn;
    for (unsigned i = 0; i < sizeof(g_data) / sizeof(g_data[0]); i++)
        if (same_ci(g_data[i].module, module) && !strcmp(g_data[i].name, name))
            return (UINT64)(uintptr_t)g_data[i].addr;
    return 0;
}
