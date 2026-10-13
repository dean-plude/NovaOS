/*
 * wddmdod.sys — a WDDM display-only driver for QEMU's standard VGA
 * (-vga std, PCI 1234:1111, the Bochs VBE "DISPI" interface), NovaOS's
 * test of step 1 of docs/design/intel-gpu.md
 *
 * Written as Microsoft's KMDOD sample (Basic Display Driver, MIT) is: its
 * DriverEntry hands DxgkInitializeDisplayOnlyDriver a
 * KMDDOD_INITIALIZATION_DATA, and everything else is a DxgkDdi* function
 * dxgkrnl calls (here kernel/nt/dxgkrnl.c).  DxgkInitializeDisplayOnlyDriver
 * is displib.lib's, a static library of the WDK; this file has its own,
 * doing what displib's does: open \Device\DxgKrnl, ask it for its
 * initialisation function with IOCTL_VIDEO_DDI_FUNC_REGISTER, call it.
 *
 *   StartDevice        checks the device is 1234:1111 (DxgkCbReadDeviceSpace),
 *                      takes the screen the firmware or the previous driver
 *                      left (DxgkCbAcquirePostDisplayOwnership), maps the
 *                      framebuffer (BAR 0) and the DISPI registers (BAR 2,
 *                      at 0x500) from its translated resources
 *   QueryChildRelations one output, always connected; its EDID is QEMU's
 *                      (BAR 2 at 0) when QEMU gives one
 *   EnumVidPnCofuncModality  the modes that fit the adapter's memory, both
 *                      as source modes (32 bpp surfaces) and target modes
 *   CommitVidPn        programs the pinned source mode into DISPI
 *   PresentDisplayOnly copies each dirty rectangle of the desktop's picture
 *                      to the framebuffer
 *   StopDeviceAndReleasePostDisplayOwnership  stops and says what it left on
 *                      the screen, for whoever draws it next
 *
 * Its DbgPrint lines are what the wddm self-test (tests/selftest/devices/
 * wddm) checks.  Built by tools/drivertest/mksys.py with its package
 * (wddmdod.inf, a test-signed catalog) into C:\Tests\Wddm.
 */
#include "../../kernel/nt/dxgk.h"

#define IMPORT __declspec(dllimport) NT_API
IMPORT unsigned int DbgPrint(const char *fmt, ...);
IMPORT NT_STATUS IoGetDeviceObjectPointer(NT_UNICODE_STRING *name, unsigned int access, NT_FILE_OBJECT **file,
                                          NT_DEVICE_OBJECT **dev);
IMPORT NT_IRP   *IoBuildDeviceIoControlRequest(unsigned int code, NT_DEVICE_OBJECT *dev, void *in, unsigned int in_len,
                                               void *out, unsigned int out_len, unsigned char internal, NT_KEVENT *ev,
                                               NT_IO_STATUS_BLOCK *iosb);
IMPORT NT_STATUS IofCallDriver(NT_DEVICE_OBJECT *dev, NT_IRP *irp);
IMPORT void      KeInitializeEvent(NT_KEVENT *ev, int type, unsigned char state);
IMPORT NT_STATUS KeWaitForSingleObject(void *obj, int reason, char mode, unsigned char alertable, long long *timeout);
IMPORT void      ObfDereferenceObject(void *obj);
IMPORT void     *ExAllocatePoolWithTag(int type, unsigned long long size, unsigned int tag);
IMPORT void      ExFreePoolWithTag(void *p, unsigned int tag);

#define NonPagedPoolNx      512
#define TAG_DOD             0x646F4457u           /* 'WDod' */
#define FILE_ALL_ACCESS     0x001F01FFu
#define MmNonCached         0
#define MmWriteCombined     2

/* The Bochs VBE ("DISPI") registers, 16 bits each at BAR 2 + 0x500 + 2 * index
 * (QEMU's hw/display/vga-pci.c) */
#define DISPI_ID            0
#define DISPI_XRES          1
#define DISPI_YRES          2
#define DISPI_BPP           3
#define DISPI_ENABLE        4
#define DISPI_BANK          5
#define DISPI_VIRT_WIDTH    6
#define DISPI_VIRT_HEIGHT   7
#define DISPI_X_OFFSET      8
#define DISPI_Y_OFFSET      9
#define DISPI_VIDEO_MEMORY_64K 10
#define DISPI_ENABLED       0x01
#define DISPI_LFB_ENABLED   0x40
#define DISPI_MAX_X         2560
#define DISPI_MAX_Y         1600

