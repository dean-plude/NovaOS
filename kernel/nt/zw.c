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
#define NT_STATUS_KEY_DELETED  ((NT_STATUS)0xC000017C)   /* the handle's key is gone */

bool NtZwKeyPath(void *handle, char *out, int cap);   /* (ntoskrnl.c's RtlQueryRegistryValues) */

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
    for (int i = 0; pre[i] && same; i++) same = (pre[i] == '\\' ? p[i] : p[i] | 0x20) == pre[i];
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

static bool is_key(const ZwObject *key)
{
    return key && key->magic == ZW_MAGIC && key->kind == ZW_KEY;
}

/* Hand an information record built in @info (@need bytes, of which the
 * first @fixed are its fixed part) to a caller's buffer, as the registry
 * does on Windows: *ResultLength is always the full size; a buffer shorter
 * than the fixed part gets nothing (BUFFER_TOO_SMALL); one that holds the
 * fixed part but not the rest gets as much as fits (BUFFER_OVERFLOW). */
static NTSTATUS give_info(const UINT8 *info, UINT32 need, UINT32 fixed, void *out, UINT32 cap, UINT32 *result_len)
{
    if (result_len) *result_len = need;
    if (cap < fixed) return NT_STATUS_BUFFER_TOO_SMALL;
    if (!out) return NT_STATUS_ACCESS_VIOLATION;
    memcpy(out, info, cap < need ? cap : need);
    return cap < need ? NT_STATUS_BUFFER_OVERFLOW : NT_STATUS_SUCCESS;
}

/* KEY_VALUE_{BASIC 0, FULL 1, PARTIAL 2}_INFORMATION for one value (its
 * name UTF-16, @nchars characters), shared by ZwQueryValueKey and
 * ZwEnumerateValueKey */
static NTSTATUS value_info(const UINT16 *name, UINT32 nchars, UINT32 type, const void *data, UINT32 dlen,
                           UINT32 cls, void *out, UINT32 cap, UINT32 *result_len)
{
    UINT32 nlen = 2 * nchars, need, fixed, doff = 0;
    switch (cls) {
    case 0: fixed = 12; need = fixed + nlen; break;               /* TitleIndex, Type, NameLength, Name[] */
    case 1: fixed = 20; doff = (fixed + nlen + 7) & ~7u; need = doff + dlen; break;
                                                                  /* + DataOffset, DataLength; data 8-aligned */
    case 2: fixed = 12; need = fixed + dlen; break;               /* TitleIndex, Type, DataLength, Data[] */
    default: return STATUS_INVALID_INFO_CLASS;
    }
    UINT8 *info = kzalloc(need);
    if (!info) return NT_STATUS_INSUFFICIENT_RESOURCES;
    memcpy(info + 4, &type, 4);
    if (cls == 0) {
        memcpy(info + 8, &nlen, 4);
        memcpy(info + 12, name, nlen);
    } else if (cls == 1) {
        memcpy(info + 8, &doff, 4);
        memcpy(info + 12, &dlen, 4);
        memcpy(info + 16, &nlen, 4);
        memcpy(info + 20, name, nlen);
        if (dlen) memcpy(info + doff, data, dlen);
    } else {
        memcpy(info + 8, &dlen, 4);
        if (dlen) memcpy(info + 12, data, dlen);
    }
    NTSTATUS st = give_info(info, need, fixed, out, cap, result_len);
    kfree(info);
    return st;
}

NT_API NTSTATUS nt_ZwQueryValueKey(ZwObject *key, NT_UNICODE_STRING *name, UINT32 cls, void *out,
                                   UINT32 cap, UINT32 *result_len)
{
    if (!is_key(key)) return NT_STATUS_INVALID_HANDLE;
    char nm[128];
    ustr_ascii(name, nm, sizeof(nm));
    UINT32 type = 0;
    int len = um_registry_get_value(key->path, nm, &type, NULL, 0);
    if (len < 0) return NT_STATUS_OBJECT_NAME_NOT_FOUND;
    UINT8 *data = kmalloc((UINT32)len + 1);
    if (!data) return NT_STATUS_INSUFFICIENT_RESOURCES;
    int got = um_registry_get_value(key->path, nm, &type, data, len);
    if (got < 0) { kfree(data); return NT_STATUS_OBJECT_NAME_NOT_FOUND; }
    if (got > len) got = len;                         /* (grew in between: what was read) */
    UINT16 wn[128];
    UINT32 n = 0;
    for (; nm[n]; n++) wn[n] = (UINT8)nm[n];
    NTSTATUS st = value_info(wn, n, type, data, (UINT32)got, cls, out, cap, result_len);
    kfree(data);
    return st;
}

