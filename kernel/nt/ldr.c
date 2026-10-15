/*
 * ldr.c — loading Windows kernel-mode drivers (.sys images)
 *
 * NtLoadDriver names a service key (HKLM\SYSTEM\CurrentControlSet\
 * Services\<name>, Type 1: a kernel driver) whose ImagePath is the
 * driver's file.  The image is a PE32+ file with subsystem native, signed
 * as Windows' kernel-mode signing policy requires (ci.c checks that before
 * anything else, so an unsigned image is refused with
 * STATUS_INVALID_IMAGE_HASH and the reason logged).  It is laid out in
 * memory as Windows lays it out, relocated to its place in the
 * driver window (DRV_BASE: next to the kernel image, in the top-level page
 * table entry every address space shares, so a driver runs in whichever
 * process calls it), its imports bound against the export tables
 * (exports.h; a missing one is logged by name and the driver does not
 * start, as on Windows), its /GS cookie set, and each section mapped with
 * its own protection (code read-only and executable, data not
 * executable).  Then DriverEntry(DRIVER_OBJECT *, UNICODE_STRING
 * *RegistryPath) runs, called with the Microsoft x64 convention.
 *
 * NtUnloadDriver calls DriverUnload, removes what it left behind
 * (devices, links) and unmaps the image; with files still open on its
 * devices that waits until the last one closes, as on Windows.
 *
 * An import from a module other than the kernel's own (ntoskrnl.exe,
 * hal.dll, usbd.sys, wmilib.sys, storport.sys, ndis.sys, cng.sys and
 * the kernel API sets such as ext-ms-win-ntos-werkernel-l1-1-1.dll:
 * NtExportModule) is an
 * export driver's, as on Windows (WDFLDR.SYS for a KMDF driver): the one
 * already loaded, or System32\drivers\<module> loaded for it: mapped the
 * same way (signature checked, its own imports bound), no driver object
 * and no DriverEntry, its DllInitialize called with its service key.
 * NovaOS keeps an export driver loaded once it is (Windows unloads one
 * when its last importer goes, calling DllUnload; the ones in use, the
 * KMDF loader, are never unloaded there either).  A DriverEntry may load
 * another driver (ZwLoadDriver: WdfLdr.sys loads Wdf01000.sys from the
 * KMDF driver's DriverEntry), so loads nest.
 *
 * A few services are drivers built into the kernel rather than files
 * (hidusb.c's "HidUsb", which Windows' input.inf runs from hidusb.sys,
 * usbvideo.c's "usbvideo", usbvideo.inf's usbvideo.sys, mshidkmdf.c's
 * "mshidkmdf", the minidriver under a KMDF HID driver, and kbdhid.c's
 * "kbdhid", "mouhid" and "hiddigi", the keyboard, mouse and digitizer
 * class drivers on HID collections):
 * NtDrvLoad gives them a driver object and calls their DriverEntry the
 * same way, with no image to map or check; they are never unloaded.
 *
 * Loads and unloads run under the big kernel lock.
 */
#include "nt.h"
#include "ci.h"
#include "hidclass.h"
#include "usbvideo.h"
#include "../ldr/ldr.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/cpu.h"
#include "../ke/smp.h"
#include "../fs/ramfs.h"
#include "../um/um.h"
#include "../um/um_internal.h"

#define DRV_BASE   UINT64_C(0xFFFFFFFF00000000)    /* 1 GiB below the kernel image's 2 */
#define DRV_SIZE   (1ull << 30)
#define DRV_SLOT   (2ull << 20)                     /* images take whole 2 MiB slots */
#define DRV_SLOTS  ((int)(DRV_SIZE / DRV_SLOT))
#define DRV_IMAGE_MAX  (64u << 20)

#define IMAGE_SUBSYSTEM_NATIVE      1
#define IMAGE_FILE_RELOCS_STRIPPED  0x0001
#define ST_IMAGE_ALREADY_LOADED     ((NTSTATUS)0xC000010E)
#define ST_PROCEDURE_NOT_FOUND      ((NTSTATUS)0xC000007A)
#define ST_DRIVER_UNLOAD_FAILED     ((NTSTATUS)0xC0000010)   /* (STATUS_INVALID_DEVICE_REQUEST, as Windows) */