typedef struct { unsigned int w, h; } Mode;

/* The modes offered, in this order (the ones the adapter's memory holds),
 * and the one the screen was in at start if it is none of these */
static const Mode g_common[] = {
    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1280, 800 }, { 1280, 1024 },
    { 1440, 900 }, { 1600, 900 }, { 1600, 1200 }, { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 },
};
#define MAX_MODES (sizeof(g_common) / sizeof(g_common[0]) + 1)

typedef struct {
    NT_DEVICE_OBJECT   *pdo;
    DXGKRNL_INTERFACE   cb;
    volatile unsigned char  *regs;                /* BAR 2 */
    volatile unsigned char  *fb;                  /* BAR 0 */
    DXGK_PHYSICAL_ADDRESS fb_phys, regs_phys;
    unsigned int        fb_len, regs_len;
    unsigned int        vram;                     /* bytes */
    DXGK_DISPLAY_INFORMATION post;                /* what was on the screen at start */
    Mode                modes[MAX_MODES];
    unsigned int        nmodes;
    Mode                cur;                      /* committed (0 x 0: none yet) */
    unsigned char       visible, started;
} Dod;

static void mzero(void *p, unsigned long long n) { for (unsigned char *b = p; n--; ) *b++ = 0; }

static unsigned short dispi_read(Dod *d, int i) { return *(volatile unsigned short *)(d->regs + 0x500 + 2 * i); }
static void dispi_write(Dod *d, int i, unsigned short v) { *(volatile unsigned short *)(d->regs + 0x500 + 2 * i) = v; }