/* ZwEnumerateValueKey: the @index'th value, as ZwQueryValueKey gives it */
NT_API NTSTATUS nt_ZwEnumerateValueKey(ZwObject *key, UINT32 index, UINT32 cls, void *out,
                                       UINT32 cap, UINT32 *result_len)
{
    if (!is_key(key)) return NT_STATUS_INVALID_HANDLE;
    UINT16 name[256];
    int nlen = 0;
    UINT32 type = 0;
    int len = um_registry_enum_value(key->path, index, name, 256, &nlen, &type, NULL, 0);
    if (len == -2) return NT_STATUS_KEY_DELETED;
    if (len < 0) return STATUS_NO_MORE_ENTRIES;
    UINT8 *data = kmalloc((UINT32)len + 1);
    if (!data) return NT_STATUS_INSUFFICIENT_RESOURCES;
    int got = um_registry_enum_value(key->path, index, name, 256, &nlen, &type, data, len);
    if (got < 0) { kfree(data); return STATUS_NO_MORE_ENTRIES; }
    if (got > len) got = len;
    if (nlen > 256) nlen = 256;
    NTSTATUS st = value_info(name, (UINT32)nlen, type, data, (UINT32)got, cls, out, cap, result_len);
    kfree(data);
    return st;
}

/* KEY_{BASIC 0, NODE 1, FULL 2, NAME 3}_INFORMATION for the key at @path
 * (named @name, @nchars UTF-16 characters), shared by ZwQueryKey and
 * ZwEnumerateKey.  NovaOS keys have no class, so ClassOffset is -1 and
 * ClassLength 0. */
