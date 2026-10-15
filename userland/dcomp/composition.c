/*
 * Windows.UI.Composition — the system compositor's Windows Runtime API, in
 * dcomp.dll as on Windows (its DllGetActivationFactory serves
 * Windows.UI.Composition.Compositor; the registry's ActivatableClassId key,
 * kernel/um/um_registry.c, names this DLL).  This is what the XAML
 * framework (Windows.UI.Xaml) draws with, and what WinRT desktop programs
 * use through ICompositorDesktopInterop.
 *
 * What is here: the Compositor; container and sprite visuals with their
 * offset, size, anchor and centre points, scale, rotation (about the Z
 * axis), 2-D transform matrix, opacity, visibility and inset clip, and a
 * visual collection (InsertAtTop/AtBottom/Above/Below, Remove, RemoveAll);
 * colour brushes and surface brushes (stretch None, Fill, Uniform and
 * UniformToFill with alignment ratios); targets on a window (the desktop
 * target, ICompositorDesktopInterop) or on the thread's CoreWindow
 * (CreateTargetForCurrentView); graphics devices on a Direct3D 11 device
 * (ICompositorInterop::CreateGraphicsDevice) and their drawing surfaces,
 * drawn through ICompositionDrawingSurfaceInterop's BeginDraw (the
 * surface's ID3D11Texture2D or IDXGISurface) and EndDraw, which reads the
 * pixels back as dcomp.dll's own surfaces do; and surfaces showing a
 * composition swap chain of dxgi.dll's (CreateCompositionSurfaceForSwapChain).
 *
 * Changes take effect as on Windows, with no Commit: each one wakes the
 * compositor's thread, which draws every target's tree in software (pixel
 * centres mapped back through each visual's transform, brushes sampled
 * nearest, premultiplied alpha blended "source over") and copies the
 * picture into the target's window with the GDI.  The parts of the window
 * no visual covers show the window class's background colour (black when
 * it has none).  A target's window is subclassed so that the compositor
 * draws again after the window paints or changes size.
 *
 * Not implemented: animations (StartAnimation is accepted and does
 * nothing), expression animations, property sets, effects, scoped and
 * commit batches, perspective and 3-D rotation (only the matrix's 2-D part
 * and the Z rotation are used), clips other than an inset clip (and a clip
 * under a rotation is its bounding box), bilinear sampling, ID2D1Device
 * graphics devices (NovaOS's Direct2D has no device contexts yet) and
 * surfaces from handles.
 */
#include <windows.h>
#include <roapi.h>
#include <novadx.h>

#define COMP_API __declspec(dllexport)
typedef BYTE boolean_t;

#define E_ILLEGAL_METHOD_CALL_ ((HRESULT)0x8000000EL)
#define RO_E_CLOSED_           ((HRESULT)0x80000013L)
#define DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED_     ((HRESULT)0x88980801L)
#define DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED_ ((HRESULT)0x88980802L)
#define D3D11_BIND_SHADER_RESOURCE_  0x8
#define D3D11_BIND_RENDER_TARGET_    0x20
#define D3D11_USAGE_STAGING_         3
#define D3D11_CPU_ACCESS_READ_       0x20000
#define MAX_TARGETS 64
#define MAX_DEPTH   64

static const GUID IID_IUnknown_  = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_ = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_ = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IClosable_ = { 0x30D5A829, 0x7FA4, 0x4026, { 0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E } };
static const GUID IID_ICompositor_ = { 0xB403CA50, 0x7F8C, 0x4E83, { 0x98, 0x5F, 0xCC, 0x45, 0x06, 0x00, 0x36, 0xD8 } };
static const GUID IID_ICompositorInterop_ = { 0x25297D5C, 0x3AD4, 0x4C9C, { 0xB5, 0xCF, 0xE3, 0x6A, 0x38, 0x51, 0x23, 0x30 } };
static const GUID IID_ICompositorDesktopInterop_ = { 0x29E691FA, 0x4567, 0x4DCA, { 0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07 } };
static const GUID IID_ICompositionObject_ = { 0xBCB4AD45, 0x7609, 0x4550, { 0x93, 0x4F, 0x16, 0x00, 0x2A, 0x68, 0xFD, 0xED } };
static const GUID IID_IVisual_ = { 0x117E202D, 0xA859, 0x4C89, { 0x87, 0x3B, 0xC2, 0xAA, 0x56, 0x67, 0x88, 0xE3 } };
static const GUID IID_IContainerVisual_ = { 0x02F6BC74, 0xED20, 0x4773, { 0xAF, 0xE6, 0xD4, 0x9B, 0x4A, 0x93, 0xDB, 0x32 } };
static const GUID IID_ISpriteVisual_ = { 0x08E05581, 0x1AD1, 0x4F97, { 0x97, 0x57, 0x40, 0x2D, 0x76, 0xE4, 0x23, 0x3B } };
static const GUID IID_IVisualCollection_ = { 0x8B745505, 0xFD3E, 0x4A98, { 0x84, 0xA8, 0xE9, 0x49, 0x46, 0x8C, 0x6B, 0xCB } };
static const GUID IID_ICompositionBrush_ = { 0xAB0D7608, 0x30C0, 0x40E9, { 0xB5, 0x68, 0xB6, 0x0A, 0x6B, 0xD1, 0xFB, 0x46 } };
static const GUID IID_ICompositionColorBrush_ = { 0x2B264C5E, 0xBF35, 0x4831, { 0x86, 0x42, 0xCF, 0x70, 0xC2, 0x0F, 0xFF, 0x2F } };
static const GUID IID_ICompositionSurfaceBrush_ = { 0xAD016D79, 0x1E4C, 0x4C0D, { 0x9C, 0x29, 0x83, 0x33, 0x8C, 0x87, 0xC1, 0x62 } };
static const GUID IID_ICompositionClip_ = { 0x1CCD2A52, 0xCFC7, 0x4ACE, { 0x99, 0x83, 0x14, 0x6B, 0xB8, 0xEB, 0x6A, 0x3C } };
static const GUID IID_IInsetClip_ = { 0x1E73E647, 0x84C7, 0x477A, { 0xB4, 0x74, 0x58, 0x80, 0xE0, 0x44, 0x2E, 0x15 } };
static const GUID IID_ICompositionTarget_ = { 0xA1BEA8BA, 0xD726, 0x4663, { 0x81, 0x29, 0x6B, 0x5E, 0x79, 0x27, 0xFF, 0xA6 } };
static const GUID IID_IDesktopWindowTarget_ = { 0x6329D6CA, 0x3366, 0x490E, { 0x9D, 0xB3, 0x25, 0x31, 0x29, 0x29, 0xAC, 0x51 } };
static const GUID IID_ICompositionGraphicsDevice_ = { 0xFB22C6E1, 0x80A2, 0x4667, { 0x99, 0x36, 0xDB, 0xEA, 0xF6, 0xEE, 0xFE, 0x95 } };
static const GUID IID_ICompositionGraphicsDeviceInterop_ = { 0xA116FF71, 0xF8BF, 0x4C8A, { 0x9C, 0x98, 0x70, 0x77, 0x9A, 0x32, 0xA9, 0xC8 } };
static const GUID IID_ICompositionDrawingSurface_ = { 0xA166C300, 0xFAD0, 0x4D11, { 0x9E, 0x67, 0xE4, 0x33, 0x16, 0x2F, 0xF4, 0x9E } };
static const GUID IID_ICompositionDrawingSurfaceInterop_ = { 0xFD04E6E3, 0xFE0C, 0x4C3C, { 0xAB, 0x19, 0xA0, 0x76, 0x01, 0xA5, 0x76, 0xEE } };
static const GUID IID_ICompositionSurface_ = { 0x1527540D, 0x42C7, 0x47A6, { 0xA4, 0x08, 0x66, 0x8F, 0x79, 0xA9, 0x0D, 0xFB } };
static const GUID IID_ICoreWindowStatic_ = { 0x4D239005, 0x3C2A, 0x41B1, { 0x90, 0x22, 0x53, 0x6B, 0xB9, 0xCF, 0x93, 0xB1 } };
static const GUID IID_ICoreWindowInterop_ = { 0x45D64A29, 0xA63E, 0x4CB6, { 0xB4, 0x98, 0x57, 0x81, 0xD2, 0x98, 0xCB, 0x4F } };
/* (NovaOS's own: hands back the object behind any of its interfaces) */
static const GUID IID_INovaCompositionObject = { 0x6e6f7661, 0x0010, 0x4358, { 0x91, 0x00, 0x63, 0x6f, 0x6d, 0x70, 0x6f, 0x73 } };

typedef struct { float X, Y; } Vec2;
typedef struct { float X, Y, Z; } Vec3;
typedef struct { float X, Y, Z, W; } Quat;
typedef struct { float m[4][4]; } Mat4;             /* M11 ... M44 */
typedef struct { BYTE A, R, G, B; } Color;          /* Windows.UI.Color */
typedef struct { float Width, Height; } SizeF;
typedef struct { float a, b, c, d, tx, ty; } Aff;   /* [x y 1] * [a b; c d; tx ty] */

