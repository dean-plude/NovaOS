/*
 * dxgkrnl.c — NovaOS's stand-in for dxgkrnl.sys, the DirectX graphics
 * kernel Windows display drivers are miniports of
 *
 * A WDDM display driver (Intel's, AMD's, NVIDIA's, Microsoft's KMDOD
 * sample) knows its hardware and nothing else; dxgkrnl does the rest.  Its
 * DriverEntry links displib.lib's DxgkInitialize or
 * DxgkInitializeDisplayOnlyDriver, which open \Device\DxgKrnl
 * (IoGetDeviceObjectPointer), ask it with IOCTL_VIDEO_DDI_FUNC_REGISTER for
 * dxgkrnl's initialisation function and call it with the driver's DDI
 * table.  From there, as Windows documents it (dispmprt.h, d3dkmddi.h):
 *
 *   DpiInitialize       the driver object's AddDevice, IRP dispatch and
 *                       unload become dxgkrnl's
 *   AddDevice           an FDO on the PCI function's PDO (pnp.c), and the
 *                       driver's DxgkDdiAddDevice for its context
 *   START_DEVICE        passed down first; then DxgkDdiStartDevice with the
 *                       DXGKRNL_INTERFACE callbacks, the driver's caps
 *                       (DxgkDdiQueryAdapterInfo), its children
 *                       (DxgkDdiQueryChildRelations/Status: the outputs)
 *                       and its modes: a VidPN with one path, source 0 to
 *                       the first output, that DxgkDdiEnumVidPnCofuncModality
 *                       fills with the source and target mode sets
 *   the screen          offered to the display layer (hal/display.c's
 *                       DisplayWddmOffer), which takes it over between two
 *                       frames: each mode change is a VidPN with that mode
 *                       pinned, checked (DxgkDdiIsSupportedVidPn) and
 *                       committed (DxgkDdiCommitVidPn), and each drawn
 *                       rectangle of the desktop's picture goes to
 *                       DxgkDdiPresentDisplayOnly
 *   REMOVE_DEVICE       DxgkDdiStopDeviceAndReleasePostDisplayOwnership:
 *                       the driver stops and says what it left on the
 *                       screen, and the display layer's built-in driver
 *                       takes it back from there; DxgkDdiRemoveDevice
 *
 * The VidPN manager's objects (a VidPN, its topology and its mode sets)
 * are this file's own, behind the interfaces DxgkCbQueryVidPnInterface
 * returns.  This is step 1 of docs/design/intel-gpu.md: the display-only
 * path, enough for a KMDOD-like driver (tools/drivertest/wddmdod.c drives
 * QEMU's Bochs VBE adapter with it).  Not yet: the render DDIs of a full
 * driver (DxgkInitialize's DRIVER_INITIALIZATION_DATA, step 2 on the
 * T14), interrupts and vertical blanks (a display-only driver on QEMU has
 * none), power states, more than one source, and monitor interfaces.
 */
#include "ntapi.h"
#include "dxgk.h"
#include "../hal/display.h"
#include "../ke/printf.h"
#include "../lib/string.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../um/um.h"

#define MAX_MODES   DISPLAY_MAX_MODES
#define MAX_PATHS   4
#define MAX_IDS     4                      /* sources and targets a VidPN keeps mode sets for */
#define MAX_CHILDREN 4
#define MAX_MAPS    8

typedef struct DxgkDriver {
    struct DxgkDriver *next;
    NT_DRIVER_OBJECT  *obj;
    char               service[64];
    KMDDOD_INITIALIZATION_DATA ddi;
    NT_UNICODE_STRING  regpath;
    NT_WCHAR           regbuf[160];
    int                adapters;
} DxgkDriver;

typedef struct DxgkAdapter {
    struct DxgkAdapter *next;
    DxgkDriver        *drv;
    NT_DEVICE_OBJECT  *fdo, *pdo, *lower;
    void              *ctx;                /* the driver's (DxgkDdiAddDevice) */
    DXGKRNL_INTERFACE  cb;
    NT_CM_RESOURCE_LIST *xlat;
    NT_KEVENT          lock;               /* a mutex: DDI calls one at a time */
    NT_KDPC            dpc;
    unsigned int       sources, nchild;
    DXGK_CHILD_DESCRIPTOR child[MAX_CHILDREN];
    D3DDDI_VIDEO_PRESENT_TARGET_ID target;
    bool               started, shown;     /* (shown: offered to the display layer) */
    DisplayWddm        disp;
    DisplayMode        modes[MAX_MODES];
    int                nmodes;
    DisplayMode        cur;
    struct { void *va; UINT64 len; } map[MAX_MAPS];
} DxgkAdapter;

static DxgkDriver  *g_drivers;
static DxgkAdapter *g_adapters;
static NT_DRIVER_OBJECT g_dxgk;           /* owns \Device\DxgKrnl */

static DxgkDriver *driver_of(NT_DRIVER_OBJECT *obj)
{
    for (DxgkDriver *d = g_drivers; d; d = d->next) if (d->obj == obj) return d;
    return NULL;
}

static void lock(DxgkAdapter *a) { nt_KeWaitForSingleObject(&a->lock, 0, 0, 0, NULL); }
static void unlock(DxgkAdapter *a) { nt_KeSetEvent(&a->lock, 0, 0); }

/* -----------------------------------------------------------------------
 * The VidPN manager: VidPNs, their topology and mode sets
 * ----------------------------------------------------------------------- */
typedef struct {
    bool  target;                          /* a target mode set (else a source one) */
    bool  assigned;                        /* belongs to a VidPN (else freed when released) */
    bool  retired;                         /* replaced by an assigned one: freed with the VidPN */
    int   n, pinned;                       /* pinned: an index, or -1 */
    union {
        D3DKMDT_VIDPN_SOURCE_MODE s[MAX_MODES];
        D3DKMDT_VIDPN_TARGET_MODE t[MAX_MODES];
    };
} ModeSet;

typedef struct {
    D3DKMDT_VIDPN_PRESENT_PATH path[MAX_PATHS];
    int      npaths;
    ModeSet *src[MAX_IDS], *tgt[MAX_IDS];
    ModeSet *retired[2 * MAX_IDS];         /* (sets the driver may still hold a handle to) */
} VidPn;

static ModeSet *modeset_new(bool target)
{
    ModeSet *m = kzalloc(sizeof(*m));
    if (m) { m->target = target; m->pinned = -1; }
    return m;
}

static VidPn *vidpn_new(void) { return kzalloc(sizeof(VidPn)); }

static void vidpn_free(VidPn *v)
{
    if (!v) return;
    for (int i = 0; i < MAX_IDS; i++) { kfree(v->src[i]); kfree(v->tgt[i]); }
    for (int i = 0; i < 2 * MAX_IDS; i++) kfree(v->retired[i]);
    kfree(v);
}

/* (a mode info the set holds, as opposed to one CreateNewModeInfo made) */
static bool in_set(const ModeSet *m, const void *p)
{
    const UINT8 *b = (const UINT8 *)(m->target ? (const void *)m->t : (const void *)m->s);
    size_t each = m->target ? sizeof(m->t[0]) : sizeof(m->s[0]);
    return (const UINT8 *)p >= b && (const UINT8 *)p < b + MAX_MODES * each;
}