#define CONTROL_KEY "Machine\\SYSTEM\\CurrentControlSet\\Control"

static NtDriver *g_drivers;
static bool      g_slot_used[DRV_SLOTS];
static bool      g_ready;
static bool      g_test_signing;
#define LOAD_DEPTH 4
static NtDriver *g_loading[LOAD_DEPTH];   /* the drivers whose DriverEntry or DllInitialize runs, outermost first */
static int       g_nloading;

/* The signing policy is Windows' (ci.c): a driver loads only with a
 * Microsoft driver signature, or with any intact signature when test
 * signing is on.  Test signing is NovaOS's "bcdedit /set testsigning on":
 * HKLM\SYSTEM\CurrentControlSet\Control\NovaOS TestSigning = 1, read when
 * NovaOS starts (a change takes a restart), and shown to programs in
 * Control's SystemStartOptions as on Windows. */
void NtDrvBootOptions(void)
{
    UINT32 v = 0;
    g_test_signing = um_registry_get_dword(CONTROL_KEY "\\NovaOS", "TestSigning", &v) && v;
    um_registry_set_sz(CONTROL_KEY, "SystemStartOptions", g_test_signing ? " TESTSIGNING" : "");
    if (g_test_signing) kprintf("[NT] Test signing on: drivers with a test signature load too (unsigned ones still do not)\n");
}

/* ci.c's catalogs: the .cat files setupapi installed in CatRoot, each
 * pinned while ci.c reads it */
static RamNode *g_cat;

static bool is_cat(const char *n)
{
    size_t k = strlen(n);
    return k > 4 && n[k - 4] == '.' && (n[k - 3] | 0x20) == 'c' && (n[k - 2] | 0x20) == 'a' && (n[k - 1] | 0x20) == 't';
}

bool CiCatalog(int i, const UINT8 **data, UINT32 *size, char *name, int cap)
{
    RamNode *f = NULL;
    FsLock();
    RamNode *dir = RamfsResolve(NULL, CI_CATROOT);
    if (dir && dir->dir && RamfsLoad(dir))
        for (RamNode *c = dir->child; c && !f; c = c->next)
            if (!c->dir && is_cat(c->name) && i-- == 0) f = c;
    if (f) RamfsPin(f);
    FsUnlock();
    if (!f) return false;
    g_cat = f;
    *data = (const UINT8 *)f->data;
    *size = f->data ? f->size : 0;
    ksnprintf(name, cap, "%s", f->name);
    return true;
}

void CiCatalogDone(int i)
{
    (void)i;
    FsLock();
    RamfsUnpin(g_cat);
    FsUnlock();
    g_cat = NULL;
}

void NtDrvInit(void)
{
    g_ready = NT_SUCCESS(paging_prepare(DRV_BASE, DRV_SIZE));
    NtIoInit();
    NtKeInit();
    NtIoWorkerInit();
    NtDxgkInit();
}

static bool same_ci(const char *a, const char *b)
{
    while (*a && *b && ((*a | 0x20) == (*b | 0x20) || *a == *b)) { a++; b++; }
    return !*a && !*b;
}

static NtDriver *find(const char *service)
{
    for (NtDriver *d = g_drivers; d; d = d->next)
        if (same_ci(d->service, service)) return d;
    return NULL;
}

NtDriver *NtDrvFind(const char *service) { return find(service); }
static NtDriver *find_image(const char *image)
{
    for (NtDriver *d = g_drivers; d; d = d->next)
        if (same_ci(d->image, image)) return d;
    return NULL;
}

static bool loading_push(NtDriver *drv)
{
    if (g_nloading == LOAD_DEPTH) return false;
    g_loading[g_nloading++] = drv;
    return true;
}

static void loading_pop(void) { g_loading[--g_nloading] = NULL; }


static NT_WCHAR *wide(const char *s, NT_UNICODE_STRING *u)
{
    size_t n = strlen(s);
    NT_WCHAR *w = kzalloc(2 * (n + 1));
    if (!w) return NULL;
    for (size_t i = 0; i < n; i++) w[i] = (UINT8)s[i];
    u->Buffer = w;
    u->Length = (unsigned short)(2 * n);
    u->MaximumLength = (unsigned short)(2 * n + 2);
    return w;
}