/* ---- objects ---------------------------------------------------------------
 * Every object is an Obj: up to five interface pointers (vt[i] is the
 * vtable of the interface at slot i, so an interface pointer is &o->vt[i]),
 * then its state.  Slots 0 and 1 of a composition object are
 * ICompositionObject and IClosable; 2-4 depend on the kind. */
enum { K_COMPOSITOR, K_CONTAINER, K_SPRITE, K_COLLECTION, K_COLOR_BRUSH, K_SURFACE_BRUSH, K_INSET_CLIP,
       K_TARGET, K_GRAPHICS, K_DRAWING_SURFACE, K_CHAIN_SURFACE, K_COUNT };
#define NIF 5
typedef struct Obj Obj;
struct Obj {
    const void *const *vt[NIF];
    LONG refs;
    int kind;
    Obj *comp;                          /* the compositor (counted; NULL: this is it) */
    BOOL closed;
    /* visuals */
    Obj *parent;                        /* (not counted) */
    Obj *target_of;                     /* the target this visual is the root of (not counted) */
    Obj **kids;                         /* bottom first (counted) */
    int nkids, capkids;
    Vec2 anchor, size;
    Vec3 center, offset, scale, axis;
    Quat orient;
    float angle, opacity;
    boolean_t visible;
    int backface, border, composite;
    Mat4 tm;
    Obj *clip, *brush;                  /* (counted) */
    Obj *of;                            /* a collection's visual (counted) */
    /* brushes and clips */
    Color color;
    Obj *surface;                       /* (counted) */
    int stretch, interp;
    float hratio, vratio;
    float inset[4];                     /* left, top, right, bottom */
    /* targets */
    HWND hwnd;
    Obj *root;                          /* (counted) */
    boolean_t topmost;
    DWORD *pic;                         /* the window's picture, pw x ph */
    int pw, ph;
    /* graphics devices and drawing surfaces */
    IUnknown *d3d, *ctx;                /* (counted) */
    IUnknown *tex, *staging;
    UINT w, h, format;
    int alpha;                          /* DirectXAlphaMode */
    BYTE *frame;                        /* BGRA, w * 4 bytes a row */
    BOOL drawing;
    RECT update;
    INovaSwapChain *chain;              /* (counted) */
};
#define OBJ_AT(p, i) ((Obj *)((char *)(p) - (i) * sizeof(void *)))
#define IF(o, i) ((void *)&(o)->vt[i])

static CRITICAL_SECTION g_cs;
static Obj *g_targets[MAX_TARGETS];
static HANDLE g_wake;                   /* the compositor's thread's event */
static BOOL g_thread;

static void lock(void) { EnterCriticalSection(&g_cs); }
static void unlock(void) { LeaveCriticalSection(&g_cs); }

/* something shown changed: the compositor draws again */
static void schedule(void) { if (g_wake) SetEvent(g_wake); }

static HRESULT make_hstring(const WCHAR *s, HSTRING *out)
{
    if (!out) return E_POINTER;
    UINT32 n = 0;
    while (s[n]) n++;
    return WindowsCreateString(s, n, out);
}

typedef struct { const GUID *iid; int slot; } Iface;
static const Iface if_compositor[] = { { &IID_ICompositor_, 0 }, { &IID_IClosable_, 1 }, { &IID_ICompositorInterop_, 2 },
                                       { &IID_ICompositorDesktopInterop_, 3 }, { 0, 0 } };
static const Iface if_container[] = { { &IID_IContainerVisual_, 3 }, { &IID_IVisual_, 2 }, { 0, 0 } };
static const Iface if_sprite[] = { { &IID_ISpriteVisual_, 4 }, { &IID_IContainerVisual_, 3 }, { &IID_IVisual_, 2 }, { 0, 0 } };
static const Iface if_collection[] = { { &IID_IVisualCollection_, 2 }, { 0, 0 } };
static const Iface if_color[] = { { &IID_ICompositionColorBrush_, 3 }, { &IID_ICompositionBrush_, 2 }, { 0, 0 } };
static const Iface if_surface_brush[] = { { &IID_ICompositionSurfaceBrush_, 3 }, { &IID_ICompositionBrush_, 2 }, { 0, 0 } };
static const Iface if_inset[] = { { &IID_IInsetClip_, 3 }, { &IID_ICompositionClip_, 2 }, { 0, 0 } };
static const Iface if_target[] = { { &IID_ICompositionTarget_, 2 }, { 0, 0 } };
static const Iface if_desktop_target[] = { { &IID_IDesktopWindowTarget_, 3 }, { &IID_ICompositionTarget_, 2 }, { 0, 0 } };
static const Iface if_graphics[] = { { &IID_ICompositionGraphicsDevice_, 2 }, { &IID_ICompositionGraphicsDeviceInterop_, 3 }, { 0, 0 } };
static const Iface if_drawing[] = { { &IID_ICompositionDrawingSurface_, 2 }, { &IID_ICompositionDrawingSurfaceInterop_, 3 },
                                    { &IID_ICompositionSurface_, 4 }, { 0, 0 } };
static const Iface if_chain[] = { { &IID_ICompositionSurface_, 2 }, { 0, 0 } };
/* the slot of the runtime class's default interface */
static int default_slot(Obj *o)
{
    switch (o->kind) {
    case K_SPRITE: return 4;
    case K_CONTAINER: case K_COLOR_BRUSH: case K_SURFACE_BRUSH: case K_INSET_CLIP: return 3;
    case K_TARGET: return o->vt[3] ? 3 : 2;
    case K_COMPOSITOR: return 0;
    }
    return 2;
}
static const Iface *ifaces(Obj *o)
{
    switch (o->kind) {
    case K_COMPOSITOR: return if_compositor;
    case K_CONTAINER: return if_container;
    case K_SPRITE: return if_sprite;
    case K_COLLECTION: return if_collection;
    case K_COLOR_BRUSH: return if_color;
    case K_SURFACE_BRUSH: return if_surface_brush;
    case K_INSET_CLIP: return if_inset;
    case K_TARGET: return o->vt[3] ? if_desktop_target : if_target;
    case K_GRAPHICS: return if_graphics;
    case K_DRAWING_SURFACE: return if_drawing;
    case K_CHAIN_SURFACE: return if_chain;
    }
    return if_target;
}

static ULONG obj_addref(Obj *o) { return (ULONG)InterlockedIncrement(&o->refs); }
static void obj_free(Obj *o);
static ULONG obj_release(Obj *o)
{
    LONG r = InterlockedDecrement(&o->refs);
    if (!r) obj_free(o);
    return (ULONG)r;
}
static HRESULT obj_qi(Obj *o, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (IsEqualGUID(iid, &IID_INovaCompositionObject)) { *out = o; obj_addref(o); return S_OK; }
    int slot = -1;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_))
        slot = default_slot(o);
    else if (o->kind != K_COMPOSITOR && IsEqualGUID(iid, &IID_ICompositionObject_)) slot = 0;
    else if (o->kind != K_COMPOSITOR && IsEqualGUID(iid, &IID_IClosable_)) slot = 1;
    else
        for (const Iface *f = ifaces(o); f->iid; f++)
            if (IsEqualGUID(iid, f->iid)) { slot = f->slot; break; }
    if (slot < 0) return E_NOINTERFACE;
    *out = IF(o, slot);
    obj_addref(o);
    return S_OK;
}
static HRESULT obj_name(Obj *o, HSTRING *n)
{
    static const WCHAR *const names[K_COUNT] = {
        L"Windows.UI.Composition.Compositor", L"Windows.UI.Composition.ContainerVisual", L"Windows.UI.Composition.SpriteVisual",
        L"Windows.UI.Composition.VisualCollection", L"Windows.UI.Composition.CompositionColorBrush",
        L"Windows.UI.Composition.CompositionSurfaceBrush", L"Windows.UI.Composition.InsetClip",
        L"Windows.UI.Composition.CompositionTarget", L"Windows.UI.Composition.CompositionGraphicsDevice",
        L"Windows.UI.Composition.CompositionDrawingSurface", L"Windows.UI.Composition.CompositionVisualSurface",
    };
    if (o->kind == K_TARGET && o->vt[3]) return make_hstring(L"Windows.UI.Composition.Desktop.DesktopWindowTarget", n);
    return make_hstring(names[o->kind], n);
}
static HRESULT STDMETHODCALLTYPE no_iids(void *p, ULONG *n, GUID **iids) { (void)p; if (!n || !iids) return E_POINTER; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *p, int *t) { (void)p; if (!t) return E_POINTER; *t = BaseTrust; return S_OK; }

#define THUNKS(i) \
    static HRESULT STDMETHODCALLTYPE qi##i(void *p, REFIID iid, void **out) { return obj_qi(OBJ_AT(p, i), iid, out); } \
    static ULONG STDMETHODCALLTYPE ar##i(void *p) { return obj_addref(OBJ_AT(p, i)); } \
    static ULONG STDMETHODCALLTYPE rl##i(void *p) { return obj_release(OBJ_AT(p, i)); } \
    static HRESULT STDMETHODCALLTYPE nm##i(void *p, HSTRING *s) { return obj_name(OBJ_AT(p, i), s); }