static NTSTATUS key_info(const char *path, const UINT16 *name, UINT32 nchars, UINT32 cls,
                         void *out, UINT32 cap, UINT32 *result_len)
{
    UmRegKeyInfo ki;
    if (!um_registry_query_key(path, &ki)) return NT_STATUS_KEY_DELETED;
    UINT32 nlen = 2 * nchars, need, fixed, no_class = 0xFFFFFFFFu;
    UINT8 *info;
    switch (cls) {
    case 0:                                               /* LastWriteTime, TitleIndex, NameLength, Name[] */
        fixed = 16; need = fixed + nlen;
        if (!(info = kzalloc(need))) return NT_STATUS_INSUFFICIENT_RESOURCES;
        memcpy(info, &ki.wtime, 8);
        memcpy(info + 12, &nlen, 4);
        memcpy(info + 16, name, nlen);
        break;
    case 1:                                               /* + ClassOffset, ClassLength before NameLength */
        fixed = 24; need = fixed + nlen;
        if (!(info = kzalloc(need))) return NT_STATUS_INSUFFICIENT_RESOURCES;
        memcpy(info, &ki.wtime, 8);
        memcpy(info + 12, &no_class, 4);
        memcpy(info + 20, &nlen, 4);
        memcpy(info + 24, name, nlen);
        break;
    case 2:                                               /* the counts; Class[] (none) */
        fixed = 44; need = fixed;
        if (!(info = kzalloc(need))) return NT_STATUS_INSUFFICIENT_RESOURCES;
        memcpy(info, &ki.wtime, 8);
        memcpy(info + 12, &no_class, 4);
        memcpy(info + 20, &ki.subkeys, 4);
        memcpy(info + 24, &ki.max_name, 4);
        memcpy(info + 32, &ki.values, 4);
        memcpy(info + 36, &ki.max_value_name, 4);
        memcpy(info + 40, &ki.max_data, 4);
        break;
    case 3: {                                             /* NameLength, Name[]: the full "\REGISTRY\..." */
        char full[300];
        ksnprintf(full, sizeof(full), "\\REGISTRY\\%s", path);
        UINT32 n = (UINT32)strlen(full);
        fixed = 4; need = fixed + 2 * n;
        if (!(info = kzalloc(need))) return NT_STATUS_INSUFFICIENT_RESOURCES;
        UINT32 fl = 2 * n;
        memcpy(info, &fl, 4);
        for (UINT32 i = 0; i < n; i++) { UINT16 c = (UINT8)full[i]; memcpy(info + 4 + 2 * i, &c, 2); }
        break;
    }
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
    NTSTATUS st = give_info(info, need, fixed, out, cap, result_len);
    kfree(info);
    return st;
}

NT_API NTSTATUS nt_ZwQueryKey(ZwObject *key, UINT32 cls, void *out, UINT32 cap, UINT32 *result_len)
{
    if (!is_key(key)) return NT_STATUS_INVALID_HANDLE;
    const char *leaf = key->path;                         /* the key's own name: its last component */
    for (const char *c = key->path; *c; c++) if (*c == '\\') leaf = c + 1;
    UINT16 name[256];
    UINT32 n = 0;
    for (; leaf[n] && n < 255; n++) name[n] = (UINT8)leaf[n];
    return key_info(key->path, name, n, cls, out, cap, result_len);
}

/* ZwEnumerateKey: the @index'th subkey, as ZwQueryKey would describe it */
NT_API NTSTATUS nt_ZwEnumerateKey(ZwObject *key, UINT32 index, UINT32 cls, void *out,
                                  UINT32 cap, UINT32 *result_len)
{
    if (!is_key(key)) return NT_STATUS_INVALID_HANDLE;
    if (cls > 2) return STATUS_INVALID_INFO_CLASS;        /* (KeyNameInformation is ZwQueryKey's) */
    UINT16 name[256];
    int n = um_registry_enum_key(key->path, index, name, 255, NULL);
    if (n == -2) return NT_STATUS_KEY_DELETED;
    if (n < 0) return STATUS_NO_MORE_ENTRIES;
    if (n > 255) n = 255;
    char sub[300];
    int at = ksnprintf(sub, sizeof(sub), "%s\\", key->path);
    if (at < 0 || at >= (int)sizeof(sub)) return NT_STATUS_OBJECT_NAME_INVALID;
    for (int i = 0; i < n && at < (int)sizeof(sub) - 1; i++) sub[at++] = name[i] && name[i] < 0x80 ? (char)name[i] : '?';
    sub[at] = 0;
    return key_info(sub, name, (UINT32)n, cls, out, cap, result_len);
}

/* NovaOS's registry keeps no security descriptors (no ACLs on keys), so
 * there is nothing to change: accepted, as a driver setting its key's
 * ACL expects */
NT_API NTSTATUS nt_ZwSetSecurityObject(void *handle, UINT32 info, void *sd)
{
    (void)info; (void)sd;
    if (!handle) return NT_STATUS_INVALID_HANDLE;
    return NT_STATUS_SUCCESS;
}

/* The registry path a Zw key handle stands for (without "\Registry\"), for
 * RtlQueryRegistryValues' RTL_REGISTRY_HANDLE (ntoskrnl.c) */
bool NtZwKeyPath(void *handle, char *out, int cap)
{
    const ZwObject *key = handle;
    if (!is_key(key) || cap <= 0) return false;
    ksnprintf(out, cap, "%s", key->path);
    return true;
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

/* (RtlQueryRegistryValues itself is ntoskrnl.c's) */
NT_API NTSTATUS nt_RtlQueryRegistryValues(UINT32 relative, const NT_WCHAR *path, void *table, void *ctx,
                                          void *environment);

/* The Ex form: the same, but a DIRECT value of a type the table did not
 * check is refused rather than written over the caller's variable */
NT_API NTSTATUS nt_RtlQueryRegistryValuesEx(UINT32 relative, const NT_WCHAR *path, RtlQueryTable *table,
                                            void *ctx, void *environment)
{
    for (RtlQueryTable *q = table; q->QueryRoutine || q->Name; q++)
        if ((q->Flags & RTL_QUERY_REGISTRY_DIRECT) && !(q->Flags & RTL_QUERY_REGISTRY_TYPECHECK)) {
            kprintf("[NT] RtlQueryRegistryValuesEx: a DIRECT entry without RTL_QUERY_REGISTRY_TYPECHECK\n");
            return NT_STATUS_INVALID_PARAMETER;
        }
    return nt_RtlQueryRegistryValues(relative, path, table, ctx, environment);
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
