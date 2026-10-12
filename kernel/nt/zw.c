/*
 * zw.c — the Zw* calls a kernel-mode driver makes
 *
 * A driver reads and writes the registry, opens files and asks the system
 * about itself with the Zw* system services, called from kernel mode (the
 * previous mode is KernelMode, so no user-pointer probing: the buffers are
 * the driver's own).  NovaOS routes the registry ones to the kernel
 * registry (um/um_registry.c), files to the RAM file system (fs/ramfs),
 * ZwLoadDriver / ZwUnloadDriver to the driver loader (ldr.c), and
 * ZwQuerySystemInformation's SystemModuleInformation to the loaded-driver
 * list, which is what a driver (KMDF among them) reads to find its own
 * image base.
 *
 * A Zw registry handle is a small kernel object holding the key's path
 * (NovaOS keeps the registry as a tree of names, not of objects a handle
 * can point straight at); a relative open appends to its parent's path.
 * ZwClose frees whichever kind of handle it is given.
 */
#include "nt.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../fs/ramfs.h"
#include "../um/um.h"
#include "../ke/kpcr.h"

#define KERNEL_IMAGE_BASE  UINT64_C(0xFFFFFFFF80200000)

/* OBJECT_ATTRIBUTES, as far as a driver fills it */
typedef struct {
    UINT32             Length;
    UINT32             pad;
    void              *RootDirectory;        /* a key handle, for a relative name */
    NT_UNICODE_STRING *ObjectName;
    UINT32             Attributes;
    void              *SecurityDescriptor;
    void              *SecurityQualityOfService;
} NtObjectAttributes;

#define ZW_MAGIC  0x5A774F62u                 /* 'ZwOb' */
enum { ZW_KEY = 1, ZW_FILE };

typedef struct {
    UINT32   magic;
    UINT32   kind;
    char     path[256];                       /* ZW_KEY: registry path without "\Registry\" */
    RamNode *node;                            /* ZW_FILE */
    UINT64   offset;
} ZwObject;

/* A UNICODE_STRING as ASCII (truncated to fit) */
static void ustr_ascii(const NT_UNICODE_STRING *u, char *out, int cap)
{
    int n = u && u->Buffer ? u->Length / 2 : 0;
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) { UINT16 c = u->Buffer[i]; out[i] = c && c < 0x80 ? (char)c : '?'; }
    out[n < 0 ? 0 : n] = 0;
}

/* The registry path OBJECT_ATTRIBUTES names, without the "\Registry\"
 * prefix (relative to a RootDirectory key handle, if given) */
static bool oa_reg_path(const NtObjectAttributes *oa, char *out, int cap)
{
    char name[256];
    ustr_ascii(oa->ObjectName, name, sizeof(name));
    ZwObject *root = oa->RootDirectory;
    if (root && root->magic == ZW_MAGIC && root->kind == ZW_KEY) {
        ksnprintf(out, cap, "%s%s%s", root->path, name[0] ? "\\" : "", name);
        return true;
    }
    const char *p = name;
    static const char pre[] = "\\registry\\";
    bool same = true;
    for (int i = 0; pre[i] && same; i++) same = (p[i] | 0x20) == pre[i];
    if (same) p += 10;
    else if (*p == '\\') p++;
    ksnprintf(out, cap, "%s", p);
    return p[0] != 0;
}

static ZwObject *zw_new(UINT32 kind)
{
    ZwObject *o = kzalloc(sizeof(*o));
    if (o) { o->magic = ZW_MAGIC; o->kind = kind; }
    return o;
}

static bool put_handle(void *handle_ptr, ZwObject *o)
{
    if (!handle_ptr) return false;
    *(void **)handle_ptr = o;
    return true;
}

/* -----------------------------------------------------------------------
 * Registry
 * ----------------------------------------------------------------------- */
NT_API NTSTATUS nt_ZwCreateKey(void *handle, UINT32 access, NtObjectAttributes *oa, UINT32 title,
                               NT_UNICODE_STRING *cls, UINT32 options, UINT32 *disposition)
{
    (void)access; (void)title; (void)cls; (void)options;
    char path[256];
    if (!oa || !oa_reg_path(oa, path, sizeof(path))) return NT_STATUS_INVALID_PARAMETER;
    bool existed = um_registry_key_exists(path);
    if (!um_registry_create_key(path)) return NT_STATUS_INSUFFICIENT_RESOURCES;
    ZwObject *o = zw_new(ZW_KEY);
    if (!o) return NT_STATUS_INSUFFICIENT_RESOURCES;
    strncpy(o->path, path, sizeof(o->path) - 1);
    if (!put_handle(handle, o)) { kfree(o); return NT_STATUS_ACCESS_VIOLATION; }
    if (disposition) *disposition = existed ? 2 : 1;   /* OPENED_EXISTING / CREATED_NEW */
    return NT_STATUS_SUCCESS;
}