THUNKS(0) THUNKS(1) THUNKS(2) THUNKS(3) THUNKS(4)
#define BASE(i) (void *)qi##i, (void *)ar##i, (void *)rl##i, (void *)no_iids, (void *)nm##i, (void *)base_trust
#define UNK(i) (void *)qi##i, (void *)ar##i, (void *)rl##i

/* The object behind an interface pointer of ours (counted), or NULL */
static Obj *obj_of(void *p, int kind1, int kind2)
{
    Obj *o = 0;
    if (!p || FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, REFIID, void **))(*(void ***)p)[0])(p, &IID_INovaCompositionObject, (void **)&o)) || !o)
        return 0;
    if (kind1 >= 0 && o->kind != kind1 && o->kind != kind2) { obj_release(o); return 0; }
    return o;
}
static BOOL is_visual(Obj *o) { return o && (o->kind == K_CONTAINER || o->kind == K_SPRITE); }

/* *@out: @o's interface at @slot, counted (NULL stays NULL) */
static HRESULT give(Obj *o, int slot, void **out)
{
    if (!out) return E_POINTER;
    *out = o ? IF(o, slot) : 0;
    if (o) obj_addref(o);
    return S_OK;
}
/* replaces the counted *@field with @o (counted by the caller) */
static void swap_ref(Obj **field, Obj *o)
{
    lock();
    Obj *old = *field;
    *field = o;
    unlock();
    if (old) obj_release(old);
    schedule();
}

static Obj *obj_new(Obj *comp, int kind)
{
    Obj *o = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *o);
    if (!o) return 0;
    o->refs = 1;
    o->kind = kind;
    if (comp) { o->comp = comp; obj_addref(comp); }
    return o;
}

/* ---- maths ----------------------------------------------------------------- */
static double c_sin(double x)
{
    const double pi = 3.14159265358979323846;
    int k = (int)(x / (2 * pi));
    x -= k * 2 * pi;
    if (x > pi) x -= 2 * pi;
    if (x < -pi) x += 2 * pi;
    if (x > pi / 2) x = pi - x;
    if (x < -pi / 2) x = -pi - x;
    double x2 = x * x, t = x, s = x;
    for (int i = 1; i <= 6; i++) { t *= -x2 / ((2 * i) * (2 * i + 1)); s += t; }
    return s;
}
static double c_cos(double x) { return c_sin(x + 3.14159265358979323846 / 2); }
static float c_abs(float f) { return f < 0 ? -f : f; }

static Aff aff_mul(Aff p, Aff q)                    /* p, then q */
{
    Aff r = { p.a * q.a + p.b * q.c, p.a * q.b + p.b * q.d, p.c * q.a + p.d * q.c, p.c * q.b + p.d * q.d,
              p.tx * q.a + p.ty * q.c + q.tx, p.tx * q.b + p.ty * q.d + q.ty };
    return r;
}
static Aff aff_move(float x, float y) { Aff r = { 1, 0, 0, 1, x, y }; return r; }
static void aff_apply(const Aff *m, float x, float y, float *ox, float *oy)
{
    *ox = x * m->a + y * m->c + m->tx;
    *oy = x * m->b + y * m->d + m->ty;
}

/* A visual's own transform: about its centre point scaled, rotated and
 * transformed by its matrix, then moved to its offset with its anchor
 * point there */
static Aff visual_matrix(Obj *v)
{
    Aff m = aff_move(-v->center.X, -v->center.Y);
    Aff s = { v->scale.X, 0, 0, v->scale.Y, 0, 0 };
    m = aff_mul(m, s);
    if (v->angle != 0.0f && v->axis.X == 0.0f && v->axis.Y == 0.0f) {
        float k = (float)c_cos(v->axis.Z < 0 ? -v->angle : v->angle), n = (float)c_sin(v->axis.Z < 0 ? -v->angle : v->angle);
        Aff r = { k, n, -n, k, 0, 0 };
        m = aff_mul(m, r);
    }
    m = aff_mul(m, aff_move(v->center.X, v->center.Y));
    Aff t = { v->tm.m[0][0], v->tm.m[0][1], v->tm.m[1][0], v->tm.m[1][1], v->tm.m[3][0], v->tm.m[3][1] };
    m = aff_mul(m, t);
    return aff_mul(m, aff_move(v->offset.X - v->anchor.X * v->size.X, v->offset.Y - v->anchor.Y * v->size.Y));
}

/* the bounding box (pixels) of the rectangle (@l, @t)-(@r, @b) under @m */
static RECT aff_box(const Aff *m, float l, float t, float r, float b)
{
    float xs[4], ys[4];
    aff_apply(m, l, t, &xs[0], &ys[0]);
    aff_apply(m, r, t, &xs[1], &ys[1]);
    aff_apply(m, l, b, &xs[2], &ys[2]);
    aff_apply(m, r, b, &xs[3], &ys[3]);
    float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < 4; i++) {
        if (xs[i] < x0) x0 = xs[i];
        if (xs[i] > x1) x1 = xs[i];
        if (ys[i] < y0) y0 = ys[i];
        if (ys[i] > y1) y1 = ys[i];
    }
    const float big = 1.0e6f;
    if (x0 < -big) x0 = -big;
    if (y0 < -big) y0 = -big;
    if (x1 > big) x1 = big;
    if (y1 > big) y1 = big;
    RECT box = { (LONG)(x0 + 0.5f) - (x0 + 0.5f < 0), (LONG)(y0 + 0.5f) - (y0 + 0.5f < 0), (LONG)(x1 + 0.5f), (LONG)(y1 + 0.5f) };
    return box;
}

/* ---- drawing ------------------------------------------------------------------ */
static DWORD premul(Color c, float op)
{
    UINT a = (UINT)(int)(c.A * op + 0.5f);
    return a << 24 | (c.R * a + 127) / 255 << 16 | (c.G * a + 127) / 255 << 8 | (c.B * a + 127) / 255;
}
/* source over: @s (premultiplied BGRA) onto @d */
static DWORD over(DWORD s, DWORD d)
{
    UINT a = s >> 24;
    if (a == 255) return s;
    if (!a) return d;
    UINT k = 255 - a;
    UINT rb = ((d & 0xFF00FF) * k + 0x800080) >> 8 & 0xFF00FF;
    UINT g = ((d >> 8 & 0xFF00FF) * k + 0x800080) & 0xFF00FF00;
    return s + (rb | g);
}
static DWORD scale_px(DWORD s, UINT op)             /* (op 0-256) */
{
    if (op >= 256) return s;
    return ((s & 0xFF00FF) * op >> 8 & 0xFF00FF) | ((s >> 8 & 0xFF00FF) * op & 0xFF00FF00);
}

/* A surface brush's surface: its pixels (BGRA), size and whether its alpha
 * means anything */
static const BYTE *surface_pixels(Obj *s, UINT *pitch, UINT *w, UINT *h, BOOL *opaque)
{
    if (!s) return 0;
    if (s->kind == K_DRAWING_SURFACE && s->frame) {
        *w = s->w; *h = s->h; *pitch = s->w * 4;
        *opaque = s->alpha == 3 || s->format == NDXGI_FORMAT_B8G8R8X8_UNORM;
        return s->frame;
    }
    if (s->kind == K_CHAIN_SURFACE && s->chain) {
        *opaque = TRUE;                 /* (a swap chain's frames are shown opaque) */
        return s->chain->lpVtbl->Frame(s->chain, pitch, w, h);
    }
    return 0;
}

/* @v (a sprite) filled with its brush in @pic (@pw x @ph), through @m,
 * inside @clip, at opacity @op */
static void fill(Obj *v, const Aff *m, RECT clip, float op, DWORD *pic, int pw, int ph)
{
    Obj *b = v->brush;
    if (!b || v->size.X <= 0 || v->size.Y <= 0) return;
    float det = m->a * m->d - m->b * m->c;
    if (c_abs(det) < 1e-9f) return;
    RECT box = aff_box(m, 0, 0, v->size.X, v->size.Y), all = { 0, 0, pw, ph };
    if (!IntersectRect(&box, &box, &clip) || !IntersectRect(&box, &box, &all)) return;
    DWORD solid = 0;
    const BYTE *bits = 0;
    UINT pitch = 0, sw = 0, sh = 0;
    BOOL opaque = FALSE;
    float cx = 0, cy = 0, cw = v->size.X, ch = v->size.Y;      /* the surface's place in the visual */
    UINT opi = (UINT)(int)(op * 256.0f + 0.5f);
    if (b->kind == K_COLOR_BRUSH) {
        solid = premul(b->color, op);
        if (!(solid >> 24)) return;
    } else if (b->kind == K_SURFACE_BRUSH) {
        bits = surface_pixels(b->surface, &pitch, &sw, &sh, &opaque);
        if (!bits || !sw || !sh) return;
        float kx = v->size.X / sw, ky = v->size.Y / sh;
        switch (b->stretch) {
        case 0: cw = (float)sw; ch = (float)sh; break;                                  /* None */
        case 2: { float k = kx < ky ? kx : ky; cw = sw * k; ch = sh * k; break; }       /* Uniform */
        case 3: { float k = kx > ky ? kx : ky; cw = sw * k; ch = sh * k; break; }       /* UniformToFill */
        }
        cx = (v->size.X - cw) * b->hratio;
        cy = (v->size.Y - ch) * b->vratio;
    } else return;
    /* each pixel's centre, back in the visual's space */
    float ia = m->d / det, ib = -m->b / det, ic = -m->c / det, id = m->a / det;
    for (LONG y = box.top; y < box.bottom; y++) {
        float X = box.left + 0.5f - m->tx, Y = y + 0.5f - m->ty;
        float u = X * ia + Y * ic, w = X * ib + Y * id;
        DWORD *row = pic + (size_t)y * pw;
        for (LONG x = box.left; x < box.right; x++, u += ia, w += ib) {
            if (u < 0 || w < 0 || u >= v->size.X || w >= v->size.Y) continue;
            DWORD s = solid;
            if (bits) {
                float su = (u - cx) * sw / cw, sv = (w - cy) * sh / ch;
                if (su < 0 || sv < 0 || su >= sw || sv >= sh) continue;
                s = ((const DWORD *)(bits + (size_t)(int)sv * pitch))[(int)su];
                if (opaque) s |= 0xFF000000u;
                s = scale_px(s, opi);
            }
            row[x] = over(s, row[x]);
        }
    }
}