/* The image's file from the service's ImagePath: \SystemRoot\..., a path
 * relative to the Windows folder (System32\drivers\x.sys, as an .inf's
 * AddService writes), %SystemRoot%\..., \??\C:\... or C:\... */
static void image_path(const char *service, char *out, int cap)
{
    char key[160], raw[260];
    ksnprintf(key, sizeof(key), "Machine\\SYSTEM\\CurrentControlSet\\Services\\%s", service);
    if (!um_registry_get_sz(key, "ImagePath", raw, sizeof(raw)) || !raw[0])
        ksnprintf(raw, sizeof(raw), "System32\\drivers\\%s.sys", service);
    const char *p = raw;
    if (!strncmp(p, "\\??\\", 4)) p += 4;
    static const char *const roots[] = { "\\SystemRoot\\", "%SystemRoot%\\", "%windir%\\" };
    for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        size_t n = strlen(roots[i]);
        bool same = true;
        for (size_t k = 0; k < n && same; k++) same = (p[k] | 0x20) == (roots[i][k] | 0x20);
        if (same) { ksnprintf(out, cap, "C:\\Windows\\%s", p + n); return; }
    }
    if (p[0] && p[1] == ':') ksnprintf(out, cap, "%s", p);
    else ksnprintf(out, cap, "C:\\Windows\\%s", p[0] == '\\' ? p + 1 : p);
}

static int slots_take(int n)
{
    for (int i = 0; i + n <= DRV_SLOTS; i++) {
        int k = 0;
        while (k < n && !g_slot_used[i + k]) k++;
        if (k == n) {
            for (k = 0; k < n; k++) g_slot_used[i + k] = true;
            return i;
        }
        i += k;
    }
    return -1;
}

/* Unmap @pages pages from @va, give their frames back, and flush every CPU */
static void unmap_image(UINT64 va, UINT32 pages)
{
    for (UINT32 i = 0; i < pages; i++) {
        uintptr_t pa = paging_virt_to_phys(va + (UINT64)i * PAGE_SIZE);
        if (!pa) continue;
        paging_unmap(va + (UINT64)i * PAGE_SIZE, PAGE_SIZE);
        pmm_free_page(pa & ~(uintptr_t)(PAGE_SIZE - 1));
    }
    smp_tlb_flush(0);
    write_cr3(read_cr3());
}

/* Apply the base relocations of image copy @img (laid out, @size bytes)
 * for a move by @delta */
static bool relocate(UINT8 *img, UINT32 size, UINT32 rva, UINT32 len, INT64 delta)
{
    if (!rva || !len || !delta) return true;
    if ((UINT64)rva + len > size) return false;
    for (UINT32 at = rva; at + 8 <= rva + len;) {
        IMAGE_BASE_RELOCATION *b = (IMAGE_BASE_RELOCATION *)(img + at);
        if (b->SizeOfBlock < 8 || at + b->SizeOfBlock > rva + len) break;
        UINT16 *e = (UINT16 *)(b + 1);
        for (UINT32 i = 0; i < (b->SizeOfBlock - 8) / 2; i++) {
            UINT32 type = e[i] >> 12, off = b->VirtualAddress + (e[i] & 0xFFF);
            if (type == IMAGE_REL_BASED_ABSOLUTE) continue;
            if (type != IMAGE_REL_BASED_DIR64 || (UINT64)off + 8 > size) return false;
            *(UINT64 *)(img + off) += (UINT64)delta;
        }
        at += b->SizeOfBlock;
    }
    return true;
}

/* The export @name (or ordinal @ord when @name is NULL) of the image
 * mapped at @base, or 0 (a forwarded export is not followed: none of the
 * export drivers NovaOS loads has one) */