/* ---- topology ---- */
static NT_API NT_STATUS topo_num_paths(D3DKMDT_HVIDPNTOPOLOGY h, unsigned long long *n)
{
    *n = (unsigned long long)((VidPn *)h)->npaths;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS topo_num_from_source(D3DKMDT_HVIDPNTOPOLOGY h, D3DDDI_VIDEO_PRESENT_SOURCE_ID s,
                                             unsigned long long *n)
{
    VidPn *v = h;
    *n = 0;
    for (int i = 0; i < v->npaths; i++) if (v->path[i].VidPnSourceId == s) (*n)++;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS topo_enum_targets(D3DKMDT_HVIDPNTOPOLOGY h, D3DDDI_VIDEO_PRESENT_SOURCE_ID s,
                                          unsigned long long index, D3DDDI_VIDEO_PRESENT_TARGET_ID *t)
{
    VidPn *v = h;
    for (int i = 0; i < v->npaths; i++)
        if (v->path[i].VidPnSourceId == s && index-- == 0) { *t = v->path[i].VidPnTargetId; return NT_STATUS_SUCCESS; }
    return NT_STATUS_INVALID_PARAMETER;
}

static NT_API NT_STATUS topo_source_from_target(D3DKMDT_HVIDPNTOPOLOGY h, D3DDDI_VIDEO_PRESENT_TARGET_ID t,
                                                D3DDDI_VIDEO_PRESENT_SOURCE_ID *s)
{
    VidPn *v = h;
    for (int i = 0; i < v->npaths; i++)
        if (v->path[i].VidPnTargetId == t) { *s = v->path[i].VidPnSourceId; return NT_STATUS_SUCCESS; }
    return STATUS_GRAPHICS_PATH_NOT_IN_TOPOLOGY;
}

static NT_API NT_STATUS topo_acquire_path(D3DKMDT_HVIDPNTOPOLOGY h, D3DDDI_VIDEO_PRESENT_SOURCE_ID s,
                                          D3DDDI_VIDEO_PRESENT_TARGET_ID t, const D3DKMDT_VIDPN_PRESENT_PATH **out)
{
    VidPn *v = h;
    for (int i = 0; i < v->npaths; i++)
        if (v->path[i].VidPnSourceId == s && v->path[i].VidPnTargetId == t) { *out = &v->path[i]; return NT_STATUS_SUCCESS; }
    *out = NULL;
    return STATUS_GRAPHICS_PATH_NOT_IN_TOPOLOGY;
}

static NT_API NT_STATUS topo_first_path(D3DKMDT_HVIDPNTOPOLOGY h, const D3DKMDT_VIDPN_PRESENT_PATH **out)
{
    VidPn *v = h;
    *out = v->npaths ? &v->path[0] : NULL;
    return v->npaths ? NT_STATUS_SUCCESS : STATUS_GRAPHICS_DATASET_IS_EMPTY;
}

static NT_API NT_STATUS topo_next_path(D3DKMDT_HVIDPNTOPOLOGY h, const D3DKMDT_VIDPN_PRESENT_PATH *cur,
                                       const D3DKMDT_VIDPN_PRESENT_PATH **next)
{
    VidPn *v = h;
    int i = (int)(cur - v->path) + 1;
    if (cur < v->path || i <= 0 || i > v->npaths) return NT_STATUS_INVALID_PARAMETER;
    *next = i < v->npaths ? &v->path[i] : NULL;
    return i < v->npaths ? NT_STATUS_SUCCESS : STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

/* The driver says which scalings and rotations the path can do */
static NT_API NT_STATUS topo_update_support(D3DKMDT_HVIDPNTOPOLOGY h, const D3DKMDT_VIDPN_PRESENT_PATH *p)
{
    VidPn *v = h;
    for (int i = 0; i < v->npaths; i++)
        if (v->path[i].VidPnSourceId == p->VidPnSourceId && v->path[i].VidPnTargetId == p->VidPnTargetId) {
            v->path[i].ContentTransformation.ScalingSupport = p->ContentTransformation.ScalingSupport;
            v->path[i].ContentTransformation.RotationSupport = p->ContentTransformation.RotationSupport;
            return NT_STATUS_SUCCESS;
        }
    return STATUS_GRAPHICS_PATH_NOT_IN_TOPOLOGY;
}

static NT_API NT_STATUS topo_release_path(D3DKMDT_HVIDPNTOPOLOGY h, const D3DKMDT_VIDPN_PRESENT_PATH *p)
{
    VidPn *v = h;
    if (p && (p < v->path || p >= v->path + MAX_PATHS)) kfree((void *)p);   /* (a new one never added) */
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS topo_new_path(D3DKMDT_HVIDPNTOPOLOGY h, D3DKMDT_VIDPN_PRESENT_PATH **out)
{
    (void)h;
    *out = kzalloc(sizeof(**out));
    return *out ? NT_STATUS_SUCCESS : NT_STATUS_NO_MEMORY;
}

static NT_API NT_STATUS topo_add_path(D3DKMDT_HVIDPNTOPOLOGY h, D3DKMDT_VIDPN_PRESENT_PATH *p)
{
    VidPn *v = h;
    if (v->npaths >= MAX_PATHS || p->VidPnSourceId >= MAX_IDS || p->VidPnTargetId >= MAX_IDS)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    v->path[v->npaths++] = *p;
    kfree(p);                                         /* (the topology owns it now) */
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS topo_remove_path(D3DKMDT_HVIDPNTOPOLOGY h, D3DDDI_VIDEO_PRESENT_SOURCE_ID s,
                                         D3DDDI_VIDEO_PRESENT_TARGET_ID t)
{
    VidPn *v = h;
    for (int i = 0; i < v->npaths; i++)
        if (v->path[i].VidPnSourceId == s && v->path[i].VidPnTargetId == t) {
            memmove(&v->path[i], &v->path[i + 1], (size_t)(v->npaths - i - 1) * sizeof(v->path[0]));
            v->npaths--;
            return NT_STATUS_SUCCESS;
        }
    return STATUS_GRAPHICS_PATH_NOT_IN_TOPOLOGY;
}

static const DXGK_VIDPNTOPOLOGY_INTERFACE g_topology_if = {
    topo_num_paths, topo_num_from_source, topo_enum_targets, topo_source_from_target, topo_acquire_path,
    topo_first_path, topo_next_path, topo_update_support, topo_release_path, topo_new_path, topo_add_path,
    topo_remove_path,
};

/* ---- mode sets (one set of functions for both kinds: the modes differ in size) ---- */
static void *mode_at(ModeSet *m, int i) { return m->target ? (void *)&m->t[i] : (void *)&m->s[i]; }
static UINT32 mode_id(ModeSet *m, const void *p)
{
    return m->target ? ((const D3DKMDT_VIDPN_TARGET_MODE *)p)->Id : ((const D3DKMDT_VIDPN_SOURCE_MODE *)p)->Id;
}

static NT_API NT_STATUS ms_num(void *h, unsigned long long *n) { *n = (unsigned long long)((ModeSet *)h)->n; return 0; }

static NT_API NT_STATUS ms_first(void *h, const void **out)
{
    ModeSet *m = h;
    *out = m->n ? mode_at(m, 0) : NULL;
    return m->n ? NT_STATUS_SUCCESS : STATUS_GRAPHICS_DATASET_IS_EMPTY;
}

static NT_API NT_STATUS ms_next(void *h, const void *cur, const void **next)
{
    ModeSet *m = h;
    if (!in_set(m, cur)) return NT_STATUS_INVALID_PARAMETER;
    int i = (int)(((const UINT8 *)cur - (const UINT8 *)mode_at(m, 0)) /
                  (m->target ? sizeof(m->t[0]) : sizeof(m->s[0]))) + 1;
    *next = i < m->n ? mode_at(m, i) : NULL;
    return i < m->n ? NT_STATUS_SUCCESS : STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

/* (no mode pinned: success and NULL, as KMDOD expects) */
static NT_API NT_STATUS ms_pinned(void *h, const void **out)
{
    ModeSet *m = h;
    *out = m->pinned >= 0 ? mode_at(m, m->pinned) : NULL;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS ms_release(void *h, const void *p)
{
    if (p && !in_set(h, p)) kfree((void *)p);         /* (a new one never added) */
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS ms_new(void *h, void **out)
{
    ModeSet *m = h;
    *out = kzalloc(m->target ? sizeof(m->t[0]) : sizeof(m->s[0]));
    return *out ? NT_STATUS_SUCCESS : NT_STATUS_NO_MEMORY;
}

static NT_API NT_STATUS ms_add(void *h, const void *p)
{
    ModeSet *m = h;
    if (m->n >= MAX_MODES) return NT_STATUS_INSUFFICIENT_RESOURCES;
    for (int i = 0; i < m->n; i++)
        if (mode_id(m, mode_at(m, i)) == mode_id(m, p)) return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
    memcpy(mode_at(m, m->n++), p, m->target ? sizeof(m->t[0]) : sizeof(m->s[0]));
    kfree((void *)p);                                 /* (the set owns it now) */
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS ms_pin(void *h, UINT32 id)
{
    ModeSet *m = h;
    for (int i = 0; i < m->n; i++) if (mode_id(m, mode_at(m, i)) == id) { m->pinned = i; return NT_STATUS_SUCCESS; }
    return NT_STATUS_INVALID_PARAMETER;
}

static const DXGK_VIDPNSOURCEMODESET_INTERFACE g_source_if = {
    (void *)ms_num, (void *)ms_first, (void *)ms_next, (void *)ms_pinned, (void *)ms_release, (void *)ms_new,
    (void *)ms_add, (void *)ms_pin,
};
static const DXGK_VIDPNTARGETMODESET_INTERFACE g_target_if = {
    (void *)ms_num, (void *)ms_first, (void *)ms_next, (void *)ms_pinned, (void *)ms_release, (void *)ms_new,
    (void *)ms_add, (void *)ms_pin,
};

/* ---- the VidPN ---- */
static NT_API NT_STATUS vp_topology(D3DKMDT_HVIDPN h, D3DKMDT_HVIDPNTOPOLOGY *t, const DXGK_VIDPNTOPOLOGY_INTERFACE **i)
{
    *t = h;
    *i = &g_topology_if;
    return NT_STATUS_SUCCESS;
}

static ModeSet **slot(VidPn *v, bool target, UINT32 id) { return id < MAX_IDS ? (target ? &v->tgt[id] : &v->src[id]) : NULL; }

static NT_STATUS acquire_set(VidPn *v, bool target, UINT32 id, void **h)
{
    ModeSet **s = slot(v, target, id);
    if (!s) return target ? STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET : STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    if (!*s) {                                        /* (an empty set until the driver assigns one) */
        *s = modeset_new(target);
        if (!*s) return NT_STATUS_NO_MEMORY;
        (*s)->assigned = true;
    }
    *h = *s;
    return NT_STATUS_SUCCESS;
}

static NT_STATUS new_set(bool target, UINT32 id, void **h)
{
    if (id >= MAX_IDS) return target ? STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET : STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    *h = modeset_new(target);
    return *h ? NT_STATUS_SUCCESS : NT_STATUS_NO_MEMORY;
}

static NT_STATUS assign_set(VidPn *v, bool target, UINT32 id, ModeSet *m)
{
    ModeSet **s = slot(v, target, id);
    if (!s || !m || m->target != target) return NT_STATUS_INVALID_PARAMETER;
    if (*s && *s != m) {                              /* (KMDOD releases the old set after assigning) */
        int i = 0;
        while (i < 2 * MAX_IDS && v->retired[i]) i++;
        if (i < 2 * MAX_IDS) { (*s)->assigned = false; (*s)->retired = true; v->retired[i] = *s; }
        else kfree(*s);
    }
    m->assigned = true;
    *s = m;
    return NT_STATUS_SUCCESS;
}

static NT_STATUS release_set(ModeSet *m)
{
    if (m && !m->assigned && !m->retired) kfree(m);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS vp_acq_src(D3DKMDT_HVIDPN h, UINT32 id, void **ms, const DXGK_VIDPNSOURCEMODESET_INTERFACE **i)
{
    *i = &g_source_if;
    return acquire_set(h, false, id, ms);
}
static NT_API NT_STATUS vp_rel_src(D3DKMDT_HVIDPN h, void *ms) { (void)h; return release_set(ms); }
static NT_API NT_STATUS vp_new_src(D3DKMDT_HVIDPN h, UINT32 id, void **ms, const DXGK_VIDPNSOURCEMODESET_INTERFACE **i)
{
    (void)h;
    *i = &g_source_if;
    return new_set(false, id, ms);
}
static NT_API NT_STATUS vp_asg_src(D3DKMDT_HVIDPN h, UINT32 id, void *ms) { return assign_set(h, false, id, ms); }
static NT_API NT_STATUS vp_msaa(D3DKMDT_HVIDPN h, UINT32 id, unsigned long long n, const void *m)
{
    (void)h; (void)id; (void)n; (void)m;
    return NT_STATUS_SUCCESS;                         /* (display-only: no multisampling) */
}
static NT_API NT_STATUS vp_acq_tgt(D3DKMDT_HVIDPN h, UINT32 id, void **ms, const DXGK_VIDPNTARGETMODESET_INTERFACE **i)
{
    *i = &g_target_if;
    return acquire_set(h, true, id, ms);
}
static NT_API NT_STATUS vp_rel_tgt(D3DKMDT_HVIDPN h, void *ms) { (void)h; return release_set(ms); }
static NT_API NT_STATUS vp_new_tgt(D3DKMDT_HVIDPN h, UINT32 id, void **ms, const DXGK_VIDPNTARGETMODESET_INTERFACE **i)
{
    (void)h;
    *i = &g_target_if;
    return new_set(true, id, ms);
}
static NT_API NT_STATUS vp_asg_tgt(D3DKMDT_HVIDPN h, UINT32 id, void *ms) { return assign_set(h, true, id, ms); }

static const DXGK_VIDPN_INTERFACE g_vidpn_if = {
    DXGK_VIDPN_INTERFACE_VERSION_V1, vp_topology, (void *)vp_acq_src, (void *)vp_rel_src, (void *)vp_new_src,
    (void *)vp_asg_src, vp_msaa, (void *)vp_acq_tgt, (void *)vp_rel_tgt, (void *)vp_new_tgt, (void *)vp_asg_tgt,
};

/* A VidPN with one path, source 0 to @target; with @w x @h, that mode
 * pinned on both ends (a VidPN to commit), else no modes (one for the
 * driver to fill with what it can do) */
static VidPn *vidpn_one(D3DDDI_VIDEO_PRESENT_TARGET_ID target, int w, int h)
{
    VidPn *v = vidpn_new();
    if (!v || target >= MAX_IDS) { kfree(v); return NULL; }
    D3DKMDT_VIDPN_PRESENT_PATH *p = &v->path[v->npaths++];
    p->VidPnSourceId = 0;
    p->VidPnTargetId = target;
    p->ImportanceOrdinal = D3DKMDT_VPPI_PRIMARY;
    p->ContentTransformation.Scaling = D3DKMDT_VPPS_IDENTITY;
    p->ContentTransformation.ScalingSupport = 1;     /* identity */
    p->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
    p->ContentTransformation.RotationSupport = 1;
    p->VidPnTargetColorBasis = D3DKMDT_CB_SRGB;
    p->Content = D3DKMDT_VPPC_GRAPHICS;
    if (w <= 0) return v;
    ModeSet *s = modeset_new(false), *t = modeset_new(true);
    if (!s || !t) { kfree(s); kfree(t); vidpn_free(v); return NULL; }
    D3DKMDT_VIDPN_SOURCE_MODE *sm = &s->s[s->n++];
    sm->Id = 0;
    sm->Type = D3DKMDT_RMT_GRAPHICS;
    sm->Format.Graphics.PrimSurfSize = (D3DKMDT_2DREGION){ (UINT32)w, (UINT32)h };
    sm->Format.Graphics.VisibleRegionSize = sm->Format.Graphics.PrimSurfSize;
    sm->Format.Graphics.Stride = (UINT64)w * 4;
    sm->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
    sm->Format.Graphics.ColorBasis = D3DKMDT_CB_SRGB;
    sm->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
    s->pinned = 0;
    D3DKMDT_VIDPN_TARGET_MODE *tm = &t->t[t->n++];
    tm->Id = 0;
    tm->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
    tm->VideoSignalInfo.TotalSize = tm->VideoSignalInfo.ActiveSize = (D3DKMDT_2DREGION){ (UINT32)w, (UINT32)h };
    tm->VideoSignalInfo.VSyncFreq = (D3DDDI_RATIONAL){ 60000, 1000 };
    tm->VideoSignalInfo.HSyncFreq = (D3DDDI_RATIONAL){ 60000u * (UINT32)h, 1000 };
    tm->VideoSignalInfo.PixelRate = (UINT64)w * h * 60;
    tm->VideoSignalInfo.ScanLineOrdering = 1;       /* progressive */
    tm->Preference = D3DKMDT_MP_PREFERRED;
    t->pinned = 0;
    s->assigned = t->assigned = true;
    v->src[0] = s;
    v->tgt[target] = t;
    return v;
}

/* -----------------------------------------------------------------------
 * The callbacks (DXGKRNL_INTERFACE)
 * ----------------------------------------------------------------------- */
static DxgkAdapter *adapter_of(void *h)
{
    for (DxgkAdapter *a = g_adapters; a; a = a->next) if (a == h) return a;
    return NULL;
}

static NT_API NT_STATUS cb_get_device_info(void *h, DXGK_DEVICE_INFO *info)
{
    DxgkAdapter *a = adapter_of(h);
    if (!a || !info) return NT_STATUS_INVALID_PARAMETER;
    memset(info, 0, sizeof(*info));
    uint64_t total = 0, free_ = 0, used = 0;
    pmm_stats(&total, &free_, &used);
    info->MiniportDeviceContext = a->ctx;
    info->PhysicalDeviceObject = a->pdo;
    info->DeviceRegistryPath = a->drv->regpath;
    info->TranslatedResourceList = a->xlat;
    info->SystemMemorySize = (long long)(total * PAGE_SIZE);
    info->HighestPhysicalAddress = total * PAGE_SIZE - 1;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS cb_read_space(void *h, unsigned int space, void *buf, unsigned int off, unsigned int len,
                                      unsigned int *done)
{
    DxgkAdapter *a = adapter_of(h);
    if (done) *done = 0;
    if (!a || space != DXGK_WHICHSPACE_CONFIG) return NT_STATUS_NOT_SUPPORTED;
    UINT32 n = NtPnpConfig(a->pdo, false, buf, off, len);
    if (done) *done = n;
    return n == len ? NT_STATUS_SUCCESS : NT_STATUS_UNSUCCESSFUL;
}

static NT_API NT_STATUS cb_write_space(void *h, unsigned int space, void *buf, unsigned int off, unsigned int len,
                                       unsigned int *done)
{
    DxgkAdapter *a = adapter_of(h);
    if (done) *done = 0;
    if (!a || space != DXGK_WHICHSPACE_CONFIG) return NT_STATUS_NOT_SUPPORTED;
    UINT32 n = NtPnpConfig(a->pdo, true, buf, off, len);
    if (done) *done = n;
    return n == len ? NT_STATUS_SUCCESS : NT_STATUS_UNSUCCESSFUL;
}

static NT_API NT_STATUS cb_map_memory(void *h, DXGK_PHYSICAL_ADDRESS pa, unsigned int len, unsigned char io,
                                      unsigned char user, int cache, void **va)
{
    DxgkAdapter *a = adapter_of(h);
    if (!a || !va || user) return NT_STATUS_NOT_SUPPORTED;   /* (no mappings into a process yet) */
    if (io) { *va = (void *)pa; return NT_STATUS_SUCCESS; }   /* (x86 port I/O: the port number) */
    int i = 0;
    while (i < MAX_MAPS && a->map[i].va) i++;
    if (i == MAX_MAPS) return NT_STATUS_INSUFFICIENT_RESOURCES;
    *va = nt_MmMapIoSpace((INT64)pa, len, cache);
    if (!*va) return NT_STATUS_INSUFFICIENT_RESOURCES;
    a->map[i].va = *va;
    a->map[i].len = len;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS cb_unmap_memory(void *h, void *va)
{
    DxgkAdapter *a = adapter_of(h);
    for (int i = 0; a && i < MAX_MAPS; i++)
        if (a->map[i].va == va) {
            nt_MmUnmapIoSpace(va, a->map[i].len);
            a->map[i].va = NULL;
            return NT_STATUS_SUCCESS;
        }
    return NT_STATUS_INVALID_PARAMETER;
}

static NT_API void dpc_routine(NT_KDPC *dpc, void *ctx, void *a1, void *a2)
{
    (void)dpc; (void)a1; (void)a2;
    DxgkAdapter *a = ctx;
    if (a->drv->ddi.DxgkDdiDpcRoutine) a->drv->ddi.DxgkDdiDpcRoutine(a->ctx);
}

static NT_API unsigned char cb_queue_dpc(void *h)
{
    DxgkAdapter *a = adapter_of(h);
    return a ? nt_KeInsertQueueDpc(&a->dpc, NULL, NULL) : 0;
}

/* (no interrupt connected yet: the routine runs as it is) */
static NT_API NT_STATUS cb_synchronize(void *h, PKSYNCHRONIZE_ROUTINE fn, void *ctx, unsigned int msg, unsigned char *ret)
{
    (void)h; (void)msg;
    unsigned char r = fn(ctx);
    if (ret) *ret = r;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS cb_indicate_child(void *h, DXGK_CHILD_STATUS *st)
{
    DxgkAdapter *a = adapter_of(h);
    if (a && st && st->Type == StatusConnection)
        kprintf("[DXGK] %s: output %u %s\n", a->drv->service, st->ChildUid, st->HotPlug.Connected ? "connected" : "disconnected");
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS cb_present(void *h, void *params, unsigned char *present)
{
    (void)h; (void)params;
    if (present) *present = 1;
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS cb_unsupported(void) { return NT_STATUS_NOT_SUPPORTED; }
static NT_API void cb_nothing(void) {}

static NT_API NT_STATUS cb_query_vidpn(D3DKMDT_HVIDPN v, int version, const DXGK_VIDPN_INTERFACE **out)
{
    if (!v || !out) return NT_STATUS_INVALID_PARAMETER;
    if (version != DXGK_VIDPN_INTERFACE_VERSION_V1) return NT_STATUS_NOT_SUPPORTED;
    *out = &g_vidpn_if;
    return NT_STATUS_SUCCESS;
}

/* What the built-in driver shows: the screen the driver takes over */
static NT_API NT_STATUS cb_post_display(void *h, DXGK_DISPLAY_INFORMATION *info)
{
    DxgkAdapter *a = adapter_of(h);
    UINT64 phys;
    int w, ht, pitch;
    if (!a || !info || !DisplayPostInfo(&phys, &w, &ht, &pitch)) return NT_STATUS_UNSUCCESSFUL;
    memset(info, 0, sizeof(*info));
    info->Width = (UINT32)w;
    info->Height = (UINT32)ht;
    info->Pitch = (UINT32)pitch;
    info->ColorFormat = D3DDDIFMT_X8R8G8B8;
    info->PhysicAddress = phys;
    info->TargetId = a->target;
    return NT_STATUS_SUCCESS;
}

/* -----------------------------------------------------------------------
 * Modes and presentation, for the display layer
 * ----------------------------------------------------------------------- */
static bool commit(DxgkAdapter *a, int w, int h)
{
    const KMDDOD_INITIALIZATION_DATA *ddi = &a->drv->ddi;
    VidPn *v = vidpn_one(a->target, w, h);
    if (!v) return false;
    DXGKARG_ISSUPPORTEDVIDPN is = { v, 0 };
    NT_STATUS st = ddi->DxgkDdiIsSupportedVidPn(a->ctx, &is);
    if (!NT_SUCCESS_(st) || !is.IsVidPnSupported) {
        kprintf("[DXGK] %s: %dx%d not supported (0x%08x)\n", a->drv->service, w, h, (UINT32)st);
        vidpn_free(v);
        return false;
    }
    DXGKARG_COMMITVIDPN c;
    memset(&c, 0, sizeof(c));
    c.hFunctionalVidPn = v;
    c.AffectedVidPnSourceId = 0;
    c.MonitorConnectivityChecks = D3DKMDT_MCC_IGNORE;
    st = ddi->DxgkDdiCommitVidPn(a->ctx, &c);
    vidpn_free(v);
    if (!NT_SUCCESS_(st)) {
        kprintf("[DXGK] %s: committing %dx%d failed (0x%08x)\n", a->drv->service, w, h, (UINT32)st);
        return false;
    }
    if (ddi->DxgkDdiSetVidPnSourceVisibility) {
        DXGKARG_SETVIDPNSOURCEVISIBILITY vis = { 0, 1 };
        ddi->DxgkDdiSetVidPnSourceVisibility(a->ctx, &vis);
    }
    a->cur = (DisplayMode){ w, h };
    kprintf("[DXGK] %s: VidPN committed, source 0 at %dx%d on output %u\n", a->drv->service, w, h, a->target);
    return true;
}

static bool disp_set_mode(void *ctx, int w, int h)
{
    DxgkAdapter *a = ctx;
    lock(a);
    bool ok = a->started && commit(a, w, h);
    unlock(a);
    return ok;
}

static void disp_present(void *ctx, const UINT32 *px, int stride, int x, int y, int w, int h)
{
    DxgkAdapter *a = ctx;
    lock(a);
    if (a->started && a->shown) {
        DXGK_RECT r = { x, y, x + w, y + h };
        DXGKARG_PRESENT_DISPLAYONLY p;
        memset(&p, 0, sizeof(p));
        p.VidPnSourceId = 0;
        p.pSource = (void *)px;
        p.BytesPerPixel = 4;
        p.Pitch = stride * 4;
        p.NumDirtyRects = 1;
        p.pDirtyRect = &r;
        NT_STATUS st = a->drv->ddi.DxgkDdiPresentDisplayOnly(a->ctx, &p);
        if (!NT_SUCCESS_(st)) kprintf("[DXGK] %s: DxgkDdiPresentDisplayOnly failed (0x%08x)\n", a->drv->service, (UINT32)st);
    }
    unlock(a);
}

/* The driver's modes: what it puts in source 0's mode set for a VidPN
 * that pins nothing */
static void read_modes(DxgkAdapter *a)
{
    a->nmodes = 0;
    VidPn *v = vidpn_one(a->target, 0, 0);
    if (!v) return;
    DXGKARG_ENUMVIDPNCOFUNCMODALITY e;
    memset(&e, 0, sizeof(e));
    e.hConstrainingVidPn = v;
    e.EnumPivotType = D3DKMDT_EPT_NOPIVOT;
    NT_STATUS st = a->drv->ddi.DxgkDdiEnumVidPnCofuncModality(a->ctx, &e);
    if (!NT_SUCCESS_(st)) kprintf("[DXGK] %s: DxgkDdiEnumVidPnCofuncModality failed (0x%08x)\n", a->drv->service, (UINT32)st);
    for (int i = 0; v->src[0] && i < v->src[0]->n && a->nmodes < MAX_MODES; i++) {
        const D3DKMDT_VIDPN_SOURCE_MODE *m = &v->src[0]->s[i];
        int f = m->Format.Graphics.PixelFormat;
        if (m->Type != D3DKMDT_RMT_GRAPHICS || (f != D3DDDIFMT_A8R8G8B8 && f != D3DDDIFMT_X8R8G8B8)) continue;
        a->modes[a->nmodes++] = (DisplayMode){ (int)m->Format.Graphics.PrimSurfSize.cx, (int)m->Format.Graphics.PrimSurfSize.cy };
    }
    int targets = v->tgt[a->target] ? v->tgt[a->target]->n : 0;
    vidpn_free(v);
    kprintf("[DXGK] %s: %d source mode(s), %d target mode(s) on output %u\n", a->drv->service, a->nmodes, targets, a->target);
}

/* -----------------------------------------------------------------------
 * Plug and Play
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS sync_done(NT_DEVICE_OBJECT *dev, NT_IRP *irp, void *ctx)
{
    (void)dev; (void)irp;
    nt_KeSetEvent(ctx, 0, 0);
    return NT_STATUS_MORE_PROCESSING_REQUIRED;
}

static NT_STATUS forward_and_wait(DxgkAdapter *a, NT_IRP *irp)
{
    NT_KEVENT ev;
    nt_KeInitializeEvent(&ev, NT_NotificationEvent, 0);
    NT_IO_STACK_LOCATION *cur = NtIrpCurrentStack(irp), *next = NtIrpNextStack(irp);
    memcpy(next, cur, __builtin_offsetof(NT_IO_STACK_LOCATION, CompletionRoutine));
    next->CompletionRoutine = sync_done;
    next->Context = &ev;
    next->Control = NT_SL_INVOKE_ON_SUCCESS | NT_SL_INVOKE_ON_ERROR | NT_SL_INVOKE_ON_CANCEL;
    if (nt_IofCallDriver(a->lower, irp) == NT_STATUS_PENDING) nt_KeWaitForSingleObject(&ev, 0, 0, 0, NULL);
    return irp->IoStatus.Status;
}

static NT_STATUS forward(DxgkAdapter *a, NT_IRP *irp)
{
    irp->CurrentLocation++;                                  /* IoSkipCurrentIrpStackLocation */
    irp->Tail.Overlay.CurrentStackLocation++;
    return nt_IofCallDriver(a->lower, irp);
}

static NT_STATUS complete(NT_IRP *irp, NT_STATUS st)
{
    irp->IoStatus.Status = st;
    nt_IofCompleteRequest(irp, 0);
    return st;
}

static NT_CM_RESOURCE_LIST *copy_resources(const NT_CM_RESOURCE_LIST *r)
{
    if (!r || !r->Count) return NULL;
    size_t n = __builtin_offsetof(NT_CM_RESOURCE_LIST, List) +
               __builtin_offsetof(NT_CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList.PartialDescriptors) +
               (size_t)r->List[0].PartialResourceList.Count * sizeof(NT_CM_PARTIAL_RESOURCE_DESCRIPTOR);
    NT_CM_RESOURCE_LIST *c = kmalloc(n);
    if (c) { memcpy(c, r, n); c->Count = 1; }
    return c;
}

static void fill_interface(DxgkAdapter *a)
{
    DXGKRNL_INTERFACE *cb = &a->cb;
    memset(cb, 0, sizeof(*cb));
    cb->Size = sizeof(*cb);
    cb->Version = DXGKDDI_INTERFACE_VERSION_WIN8;
    cb->DeviceHandle = a;
    cb->DxgkCbEvalAcpiMethod = (void *)cb_unsupported;
    cb->DxgkCbGetDeviceInformation = cb_get_device_info;
    cb->DxgkCbIndicateChildStatus = cb_indicate_child;
    cb->DxgkCbMapMemory = cb_map_memory;
    cb->DxgkCbQueueDpc = cb_queue_dpc;
    cb->DxgkCbQueryServices = (void *)cb_unsupported;
    cb->DxgkCbReadDeviceSpace = cb_read_space;
    cb->DxgkCbSynchronizeExecution = cb_synchronize;
    cb->DxgkCbUnmapMemory = cb_unmap_memory;
    cb->DxgkCbWriteDeviceSpace = cb_write_space;
    cb->DxgkCbIsDevicePresent = cb_present;
    cb->DxgkCbNotifyInterrupt = (void *)cb_nothing;
    cb->DxgkCbNotifyDpc = (void *)cb_nothing;
    cb->DxgkCbQueryVidPnInterface = cb_query_vidpn;
    cb->DxgkCbQueryMonitorInterface = (void *)cb_unsupported;
    cb->DxgkCbAcquirePostDisplayOwnership = cb_post_display;
}

static NT_STATUS adapter_start(DxgkAdapter *a, NT_CM_RESOURCE_LIST *xlat)
{
    const KMDDOD_INITIALIZATION_DATA *ddi = &a->drv->ddi;
    const char *svc = a->drv->service;
    kfree(a->xlat);
    a->xlat = copy_resources(xlat);
    fill_interface(a);
    DXGK_START_INFO si;
    memset(&si, 0, sizeof(si));
    si.AdapterLuid.LowPart = 0x4E560000u | ((UINT32)(uintptr_t)a->pdo >> 4 & 0xFFFF);   /* (unique while it runs) */
    unsigned int sources = 0, children = 0;
    NT_STATUS st = ddi->DxgkDdiStartDevice(a->ctx, &si, &a->cb, &sources, &children);
    if (!NT_SUCCESS_(st)) {
        kprintf("[DXGK] %s: DxgkDdiStartDevice failed (0x%08x)\n", svc, (UINT32)st);
        return st;
    }
    a->started = true;
    a->sources = sources;
    DXGK_DRIVERCAPS caps;
    memset(&caps, 0, sizeof(caps));
    DXGKARG_QUERYADAPTERINFO q = { DXGKQAITYPE_DRIVERCAPS, NULL, 0, &caps, sizeof(caps), 0, NULL };
    if (ddi->DxgkDdiQueryAdapterInfo && NT_SUCCESS_(ddi->DxgkDdiQueryAdapterInfo(a->ctx, &q)))
        kprintf("[DXGK] %s: started, %u source(s), %u output(s), WDDM %u.%u\n", svc, sources, children,
                (UINT32)caps.WDDMVersion >> 12, (UINT32)caps.WDDMVersion >> 8 & 0xF);
    else
        kprintf("[DXGK] %s: started, %u source(s), %u output(s)\n", svc, sources, children);

    /* Its outputs: the first connected video output gets source 0 */
    if (children > MAX_CHILDREN) children = MAX_CHILDREN;
    DXGK_CHILD_DESCRIPTOR rel[MAX_CHILDREN + 1];
    memset(rel, 0, sizeof(rel));
    st = ddi->DxgkDdiQueryChildRelations(a->ctx, rel, (unsigned int)sizeof(DXGK_CHILD_DESCRIPTOR) * (children + 1));
    if (!NT_SUCCESS_(st)) children = 0;
    a->nchild = 0;
    bool found = false;
    for (unsigned int i = 0; i < children && rel[i].ChildDeviceType != TypeUninitialized; i++) {
        a->child[a->nchild++] = rel[i];
        if (rel[i].ChildDeviceType != TypeVideoOutput || found) continue;
        DXGK_CHILD_STATUS cs;
        memset(&cs, 0, sizeof(cs));
        cs.Type = StatusConnection;
        cs.ChildUid = rel[i].ChildUid;
        bool on = rel[i].ChildCapabilities.HpdAwareness == HpdAwarenessAlwaysConnected ||
                  (NT_SUCCESS_(ddi->DxgkDdiQueryChildStatus(a->ctx, &cs, 1)) && cs.HotPlug.Connected);
        if (on && rel[i].ChildUid < MAX_IDS) { a->target = rel[i].ChildUid; found = true; }
    }
    if (!found || !sources) {
        kprintf("[DXGK] %s: no connected output to show the desktop on\n", svc);
        return NT_STATUS_SUCCESS;                     /* (it runs; the screen stays the built-in driver's) */
    }
    if (ddi->DxgkDdiQueryDeviceDescriptor) {          /* (the monitor's EDID, when it has one) */
        UINT8 edid[128];
        DXGK_DEVICE_DESCRIPTOR dd = { 0, sizeof(edid), edid };
        st = ddi->DxgkDdiQueryDeviceDescriptor(a->ctx, a->target, &dd);
        if (st == STATUS_MONITOR_NO_DESCRIPTOR) kprintf("[DXGK] %s: output %u has no EDID\n", svc, a->target);
        else if (NT_SUCCESS_(st)) kprintf("[DXGK] %s: output %u's EDID read\n", svc, a->target);
    }
    read_modes(a);
    UINT64 phys;
    int w, h, pitch;
    if (!a->nmodes || !DisplayPostInfo(&phys, &w, &h, &pitch)) {
        kprintf("[DXGK] %s: no modes to offer the desktop\n", svc);
        return NT_STATUS_SUCCESS;
    }
    a->cur = a->modes[0];
    for (int i = 0; i < a->nmodes; i++)               /* (the mode on the screen now, if the driver has it) */
        if (a->modes[i].w == w && a->modes[i].h == h) a->cur = a->modes[i];
    a->disp = (DisplayWddm){ a->drv->service, a, disp_set_mode, disp_present };
    a->shown = true;
    DisplayWddmOffer(&a->disp, a->modes, a->nmodes, a->cur);
    return NT_STATUS_SUCCESS;
}

/* SURPRISE_REMOVAL or REMOVE_DEVICE: the driver stops, @irp goes down
 * (the PCI bus turns the function's decoding off), and the display
 * layer's built-in driver gets the screen back as the driver left it */
static NT_STATUS adapter_stop(DxgkAdapter *a, NT_IRP *irp)
{
    const KMDDOD_INITIALIZATION_DATA *ddi = &a->drv->ddi;
    if (!a->started) return forward(a, irp);
    DesktopLockAlone();                               /* (no frame half presented; the desktop's order: it, then us) */
    lock(a);
    DXGK_DISPLAY_INFORMATION info;
    memset(&info, 0, sizeof(info));
    NT_STATUS st = NT_STATUS_UNSUCCESSFUL;
    if (ddi->DxgkDdiStopDeviceAndReleasePostDisplayOwnership)
        st = ddi->DxgkDdiStopDeviceAndReleasePostDisplayOwnership(a->ctx, a->target, &info);
    if (NT_SUCCESS_(st))
        kprintf("[DXGK] %s: stopped; it left %ux%u (pitch %u) at %llx\n", a->drv->service, info.Width, info.Height,
                info.Pitch, (unsigned long long)info.PhysicAddress);
    else {
        ddi->DxgkDdiStopDevice(a->ctx);
        info.Width = (UINT32)a->cur.w;
        info.Height = (UINT32)a->cur.h;
        kprintf("[DXGK] %s: stopped\n", a->drv->service);
    }
    a->started = false;
    st = forward(a, irp);
    if (a->shown) DisplayWddmWithdraw(&a->disp, (DisplayMode){ (int)info.Width, (int)info.Height });
    a->shown = false;
    unlock(a);
    DesktopUnlockAlone();
    for (int i = 0; i < MAX_MAPS; i++)                /* (anything the driver left mapped) */
        if (a->map[i].va) { nt_MmUnmapIoSpace(a->map[i].va, a->map[i].len); a->map[i].va = NULL; }
    return st;
}

static void adapter_free(DxgkAdapter *a)
{
    for (DxgkAdapter **pp = &g_adapters; *pp; pp = &(*pp)->next)
        if (*pp == a) { *pp = a->next; break; }
    a->drv->adapters--;
    kfree(a->xlat);
    kfree(a);
}

static NT_STATUS adapter_pnp(DxgkAdapter *a, NT_IRP *irp, NT_IO_STACK_LOCATION *sl)
{
    switch (sl->MinorFunction) {
    case NT_IRP_MN_START_DEVICE: {
        NT_STATUS st = forward_and_wait(a, irp);
        if (NT_SUCCESS_(st)) st = adapter_start(a, sl->Parameters.StartDevice.AllocatedResourcesTranslated);
        return complete(irp, st);
    }
    case NT_IRP_MN_QUERY_REMOVE_DEVICE:
    case NT_IRP_MN_QUERY_STOP_DEVICE:
    case NT_IRP_MN_CANCEL_REMOVE_DEVICE:
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        return forward(a, irp);
    case NT_IRP_MN_SURPRISE_REMOVAL:
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        return adapter_stop(a, irp);
    case NT_IRP_MN_REMOVE_DEVICE: {
        irp->IoStatus.Status = NT_STATUS_SUCCESS;
        NT_STATUS st = adapter_stop(a, irp);
        a->drv->ddi.DxgkDdiRemoveDevice(a->ctx);
        nt_IoDetachDevice(a->lower);
        nt_IoDeleteDevice(a->fdo);
        kprintf("[DXGK] %s: adapter removed\n", a->drv->service);
        adapter_free(a);
        return st;
    }
    default:
        return forward(a, irp);
    }
}

static NT_API NT_STATUS dispatch(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    DxgkAdapter *a = dev->DeviceExtension ? *(DxgkAdapter **)dev->DeviceExtension : NULL;
    if (!a) return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST);
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_PNP:            return adapter_pnp(a, irp, sl);
    case NT_IRP_MJ_POWER:
    case NT_IRP_MJ_SYSTEM_CONTROL: return forward(a, irp);
    case NT_IRP_MJ_CREATE:
    case NT_IRP_MJ_CLOSE:
    case NT_IRP_MJ_CLEANUP:        return complete(irp, NT_STATUS_SUCCESS);
    default:                       return complete(irp, NT_STATUS_INVALID_DEVICE_REQUEST);
    }
}

static NT_API NT_STATUS add_device(NT_DRIVER_OBJECT *obj, NT_DEVICE_OBJECT *pdo)
{
    NtPnpLineOnly(pdo);                                   /* (no interrupts connected by this host yet) */
    DxgkDriver *d = driver_of(obj);
    if (!d) return NT_STATUS_INVALID_DEVICE_REQUEST;
    DxgkAdapter *a = kzalloc(sizeof(*a));
    if (!a) return NT_STATUS_INSUFFICIENT_RESOURCES;
    NT_DEVICE_OBJECT *fdo;
    NT_STATUS st = nt_IoCreateDevice(obj, sizeof(DxgkAdapter *), NULL, 0x23 /* FILE_DEVICE_VIDEO */, 0, 0, &fdo);
    if (!NT_SUCCESS_(st)) { kfree(a); return st; }
    *(DxgkAdapter **)fdo->DeviceExtension = a;
    a->drv = d;
    a->fdo = fdo;
    a->pdo = pdo;
    nt_KeInitializeEvent(&a->lock, NT_SynchronizationEvent, 1);
    nt_KeInitializeDpc(&a->dpc, dpc_routine, a);
    st = d->ddi.DxgkDdiAddDevice(pdo, &a->ctx);
    if (!NT_SUCCESS_(st)) {
        kprintf("[DXGK] %s: DxgkDdiAddDevice failed (0x%08x)\n", d->service, (UINT32)st);
        nt_IoDeleteDevice(fdo);
        kfree(a);
        return st;
    }
    a->lower = nt_IoAttachDeviceToDeviceStack(fdo, pdo);
    if (!a->lower) {
        d->ddi.DxgkDdiRemoveDevice(a->ctx);
        nt_IoDeleteDevice(fdo);
        kfree(a);
        return NT_STATUS_UNSUCCESSFUL;
    }
    a->next = g_adapters;
    g_adapters = a;
    d->adapters++;
    fdo->Flags &= ~NT_DO_DEVICE_INITIALIZING;
    return NT_STATUS_SUCCESS;
}

static NT_API void driver_unload(NT_DRIVER_OBJECT *obj)
{
    DxgkDriver *d = driver_of(obj);
    if (!d) return;
    if (d->ddi.DxgkDdiUnload) d->ddi.DxgkDdiUnload();
    for (DxgkDriver **pp = &g_drivers; *pp; pp = &(*pp)->next)
        if (*pp == d) { *pp = d->next; break; }
    kfree(d);
}

/* -----------------------------------------------------------------------
 * \Device\DxgKrnl and the initialisation function
 * ----------------------------------------------------------------------- */
static NT_API NT_STATUS dpi_initialize(NT_DRIVER_OBJECT *obj, NT_UNICODE_STRING *reg, void *init)
{
    const KMDDOD_INITIALIZATION_DATA *dod = init;
    if (!obj || !dod) return NT_STATUS_INVALID_PARAMETER;
    NtDriver *nd = NtIoDriverOf(obj);
    const char *svc = nd ? nd->service : "display";
    /* (a display-only driver's table, KMDDOD_INITIALIZATION_DATA, from
     * Windows 8's interface on: the full DRIVER_INITIALIZATION_DATA of a
     * render driver is step 2's) */
    if (dod->Version < DXGKDDI_INTERFACE_VERSION_WIN8) {
        kprintf("[DXGK] %s: DDI version 0x%x is older than Windows 8's display-only interface\n", svc, dod->Version);
        return NT_STATUS_NOT_SUPPORTED;
    }
    if (!dod->DxgkDdiAddDevice || !dod->DxgkDdiStartDevice || !dod->DxgkDdiStopDevice || !dod->DxgkDdiRemoveDevice ||
        !dod->DxgkDdiQueryChildRelations || !dod->DxgkDdiQueryChildStatus || !dod->DxgkDdiIsSupportedVidPn ||
        !dod->DxgkDdiEnumVidPnCofuncModality || !dod->DxgkDdiCommitVidPn || !dod->DxgkDdiPresentDisplayOnly) {
        kprintf("[DXGK] %s: the driver's DDI table lacks a function a display-only driver must have\n", svc);
        return NT_STATUS_INVALID_PARAMETER;
    }
    DxgkDriver *d = driver_of(obj);
    if (!d) {
        d = kzalloc(sizeof(*d));
        if (!d) return NT_STATUS_INSUFFICIENT_RESOURCES;
        d->obj = obj;
        d->next = g_drivers;
        g_drivers = d;
    }
    d->ddi = *dod;
    strncpy(d->service, svc, sizeof(d->service) - 1);
    UINT16 n = reg && reg->Buffer ? reg->Length / 2 : 0;
    if (n > 159) n = 159;
    for (UINT16 i = 0; i < n; i++) d->regbuf[i] = reg->Buffer[i];
    d->regpath.Buffer = d->regbuf;
    d->regpath.Length = (UINT16)(n * 2);
    d->regpath.MaximumLength = sizeof(d->regbuf);
    obj->DriverExtension->AddDevice = add_device;
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) obj->MajorFunction[i] = dispatch;
    obj->DriverUnload = driver_unload;
    kprintf("[DXGK] %s: display-only miniport registered (DDI version 0x%x)\n", d->service, dod->Version);
    return NT_STATUS_SUCCESS;
}

static NT_API NT_STATUS dxgk_dispatch(NT_DEVICE_OBJECT *dev, NT_IRP *irp)
{
    (void)dev;
    NT_IO_STACK_LOCATION *sl = NtIrpCurrentStack(irp);
    NT_STATUS st = NT_STATUS_INVALID_DEVICE_REQUEST;
    switch (sl->MajorFunction) {
    case NT_IRP_MJ_CREATE:
    case NT_IRP_MJ_CLOSE:
    case NT_IRP_MJ_CLEANUP:
        st = NT_STATUS_SUCCESS;
        break;
    case 0x0F:                                        /* IRP_MJ_INTERNAL_DEVICE_CONTROL */
        if (sl->Parameters.DeviceIoControl.IoControlCode == IOCTL_VIDEO_DDI_FUNC_REGISTER && irp->UserBuffer &&
            sl->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(DXGKRNL_DPIINITIALIZE)) {
            *(DXGKRNL_DPIINITIALIZE *)irp->UserBuffer = dpi_initialize;
            irp->IoStatus.Information = sizeof(DXGKRNL_DPIINITIALIZE);
            st = NT_STATUS_SUCCESS;
        }
        break;
    }
    return complete(irp, st);
}

void NtDxgkInit(void)
{
    g_dxgk.Type = 4;
    g_dxgk.Size = (short)sizeof(g_dxgk);
    for (int i = 0; i <= NT_IRP_MJ_MAXIMUM_FUNCTION; i++) g_dxgk.MajorFunction[i] = dxgk_dispatch;
    static NT_WCHAR name[] = { '\\', 'D', 'e', 'v', 'i', 'c', 'e', '\\', 'D', 'x', 'g', 'K', 'r', 'n', 'l' };
    NT_UNICODE_STRING u = { sizeof(name), sizeof(name), name };
    NT_DEVICE_OBJECT *dev;
    if (!NT_SUCCESS_(nt_IoCreateDevice(&g_dxgk, 0, &u, 0x23 /* FILE_DEVICE_VIDEO */, 0, 0, &dev))) {
        kprintf("[DXGK] \\Device\\DxgKrnl could not be made\n");
        return;
    }
    dev->Flags &= ~NT_DO_DEVICE_INITIALIZING;
}