NT_API NTSTATUS nt_ZwOpenKey(void *handle, UINT32 access, NtObjectAttributes *oa)
{
    (void)access;
    char path[256];
    if (!oa || !oa_reg_path(oa, path, sizeof(path))) return NT_STATUS_INVALID_PARAMETER;
    if (!um_registry_key_exists(path)) return NT_STATUS_OBJECT_NAME_NOT_FOUND;
    ZwObject *o = zw_new(ZW_KEY);
    if (!o) return NT_STATUS_INSUFFICIENT_RESOURCES;
    strncpy(o->path, path, sizeof(o->path) - 1);
    if (!put_handle(handle, o)) { kfree(o); return NT_STATUS_ACCESS_VIOLATION; }
    return NT_STATUS_SUCCESS;
}

NT_API NTSTATUS nt_ZwSetValueKey(ZwObject *key, NT_UNICODE_STRING *name, UINT32 title, UINT32 type,
                                 void *data, UINT32 size)
{
    (void)title;
    if (!key || key->magic != ZW_MAGIC || key->kind != ZW_KEY) return NT_STATUS_INVALID_HANDLE;
    char nm[128];
    ustr_ascii(name, nm, sizeof(nm));
    um_registry_set_value(key->path, nm, type, data, size);
    return NT_STATUS_SUCCESS;
}

/* KEY_VALUE_{BASIC 0, FULL 1, PARTIAL 2}_INFORMATION */
NT_API NTSTATUS nt_ZwQueryValueKey(ZwObject *key, NT_UNICODE_STRING *name, UINT32 cls, void *out,
                                   UINT32 cap, UINT32 *result_len)
{
    if (!key || key->magic != ZW_MAGIC || key->kind != ZW_KEY) return NT_STATUS_INVALID_HANDLE;
    char nm[128];
    ustr_ascii(name, nm, sizeof(nm));
    UINT8 data[1024];
    UINT32 type = 0;
    int len = um_registry_get_value(key->path, nm, &type, data, sizeof(data));
    if (len < 0) return NT_STATUS_OBJECT_NAME_NOT_FOUND;
    UINT32 dlen = (UINT32)len, need, fixed;
    UINT8 info[1024 + 32];
    if (cls == 2) {                                   /* PARTIAL: TitleIndex, Type, DataLength, Data[] */
        fixed = 12;
        need = fixed + dlen;
        memset(info, 0, 12);
        memcpy(info + 4, &type, 4);
        memcpy(info + 8, &dlen, 4);
        if (dlen) memcpy(info + 12, data, dlen);
    } else if (cls == 1) {                            /* FULL: + DataOffset, NameLength, Name[] */
        UINT32 nlen = 0; for (const char *c = nm; *c; c++) nlen += 2;
        UINT32 doff = (20 + nlen + 7) & ~7u;
        fixed = 20;
        need = doff + dlen;
        memset(info, 0, need < sizeof(info) ? need : sizeof(info));
        memcpy(info + 4, &type, 4);
        memcpy(info + 8, &doff, 4);
        memcpy(info + 12, &dlen, 4);
        memcpy(info + 16, &nlen, 4);
        for (UINT32 i = 0; i * 2 < nlen; i++) *(UINT16 *)(info + 20 + 2 * i) = (UINT8)nm[i];
        if (dlen && doff + dlen <= sizeof(info)) memcpy(info + doff, data, dlen);
    } else {                                          /* BASIC: TitleIndex, Type, NameLength, Name[] */
        UINT32 nlen = 0; for (const char *c = nm; *c; c++) nlen += 2;
        fixed = 12;
        need = fixed + nlen;
        memset(info, 0, 12);
        memcpy(info + 4, &type, 4);
        memcpy(info + 8, &nlen, 4);
        for (UINT32 i = 0; i * 2 < nlen; i++) *(UINT16 *)(info + 12 + 2 * i) = (UINT8)nm[i];
    }
    if (result_len) *result_len = need;
    if (cap < fixed) return NT_STATUS_BUFFER_TOO_SMALL;
    UINT32 k = cap < need ? cap : need;
    memcpy(out, info, k);
    return cap < need ? NT_STATUS_BUFFER_OVERFLOW : NT_STATUS_SUCCESS;
}