static void draw_visual(Obj *v, Aff parent, RECT clip, float op, DWORD *pic, int pw, int ph, int depth)
{
    if (!v || depth > MAX_DEPTH || !v->visible || v->opacity <= 0.0f) return;
    Aff m = aff_mul(visual_matrix(v), parent);
    op *= v->opacity > 1.0f ? 1.0f : v->opacity;
    if (v->clip && v->clip->kind == K_INSET_CLIP) {
        const float *k = v->clip->inset;
        RECT c = aff_box(&m, k[0], k[1], v->size.X - k[2], v->size.Y - k[3]);
        if (!IntersectRect(&clip, &clip, &c)) return;
    }
    if (v->kind == K_SPRITE) fill(v, &m, clip, op, pic, pw, ph);
    for (int i = 0; i < v->nkids; i++) draw_visual(v->kids[i], m, clip, op, pic, pw, ph, depth + 1);
}

static DWORD window_background(HWND h)
{
    HBRUSH br = (HBRUSH)GetClassLongPtrW(h, GCLP_HBRBACKGROUND);
    if (!br) return 0xFF000000u;
    if ((ULONG_PTR)br <= 31) {
        COLORREF c = GetSysColor((int)(ULONG_PTR)br - 1);
        return 0xFF000000u | GetRValue(c) << 16 | GetGValue(c) << 8 | GetBValue(c);
    }
    LOGBRUSH lb;
    if (GetObjectW(br, sizeof lb, &lb) != sizeof lb || lb.lbStyle != 0 /* BS_SOLID */) return 0xFF000000u;
    return 0xFF000000u | GetRValue(lb.lbColor) << 16 | GetGValue(lb.lbColor) << 8 | GetBValue(lb.lbColor);
}

/* @t's tree drawn into its window (under the lock) */
static void target_draw(Obj *t)
{
    RECT cr;
    if (!t->hwnd || !IsWindow(t->hwnd) || !IsWindowVisible(t->hwnd) || !GetClientRect(t->hwnd, &cr)) return;
    int w = cr.right, h = cr.bottom;
    if (w <= 0 || h <= 0) return;
    if (w != t->pw || h != t->ph || !t->pic) {
        HeapFree(GetProcessHeap(), 0, t->pic);
        t->pic = HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 4);
        t->pw = t->pic ? w : 0; t->ph = t->pic ? h : 0;
        if (!t->pic) return;
    }
    DWORD bg = window_background(t->hwnd);
    for (size_t i = 0, n = (size_t)w * h; i < n; i++) t->pic[i] = bg;
    Aff id = { 1, 0, 0, 1, 0, 0 };
    RECT all = { 0, 0, w, h };
    draw_visual(t->root, id, all, 1.0f, t->pic, w, h, 0);
    HDC dc = GetDC(t->hwnd);
    if (!dc) return;
    novadx_blit(dc, 0, 0, (const BYTE *)t->pic, (UINT)w * 4, (UINT)w, (UINT)h, NULL);
    ReleaseDC(t->hwnd, dc);
}

/* The compositor's thread: draws every target after a change (several
 * changes made together are drawn once) */
static DWORD WINAPI compositor_thread(void *arg)
{
    (void)arg;
    for (;;) {
        WaitForSingleObject(g_wake, INFINITE);
        Sleep(2);
        lock();
        for (int i = 0; i < MAX_TARGETS; i++) if (g_targets[i]) target_draw(g_targets[i]);
        unlock();
    }
    return 0;
}

/* ---- targets' windows -----------------------------------------------------------
 * A target's window is subclassed: after it paints, changes size or is
 * shown, the compositor draws again */
typedef struct { HWND hwnd; WNDPROC old; } Sub;
static Sub g_subs[MAX_TARGETS];
static LRESULT CALLBACK target_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC old = 0;
    lock();
    for (int i = 0; i < MAX_TARGETS; i++) if (g_subs[i].hwnd == h) { old = g_subs[i].old; break; }
    unlock();
    LRESULT r = old ? CallWindowProcW(old, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_PAINT: case WM_SIZE: case WM_SHOWWINDOW: case WM_WINDOWPOSCHANGED: case WM_ERASEBKGND:
        schedule();
        break;
    case WM_NCDESTROY:
        lock();
        for (int i = 0; i < MAX_TARGETS; i++) if (g_subs[i].hwnd == h) g_subs[i].hwnd = 0;
        unlock();
        break;
    }
    return r;
}
static void subclass(HWND h)
{
    for (int i = 0; i < MAX_TARGETS; i++) if (g_subs[i].hwnd == h) return;
    for (int i = 0; i < MAX_TARGETS; i++)
        if (!g_subs[i].hwnd) {
            g_subs[i].old = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
            g_subs[i].hwnd = h;
            SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)target_proc);
            return;
        }
}
static void unsubclass(HWND h)
{
    for (int i = 0; i < MAX_TARGETS; i++) if (g_targets[i] && g_targets[i]->hwnd == h) return;     /* (another target's) */
    for (int i = 0; i < MAX_TARGETS; i++)
        if (g_subs[i].hwnd == h) {
            if (IsWindow(h) && (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC) == target_proc)
                SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)g_subs[i].old);
            g_subs[i].hwnd = 0;
        }
}

/* ---- freeing ------------------------------------------------------------------- */
static void visual_unhook(Obj *kid) { kid->parent = 0; }
static void obj_free(Obj *o)
{
    Obj *drop[8];
    int n = 0;
    lock();
    for (int i = 0; i < o->nkids; i++) visual_unhook(o->kids[i]);
    if (o->kind == K_TARGET) {
        for (int i = 0; i < MAX_TARGETS; i++) if (g_targets[i] == o) g_targets[i] = 0;
        if (o->hwnd) unsubclass(o->hwnd);
        if (o->root) o->root->target_of = 0;
    }
    if (o->chain) o->chain->lpVtbl->SetSink(o->chain, 0, 0);
    drop[n++] = o->clip; drop[n++] = o->brush; drop[n++] = o->of; drop[n++] = o->surface; drop[n++] = o->root;
    unlock();
    for (int i = 0; i < o->nkids; i++) obj_release(o->kids[i]);
    for (int i = 0; i < n; i++) if (drop[i]) obj_release(drop[i]);
    HeapFree(GetProcessHeap(), 0, o->kids);
    HeapFree(GetProcessHeap(), 0, o->pic);
    HeapFree(GetProcessHeap(), 0, o->frame);
    if (o->chain) o->chain->lpVtbl->Release(o->chain);
    if (o->tex) o->tex->lpVtbl->Release(o->tex);
    if (o->staging) o->staging->lpVtbl->Release(o->staging);
    if (o->ctx) o->ctx->lpVtbl->Release(o->ctx);
    if (o->d3d) o->d3d->lpVtbl->Release(o->d3d);
    if (o->comp) obj_release(o->comp);
    HeapFree(GetProcessHeap(), 0, o);
    schedule();
}