static UINT64 image_export(UINT64 base, UINT32 size, const char *name, UINT32 ord)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)(uintptr_t)base;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(uintptr_t)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY *d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!d->VirtualAddress || d->Size < sizeof(IMAGE_EXPORT_DIRECTORY) || (UINT64)d->VirtualAddress + d->Size > size) return 0;
    const IMAGE_EXPORT_DIRECTORY *e = (const IMAGE_EXPORT_DIRECTORY *)(uintptr_t)(base + d->VirtualAddress);
    const UINT32 *fns = (const UINT32 *)(uintptr_t)(base + e->AddressOfFunctions);
    const UINT32 *names = (const UINT32 *)(uintptr_t)(base + e->AddressOfNames);
    const UINT16 *ords = (const UINT16 *)(uintptr_t)(base + e->AddressOfNameOrdinals);
    UINT32 index = (UINT32)-1;
    if (!name) index = ord - e->Base;
    else
        for (UINT32 i = 0; i < e->NumberOfNames; i++)
            if (names[i] < size && !strcmp((const char *)(uintptr_t)(base + names[i]), name)) { index = ords[i]; break; }
    if (index >= e->NumberOfFunctions || !fns[index] || fns[index] >= size) return 0;
    if (fns[index] - d->VirtualAddress < d->Size) return 0;             /* (forwarded) */
    return base + fns[index];
}

static NtDriver *load_export_driver(const char *image);

/* Bind the imports: false (each missing one logged) when any is missing */
static bool bind_imports(const char *image, UINT8 *img, UINT32 size, UINT32 rva, UINT32 len)
{
    if (!rva || !len) return true;
    bool ok = true;
    for (UINT32 at = rva; at + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= size; at += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(img + at);
        if (!d->Name && !d->FirstThunk) break;
        if (d->Name >= size || d->FirstThunk >= size) return false;
        const char *mod = (const char *)(img + d->Name);
        NtDriver *exp = NULL;                     /* (an export driver's import) */
        if (!NtExportModule(mod)) {
            exp = find_image(mod);
            if (!exp) exp = load_export_driver(mod);
            if (!exp) {
                kprintf("[NT] %s: imports from %s, which did not load\n", image, mod);
                ok = false;
                continue;
            }
        }
        UINT32 lookup = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
        for (UINT32 i = 0;; i++) {
            if ((UINT64)lookup + 8 * (i + 1) > size || (UINT64)d->FirstThunk + 8 * (i + 1) > size) return false;
            UINT64 t = ((UINT64 *)(img + lookup))[i];
            if (!t) break;
            UINT64 fn = 0;
            char name[96];
            if (t >> 63) ksnprintf(name, sizeof(name), "#%u", (UINT32)(t & 0xFFFF));
            else if ((t & 0x7FFFFFFF) + 2 < size) ksnprintf(name, sizeof(name), "%s", (const char *)(img + (t & 0x7FFFFFFF) + 2));
            else return false;
            if (exp) fn = image_export(exp->base, exp->size, t >> 63 ? NULL : name, (UINT32)(t & 0xFFFF));
            else if (!(t >> 63)) fn = NtExport(mod, name);
            if (!fn) {
                kprintf("[NT] %s: %s!%s not implemented\n", image, mod, name);
                ok = false;
            }
            ((UINT64 *)(img + d->FirstThunk))[i] = fn;
        }
    }
    return ok;
}

/* The /GS cookie (IMAGE_LOAD_CONFIG_DIRECTORY64.SecurityCookie): a
 * driver's GsDriverEntry refuses to start with the linker's default */
static void set_cookie(UINT8 *img, UINT32 size, UINT32 rva, UINT32 len, UINT64 base, UINT64 pref)
{
    if (!rva || len < 0x60 || (UINT64)rva + 0x60 > size) return;
    UINT64 va = *(UINT64 *)(img + rva + 0x58);
    if (!va) return;
    UINT64 at = va - pref;                         /* (the load config's pointers are still the preferred base's) */
    if (va - base < size) at = va - base;          /* (or already relocated) */
    if (at + 8 > size) return;
    UINT64 c = (rdtsc() * UINT64_C(0x9E3779B97F4A7C15)) & UINT64_C(0x0000FFFFFFFFFFFF);
    if (c == UINT64_C(0x00002B992DDFA232) || !c) c ^= 0x5A5A5A5A5A5Aull;
    *(UINT64 *)(img + at) = c;
}

/* A mapped image */
typedef struct {
    UINT64 base;
    UINT32 size, pages;
    int    slot, nslots;
    UINT32 entry;                                  /* AddressOfEntryPoint */
} Mapped;