/* -----------------------------------------------------------------------
 * Plug and Play
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS dod_add_device(NT_DEVICE_OBJECT *pdo, void **ctx)
{
    if (!pdo || !ctx) return NT_STATUS_INVALID_PARAMETER;
    Dod *d = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*d), TAG_DOD);
    if (!d) return NT_STATUS_NO_MEMORY;
    mzero(d, sizeof(*d));
    d->pdo = pdo;
    *ctx = d;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_remove_device(void *ctx)
{
    if (ctx) ExFreePoolWithTag(ctx, TAG_DOD);
    return NT_STATUS_SUCCESS;
}

static void add_mode(Dod *d, unsigned int w, unsigned int h)
{
    if (w > DISPI_MAX_X || h > DISPI_MAX_Y || (unsigned long long)w * h * 4 > d->vram) return;
    for (unsigned int i = 0; i < d->nmodes; i++) if (d->modes[i].w == w && d->modes[i].h == h) return;
    if (d->nmodes < MAX_MODES) d->modes[d->nmodes++] = (Mode){ w, h };
}

static void unmap(Dod *d)
{
    if (d->fb) d->cb.DxgkCbUnmapMemory(d->cb.DeviceHandle, (void *)d->fb);
    if (d->regs) d->cb.DxgkCbUnmapMemory(d->cb.DeviceHandle, (void *)d->regs);
    d->fb = d->regs = 0;
}

static NT_API NT_STATUS dod_start_device(void *ctx, DXGK_START_INFO *si, DXGKRNL_INTERFACE *cb, unsigned int *sources,
                                         unsigned int *children)
{
    Dod *d = ctx;
    (void)si;
    if (!d || !cb || !sources || !children) return NT_STATUS_INVALID_PARAMETER;
    d->cb = *cb;
    DXGK_DEVICE_INFO info;
    NT_STATUS st = cb->DxgkCbGetDeviceInformation(cb->DeviceHandle, &info);
    if (!NT_SUCCESS_(st)) return st;

    unsigned short id[2] = { 0, 0 };
    unsigned int got = 0;
    st = cb->DxgkCbReadDeviceSpace(cb->DeviceHandle, DXGK_WHICHSPACE_CONFIG, id, 0, sizeof(id), &got);
    if (!NT_SUCCESS_(st) || id[0] != 0x1234 || id[1] != 0x1111) {
        DbgPrint("wddmdod: not QEMU's standard VGA (%04x:%04x)\n", id[0], id[1]);
        return NT_STATUS_UNSUCCESSFUL;
    }

    /* BAR 0 is the framebuffer, BAR 2 the 4 KiB of registers (resources come in BAR order) */
    const NT_CM_RESOURCE_LIST *r = info.TranslatedResourceList;
    unsigned int nmem = 0;
    for (unsigned int i = 0; r && r->Count && i < r->List[0].PartialResourceList.Count; i++) {
        const NT_CM_PARTIAL_RESOURCE_DESCRIPTOR *p = &r->List[0].PartialResourceList.PartialDescriptors[i];
        if (p->Type != NT_CmResourceTypeMemory) continue;
        if (nmem == 0) { d->fb_phys = (DXGK_PHYSICAL_ADDRESS)p->u.Memory.Start; d->fb_len = p->u.Memory.Length; }
        else if (nmem == 1) { d->regs_phys = (DXGK_PHYSICAL_ADDRESS)p->u.Memory.Start; d->regs_len = p->u.Memory.Length; }
        nmem++;
    }
    if (nmem < 2 || d->regs_len < 0x600) {
        DbgPrint("wddmdod: the adapter's memory and registers are not in its resources\n");
        return NT_STATUS_UNSUCCESSFUL;
    }
    void *va = 0;
    st = cb->DxgkCbMapMemory(cb->DeviceHandle, d->regs_phys, d->regs_len, 0, 0, MmNonCached, &va);
    if (!NT_SUCCESS_(st)) return st;
    d->regs = va;
    unsigned short ver = dispi_read(d, DISPI_ID);
    if (ver < 0xB0C0 || ver > 0xB0CF) {
        DbgPrint("wddmdod: no DISPI interface (id %04x)\n", ver);
        unmap(d);
        return NT_STATUS_UNSUCCESSFUL;
    }
    d->vram = (unsigned int)dispi_read(d, DISPI_VIDEO_MEMORY_64K) << 16;
    if (!d->vram || d->vram > d->fb_len) d->vram = d->fb_len;
    st = cb->DxgkCbMapMemory(cb->DeviceHandle, d->fb_phys, d->vram, 0, 0, MmWriteCombined, &va);
    if (!NT_SUCCESS_(st)) { unmap(d); return st; }
    d->fb = va;

    /* The screen as it is: kept on until the first commit */
    mzero(&d->post, sizeof(d->post));
    if (!NT_SUCCESS_(cb->DxgkCbAcquirePostDisplayOwnership(cb->DeviceHandle, &d->post)) || !d->post.Width) {
        d->post.Width = dispi_read(d, DISPI_XRES);
        d->post.Height = dispi_read(d, DISPI_YRES);
        d->post.Pitch = d->post.Width * 4;
    }
    d->cur = (Mode){ d->post.Width, d->post.Height };
    d->nmodes = 0;
    for (unsigned int i = 0; i < sizeof(g_common) / sizeof(g_common[0]); i++) add_mode(d, g_common[i].w, g_common[i].h);
    add_mode(d, d->post.Width, d->post.Height);
    d->started = 1;
    d->visible = 1;
    *sources = 1;
    *children = 1;
    DbgPrint("wddmdod: started on 1234:1111 (DISPI %04x, %u MiB), the screen at %ux%u, %u modes\n", ver,
             d->vram >> 20, d->post.Width, d->post.Height, d->nmodes);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_stop_device(void *ctx)
{
    Dod *d = ctx;
    if (!d) return NT_STATUS_INVALID_PARAMETER;
    unmap(d);
    d->started = 0;
    DbgPrint("wddmdod: stopped\n");
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_stop_release(void *ctx, D3DDDI_VIDEO_PRESENT_TARGET_ID target, DXGK_DISPLAY_INFORMATION *info)
{
    Dod *d = ctx;
    if (!d || !info || target != 0) return NT_STATUS_INVALID_PARAMETER;
    mzero(info, sizeof(*info));
    info->Width = d->cur.w;
    info->Height = d->cur.h;
    info->Pitch = d->cur.w * 4;
    info->ColorFormat = D3DDDIFMT_X8R8G8B8;
    info->PhysicAddress = d->fb_phys;
    info->TargetId = 0;
    unmap(d);
    d->started = 0;
    DbgPrint("wddmdod: display released at %ux%u\n", d->cur.w, d->cur.h);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_dispatch_io(void *ctx, unsigned int source, void *vrp)
{
    (void)ctx; (void)source; (void)vrp;
    return NT_STATUS_NOT_SUPPORTED;
}

static NT_API unsigned char dod_interrupt(void *ctx, unsigned int msg) { (void)ctx; (void)msg; return 0; }
static NT_API void dod_dpc(void *ctx) { (void)ctx; }
static NT_API void dod_reset(void *ctx) { (void)ctx; }
static NT_API void dod_unload(void) { DbgPrint("wddmdod: unloaded\n"); }

static NT_API NT_STATUS dod_set_power(void *ctx, unsigned int uid, int dstate, int action)
{
    (void)ctx; (void)uid; (void)dstate; (void)action;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_query_interface(void *ctx, void *qi) { (void)ctx; (void)qi; return NT_STATUS_NOT_SUPPORTED; }

/* -----------------------------------------------------------------------
 * The output
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS dod_child_relations(void *ctx, DXGK_CHILD_DESCRIPTOR *rel, unsigned int size)
{
    (void)ctx;
    if (!rel || size < 2 * sizeof(*rel)) return NT_STATUS_BUFFER_TOO_SMALL;
    mzero(rel, size);
    rel[0].ChildDeviceType = TypeVideoOutput;
    rel[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = D3DKMDT_VOT_OTHER;
    rel[0].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    rel[0].AcpiUid = 0;
    rel[0].ChildUid = 0;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_child_status(void *ctx, DXGK_CHILD_STATUS *st, unsigned char nondestructive)
{
    (void)ctx; (void)nondestructive;
    if (!st || st->ChildUid != 0) return NT_STATUS_INVALID_PARAMETER;
    if (st->Type == StatusConnection) st->HotPlug.Connected = 1;
    else if (st->Type == StatusRotation) st->Rotation.Angle = 0;
    return NT_STATUS_SUCCESS;
}

/* QEMU puts the EDID it makes up for the display (edid=on, the default) at BAR 2's start */
static NT_API NT_STATUS dod_device_descriptor(void *ctx, unsigned int uid, DXGK_DEVICE_DESCRIPTOR *dd)
{
    Dod *d = ctx;
    static const unsigned char hdr[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    if (!d || !dd || uid != 0) return NT_STATUS_INVALID_PARAMETER;
    if (!d->regs) return STATUS_MONITOR_NO_DESCRIPTOR;
    for (int i = 0; i < 8; i++) if (d->regs[i] != hdr[i]) return STATUS_MONITOR_NO_DESCRIPTOR;
    if (dd->DescriptorOffset >= 256) return STATUS_MONITOR_NO_DESCRIPTOR;
    unsigned int n = dd->DescriptorLength;
    if (dd->DescriptorOffset + n > 256) n = 256 - dd->DescriptorOffset;
    unsigned char *out = dd->DescriptorBuffer;
    for (unsigned int i = 0; i < n; i++) out[i] = d->regs[dd->DescriptorOffset + i];
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_query_adapter_info(void *adapter, const DXGKARG_QUERYADAPTERINFO *q)
{
    (void)adapter;
    if (!q) return NT_STATUS_INVALID_PARAMETER;
    if (q->Type != DXGKQAITYPE_DRIVERCAPS) return NT_STATUS_NOT_SUPPORTED;
    if (!q->pOutputData || q->OutputDataSize < sizeof(DXGK_DRIVERCAPS)) return NT_STATUS_BUFFER_TOO_SMALL;
    DXGK_DRIVERCAPS *caps = q->pOutputData;
    mzero(caps, sizeof(*caps));
    caps->HighestAcceptableAddress = ~0ull;
    caps->WDDMVersion = DXGKDDI_WDDMv1_2;
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * The VidPN
 * ----------------------------------------------------------------------- */
static int find_mode(Dod *d, unsigned int w, unsigned int h)
{
    for (unsigned int i = 0; i < d->nmodes; i++) if (d->modes[i].w == w && d->modes[i].h == h) return (int)i;
    return -1;
}

static NT_STATUS add_source_modes(Dod *d, const DXGK_VIDPN_INTERFACE *vi, D3DKMDT_HVIDPN v,
                                  D3DDDI_VIDEO_PRESENT_SOURCE_ID src, int only)
{
    D3DKMDT_HVIDPNSOURCEMODESET set;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *si;
    NT_STATUS st = vi->pfnCreateNewSourceModeSet(v, src, &set, &si);
    if (!NT_SUCCESS_(st)) return st;
    for (unsigned int i = 0; i < d->nmodes; i++) {
        if (only >= 0 && (int)i != only) continue;
        D3DKMDT_VIDPN_SOURCE_MODE *m;
        if (!NT_SUCCESS_(st = si->pfnCreateNewModeInfo(set, &m))) break;
        m->Id = i;
        m->Type = D3DKMDT_RMT_GRAPHICS;
        m->Format.Graphics.PrimSurfSize.cx = m->Format.Graphics.VisibleRegionSize.cx = d->modes[i].w;
        m->Format.Graphics.PrimSurfSize.cy = m->Format.Graphics.VisibleRegionSize.cy = d->modes[i].h;
        m->Format.Graphics.Stride = (unsigned long long)d->modes[i].w * 4;
        m->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        m->Format.Graphics.ColorBasis = D3DKMDT_CB_SRGB;
        m->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
        if (!NT_SUCCESS_(st = si->pfnAddMode(set, m))) { si->pfnReleaseModeInfo(set, m); break; }
    }
    if (NT_SUCCESS_(st)) st = vi->pfnAssignSourceModeSet(v, src, set);
    if (!NT_SUCCESS_(st)) vi->pfnReleaseSourceModeSet(v, set);
    return st;
}

static NT_STATUS add_target_modes(Dod *d, const DXGK_VIDPN_INTERFACE *vi, D3DKMDT_HVIDPN v,
                                  D3DDDI_VIDEO_PRESENT_TARGET_ID tgt, int only)
{
    D3DKMDT_HVIDPNTARGETMODESET set;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *ti;
    NT_STATUS st = vi->pfnCreateNewTargetModeSet(v, tgt, &set, &ti);
    if (!NT_SUCCESS_(st)) return st;
    for (unsigned int i = 0; i < d->nmodes; i++) {
        if (only >= 0 && (int)i != only) continue;
        D3DKMDT_VIDPN_TARGET_MODE *m;
        if (!NT_SUCCESS_(st = ti->pfnCreateNewModeInfo(set, &m))) break;
        m->Id = i;
        m->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
        m->VideoSignalInfo.TotalSize.cx = m->VideoSignalInfo.ActiveSize.cx = d->modes[i].w;
        m->VideoSignalInfo.TotalSize.cy = m->VideoSignalInfo.ActiveSize.cy = d->modes[i].h;
        m->VideoSignalInfo.VSyncFreq = (D3DDDI_RATIONAL){ 60000, 1000 };
        m->VideoSignalInfo.HSyncFreq = (D3DDDI_RATIONAL){ 60000 * d->modes[i].h, 1000 };
        m->VideoSignalInfo.PixelRate = (unsigned long long)d->modes[i].w * d->modes[i].h * 60;
        m->VideoSignalInfo.ScanLineOrdering = 1;  /* progressive */
        m->Preference = d->modes[i].w == d->post.Width && d->modes[i].h == d->post.Height ? D3DKMDT_MP_PREFERRED
                                                                                      : D3DKMDT_MP_NOTPREFERRED;
        if (!NT_SUCCESS_(st = ti->pfnAddMode(set, m))) { ti->pfnReleaseModeInfo(set, m); break; }
    }
    if (NT_SUCCESS_(st)) st = vi->pfnAssignTargetModeSet(v, tgt, set);
    if (!NT_SUCCESS_(st)) vi->pfnReleaseTargetModeSet(v, set);
    return st;
}

/* The source mode pinned on @src (-1 none, -2 one this driver does not have) */
static int pinned_source(Dod *d, const DXGK_VIDPN_INTERFACE *vi, D3DKMDT_HVIDPN v, D3DDDI_VIDEO_PRESENT_SOURCE_ID src)
{
    D3DKMDT_HVIDPNSOURCEMODESET set;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *si;
    if (!NT_SUCCESS_(vi->pfnAcquireSourceModeSet(v, src, &set, &si))) return -1;
    const D3DKMDT_VIDPN_SOURCE_MODE *m = 0;
    int r = -1;
    if (NT_SUCCESS_(si->pfnAcquirePinnedModeInfo(set, &m)) && m) {
        r = m->Type == D3DKMDT_RMT_GRAPHICS ? find_mode(d, m->Format.Graphics.PrimSurfSize.cx,
                                                        m->Format.Graphics.PrimSurfSize.cy) : -2;
        if (r < 0) r = -2;
        si->pfnReleaseModeInfo(set, m);
    }
    vi->pfnReleaseSourceModeSet(v, set);
    return r;
}

static int pinned_target(Dod *d, const DXGK_VIDPN_INTERFACE *vi, D3DKMDT_HVIDPN v, D3DDDI_VIDEO_PRESENT_TARGET_ID tgt)
{
    D3DKMDT_HVIDPNTARGETMODESET set;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *ti;
    if (!NT_SUCCESS_(vi->pfnAcquireTargetModeSet(v, tgt, &set, &ti))) return -1;
    const D3DKMDT_VIDPN_TARGET_MODE *m = 0;
    int r = -1;
    if (NT_SUCCESS_(ti->pfnAcquirePinnedModeInfo(set, &m)) && m) {
        r = find_mode(d, m->VideoSignalInfo.ActiveSize.cx, m->VideoSignalInfo.ActiveSize.cy);
        if (r < 0) r = -2;
        ti->pfnReleaseModeInfo(set, m);
    }
    vi->pfnReleaseTargetModeSet(v, set);
    return r;
}

static NT_API NT_STATUS dod_enum_cofunc(void *adapter, const DXGKARG_ENUMVIDPNCOFUNCMODALITY *e)
{
    Dod *d = adapter;
    if (!d || !e) return NT_STATUS_INVALID_PARAMETER;
    const DXGK_VIDPN_INTERFACE *vi;
    NT_STATUS st = d->cb.DxgkCbQueryVidPnInterface(e->hConstrainingVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &vi);
    if (!NT_SUCCESS_(st)) return st;
    D3DKMDT_HVIDPNTOPOLOGY topo;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *ti;
    if (!NT_SUCCESS_(st = vi->pfnGetTopology(e->hConstrainingVidPn, &topo, &ti))) return st;
    const D3DKMDT_VIDPN_PRESENT_PATH *p = 0;
    st = ti->pfnAcquireFirstPathInfo(topo, &p);
    while (NT_SUCCESS_(st) && p) {
        D3DDDI_VIDEO_PRESENT_SOURCE_ID src = p->VidPnSourceId;
        D3DDDI_VIDEO_PRESENT_TARGET_ID tgt = p->VidPnTargetId;
        if (src != 0 || tgt != 0) { ti->pfnReleasePathInfo(topo, p); return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY; }
        int ps = pinned_source(d, vi, e->hConstrainingVidPn, src);
        int pt = pinned_target(d, vi, e->hConstrainingVidPn, tgt);
        /* (the source and target are the same size here: identity scaling only) */
        if (ps == -1 && !(e->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE && e->EnumPivot.VidPnSourceId == src))
            st = add_source_modes(d, vi, e->hConstrainingVidPn, src, pt >= 0 ? pt : -1);
        if (NT_SUCCESS_(st) && pt == -1 && !(e->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET && e->EnumPivot.VidPnTargetId == tgt))
            st = add_target_modes(d, vi, e->hConstrainingVidPn, tgt, ps >= 0 ? ps : -1);
        if (NT_SUCCESS_(st) && p->ContentTransformation.Scaling == D3DKMDT_VPPS_UNINITIALIZED) {
            D3DKMDT_VIDPN_PRESENT_PATH u = *p;
            u.ContentTransformation.ScalingSupport = 1;   /* identity */
            u.ContentTransformation.RotationSupport = 1;
            st = ti->pfnUpdatePathSupportInfo(topo, &u);
        }
        const D3DKMDT_VIDPN_PRESENT_PATH *next = 0;
        NT_STATUS sn = ti->pfnAcquireNextPathInfo(topo, p, &next);
        ti->pfnReleasePathInfo(topo, p);
        if (!NT_SUCCESS_(st)) return st;
        p = next;
        st = sn;
    }
    return st == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET || st == STATUS_GRAPHICS_DATASET_IS_EMPTY
               ? NT_STATUS_SUCCESS : st;
}

static NT_API NT_STATUS dod_is_supported(void *adapter, DXGKARG_ISSUPPORTEDVIDPN *is)
{
    Dod *d = adapter;
    if (!d || !is) return NT_STATUS_INVALID_PARAMETER;
    is->IsVidPnSupported = 1;
    if (!is->hDesiredVidPn) return NT_STATUS_SUCCESS;     /* (none: the screen off, always possible) */
    const DXGK_VIDPN_INTERFACE *vi;
    NT_STATUS st = d->cb.DxgkCbQueryVidPnInterface(is->hDesiredVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &vi);
    if (!NT_SUCCESS_(st)) return st;
    D3DKMDT_HVIDPNTOPOLOGY topo;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *ti;
    if (!NT_SUCCESS_(st = vi->pfnGetTopology(is->hDesiredVidPn, &topo, &ti))) return st;
    unsigned long long n = 0;
    ti->pfnGetNumPaths(topo, &n);
    if (n > 1) { is->IsVidPnSupported = 0; return NT_STATUS_SUCCESS; }
    const D3DKMDT_VIDPN_PRESENT_PATH *p = 0;
    if (!n || !NT_SUCCESS_(ti->pfnAcquireFirstPathInfo(topo, &p)) || !p) return NT_STATUS_SUCCESS;
    if (p->VidPnSourceId != 0 || p->VidPnTargetId != 0) is->IsVidPnSupported = 0;
    else {
        int ps = pinned_source(d, vi, is->hDesiredVidPn, 0), pt = pinned_target(d, vi, is->hDesiredVidPn, 0);
        if (ps == -2 || pt == -2 || (ps >= 0 && pt >= 0 && ps != pt)) is->IsVidPnSupported = 0;
    }
    ti->pfnReleasePathInfo(topo, p);
    return NT_STATUS_SUCCESS;
}

static void program(Dod *d, Mode m)
{
    dispi_write(d, DISPI_ENABLE, 0);
    dispi_write(d, DISPI_BANK, 0);
    dispi_write(d, DISPI_BPP, 32);
    dispi_write(d, DISPI_XRES, (unsigned short)m.w);
    dispi_write(d, DISPI_YRES, (unsigned short)m.h);
    dispi_write(d, DISPI_VIRT_WIDTH, (unsigned short)m.w);
    dispi_write(d, DISPI_X_OFFSET, 0);
    dispi_write(d, DISPI_Y_OFFSET, 0);
    dispi_write(d, DISPI_ENABLE, DISPI_ENABLED | DISPI_LFB_ENABLED);
}

static NT_API NT_STATUS dod_commit(void *adapter, const DXGKARG_COMMITVIDPN *c)
{
    Dod *d = adapter;
    if (!d || !c) return NT_STATUS_INVALID_PARAMETER;
    if (!d->started) return NT_STATUS_UNSUCCESSFUL;
    if (!c->hFunctionalVidPn) return NT_STATUS_SUCCESS;
    const DXGK_VIDPN_INTERFACE *vi;
    NT_STATUS st = d->cb.DxgkCbQueryVidPnInterface(c->hFunctionalVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1, &vi);
    if (!NT_SUCCESS_(st)) return st;
    if (c->AffectedVidPnSourceId != 0) return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    int ps = pinned_source(d, vi, c->hFunctionalVidPn, 0);
    if (ps == -2) return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    if (ps < 0) {                                         /* (no mode: the source is off) */
        d->visible = 0;
        DbgPrint("wddmdod: source 0 off\n");
        return NT_STATUS_SUCCESS;
    }
    Mode m = d->modes[ps];
    program(d, m);
    if (dispi_read(d, DISPI_XRES) != m.w || dispi_read(d, DISPI_YRES) != m.h) {
        DbgPrint("wddmdod: the adapter refused %ux%u\n", m.w, m.h);
        program(d, d->cur);
        return NT_STATUS_UNSUCCESSFUL;
    }
    d->cur = m;
    DbgPrint("wddmdod: mode %ux%u\n", m.w, m.h);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_visibility(void *adapter, const DXGKARG_SETVIDPNSOURCEVISIBILITY *v)
{
    Dod *d = adapter;
    if (!d || !v || v->VidPnSourceId != 0) return NT_STATUS_INVALID_PARAMETER;
    d->visible = v->Visible;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_update_path(void *adapter, const DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *u)
{
    (void)adapter;
    if (!u) return NT_STATUS_INVALID_PARAMETER;
    return NT_STATUS_SUCCESS;                             /* (identity only) */
}

static NT_API NT_STATUS dod_recommend_monitor_modes(void *adapter, const DXGKARG_RECOMMENDMONITORMODES *r)
{
    (void)adapter; (void)r;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dod_vidpn_hw_cap(void *adapter, DXGKARG_QUERYVIDPNHWCAPABILITY *q)
{
    (void)adapter;
    if (!q) return NT_STATUS_INVALID_PARAMETER;
    q->VidPnHWCaps = 0;                                   /* (no rotation, scaling or clone hardware) */
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Presenting
 * ----------------------------------------------------------------------- */
static void blit(Dod *d, const unsigned char *src, int pitch, DXGK_RECT r)
{
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > (int)d->cur.w) r.right = (int)d->cur.w;
    if (r.bottom > (int)d->cur.h) r.bottom = (int)d->cur.h;
    if (r.left >= r.right || r.top >= r.bottom) return;
    unsigned int dpitch = d->cur.w * 4;
    for (int y = r.top; y < r.bottom; y++) {
        const unsigned int *s = (const unsigned int *)(src + (long long)y * pitch) + r.left;
        volatile unsigned int *o = (volatile unsigned int *)(d->fb + (unsigned long long)y * dpitch) + r.left;
        for (int x = r.left; x < r.right; x++) *o++ = *s++;
    }
}

static NT_API NT_STATUS dod_present(void *adapter, const DXGKARG_PRESENT_DISPLAYONLY *p)
{
    Dod *d = adapter;
    if (!d || !p || p->VidPnSourceId != 0 || p->BytesPerPixel != 4) return NT_STATUS_INVALID_PARAMETER;
    if (!d->started || !d->visible || !d->fb) return NT_STATUS_SUCCESS;
    for (unsigned int i = 0; i < p->NumMoves; i++) blit(d, p->pSource, p->Pitch, p->pMoves[i].DestRect);
    for (unsigned int i = 0; i < p->NumDirtyRects; i++) blit(d, p->pSource, p->Pitch, p->pDirtyRect[i]);
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * DriverEntry, and displib.lib's DxgkInitializeDisplayOnlyDriver
 * ----------------------------------------------------------------------- */
static NT_STATUS initialize_display_only(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg, KMDDOD_INITIALIZATION_DATA *init)
{
    static NT_WCHAR name[] = { '\\', 'D', 'e', 'v', 'i', 'c', 'e', '\\', 'D', 'x', 'g', 'K', 'r', 'n', 'l' };
    NT_UNICODE_STRING u = { sizeof(name), sizeof(name), name };
    NT_FILE_OBJECT *file;
    NT_DEVICE_OBJECT *dev;
    NT_STATUS st = IoGetDeviceObjectPointer(&u, FILE_ALL_ACCESS, &file, &dev);
    if (!NT_SUCCESS_(st)) return st;
    DXGKRNL_DPIINITIALIZE dpi = 0;
    NT_KEVENT ev;
    NT_IO_STATUS_BLOCK iosb;
    KeInitializeEvent(&ev, 0 /* NotificationEvent */, 0);
    NT_IRP *irp = IoBuildDeviceIoControlRequest(IOCTL_VIDEO_DDI_FUNC_REGISTER, dev, 0, 0, &dpi, sizeof(dpi), 1, &ev, &iosb);
    if (!irp) { ObfDereferenceObject(file); return NT_STATUS_INSUFFICIENT_RESOURCES; }
    st = IofCallDriver(dev, irp);
    if (st == NT_STATUS_PENDING) { KeWaitForSingleObject(&ev, 0, 0, 0, 0); st = iosb.Status; }
    ObfDereferenceObject(file);
    if (!NT_SUCCESS_(st) || !dpi) return NT_SUCCESS_(st) ? NT_STATUS_UNSUCCESSFUL : st;
    return dpi(drv, reg, init);
}

NT_API NT_STATUS DriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg)
{
    KMDDOD_INITIALIZATION_DATA init;
    mzero(&init, sizeof(init));
    init.Version = DXGKDDI_INTERFACE_VERSION_WIN8;
    init.DxgkDdiAddDevice = dod_add_device;
    init.DxgkDdiStartDevice = dod_start_device;
    init.DxgkDdiStopDevice = dod_stop_device;
    init.DxgkDdiRemoveDevice = dod_remove_device;
    init.DxgkDdiDispatchIoRequest = dod_dispatch_io;
    init.DxgkDdiInterruptRoutine = dod_interrupt;
    init.DxgkDdiDpcRoutine = dod_dpc;
    init.DxgkDdiQueryChildRelations = dod_child_relations;
    init.DxgkDdiQueryChildStatus = dod_child_status;
    init.DxgkDdiQueryDeviceDescriptor = dod_device_descriptor;
    init.DxgkDdiSetPowerState = dod_set_power;
    init.DxgkDdiResetDevice = dod_reset;
    init.DxgkDdiUnload = dod_unload;
    init.DxgkDdiQueryInterface = dod_query_interface;
    init.DxgkDdiQueryAdapterInfo = dod_query_adapter_info;
    init.DxgkDdiIsSupportedVidPn = dod_is_supported;
    init.DxgkDdiEnumVidPnCofuncModality = dod_enum_cofunc;
    init.DxgkDdiSetVidPnSourceVisibility = dod_visibility;
    init.DxgkDdiCommitVidPn = dod_commit;
    init.DxgkDdiUpdateActiveVidPnPresentPath = dod_update_path;
    init.DxgkDdiRecommendMonitorModes = dod_recommend_monitor_modes;
    init.DxgkDdiQueryVidPnHWCapability = dod_vidpn_hw_cap;
    init.DxgkDdiPresentDisplayOnly = dod_present;
    init.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = dod_stop_release;
    NT_STATUS st = initialize_display_only(drv, reg, &init);
    DbgPrint("wddmdod: DxgkInitializeDisplayOnlyDriver returned 0x%08x\n", (unsigned int)st);
    return st;
}