/* ---- ICompositionObject and IClosable (slots 0 and 1) ------------------------------ */
static HRESULT STDMETHODCALLTYPE co_compositor(void *p, void **out) { Obj *o = OBJ_AT(p, 0); return give(o->comp, 0, out); }
static HRESULT STDMETHODCALLTYPE co_dispatcher(void *p, void **out) { (void)p; if (!out) return E_POINTER; *out = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE co_properties(void *p, void **out) { (void)p; if (out) *out = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE co_start(void *p, HSTRING name, void *anim) { (void)p; (void)name; (void)anim; return S_OK; }
static HRESULT STDMETHODCALLTYPE co_stop(void *p, HSTRING name) { (void)p; (void)name; return S_OK; }
static const void *const co_vt[] = { BASE(0), (void *)co_compositor, (void *)co_dispatcher, (void *)co_properties,
                                     (void *)co_start, (void *)co_stop };
/* Close: a target stops showing its tree; the rest lets go of what it holds
 * once released */
static HRESULT STDMETHODCALLTYPE cl_close(void *p)
{
    Obj *o = OBJ_AT(p, 1);
    if (o->kind == K_TARGET) {
        lock();
        for (int i = 0; i < MAX_TARGETS; i++) if (g_targets[i] == o) g_targets[i] = 0;
        if (o->hwnd) unsubclass(o->hwnd);
        unlock();
    }
    o->closed = TRUE;
    return S_OK;
}
static const void *const closable_vt[] = { BASE(1), (void *)cl_close };

/* ---- visuals: IVisual (slot 2) ------------------------------------------------------ */
#define V(p) OBJ_AT(p, 2)
#define GETSET(name, field, T) \
    static HRESULT STDMETHODCALLTYPE v_get_##name(void *p, T *out) { if (!out) return E_POINTER; lock(); *out = V(p)->field; unlock(); return S_OK; } \
    static HRESULT STDMETHODCALLTYPE v_put_##name(void *p, T in) { lock(); V(p)->field = in; unlock(); schedule(); return S_OK; }
GETSET(anchor, anchor, Vec2) GETSET(backface, backface, int) GETSET(border, border, int) GETSET(center, center, Vec3)
GETSET(composite, composite, int) GETSET(visible, visible, boolean_t) GETSET(offset, offset, Vec3) GETSET(orient, orient, Quat)
GETSET(angle, angle, float) GETSET(axis, axis, Vec3) GETSET(scale, scale, Vec3) GETSET(size, size, Vec2) GETSET(tm, tm, Mat4)
static HRESULT STDMETHODCALLTYPE v_get_opacity(void *p, float *out) { if (!out) return E_POINTER; *out = V(p)->opacity; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_put_opacity(void *p, float f)
{
    if (!(f >= 0.0f && f <= 1.0f)) return E_INVALIDARG;
    lock(); V(p)->opacity = f; unlock();
    schedule();
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE v_get_degrees(void *p, float *out)
{
    if (!out) return E_POINTER;
    *out = V(p)->angle * (180.0f / 3.14159265f);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE v_put_degrees(void *p, float f) { return v_put_angle(p, f * (3.14159265f / 180.0f)); }
static HRESULT STDMETHODCALLTYPE v_get_clip(void *p, void **out) { lock(); HRESULT hr = give(V(p)->clip, 2, out); unlock(); return hr; }
static HRESULT STDMETHODCALLTYPE v_put_clip(void *p, void *clip)
{
    Obj *c = 0;
    if (clip && !(c = obj_of(clip, K_INSET_CLIP, -1))) return E_INVALIDARG;
    swap_ref(&V(p)->clip, c);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE v_get_parent(void *p, void **out) { lock(); HRESULT hr = give(V(p)->parent, 3, out); unlock(); return hr; }
static const void *const visual_vt[] = {
    BASE(2),
    (void *)v_get_anchor, (void *)v_put_anchor, (void *)v_get_backface, (void *)v_put_backface, (void *)v_get_border,
    (void *)v_put_border, (void *)v_get_center, (void *)v_put_center, (void *)v_get_clip, (void *)v_put_clip,
    (void *)v_get_composite, (void *)v_put_composite, (void *)v_get_visible, (void *)v_put_visible, (void *)v_get_offset,
    (void *)v_put_offset, (void *)v_get_opacity, (void *)v_put_opacity, (void *)v_get_orient, (void *)v_put_orient,
    (void *)v_get_parent, (void *)v_get_angle, (void *)v_put_angle, (void *)v_get_degrees, (void *)v_put_degrees,
    (void *)v_get_axis, (void *)v_put_axis, (void *)v_get_scale, (void *)v_put_scale, (void *)v_get_size, (void *)v_put_size,
    (void *)v_get_tm, (void *)v_put_tm,
};
_Static_assert(sizeof visual_vt / sizeof visual_vt[0] == 6 + 33, "IVisual has 33 methods");

/* IContainerVisual (slot 3): Children, a collection on the visual */
static const void *const co_vt[], *const closable_vt[], *const collection_vt[];
static HRESULT STDMETHODCALLTYPE cv_children(void *p, void **out)
{
    Obj *v = OBJ_AT(p, 3);
    if (!out) return E_POINTER;
    *out = 0;
    Obj *c = obj_new(v->comp, K_COLLECTION);
    if (!c) return E_OUTOFMEMORY;
    c->vt[0] = co_vt; c->vt[1] = closable_vt; c->vt[2] = collection_vt;
    c->of = v;
    obj_addref(v);
    *out = IF(c, 2);
    return S_OK;
}
static const void *const container_vt[] = { BASE(3), (void *)cv_children };

/* ISpriteVisual (slot 4): Brush */
static HRESULT STDMETHODCALLTYPE sv_get_brush(void *p, void **out) { lock(); HRESULT hr = give(OBJ_AT(p, 4)->brush, 2, out); unlock(); return hr; }
static HRESULT STDMETHODCALLTYPE sv_put_brush(void *p, void *brush)
{
    Obj *b = 0;
    if (brush && !(b = obj_of(brush, K_COLOR_BRUSH, K_SURFACE_BRUSH))) return E_INVALIDARG;
    swap_ref(&OBJ_AT(p, 4)->brush, b);
    return S_OK;
}
static const void *const sprite_vt[] = { BASE(4), (void *)sv_get_brush, (void *)sv_put_brush };

/* ---- IVisualCollection (slot 2 of a collection) ------------------------------------- */
#define COLL(p) (OBJ_AT(p, 2)->of)
static HRESULT STDMETHODCALLTYPE vc_count(void *p, INT32 *n) { if (!n) return E_POINTER; *n = COLL(p)->nkids; return S_OK; }
static int kid_index(Obj *v, Obj *kid)
{
    for (int i = 0; i < v->nkids; i++) if (v->kids[i] == kid) return i;
    return -1;
}
/* @kid into @v's children at @at (0: the bottom) */
static HRESULT insert(Obj *v, void *child, int where, void *sibling)
{
    Obj *kid = obj_of(child, K_CONTAINER, K_SPRITE), *sib = 0;
    if (!kid) return E_INVALIDARG;
    if (sibling && !(sib = obj_of(sibling, K_CONTAINER, K_SPRITE))) { obj_release(kid); return E_INVALIDARG; }
    HRESULT hr = S_OK;
    lock();
    int at = where == 0 ? 0 : v->nkids, si = sib ? kid_index(v, sib) : 0;
    for (Obj *a = v; a; a = a->parent) if (a == kid) hr = E_INVALIDARG;                /* (into itself) */
    if (kid->parent || kid->target_of || si < 0) hr = E_INVALIDARG;
    if (SUCCEEDED(hr) && sib) at = where == 2 ? si + 1 : si;                         /* (2: above, 3: below) */
    if (SUCCEEDED(hr) && v->nkids == v->capkids) {
        int cap = v->capkids ? v->capkids * 2 : 8;
        Obj **k = v->kids ? HeapReAlloc(GetProcessHeap(), 0, v->kids, cap * sizeof *k) : HeapAlloc(GetProcessHeap(), 0, cap * sizeof *k);
        if (!k) hr = E_OUTOFMEMORY;
        else { v->kids = k; v->capkids = cap; }
    }
    if (SUCCEEDED(hr)) {
        for (int i = v->nkids; i > at; i--) v->kids[i] = v->kids[i - 1];
        v->kids[at] = kid;                                                           /* (the reference obj_of took) */
        v->nkids++;
        kid->parent = v;
        kid = 0;
    }
    unlock();
    if (kid) obj_release(kid);
    if (sib) obj_release(sib);
    schedule();
    return hr;
}
static HRESULT STDMETHODCALLTYPE vc_above(void *p, void *child, void *sib) { return sib ? insert(COLL(p), child, 2, sib) : E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE vc_bottom(void *p, void *child) { return insert(COLL(p), child, 0, 0); }
static HRESULT STDMETHODCALLTYPE vc_top(void *p, void *child) { return insert(COLL(p), child, 1, 0); }
static HRESULT STDMETHODCALLTYPE vc_below(void *p, void *child, void *sib) { return sib ? insert(COLL(p), child, 3, sib) : E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE vc_remove(void *p, void *child)
{
    Obj *v = COLL(p), *kid = obj_of(child, K_CONTAINER, K_SPRITE);
    if (!kid) return E_INVALIDARG;
    lock();
    int i = kid_index(v, kid);
    if (i >= 0) {
        for (; i + 1 < v->nkids; i++) v->kids[i] = v->kids[i + 1];
        v->nkids--;
        kid->parent = 0;
    }
    unlock();
    if (i >= 0) obj_release(kid);                   /* (the collection's) */
    obj_release(kid);
    schedule();
    return i >= 0 ? S_OK : E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE vc_remove_all(void *p)
{
    Obj *v = COLL(p);
    lock();
    Obj **kids = v->kids;
    int n = v->nkids;
    for (int i = 0; i < n; i++) kids[i]->parent = 0;
    v->kids = 0; v->nkids = v->capkids = 0;
    unlock();
    for (int i = 0; i < n; i++) obj_release(kids[i]);
    HeapFree(GetProcessHeap(), 0, kids);
    schedule();
    return S_OK;
}
static const void *const collection_vt[] = { BASE(2), (void *)vc_count, (void *)vc_above, (void *)vc_bottom, (void *)vc_top,
                                             (void *)vc_below, (void *)vc_remove, (void *)vc_remove_all };

/* ---- brushes and clips --------------------------------------------------------------- */
static const void *const empty2_vt[] = { BASE(2) };                 /* ICompositionBrush, ICompositionClip, ICompositionSurface */
static const void *const empty4_vt[] = { BASE(4) };
static HRESULT STDMETHODCALLTYPE cb_get(void *p, Color *c) { if (!c) return E_POINTER; *c = OBJ_AT(p, 3)->color; return S_OK; }
static HRESULT STDMETHODCALLTYPE cb_put(void *p, Color c) { lock(); OBJ_AT(p, 3)->color = c; unlock(); schedule(); return S_OK; }
static const void *const color_vt[] = { BASE(3), (void *)cb_get, (void *)cb_put };

#define SB(p) OBJ_AT(p, 3)
static HRESULT STDMETHODCALLTYPE sb_get_interp(void *p, int *v) { if (!v) return E_POINTER; *v = SB(p)->interp; return S_OK; }
static HRESULT STDMETHODCALLTYPE sb_put_interp(void *p, int v) { SB(p)->interp = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE sb_get_h(void *p, float *v) { if (!v) return E_POINTER; *v = SB(p)->hratio; return S_OK; }
static HRESULT STDMETHODCALLTYPE sb_put_h(void *p, float v) { if (!(v >= 0 && v <= 1)) return E_INVALIDARG; lock(); SB(p)->hratio = v; unlock(); schedule(); return S_OK; }
static HRESULT STDMETHODCALLTYPE sb_get_stretch(void *p, int *v) { if (!v) return E_POINTER; *v = SB(p)->stretch; return S_OK; }
static HRESULT STDMETHODCALLTYPE sb_put_stretch(void *p, int v) { if (v < 0 || v > 3) return E_INVALIDARG; lock(); SB(p)->stretch = v; unlock(); schedule(); return S_OK; }
static int surface_slot(Obj *s) { return s && s->kind == K_DRAWING_SURFACE ? 4 : 2; }
static HRESULT STDMETHODCALLTYPE sb_get_surface(void *p, void **out)
{
    lock();
    Obj *s = SB(p)->surface;
    HRESULT hr = give(s, surface_slot(s), out);
    unlock();
    return hr;
}
static HRESULT STDMETHODCALLTYPE sb_put_surface(void *p, void *surface)
{
    Obj *s = 0;
    if (surface && !(s = obj_of(surface, K_DRAWING_SURFACE, K_CHAIN_SURFACE))) return E_INVALIDARG;
    swap_ref(&SB(p)->surface, s);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sb_get_v(void *p, float *v) { if (!v) return E_POINTER; *v = SB(p)->vratio; return S_OK; }
static HRESULT STDMETHODCALLTYPE sb_put_v(void *p, float v) { if (!(v >= 0 && v <= 1)) return E_INVALIDARG; lock(); SB(p)->vratio = v; unlock(); schedule(); return S_OK; }
static const void *const surface_brush_vt[] = { BASE(3), (void *)sb_get_interp, (void *)sb_put_interp, (void *)sb_get_h, (void *)sb_put_h,
                                                (void *)sb_get_stretch, (void *)sb_put_stretch, (void *)sb_get_surface,
                                                (void *)sb_put_surface, (void *)sb_get_v, (void *)sb_put_v };

/* IInsetClip: Bottom, Left, Right, Top */
#define INSET(name, i) \
    static HRESULT STDMETHODCALLTYPE ic_get_##name(void *p, float *v) { if (!v) return E_POINTER; *v = OBJ_AT(p, 3)->inset[i]; return S_OK; } \
    static HRESULT STDMETHODCALLTYPE ic_put_##name(void *p, float v) { lock(); OBJ_AT(p, 3)->inset[i] = v; unlock(); schedule(); return S_OK; }
INSET(left, 0) INSET(top, 1) INSET(right, 2) INSET(bottom, 3)
static const void *const inset_vt[] = { BASE(3), (void *)ic_get_bottom, (void *)ic_put_bottom, (void *)ic_get_left, (void *)ic_put_left,
                                        (void *)ic_get_right, (void *)ic_put_right, (void *)ic_get_top, (void *)ic_put_top };

/* ---- targets: ICompositionTarget (slot 2), IDesktopWindowTarget (slot 3) -------------- */
static HRESULT STDMETHODCALLTYPE ct_get_root(void *p, void **out) { lock(); HRESULT hr = give(OBJ_AT(p, 2)->root, 2, out); unlock(); return hr; }
static HRESULT STDMETHODCALLTYPE ct_put_root(void *p, void *visual)
{
    Obj *t = OBJ_AT(p, 2), *v = 0;
    if (visual && !(v = obj_of(visual, K_CONTAINER, K_SPRITE))) return E_INVALIDARG;
    lock();
    if (v && (v->parent || (v->target_of && v->target_of != t))) { unlock(); obj_release(v); return E_INVALIDARG; }
    Obj *old = t->root;
    if (old) old->target_of = 0;
    t->root = v;
    if (v) v->target_of = t;
    unlock();
    if (old) obj_release(old);
    schedule();
    return S_OK;
}
static const void *const target_vt[] = { BASE(2), (void *)ct_get_root, (void *)ct_put_root };
static HRESULT STDMETHODCALLTYPE dt_topmost(void *p, boolean_t *v) { if (!v) return E_POINTER; *v = OBJ_AT(p, 3)->topmost; return S_OK; }
static const void *const desktop_target_vt[] = { BASE(3), (void *)dt_topmost };

static HRESULT target_new(Obj *comp, HWND h, BOOL desktop, boolean_t topmost, Obj **out)
{
    *out = 0;
    if (!IsWindow(h)) return E_INVALIDARG;
    Obj *t = obj_new(comp, K_TARGET);
    if (!t) return E_OUTOFMEMORY;
    t->vt[0] = co_vt; t->vt[1] = closable_vt; t->vt[2] = target_vt;
    if (desktop) t->vt[3] = desktop_target_vt;
    t->hwnd = h;
    t->topmost = topmost;
    HRESULT hr = E_OUTOFMEMORY;
    lock();
    if (!g_thread) {
        HANDLE th = CreateThread(0, 0, compositor_thread, 0, 0, 0);
        if (th) { CloseHandle(th); g_thread = TRUE; }
    }
    for (int i = 0; i < MAX_TARGETS; i++)
        if (!g_targets[i]) { g_targets[i] = t; hr = S_OK; break; }
    if (SUCCEEDED(hr)) subclass(h);
    unlock();
    if (FAILED(hr)) { obj_release(t); return hr; }
    *out = t;
    schedule();
    return S_OK;
}

/* ---- graphics devices and drawing surfaces ---------------------------------------- */
typedef HRESULT (STDMETHODCALLTYPE *CreateTexture2D_t)(IUnknown *, const ND3D11_TEXTURE2D_DESC *, const void *, IUnknown **);
static HRESULT surface_make(Obj *s, UINT w, UINT h)
{
    ND3D11_TEXTURE2D_DESC td;
    __builtin_memset(&td, 0, sizeof(td));
    td.Width = w; td.Height = h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = s->format;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET_ | D3D11_BIND_SHADER_RESOURCE_;
    IUnknown *dev = s->d3d;
    CreateTexture2D_t create = (CreateTexture2D_t)((void **)*(void **)dev)[5];
    IUnknown *tex = 0, *staging = 0;
    HRESULT hr = create(dev, &td, NULL, &tex);
    if (FAILED(hr)) return hr;
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING_;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ_;
    hr = create(dev, &td, NULL, &staging);
    BYTE *frame = SUCCEEDED(hr) ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)w * 4 * h) : 0;
    if (!frame) {
        tex->lpVtbl->Release(tex);
        if (staging) staging->lpVtbl->Release(staging);
        return FAILED(hr) ? hr : E_OUTOFMEMORY;
    }
    lock();
    UINT cw = w < s->w ? w : s->w, ch = h < s->h ? h : s->h;
    if (s->tex && cw && ch) {           /* (a resize keeps the pixels both sizes cover) */
        typedef void (STDMETHODCALLTYPE *CopyRegion_t)(IUnknown *, IUnknown *, UINT, UINT, UINT, UINT, IUnknown *, UINT, const ND3D11_BOX *);
        ND3D11_BOX box = { 0, 0, 0, cw, ch, 1 };
        ((CopyRegion_t)((void **)*(void **)s->ctx)[46])(s->ctx, tex, 0, 0, 0, 0, s->tex, 0, &box);
        for (UINT y = 0; s->frame && y < ch; y++) __builtin_memcpy(frame + (size_t)y * w * 4, s->frame + (size_t)y * s->w * 4, (size_t)cw * 4);
    }
    IUnknown *old_tex = s->tex, *old_staging = s->staging;
    BYTE *old_frame = s->frame;
    s->tex = tex; s->staging = staging; s->frame = frame;
    s->w = w; s->h = h;
    unlock();
    if (old_tex) old_tex->lpVtbl->Release(old_tex);
    if (old_staging) old_staging->lpVtbl->Release(old_staging);
    HeapFree(GetProcessHeap(), 0, old_frame);
    schedule();
    return S_OK;
}

static const void *const drawing_vt[], *const drawing_interop_vt[];
/* ICompositionGraphicsDevice (slot 2): CreateDrawingSurface(size in pixels,
 * DirectXPixelFormat (DXGI_FORMAT's values), DirectXAlphaMode) */
static HRESULT STDMETHODCALLTYPE gd_create(void *p, SizeF size, int format, int alpha, void **out)
{
    Obj *g = OBJ_AT(p, 2);
    if (!out) return E_POINTER;
    *out = 0;
    if (!(format == NDXGI_FORMAT_B8G8R8A8_UNORM || format == NDXGI_FORMAT_R8G8B8A8_UNORM || format == NDXGI_FORMAT_B8G8R8X8_UNORM ||
          format == NDXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == NDXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) return E_INVALIDARG;
    if (!(size.Width >= 0 && size.Height >= 0)) return E_INVALIDARG;
    if (!g->d3d) return E_ILLEGAL_METHOD_CALL_;
    Obj *s = obj_new(g->comp, K_DRAWING_SURFACE);
    if (!s) return E_OUTOFMEMORY;
    s->vt[0] = co_vt; s->vt[1] = closable_vt; s->vt[2] = drawing_vt; s->vt[3] = drawing_interop_vt; s->vt[4] = empty4_vt;
    s->format = (UINT)format;
    s->alpha = alpha;
    s->d3d = g->d3d; s->d3d->lpVtbl->AddRef(s->d3d);
    s->ctx = g->ctx; s->ctx->lpVtbl->AddRef(s->ctx);
    UINT w = (UINT)(int)size.Width, h = (UINT)(int)size.Height;
    HRESULT hr = w && h ? surface_make(s, w, h) : S_OK;           /* (an empty surface gets its texture at Resize) */
    if (FAILED(hr)) { obj_release(s); return hr; }
    *out = IF(s, 2);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gd_add_replaced(void *p, void *h, EventRegistrationToken *t)
{
    (void)p; (void)h;
    if (!t) return E_POINTER;
    t->value = 1;                       /* (the device is never replaced) */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gd_remove_replaced(void *p, EventRegistrationToken t) { (void)p; (void)t; return S_OK; }
static const void *const graphics_vt[] = { BASE(2), (void *)gd_create, (void *)gd_add_replaced, (void *)gd_remove_replaced };

/* @dev as a Direct3D 11 device and its immediate context (counted) */
static HRESULT d3d_of(IUnknown *dev, IUnknown **d3d, IUnknown **ctx)
{
    *d3d = *ctx = 0;
    if (!dev) return E_INVALIDARG;
    if (FAILED(dev->lpVtbl->QueryInterface(dev, &NIID_ID3D11Device, (void **)d3d)) || !*d3d) return E_INVALIDARG;
    ((void (STDMETHODCALLTYPE *)(IUnknown *, IUnknown **))((void **)*(void **)*d3d)[40])(*d3d, ctx);      /* GetImmediateContext */
    if (!*ctx) { (*d3d)->lpVtbl->Release(*d3d); *d3d = 0; return E_FAIL; }
    return S_OK;
}
/* ICompositionGraphicsDeviceInterop (slot 3) */
static HRESULT STDMETHODCALLTYPE gdi_get(void *p, IUnknown **out)
{
    Obj *g = OBJ_AT(p, 3);
    if (!out) return E_POINTER;
    *out = g->d3d;
    if (*out) (*out)->lpVtbl->AddRef(*out);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gdi_set(void *p, IUnknown *dev)
{
    Obj *g = OBJ_AT(p, 3);
    IUnknown *d3d, *ctx;
    HRESULT hr = d3d_of(dev, &d3d, &ctx);
    if (FAILED(hr)) return hr;
    if (g->d3d) g->d3d->lpVtbl->Release(g->d3d);
    if (g->ctx) g->ctx->lpVtbl->Release(g->ctx);
    g->d3d = d3d; g->ctx = ctx;
    return S_OK;
}
static const void *const graphics_interop_vt[] = { UNK(3), (void *)gdi_get, (void *)gdi_set };

/* ICompositionDrawingSurface (slot 2) */
static HRESULT STDMETHODCALLTYPE ds_alpha(void *p, int *v) { if (!v) return E_POINTER; *v = OBJ_AT(p, 2)->alpha; return S_OK; }
static HRESULT STDMETHODCALLTYPE ds_format(void *p, int *v) { if (!v) return E_POINTER; *v = (int)OBJ_AT(p, 2)->format; return S_OK; }
static HRESULT STDMETHODCALLTYPE ds_size(void *p, SizeF *v) { if (!v) return E_POINTER; v->Width = (float)OBJ_AT(p, 2)->w; v->Height = (float)OBJ_AT(p, 2)->h; return S_OK; }
static const void *const drawing_vt[] = { BASE(2), (void *)ds_alpha, (void *)ds_format, (void *)ds_size };

/* ICompositionDrawingSurfaceInterop (slot 3): BeginDraw hands out the
 * surface's texture (as @iid: ID3D11Texture2D, IDXGISurface) with the
 * update rectangle at *@offset in it; EndDraw reads the rectangle back */
#define DS(p) OBJ_AT(p, 3)
static HRESULT STDMETHODCALLTYPE dsi_begin(void *p, const RECT *r, REFIID iid, void **out, POINT *offset)
{
    Obj *s = DS(p);
    if (!out || !offset) return E_POINTER;
    *out = 0;
    if (s->drawing) return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED_;
    if (!s->tex) return E_ILLEGAL_METHOD_CALL_;
    RECT all = { 0, 0, (LONG)s->w, (LONG)s->h }, u = all;
    if (r) {
        if (r->left < 0 || r->top < 0 || r->right > all.right || r->bottom > all.bottom || r->left >= r->right || r->top >= r->bottom)
            return E_INVALIDARG;
        u = *r;
    }
    HRESULT hr = s->tex->lpVtbl->QueryInterface(s->tex, iid, out);
    if (FAILED(hr)) return hr;
    s->drawing = TRUE;
    s->update = u;
    offset->x = u.left; offset->y = u.top;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dsi_end(void *p)
{
    Obj *s = DS(p);
    if (!s->drawing) return DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED_;
    lock();
    novadx_read_back(s->ctx, s->staging, s->tex, s->format, s->w, s->h, s->frame, &s->update);
    unlock();
    s->drawing = FALSE;
    schedule();
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dsi_resize(void *p, SIZE size)
{
    Obj *s = DS(p);
    if (s->drawing) return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED_;
    if (size.cx <= 0 || size.cy <= 0) return E_INVALIDARG;
    if ((UINT)size.cx == s->w && (UINT)size.cy == s->h && s->tex) return S_OK;
    return surface_make(s, (UINT)size.cx, (UINT)size.cy);
}
static HRESULT STDMETHODCALLTYPE dsi_scroll(void *p, const RECT *scroll, const RECT *clip, int dx, int dy)
{ (void)p; (void)scroll; (void)clip; (void)dx; (void)dy; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE dsi_resume(void *p) { return DS(p)->drawing ? S_OK : DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED_; }
static const void *const drawing_interop_vt[] = { UNK(3), (void *)dsi_begin, (void *)dsi_end, (void *)dsi_resize, (void *)dsi_scroll,
                                                  (void *)dsi_resume, (void *)dsi_resume };

/* a present of a swap chain a surface shows */
static void STDMETHODCALLTYPE chain_sink(void *ctx, const RECT *dirty) { (void)ctx; (void)dirty; schedule(); }

/* ---- the Compositor ------------------------------------------------------------------ */
#define COMP(p, i) OBJ_AT(p, i)
static HRESULT not_made(void **out) { if (out) *out = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE c_none(void *p, void **out) { (void)p; return not_made(out); }
static HRESULT STDMETHODCALLTYPE c_none_easing(void *p, Vec2 a, Vec2 b, void **out) { (void)p; (void)a; (void)b; return not_made(out); }
static HRESULT STDMETHODCALLTYPE c_none_effect(void *p, void *e, void **out) { (void)p; (void)e; return not_made(out); }
static HRESULT STDMETHODCALLTYPE c_none_effect2(void *p, void *e, void *names, void **out) { (void)p; (void)e; (void)names; return not_made(out); }
static HRESULT STDMETHODCALLTYPE c_none_expr(void *p, HSTRING s, void **out) { (void)p; (void)s; return not_made(out); }
static HRESULT STDMETHODCALLTYPE c_none_batch(void *p, int type, void **out) { (void)p; (void)type; return not_made(out); }

static HRESULT new_visual(Obj *comp, int kind, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    Obj *v = obj_new(comp, kind);
    if (!v) return E_OUTOFMEMORY;
    v->vt[0] = co_vt; v->vt[1] = closable_vt; v->vt[2] = visual_vt; v->vt[3] = container_vt;
    if (kind == K_SPRITE) v->vt[4] = sprite_vt;
    v->opacity = 1.0f;
    v->visible = 1;
    v->scale.X = v->scale.Y = v->scale.Z = 1.0f;
    v->axis.Z = 1.0f;
    v->orient.W = 1.0f;
    for (int i = 0; i < 4; i++) v->tm.m[i][i] = 1.0f;
    *out = IF(v, kind == K_SPRITE ? 4 : 3);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_container(void *p, void **out) { return new_visual(COMP(p, 0), K_CONTAINER, out); }
static HRESULT STDMETHODCALLTYPE c_sprite(void *p, void **out) { return new_visual(COMP(p, 0), K_SPRITE, out); }
static HRESULT STDMETHODCALLTYPE c_color_brush_with(void *p, Color c, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    Obj *b = obj_new(COMP(p, 0), K_COLOR_BRUSH);
    if (!b) return E_OUTOFMEMORY;
    b->vt[0] = co_vt; b->vt[1] = closable_vt; b->vt[2] = empty2_vt; b->vt[3] = color_vt;
    b->color = c;
    *out = IF(b, 3);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_color_brush(void *p, void **out) { Color black = { 255, 0, 0, 0 }; return c_color_brush_with(p, black, out); }
static HRESULT STDMETHODCALLTYPE c_surface_brush_with(void *p, void *surface, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    Obj *s = 0;
    if (surface && !(s = obj_of(surface, K_DRAWING_SURFACE, K_CHAIN_SURFACE))) return E_INVALIDARG;
    Obj *b = obj_new(COMP(p, 0), K_SURFACE_BRUSH);
    if (!b) { if (s) obj_release(s); return E_OUTOFMEMORY; }
    b->vt[0] = co_vt; b->vt[1] = closable_vt; b->vt[2] = empty2_vt; b->vt[3] = surface_brush_vt;
    b->surface = s;
    b->stretch = 1;                     /* Fill */
    b->interp = 1;                      /* Linear (sampled nearest here) */
    b->hratio = b->vratio = 0.5f;
    *out = IF(b, 3);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_surface_brush(void *p, void **out) { return c_surface_brush_with(p, 0, out); }
static HRESULT STDMETHODCALLTYPE c_inset_with(void *p, float l, float t, float r, float b, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    Obj *c = obj_new(COMP(p, 0), K_INSET_CLIP);
    if (!c) return E_OUTOFMEMORY;
    c->vt[0] = co_vt; c->vt[1] = closable_vt; c->vt[2] = empty2_vt; c->vt[3] = inset_vt;
    c->inset[0] = l; c->inset[1] = t; c->inset[2] = r; c->inset[3] = b;
    *out = IF(c, 3);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_inset(void *p, void **out) { return c_inset_with(p, 0, 0, 0, 0, out); }
/* the thread's CoreWindow (twinapi.appcore.dll's) */
static HRESULT STDMETHODCALLTYPE c_target_for_view(void *p, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    static const WCHAR cls[] = L"Windows.UI.Core.CoreWindow";
    HSTRING s = 0;
    void *statics = 0, *win = 0, *interop = 0;
    HWND h = 0;
    WindowsCreateString(cls, sizeof cls / sizeof cls[0] - 1, &s);
    HRESULT hr = RoGetActivationFactory(s, &IID_ICoreWindowStatic_, &statics);
    WindowsDeleteString(s);
    if (SUCCEEDED(hr)) hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)statics)[6])(statics, &win);   /* GetForCurrentThread */
    if (SUCCEEDED(hr) && !win) hr = E_ILLEGAL_METHOD_CALL_;                      /* (no CoreWindow on this thread) */
    if (SUCCEEDED(hr)) hr = ((IUnknown *)win)->lpVtbl->QueryInterface((IUnknown *)win, &IID_ICoreWindowInterop_, &interop);
    if (SUCCEEDED(hr)) hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, HWND *))(*(void ***)interop)[3])(interop, &h);
    if (interop) ((IUnknown *)interop)->lpVtbl->Release((IUnknown *)interop);
    if (win) ((IUnknown *)win)->lpVtbl->Release((IUnknown *)win);
    if (statics) ((IUnknown *)statics)->lpVtbl->Release((IUnknown *)statics);
    if (FAILED(hr)) return hr;
    Obj *t;
    hr = target_new(COMP(p, 0), h, FALSE, 0, &t);
    if (SUCCEEDED(hr)) *out = IF(t, 2);
    return hr;
}
static const void *const compositor_vt[] = {
    BASE(0),
    (void *)c_none, (void *)c_color_brush, (void *)c_color_brush_with, (void *)c_container, (void *)c_none_easing,
    (void *)c_none_effect, (void *)c_none_effect2, (void *)c_none, (void *)c_none_expr, (void *)c_inset, (void *)c_inset_with,
    (void *)c_none, (void *)c_none, (void *)c_none, (void *)c_none, (void *)c_none_batch, (void *)c_sprite, (void *)c_surface_brush,
    (void *)c_surface_brush_with, (void *)c_target_for_view, (void *)c_none, (void *)c_none, (void *)c_none, (void *)c_none_batch,
};
_Static_assert(sizeof compositor_vt / sizeof compositor_vt[0] == 6 + 24, "ICompositor has 24 methods");
static HRESULT STDMETHODCALLTYPE c_close(void *p) { (void)p; return S_OK; }
static const void *const compositor_closable_vt[] = { BASE(1), (void *)c_close };

/* ICompositorInterop (slot 2) */
static HRESULT STDMETHODCALLTYPE ci_for_handle(void *p, HANDLE h, void **out) { (void)p; (void)h; return not_made(out); }
static HRESULT STDMETHODCALLTYPE ci_for_chain(void *p, IUnknown *chain, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    INovaSwapChain *nc = 0;
    if (!chain || FAILED(chain->lpVtbl->QueryInterface(chain, &NIID_INovaSwapChain, (void **)&nc)) || !nc) return E_INVALIDARG;
    Obj *s = obj_new(COMP(p, 2), K_CHAIN_SURFACE);
    if (!s) { nc->lpVtbl->Release(nc); return E_OUTOFMEMORY; }
    s->vt[0] = co_vt; s->vt[1] = closable_vt; s->vt[2] = empty2_vt;
    s->chain = nc;
    nc->lpVtbl->SetSink(nc, chain_sink, s);
    *out = IF(s, 2);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ci_graphics(void *p, IUnknown *dev, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    IUnknown *d3d, *ctx;
    HRESULT hr = d3d_of(dev, &d3d, &ctx);
    if (FAILED(hr)) return hr;
    Obj *g = obj_new(COMP(p, 2), K_GRAPHICS);
    if (!g) { ctx->lpVtbl->Release(ctx); d3d->lpVtbl->Release(d3d); return E_OUTOFMEMORY; }
    g->vt[0] = co_vt; g->vt[1] = closable_vt; g->vt[2] = graphics_vt; g->vt[3] = graphics_interop_vt;
    g->d3d = d3d; g->ctx = ctx;
    *out = IF(g, 2);
    return S_OK;
}
static const void *const compositor_interop_vt[] = { UNK(2), (void *)ci_for_handle, (void *)ci_for_chain, (void *)ci_graphics };

/* ICompositorDesktopInterop (slot 3) */
static HRESULT STDMETHODCALLTYPE cd_target(void *p, HWND h, BOOL topmost, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    Obj *t;
    HRESULT hr = target_new(COMP(p, 3), h, TRUE, topmost ? 1 : 0, &t);
    if (SUCCEEDED(hr)) *out = IF(t, 3);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cd_ensure(void *p, DWORD tid) { (void)p; (void)tid; return S_OK; }
static const void *const compositor_desktop_vt[] = { UNK(3), (void *)cd_target, (void *)cd_ensure };

/* ---- the activation factory ------------------------------------------------------------ */
static HRESULT STDMETHODCALLTYPE f_qi(void *p, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IActivationFactory_)) { *out = p; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE f_ref(void *p) { (void)p; return 2; }
static HRESULT STDMETHODCALLTYPE f_name(void *p, HSTRING *n) { (void)p; return make_hstring(L"Windows.UI.Composition.Compositor", n); }
static HRESULT STDMETHODCALLTYPE f_activate(void *p, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    *out = 0;
    Obj *c = obj_new(0, K_COMPOSITOR);
    if (!c) return E_OUTOFMEMORY;
    c->vt[0] = compositor_vt; c->vt[1] = compositor_closable_vt; c->vt[2] = compositor_interop_vt; c->vt[3] = compositor_desktop_vt;
    *out = IF(c, 0);
    return S_OK;
}
static const void *const factory_vt[] = { (void *)f_qi, (void *)f_ref, (void *)f_ref, (void *)no_iids, (void *)f_name, (void *)base_trust,
                                          (void *)f_activate };
static const void *const *const g_factory = factory_vt;

static BOOL name_is(HSTRING s, const WCHAR *want)
{
    UINT32 n, i = 0;
    const WCHAR *c = WindowsGetStringRawBuffer(s, &n);
    for (; i < n && want[i]; i++) if (c[i] != want[i]) return FALSE;
    return i == n && !want[i];
}
COMP_API HRESULT WINAPI DllGetActivationFactory(HSTRING cls, void **factory)
{
    if (!factory) return E_POINTER;
    if (name_is(cls, L"Windows.UI.Composition.Compositor")) { *factory = (void *)&g_factory; return S_OK; }
    *factory = 0;
    return CLASS_E_CLASSNOTAVAILABLE;
}
COMP_API HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

void composition_init(void)
{
    InitializeCriticalSection(&g_cs);
    g_wake = CreateEventW(0, FALSE, FALSE, 0);
}