/* Map driver file @path (@image its file name) into the driver window:
 * signature checked, laid out, relocated, imports bound (which may load an
 * export driver), each section mapped with its protection */
static NTSTATUS map_image(const char *path, const char *image, Mapped *m)
{
    FsLock();
    RamNode *f = RamfsResolve(NULL, path);
    if (f && !f->dir) RamfsPin(f);
    FsUnlock();
    if (!f || f->dir) {
        kprintf("[NT] %s: no driver file %s\n", image, path);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    NTSTATUS st;
    UINT8 *img = NULL;
    int nslots = 0, slot = -1;
    CiResult signer;
    st = CiValidateImage(image, (const UINT8 *)f->data, f->data ? f->size : 0, g_test_signing, &signer);
    if (!NT_SUCCESS(st)) goto out;
    st = STATUS_INVALID_IMAGE_FORMAT;
    const UINT8 *file = (const UINT8 *)f->data;
    UINT32 fsize = f->size;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)file;
    const IMAGE_NT_HEADERS64 *nt = NULL;
    if (file && fsize >= sizeof(*dos) && dos->e_magic == IMAGE_DOS_SIGNATURE &&
        (UINT64)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) <= fsize)
        nt = (const IMAGE_NT_HEADERS64 *)(file + dos->e_lfanew);
    const IMAGE_OPTIONAL_HEADER64 *opt = nt ? &nt->OptionalHeader : NULL;
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        opt->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || opt->Subsystem != IMAGE_SUBSYSTEM_NATIVE ||
        !opt->SizeOfImage || opt->SizeOfImage > DRV_IMAGE_MAX || opt->SizeOfHeaders > fsize ||
        opt->SizeOfHeaders > opt->SizeOfImage || opt->AddressOfEntryPoint >= opt->SizeOfImage) {
        kprintf("[NT] %s: not a 64-bit kernel-mode driver image\n", image);
        goto out;
    }

    /* Laid out as in memory, in a copy */
    UINT32 size = (UINT32)ALIGN_UP(opt->SizeOfImage, PAGE_SIZE), pages = size / PAGE_SIZE;
    img = kzalloc(size);
    if (!img) { st = STATUS_INSUFFICIENT_RESOURCES; goto out; }
    memcpy(img, file, opt->SizeOfHeaders);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    UINT32 nsec = nt->FileHeader.NumberOfSections;
    if ((const UINT8 *)(sec + nsec) > file + fsize) goto out;
    for (UINT32 i = 0; i < nsec; i++) {
        UINT32 raw = sec[i].SizeOfRawData < sec[i].VirtualSize || !sec[i].VirtualSize ? sec[i].SizeOfRawData : sec[i].VirtualSize;
        if ((UINT64)sec[i].VirtualAddress + raw > size || (raw && (UINT64)sec[i].PointerToRawData + raw > fsize)) goto out;
        if (raw) memcpy(img + sec[i].VirtualAddress, file + sec[i].PointerToRawData, raw);
    }

    /* Its place in the driver window */
    nslots = (int)((size + DRV_SLOT - 1) / DRV_SLOT);
    slot = slots_take(nslots);
    if (slot < 0) { st = STATUS_INSUFFICIENT_RESOURCES; goto out; }
    UINT64 base = DRV_BASE + (UINT64)slot * DRV_SLOT;
    const IMAGE_DATA_DIRECTORY *dir = opt->DataDirectory;
    if (base != opt->ImageBase && (nt->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED)) {
        kprintf("[NT] %s: has no relocations\n", image);
        goto out;
    }
    if (!relocate(img, size, dir[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress, dir[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size,
                  (INT64)(base - opt->ImageBase)))
        goto out;
    if (!bind_imports(image, img, size, dir[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress, dir[IMAGE_DIRECTORY_ENTRY_IMPORT].Size)) {
        st = ST_PROCEDURE_NOT_FOUND;
        goto out;
    }
    set_cookie(img, size, dir[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress, dir[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size,
               base, opt->ImageBase);

    /* Mapped page by page, each with its section's protection (headers
     * and gaps read-only) */
    for (UINT32 p = 0; p < pages; p++) {
        UINT32 rva = p * PAGE_SIZE, ch = IMAGE_SCN_MEM_READ;
        for (UINT32 i = 0; i < nsec; i++) {
            UINT32 vs = sec[i].VirtualSize ? sec[i].VirtualSize : sec[i].SizeOfRawData;
            if (rva + PAGE_SIZE > sec[i].VirtualAddress && rva < sec[i].VirtualAddress + vs) ch |= sec[i].Characteristics;
        }
        uintptr_t pa = pmm_alloc_page();
        if (!pa) { unmap_image(base, p); st = STATUS_INSUFFICIENT_RESOURCES; goto out; }
        memcpy((void *)(uintptr_t)(PHYSMAP_BASE + pa), img + rva, PAGE_SIZE);
        MapFlags mf = (ch & IMAGE_SCN_MEM_WRITE ? MAP_WRITABLE : 0) | (ch & IMAGE_SCN_MEM_EXECUTE ? 0 : MAP_NO_EXEC);
        if (!NT_SUCCESS(paging_map(base + rva, pa, PAGE_SIZE, mf))) {
            pmm_free_page(pa);
            unmap_image(base, p);
            st = STATUS_INSUFFICIENT_RESOURCES;
            goto out;
        }
    }
    m->base = base;
    m->size = size;
    m->pages = pages;
    m->slot = slot;
    m->nslots = nslots;
    m->entry = opt->AddressOfEntryPoint;
    slot = -1;
    st = STATUS_SUCCESS;
out:
    if (slot >= 0)
        for (int k = 0; k < nslots; k++) g_slot_used[slot + k] = false;
    kfree(img);
    FsLock();
    RamfsUnpin(f);
    FsUnlock();
    return st;
}

static void unmap(const Mapped *m)
{
    unmap_image(m->base, m->pages);
    for (int k = 0; k < m->nslots; k++) g_slot_used[m->slot + k] = false;
}

static NtDriver *new_driver(const char *service, const char *image, const char *path, const Mapped *m)
{
    NtDriver *drv = kzalloc(sizeof(*drv));
    char name[128];
    ksnprintf(name, sizeof(name), "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%s", service);
    if (!drv || !wide(name, &drv->reg_path) || !wide(service, &drv->ext.ServiceKeyName)) {
        if (drv) { kfree(drv->reg_path.Buffer); kfree(drv); }
        return NULL;
    }
    strncpy(drv->service, service, sizeof(drv->service) - 1);
    strncpy(drv->image, image, sizeof(drv->image) - 1);
    strncpy(drv->path, path, sizeof(drv->path) - 1);
    drv->base = m->base;
    drv->size = m->size;
    drv->slot = m->slot;
    drv->slots = m->nslots;
    return drv;
}

static void free_driver(NtDriver *drv)
{
    if (drv->obj) kfree(drv->obj->DriverName.Buffer);
    kfree(drv->obj);
    kfree(drv->reg_path.Buffer);
    kfree(drv->ext.ServiceKeyName.Buffer);
    kfree(drv);
}

/* An export driver an image imports from (@image: "WDFLDR.SYS"), loaded
 * from System32\drivers for it; NULL when it cannot be */
typedef NTSTATUS (NT_API *DllInitializeFn)(NT_UNICODE_STRING *registry_path);

static NtDriver *load_export_driver(const char *image)
{
    char path[260], service[64];
    ksnprintf(path, sizeof(path), "C:\\Windows\\System32\\drivers\\%s", image);
    ksnprintf(service, sizeof(service), "%s", image);
    char *dot = NULL;
    for (char *c = service; *c; c++) if (*c == '.') dot = c;
    if (dot) *dot = 0;
    Mapped m;
    NTSTATUS st = map_image(path, image, &m);
    if (!NT_SUCCESS(st)) return NULL;
    NtDriver *drv = new_driver(service, image, path, &m);
    if (!drv) { unmap(&m); return NULL; }
    drv->dll = true;
    DllInitializeFn init = (DllInitializeFn)(uintptr_t)image_export(m.base, m.size, "DllInitialize", 0);
    if (init) {
        if (!loading_push(drv)) { free_driver(drv); unmap(&m); return NULL; }
        st = init(&drv->reg_path);
        loading_pop();
    }
    kprintf("[NT] %s loaded at 0x%llx as an export driver: DllInitialize %s 0x%08x\n", image,
            (unsigned long long)m.base, init ? "returned" : "absent,", (UINT32)(init ? st : 0));
    if (!NT_SUCCESS(st)) {
        NtPoolDriverGone(drv);
        unmap(&m);
        free_driver(drv);
        return NULL;
    }
    drv->started = true;
    drv->next = g_drivers;
    g_drivers = drv;
    return drv;
}

/* The drivers built into the kernel, by service name */
static const struct { const char *service, *image; NT_DRIVER_INITIALIZE entry; } g_builtin[] = {
    { "HidUsb", "hidusb.sys", HidUsbDriverEntry },
    { "usbvideo", "usbvideo.sys", UsbVideoDriverEntry },
    { "mshidkmdf", "mshidkmdf.sys", MsHidKmdfDriverEntry },
    { "kbdhid", "kbdhid.sys", KbdHidDriverEntry },
    { "mouhid", "mouhid.sys", MouHidDriverEntry },
    { "hiddigi", "hiddigi.sys", DigiHidDriverEntry },
};

static NTSTATUS drv_load(const char *service)
{
    if (!g_ready) return STATUS_INSUFFICIENT_RESOURCES;
    if (find(service)) return ST_IMAGE_ALREADY_LOADED;
    if (g_nloading == LOAD_DEPTH) return STATUS_INSUFFICIENT_RESOURCES;

    char path[260];
    const char *image = path;
    Mapped m;
    NTSTATUS st;
    NT_DRIVER_INITIALIZE builtin = NULL;
    for (unsigned i = 0; i < sizeof(g_builtin) / sizeof(g_builtin[0]); i++)
        if (same_ci(g_builtin[i].service, service)) {
            builtin = g_builtin[i].entry;
            ksnprintf(path, sizeof(path), "C:\\Windows\\System32\\drivers\\%s", g_builtin[i].image);
            image = path + strlen("C:\\Windows\\System32\\drivers\\");
            memset(&m, 0, sizeof(m));
        }
    if (!builtin) {
        image_path(service, path, sizeof(path));
        for (const char *c = path; *c; c++) if (*c == '\\') image = c + 1;
        if (find_image(image)) return ST_IMAGE_ALREADY_LOADED;      /* (loaded as an export driver) */
        st = map_image(path, image, &m);
        if (!NT_SUCCESS(st)) return st;
    }

    /* The driver object, as the I/O manager gives it to DriverEntry */
    NtDriver *drv = new_driver(service, image, path, &m);
    NT_DRIVER_OBJECT *obj = drv ? kzalloc(sizeof(*obj)) : NULL;
    char name[96];
    ksnprintf(name, sizeof(name), "\\Driver\\%s", service);
    if (obj) drv->obj = obj;
    if (!obj || !wide(name, &obj->DriverName)) {
        if (drv) free_driver(drv);
        if (!builtin) unmap(&m);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    obj->Type = 4;
    obj->Size = (short)sizeof(*obj);
    obj->DriverStart = (void *)(uintptr_t)m.base;
    obj->DriverSize = m.size;
    obj->DriverSection = drv;
    obj->DriverExtension = &drv->ext;
    obj->DriverInit = builtin ? builtin : (NT_DRIVER_INITIALIZE)(uintptr_t)(m.base + m.entry);
    drv->ext.DriverObject = obj;
    NtIoAddDriver(drv);                            /* (fills MajorFunction with the default) */

    loading_push(drv);
    st = obj->DriverInit(obj, &drv->reg_path);
    loading_pop();
    if (builtin) kprintf("[NT] %s (built in, service %s): DriverEntry returned 0x%08x\n", image, service, (UINT32)st);
    else kprintf("[NT] %s loaded at 0x%llx (service %s): DriverEntry returned 0x%08x\n", image,
                 (unsigned long long)m.base, service, (UINT32)st);
    if (!NT_SUCCESS(st)) {
        NtIoDropDevices(drv);
        NtIoRemoveDriver(drv);
        NtPoolDriverGone(drv);
        if (!builtin) unmap(&m);
        free_driver(drv);
        return st;
    }
    for (NT_DEVICE_OBJECT *d = obj->DeviceObject; d; d = d->NextDevice) d->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    drv->started = true;
    drv->next = g_drivers;
    g_drivers = drv;
    return st;
}

/* NtLoadDriver's load: the service control manager logs its own failures */
NTSTATUS NtDrvLoadQuiet(const char *service)
{
    return drv_load(service);
}

/* Load @service's driver; one that does not load goes to the System log,
 * as Windows' service control manager reports it (event 7000) */

NTSTATUS NtDrvLoad(const char *service)
{
    NTSTATUS st = drv_load(service);
    if (NT_SUCCESS(st) || st == ST_IMAGE_ALREADY_LOADED || !g_ready) return st;
    char why[160];
    if ((UINT32)st == 0xC0000428u)                      /* STATUS_INVALID_IMAGE_HASH */
        ksnprintf(why, sizeof(why), "Windows cannot verify the digital signature for this file. (0x%08X)", (UINT32)st);
    else if ((UINT32)st == 0xC0000034u || (UINT32)st == 0xC000003Au || (UINT32)st == 0xC000000Fu)
        ksnprintf(why, sizeof(why), "The system cannot find the file specified. (0x%08X)", (UINT32)st);
    else
        ksnprintf(why, sizeof(why), "The driver did not start (status 0x%08X).", (UINT32)st);
    const char *s[2] = { service, why };
    kprintf("[EVT] Service Control Manager event 7000: %s: %s\n", service, why);
    EvLogReport("System", 1, "Service Control Manager", 0xC0001B58u /* 7000 */, 2, s);
    return st;
}

static void unload(NtDriver *drv)
{
    NtDriver **pp = &g_drivers;
    while (*pp && *pp != drv) pp = &(*pp)->next;
    if (*pp) *pp = drv->next;
    if (drv->obj->DriverUnload) drv->obj->DriverUnload(drv->obj);
    NtIoDropDevices(drv);
    NtIoRemoveDriver(drv);
    NtPoolDriverGone(drv);
    if (drv->base) unmap_image(drv->base, drv->size / PAGE_SIZE);
    for (int k = 0; k < drv->slots; k++) g_slot_used[drv->slot + k] = false;
    kprintf("[NT] %s unloaded (service %s)\n", drv->image, drv->service);
    free_driver(drv);
}

NTSTATUS NtDrvUnload(const char *service)
{
    NtDriver *drv = find(service);
    if (!drv || drv->unload_pending) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (drv->dll || !drv->obj->DriverUnload) return ST_DRIVER_UNLOAD_FAILED;
    if (drv->opens) {
        drv->unload_pending = true;
        kprintf("[NT] %s: unloads when its %d open file%s close\n", drv->image, drv->opens, drv->opens == 1 ? "" : "s");
        return STATUS_SUCCESS;
    }
    unload(drv);
    return STATUS_SUCCESS;
}

void NtDrvLastClose(NtDriver *drv)
{
    if (drv->unload_pending) unload(drv);
}

int NtDrvModules(UINT64 *base, UINT32 *size, char (*path)[260], int max)
{
    int n = 0;
    for (NtDriver *d = g_drivers; d && n < max; d = d->next) {
        if (!d->base) continue;                                  /* (built in: part of the kernel's image) */
        base[n] = d->base;
        size[n] = d->size;
        memcpy(path[n], d->path, sizeof(d->path));
        n++;
    }
    return n;
}

/* For backtraces (kernel/ke/ksym.c): the image's file name and the offset
 * in it, or NULL */
const char *NtDrvNameOf(UINT64 addr, UINT64 *offset)
{
    NtDriver *d = NtDrvByAddress(addr);
    if (!d) return NULL;
    *offset = addr - d->base;
    return d->image;
}

NtDriver *NtDrvByAddress(UINT64 addr)
{
    if (addr < DRV_BASE || addr >= DRV_BASE + DRV_SIZE) return NULL;
    for (NtDriver *d = g_drivers; d; d = d->next)
        if (addr - d->base < d->size) return d;
    /* (DriverEntry or DllInitialize is running: not on the list yet) */
    for (int i = 0; i < g_nloading; i++)
        if (addr - g_loading[i]->base < g_loading[i]->size) return g_loading[i];
    return NULL;
}