NT_API NTSTATUS nt_ZwDeleteValueKey(ZwObject *key, NT_UNICODE_STRING *name)
{
    if (!key || key->magic != ZW_MAGIC || key->kind != ZW_KEY) return NT_STATUS_INVALID_HANDLE;
    char nm[128];
    ustr_ascii(name, nm, sizeof(nm));
    return um_registry_delete_value(key->path, nm) ? NT_STATUS_SUCCESS : NT_STATUS_OBJECT_NAME_NOT_FOUND;
}

NT_API NTSTATUS nt_ZwDeleteKey(ZwObject *key)
{
    if (!key || key->magic != ZW_MAGIC || key->kind != ZW_KEY) return NT_STATUS_INVALID_HANDLE;
    (void)key;   /* NovaOS keeps keys in a shared tree; a driver's test key is left for the hive to drop */
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Files
 * ----------------------------------------------------------------------- */
NT_API NTSTATUS nt_ZwCreateFile(void *handle, UINT32 access, NtObjectAttributes *oa, void *iosb,
                                void *alloc, UINT32 attribs, UINT32 share, UINT32 disposition,
                                UINT32 options, void *ea, UINT32 ea_len)
{
    (void)access; (void)iosb; (void)alloc; (void)attribs; (void)share; (void)disposition; (void)options; (void)ea; (void)ea_len;
    char name[256];
    if (!oa) return NT_STATUS_INVALID_PARAMETER;
    ustr_ascii(oa->ObjectName, name, sizeof(name));
    /* \??\C:\... or \DosDevices\C:\...: the drive-letter path */
    const char *p = name;
    if (!strncmp(p, "\\??\\", 4)) p += 4;
    else if (!strncmp(p, "\\DosDevices\\", 12)) p += 12;
    FsLock();
    RamNode *n = RamfsResolve(NULL, p);
    if (n && !n->dir) RamfsPin(n);
    FsUnlock();
    if (!n || n->dir) return NT_STATUS_OBJECT_NAME_NOT_FOUND;
    ZwObject *o = zw_new(ZW_FILE);
    if (!o) { FsLock(); RamfsUnpin(n); FsUnlock(); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    o->node = n;
    if (!put_handle(handle, o)) { kfree(o); FsLock(); RamfsUnpin(n); FsUnlock(); return NT_STATUS_ACCESS_VIOLATION; }
    return NT_STATUS_SUCCESS;
}

NT_API NTSTATUS nt_ZwClose(ZwObject *o)
{
    if (NtPsHandleClose(o)) return NT_STATUS_SUCCESS;   /* a system-thread handle (ps.c) */
    if (!o || o->magic != ZW_MAGIC) return NT_STATUS_INVALID_HANDLE;
    if (o->kind == ZW_FILE && o->node) { FsLock(); RamfsUnpin(o->node); FsUnlock(); }
    o->magic = 0;
    kfree(o);
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * ZwLoadDriver / ZwUnloadDriver: a driver (wdfldr) loads another
 * ----------------------------------------------------------------------- */
/* The service name at the end of "\Registry\...\Services\<name>" */
static void service_of(const NT_UNICODE_STRING *path, char *out, int cap)
{
    char full[260];
    ustr_ascii(path, full, sizeof(full));
    const char *s = full;
    for (const char *c = full; *c; c++) if (*c == '\\') s = c + 1;
    ksnprintf(out, cap, "%s", s);
}

NT_API NTSTATUS nt_ZwLoadDriver(NT_UNICODE_STRING *service_path)
{
    char name[64];
    service_of(service_path, name, sizeof(name));
    return NtDrvLoad(name);
}

NT_API NTSTATUS nt_ZwUnloadDriver(NT_UNICODE_STRING *service_path)
{
    char name[64];
    service_of(service_path, name, sizeof(name));
    return NtDrvUnload(name);
}

/* -----------------------------------------------------------------------
 * ZwQuerySystemInformation: SystemModuleInformation (class 11), which a
 * driver reads to find image bases (ntoskrnl.exe and the loaded .sys)
 * ----------------------------------------------------------------------- */
#define SystemModuleInformation 11

typedef struct {
    void   *Section, *MappedBase, *ImageBase;
    UINT32  ImageSize, Flags;
    UINT16  LoadOrderIndex, InitOrderIndex, LoadCount, OffsetToFileName;
    UINT8   FullPathName[256];
} RtlProcessModule;

NT_API NTSTATUS nt_ZwQuerySystemInformation(UINT32 cls, void *info, UINT32 len, UINT32 *ret_len)
{
    if (cls != SystemModuleInformation) {
        if (ret_len) *ret_len = 0;
        return NT_STATUS_NOT_IMPLEMENTED;
    }
    UINT64 base[16];
    UINT32 size[16];
    char path[16][260];
    int n = NtDrvModules(base, size, path, 16);
    UINT32 need = 4 + (UINT32)(n + 1) * sizeof(RtlProcessModule);   /* +1 for ntoskrnl.exe */
    if (ret_len) *ret_len = need;
    if (len < need) return NT_STATUS_INFO_LENGTH_MISMATCH;
    *(UINT32 *)info = (UINT32)(n + 1);
    RtlProcessModule *m = (RtlProcessModule *)((UINT8 *)info + 8);   /* ULONG count + 4 pad */
    memset(m, 0, (n + 1) * sizeof(*m));
    m[0].ImageBase = m[0].MappedBase = (void *)(uintptr_t)KERNEL_IMAGE_BASE;
    m[0].LoadOrderIndex = 0;
    ksnprintf((char *)m[0].FullPathName, 256, "\\SystemRoot\\system32\\ntoskrnl.exe");
    m[0].OffsetToFileName = (UINT16)(strlen("\\SystemRoot\\system32\\") );
    for (int i = 0; i < n; i++) {
        RtlProcessModule *e = &m[i + 1];
        e->ImageBase = e->MappedBase = (void *)(uintptr_t)base[i];
        e->ImageSize = size[i];
        e->LoadOrderIndex = (UINT16)(i + 1);
        const char *leaf = path[i];
        for (const char *c = path[i]; *c; c++) if (*c == '\\' || *c == '/') leaf = c + 1;
        ksnprintf((char *)e->FullPathName, 256, "\\SystemRoot\\system32\\drivers\\%s", leaf);
        e->OffsetToFileName = (UINT16)(strlen("\\SystemRoot\\system32\\drivers\\"));
    }
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * RtlQueryRegistryValues(Ex): a table of values to read from a key, each
 * stored straight into the caller's variable (RTL_QUERY_REGISTRY_DIRECT)
 * or handed to a query routine, with a default for a missing one
 * ----------------------------------------------------------------------- */
typedef NTSTATUS (NT_API *RtlQueryRoutine)(NT_WCHAR *name, UINT32 type, void *data, UINT32 len, void *ctx, void *entry_ctx);

typedef struct {                                     /* RTL_QUERY_REGISTRY_TABLE */
    RtlQueryRoutine QueryRoutine;
    UINT32          Flags;
    NT_WCHAR       *Name;
    void           *EntryContext;
    UINT32          DefaultType;
    void           *DefaultData;
    UINT32          DefaultLength;
} RtlQueryTable;

#define RTL_REGISTRY_ABSOLUTE     0
#define RTL_REGISTRY_SERVICES     1
#define RTL_REGISTRY_CONTROL      2
#define RTL_REGISTRY_WINDOWS_NT   3
#define RTL_REGISTRY_DEVICEMAP    4
#define RTL_REGISTRY_HANDLE       0x40000000u
#define RTL_REGISTRY_OPTIONAL     0x80000000u
#define RTL_QUERY_REGISTRY_SUBKEY     0x01u
#define RTL_QUERY_REGISTRY_TOPKEY     0x02u
#define RTL_QUERY_REGISTRY_REQUIRED   0x04u
#define RTL_QUERY_REGISTRY_NOVALUE    0x08u
#define RTL_QUERY_REGISTRY_DIRECT     0x20u
#define RTL_QUERY_REGISTRY_DELETE     0x40u
#define RTL_QUERY_REGISTRY_TYPECHECK  0x100u
#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_MULTI_SZ 7

static void wide_ascii(const NT_WCHAR *w, char *out, int cap)
{
    int n = 0;
    while (w && w[n] && n < cap - 1) { out[n] = w[n] < 0x80 ? (char)w[n] : '?'; n++; }
    out[n] = 0;
}

/* The key @relative and @path name, without "\Registry\" */
static bool query_key(UINT32 relative, const NT_WCHAR *path, char *out, int cap)
{
    if (relative & RTL_REGISTRY_HANDLE) {
        const ZwObject *k = (const ZwObject *)path;
        if (!k || k->magic != ZW_MAGIC || k->kind != ZW_KEY) return false;
        ksnprintf(out, cap, "%s", k->path);
        return true;
    }
    static const char *const roots[] = {
        "", "Machine\\System\\CurrentControlSet\\Services", "Machine\\System\\CurrentControlSet\\Control",
        "Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion", "Machine\\Hardware\\DeviceMap",
    };
    UINT32 r = relative & 0xFFFF;
    if (r > RTL_REGISTRY_DEVICEMAP) return false;
    char p[200];
    wide_ascii(path, p, sizeof(p));
    if (r == RTL_REGISTRY_ABSOLUTE) {
        NT_UNICODE_STRING u = { 0 };
        NtObjectAttributes oa = { 0 };
        u.Buffer = (NT_WCHAR *)path;
        u.Length = u.MaximumLength = 0;
        while (path && path[u.Length / 2]) u.Length += 2;
        oa.ObjectName = &u;
        return oa_reg_path(&oa, out, cap);
    }
    ksnprintf(out, cap, "%s%s%s", roots[r], p[0] ? "\\" : "", p[0] == '\\' ? p + 1 : p);
    return true;
}

/* One value for an entry: into its variable (DIRECT) or to its routine */
static NTSTATUS query_deliver(const RtlQueryTable *q, UINT32 type, const UINT8 *data, UINT32 len, void *ctx)
{
    if (q->Flags & RTL_QUERY_REGISTRY_DIRECT) {
        if (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) {
            NT_UNICODE_STRING *u = q->EntryContext;
            UINT32 n = len >= 2 && !data[len - 1] && !data[len - 2] ? len - 2 : len;   /* (without its terminator) */
            if (!u->Buffer) {
                u->Buffer = kzalloc(n + 2);
                if (!u->Buffer) return STATUS_INSUFFICIENT_RESOURCES;
                u->MaximumLength = (unsigned short)(n + 2);
            }
            if (n + 2 > u->MaximumLength) return STATUS_BUFFER_TOO_SMALL;
            memcpy(u->Buffer, data, n);
            u->Buffer[n / 2] = 0;
            u->Length = (unsigned short)n;
            return NT_STATUS_SUCCESS;
        }
        if (len <= 4) {                               /* (a DWORD or smaller: stored as it is) */
            memcpy(q->EntryContext, data, len);
            return NT_STATUS_SUCCESS;
        }
        /* larger: the first LONG of the buffer is minus its size */
        INT32 cap = -*(INT32 *)q->EntryContext;
        if (cap < (INT32)len) return STATUS_BUFFER_TOO_SMALL;
        memcpy(q->EntryContext, data, len);
        return NT_STATUS_SUCCESS;
    }
    return q->QueryRoutine(q->Name, type, (void *)data, len, ctx, q->EntryContext);
}

static NTSTATUS query_values(UINT32 relative, const NT_WCHAR *path, RtlQueryTable *table, void *ctx)
{
    char top[256], key[256];
    if (!query_key(relative, path, top, sizeof(top))) return NT_STATUS_INVALID_PARAMETER;
    if (!um_registry_key_exists(top))
        return relative & RTL_REGISTRY_OPTIONAL ? NT_STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND;
    ksnprintf(key, sizeof(key), "%s", top);
    for (RtlQueryTable *q = table; q->QueryRoutine || q->Name; q++) {
        if (q->Flags & (RTL_QUERY_REGISTRY_SUBKEY | RTL_QUERY_REGISTRY_TOPKEY)) {
            char sub[128];
            wide_ascii(q->Name, sub, sizeof(sub));
            if (q->Flags & RTL_QUERY_REGISTRY_TOPKEY) ksnprintf(key, sizeof(key), "%s", top);
            if ((q->Flags & RTL_QUERY_REGISTRY_SUBKEY) && sub[0]) {
                ksnprintf(key, sizeof(key), "%s\\%s", top, sub);
                if (!um_registry_key_exists(key)) {
                    if (q->Flags & RTL_QUERY_REGISTRY_REQUIRED) return STATUS_OBJECT_NAME_NOT_FOUND;
                    ksnprintf(key, sizeof(key), "%s", top);
                }
            }
            if (!q->QueryRoutine) continue;
        }
        if (!q->Name) {
            /* (a routine for every value of the key: NovaOS's registry
             * lists no values here, so it is called once with none, as
             * Windows calls it for a key without values when NOVALUE) */
            if (q->Flags & RTL_QUERY_REGISTRY_NOVALUE) {
                NTSTATUS st = q->QueryRoutine(NULL, REG_NONE, NULL, 0, ctx, q->EntryContext);
                if (!NT_SUCCESS(st)) return st;
            }
            continue;
        }
        char nm[128];
        wide_ascii(q->Name, nm, sizeof(nm));
        UINT8 data[1024];
        UINT32 type = 0;
        int len = um_registry_get_value(key, nm, &type, data, sizeof(data));
        UINT32 want = q->DefaultType >> 24;          /* (RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) */
        NTSTATUS st;
        if (len >= 0) {
            if ((q->Flags & RTL_QUERY_REGISTRY_TYPECHECK) && want != type) return STATUS_OBJECT_TYPE_MISMATCH;
            st = query_deliver(q, type, data, (UINT32)len, ctx);
        } else {
            UINT32 dtype = q->DefaultType & 0xFFFFFF;
            if (dtype == REG_NONE || !q->DefaultData) {
                if (q->Flags & RTL_QUERY_REGISTRY_REQUIRED) return STATUS_OBJECT_NAME_NOT_FOUND;
                continue;
            }
            UINT32 dlen = q->DefaultLength;
            if (!dlen && (dtype == REG_SZ || dtype == REG_EXPAND_SZ)) {
                const NT_WCHAR *w = q->DefaultData;
                while (w[dlen / 2]) dlen += 2;
                dlen += 2;
            }
            st = query_deliver(q, dtype, q->DefaultData, dlen, ctx);
        }
        if (!NT_SUCCESS(st)) return st;
        if (q->Flags & RTL_QUERY_REGISTRY_DELETE) um_registry_delete_value(key, nm);
    }
    return NT_STATUS_SUCCESS;
}

NT_API NTSTATUS nt_RtlQueryRegistryValues(UINT32 relative, const NT_WCHAR *path, RtlQueryTable *table,
                                          void *ctx, void *environment)
{
    (void)environment;
    return query_values(relative, path, table, ctx);
}

/* The Ex form: the same, but a DIRECT value of a type the table did not
 * check is refused rather than written over the caller's variable */
NT_API NTSTATUS nt_RtlQueryRegistryValuesEx(UINT32 relative, const NT_WCHAR *path, RtlQueryTable *table,
                                            void *ctx, void *environment)
{
    (void)environment;
    for (RtlQueryTable *q = table; q->QueryRoutine || q->Name; q++)
        if ((q->Flags & RTL_QUERY_REGISTRY_DIRECT) && !(q->Flags & RTL_QUERY_REGISTRY_TYPECHECK)) {
            kprintf("[NT] RtlQueryRegistryValuesEx: a DIRECT entry without RTL_QUERY_REGISTRY_TYPECHECK\n");
            return NT_STATUS_INVALID_PARAMETER;
        }
    return query_values(relative, path, table, ctx);
}

/* IoOpenDriverRegistryKey: the driver's Parameters key, or its State key
 * for state it keeps across starts (under its service key, created when
 * missing) */
NT_API NTSTATUS nt_IoOpenDriverRegistryKey(NT_DRIVER_OBJECT *obj, int type, UINT32 access, UINT32 flags, void *handle)
{
    (void)access; (void)flags;
    NtDriver *drv = NtIoDriverOf(obj);
    if (!drv || (type != 0 && type != 1)) return NT_STATUS_INVALID_PARAMETER;   /* DriverRegKeyParameters, DriverRegKeyPersistentState */
    ZwObject *o = zw_new(ZW_KEY);
    if (!o) return STATUS_INSUFFICIENT_RESOURCES;
    ksnprintf(o->path, sizeof(o->path), "Machine\\System\\CurrentControlSet\\Services\\%s\\%s", drv->service,
              type ? "State" : "Parameters");
    if (!um_registry_create_key(o->path)) { kfree(o); return STATUS_INSUFFICIENT_RESOURCES; }
    if (!put_handle(handle, o)) { kfree(o); return NT_STATUS_ACCESS_VIOLATION; }
    return NT_STATUS_SUCCESS;
}
