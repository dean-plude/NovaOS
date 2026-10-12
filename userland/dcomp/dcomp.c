/*
 * dcomp.dll — DirectComposition, NovaOS's own.
 *
 * NovaOS has no compositor engine for DirectComposition's visual trees.
 * What it provides is what Chromium's compositor (the GPU process of
 * WebView2 and of Steam's and Galaxy's browsers) uses, in software and on
 * a GPU: devices, a target on a window, visuals with offsets, translations,
 * rectangle clips and children, and a visual's content, which is either a
 * composition swap chain of dxgi.dll's (novadx.h; on the software device,
 * or on a GPU device, DXVK's, whose frames dxgi.dll reads back from the
 * GPU) or a surface of this dll's.  Each present of such a swap chain draws
 * its frame into the target's window with GDI, at the visual's place and
 * inside its clips; each Commit draws what changed since the last one: the
 * whole tree after a change to it, else the parts of surfaces drawn since.
 *
 * A surface (IDCompositionSurface) is a texture on the rendering device
 * the DirectComposition device was made with (DCompositionCreateDevice2 or
 * 3's, or DCompositionCreateDevice's): BeginDraw hands the program that
 * texture with the update rectangle's offset in it, and EndDraw copies the
 * rectangle into a staging texture and reads it back, as dxgi.dll does at
 * a GPU swap chain's present.  Chromium turns on its hardware
 * DirectComposition path (its root surface a DirectComposition surface or
 * a composition swap chain on ANGLE's Direct3D 11 device) when
 * DCompositionCreateDevice3 exists and gives it IDCompositionDesktopDevice
 * and IDCompositionDevice3.
 *
 * Not implemented: transform objects, effects other than an effect group's
 * opacity, animations, scaling and rotation (only a transform's
 * translation is kept), surfaces from handles or windows, rounded clips.
 */
#include <windows.h>
#include <novadx.h>

#define DCOMPAPI __declspec(dllexport)
#define MAX_TARGETS 64
#define MAX_KIDS    64
#define MAX_DEPTH   64
#define D3D11_BIND_SHADER_RESOURCE_  0x8
#define D3D11_BIND_RENDER_TARGET_    0x20
#define D3D11_USAGE_STAGING_         3
#define D3D11_CPU_ACCESS_READ_       0x20000
#define DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED_     ((HRESULT)0x88980801L)
#define DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED_ ((HRESULT)0x88980802L)
#define FAR_AWAY 1.0e6f                 /* (no clip: this far in every direction) */

int _fltused = 0x9875;                  /* (offsets are floats; no C runtime here) */

typedef struct Dc Dc;
typedef struct Target Target;
typedef struct Visual Visual;
typedef struct Surface Surface;
typedef struct Clip Clip;
typedef struct Effect Effect;

/* A surface: a texture on the device, and what was read back of it */
struct Surface {
    const void *const *vtbl;
    LONG refs;
    IUnknown *dev, *ctx;                /* the ID3D11Device and its immediate context (counted) */
    IUnknown *tex, *staging;
    UINT w, h, format;
    BYTE *frame;                        /* BGRA, w * 4 bytes a row */
    BOOL drawing;
    RECT update;                        /* (BeginDraw's) */
    RECT dirty;                         /* read back since the last Commit */
};

/* A rectangle clip: left, top, right, bottom */
struct Clip {
    const void *const *vtbl;
    LONG refs;
    float r[4];
};

/* An effect group: its opacity */
struct Effect {
    const void *const *vtbl;
    LONG refs;
    float opacity;
};

struct Visual {
    const void *const *vtbl;
    LONG refs;
    IUnknown *content;
    INovaSwapChain *chain;              /* (the content as a swap chain) */
    Surface *surface;                   /* (the content as a surface; not counted: content is) */
    Target *target;                     /* (the root of this target's tree; not counted) */
    Visual *parent;                     /* (not counted) */
    Visual *kids[MAX_KIDS];             /* (counted) */
    int nkids;
    float ox, oy, tx, ty;               /* the offset and the transform's translation */
    float rect_clip[4];                 /* (SetClip with a rectangle; has_rect_clip) */
    int has_rect_clip;
    Clip *clip;                         /* (SetClip with a clip object; counted) */
    Effect *effect;                     /* (counted) */
    float opacity;
    BOOL hidden;
};

struct Target {
    const void *const *vtbl;
    LONG refs;
    HWND hwnd;
    Visual *root;                       /* (counted) */
};

/* The targets of every device, for Commit (a visual tree is the same
 * whichever device made it) */
static Target *g_targets[MAX_TARGETS];
static CRITICAL_SECTION g_lock;
static int g_changed;                   /* a tree changed since the last Commit */

static const void *const g_visual_vtbl[], *const g_surface_vtbl[], *const g_clip_vtbl[], *const g_effect_vtbl[];
static Visual *visual_of(void *p) { Visual *v = p; return v && v->vtbl == g_visual_vtbl ? v : NULL; }
static Surface *surface_of(void *p) { Surface *s = p; return s && s->vtbl == g_surface_vtbl ? s : NULL; }
static Clip *clip_of(void *p) { Clip *c = p; return c && c->vtbl == g_clip_vtbl ? c : NULL; }
static Effect *effect_of(void *p) { Effect *e = p; return e && e->vtbl == g_effect_vtbl ? e : NULL; }

static void changed(void) { g_changed = 1; }

/* ---- Drawing ------------------------------------------------------------- */
static LONG to_px(float f)
{
    if (f < -FAR_AWAY) f = -FAR_AWAY;
    if (f > FAR_AWAY) f = FAR_AWAY;
    return (LONG)f;
}

/* @v's place: its origin moved by (@fx, @fy), and @clip (window coordinates)
 * narrowed by its clip; FALSE: it is not shown */
static BOOL visual_step(Visual *v, float *fx, float *fy, RECT *clip)
{
    if (v->hidden || v->opacity <= 0.0f || (v->effect && v->effect->opacity <= 0.0f)) return FALSE;
    *fx += v->ox + v->tx;
    *fy += v->oy + v->ty;
    const float *r = v->clip ? v->clip->r : v->has_rect_clip ? v->rect_clip : NULL;
    if (r) {
        RECT c = { to_px(*fx + r[0]), to_px(*fy + r[1]), to_px(*fx + r[2]), to_px(*fy + r[3]) };
        if (!IntersectRect(clip, clip, &c)) SetRectEmpty(clip);
    }
    return TRUE;
}

static const RECT g_everywhere = { -(LONG)FAR_AWAY, -(LONG)FAR_AWAY, (LONG)FAR_AWAY, (LONG)FAR_AWAY };

/* @v's target, its content's place (@x, @y) in the target's window and its
 * clip there (NULL: not shown) */
static Target *visual_place(Visual *v, int *x, int *y, RECT *clip)
{
    Visual *path[MAX_DEPTH];
    int n = 0;
    for (; v && n < MAX_DEPTH; v = v->parent) {
        path[n++] = v;
        if (v->target) break;
    }
    if (!n || !path[n - 1]->target) return NULL;
    Target *t = path[n - 1]->target;
    float fx = 0, fy = 0;
    *clip = g_everywhere;
    while (n--) if (!visual_step(path[n], &fx, &fy, clip)) return NULL;
    *x = to_px(fx); *y = to_px(fy);
    return t;
}

/* the part @dirty (content coordinates; NULL: all) of @v's content, at
 * (@x, @y) of @t's window, inside @clip */
static void draw_content(Visual *v, Target *t, int x, int y, const RECT *clip, const RECT *dirty)
{
    if (!IsWindow(t->hwnd)) return;
    UINT pitch = 0, w = 0, h = 0;
    BYTE *bits = NULL;
    if (v->chain) bits = v->chain->lpVtbl->Frame(v->chain, &pitch, &w, &h);
    else if (v->surface && v->surface->frame) { bits = v->surface->frame; w = v->surface->w; h = v->surface->h; pitch = w * 4; }
    if (!bits) return;
    RECT part = { clip->left - x, clip->top - y, clip->right - x, clip->bottom - y };
    if (dirty && !IntersectRect(&part, &part, dirty)) return;
    if (IsRectEmpty(&part)) return;
    HDC dc = GetDC(t->hwnd);
    if (!dc) return;
    novadx_blit(dc, x, y, bits, pitch, w, h, &part);
    ReleaseDC(t->hwnd, dc);
}

/* @v's tree: all of it (@all), or the parts of its surfaces read back since
 * the last Commit */
static void draw_tree(Visual *v, Target *t, float fx, float fy, RECT clip, int all, int depth)
{
    if (!v || depth > MAX_DEPTH || !visual_step(v, &fx, &fy, &clip) || IsRectEmpty(&clip)) return;
    if (all) draw_content(v, t, to_px(fx), to_px(fy), &clip, NULL);
    else if (v->surface && !IsRectEmpty(&v->surface->dirty)) draw_content(v, t, to_px(fx), to_px(fy), &clip, &v->surface->dirty);
    for (int i = 0; i < v->nkids; i++) draw_tree(v->kids[i], t, fx, fy, clip, all, depth + 1);
}

static void clear_dirty(Visual *v, int depth)
{
    if (!v || depth > MAX_DEPTH) return;
    if (v->surface) SetRectEmpty(&v->surface->dirty);
    for (int i = 0; i < v->nkids; i++) clear_dirty(v->kids[i], depth + 1);
}

static void commit(void)
{
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_TARGETS; i++)
        if (g_targets[i]) draw_tree(g_targets[i]->root, g_targets[i], 0, 0, g_everywhere, g_changed, 0);
    for (int i = 0; i < MAX_TARGETS; i++) if (g_targets[i]) clear_dirty(g_targets[i]->root, 0);
    g_changed = 0;
    LeaveCriticalSection(&g_lock);
}

/* a present of the swap chain a visual shows */
static void STDMETHODCALLTYPE visual_sink(void *ctx, const RECT *dirty)
{
    EnterCriticalSection(&g_lock);
    Visual *v = ctx;
    int x = 0, y = 0;
    RECT clip;
    Target *t = visual_place(v, &x, &y, &clip);
    if (t) draw_content(v, t, x, y, &clip, dirty);
    LeaveCriticalSection(&g_lock);
}

/* ---- Surfaces ------------------------------------------------------------ */
typedef HRESULT (STDMETHODCALLTYPE *CreateTexture2D_t)(IUnknown *, const ND3D11_TEXTURE2D_DESC *, const void *, IUnknown **);

static int surface_format(UINT f)
{
    return f == NDXGI_FORMAT_B8G8R8A8_UNORM || f == NDXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == NDXGI_FORMAT_B8G8R8X8_UNORM ||
           f == NDXGI_FORMAT_R8G8B8A8_UNORM || f == NDXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}

static void surface_drop(Surface *s)
{
    if (s->tex) { s->tex->lpVtbl->Release(s->tex); s->tex = NULL; }
    if (s->staging) { s->staging->lpVtbl->Release(s->staging); s->staging = NULL; }
    HeapFree(GetProcessHeap(), 0, s->frame);
    s->frame = NULL;
}

static HRESULT surface_make(Surface *s, UINT w, UINT h)
{
    ND3D11_TEXTURE2D_DESC td;
    __builtin_memset(&td, 0, sizeof(td));
    td.Width = w; td.Height = h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = s->format;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET_ | D3D11_BIND_SHADER_RESOURCE_;
    CreateTexture2D_t create = (CreateTexture2D_t)((void **)*(void **)s->dev)[5];
    HRESULT hr = create(s->dev, &td, NULL, &s->tex);
    if (FAILED(hr)) { s->tex = NULL; return hr; }
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING_;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ_;
    hr = create(s->dev, &td, NULL, &s->staging);
    if (FAILED(hr)) { s->staging = NULL; surface_drop(s); return hr; }
    s->frame = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)w * 4 * h);
    if (!s->frame) { surface_drop(s); return E_OUTOFMEMORY; }
    s->w = w; s->h = h;
    SetRectEmpty(&s->dirty);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE s_qi(Surface *s, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (novadx_same(iid, &NIID_IUnknown) || novadx_same(iid, &NIID_IDCompositionSurface)) {
        InterlockedIncrement(&s->refs);
        *out = s;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE s_addref(Surface *s) { return (ULONG)InterlockedIncrement(&s->refs); }
static ULONG STDMETHODCALLTYPE s_release(Surface *s)
{
    LONG r = InterlockedDecrement(&s->refs);
    if (!r) {
        surface_drop(s);
        s->ctx->lpVtbl->Release(s->ctx);
        s->dev->lpVtbl->Release(s->dev);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return (ULONG)r;
}
/* the texture, to draw @r of it (NULL: all), at *@offset in it */
static HRESULT STDMETHODCALLTYPE s_begin(Surface *s, const RECT *r, const GUID *iid, void **out, POINT *offset)
{
    if (!out || !offset) return E_POINTER;
    *out = NULL;
    if (s->drawing) return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED_;
    RECT all = { 0, 0, (LONG)s->w, (LONG)s->h }, u = all;
    if (r) {
        if (r->left < 0 || r->top < 0 || r->right > all.right || r->bottom > all.bottom || r->left >= r->right ||
            r->top >= r->bottom) return E_INVALIDARG;
        u = *r;
    }
    if (!s->tex) return E_FAIL;
    HRESULT hr = s->tex->lpVtbl->QueryInterface(s->tex, iid, out);
    if (FAILED(hr)) return hr;
    s->drawing = TRUE;
    s->update = u;
    offset->x = u.left; offset->y = u.top;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_end(Surface *s)
{
    if (!s->drawing) return DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED_;
    EnterCriticalSection(&g_lock);
    novadx_read_back(s->ctx, s->staging, s->tex, s->format, s->w, s->h, s->frame, &s->update);
    UnionRect(&s->dirty, &s->dirty, &s->update);
    LeaveCriticalSection(&g_lock);
    s->drawing = FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_suspend(Surface *s) { return s->drawing ? S_OK : DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED_; }
static HRESULT STDMETHODCALLTYPE s_scroll(Surface *s, const RECT *scroll, const RECT *clip, int dx, int dy)
{ (void)s; (void)scroll; (void)clip; (void)dx; (void)dy; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE s_resize(Surface *s, UINT w, UINT h)              /* (IDCompositionVirtualSurface) */
{
    if (s->drawing) return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED_;
    if (!w || !h) return E_INVALIDARG;
    EnterCriticalSection(&g_lock);
    surface_drop(s);
    HRESULT hr = surface_make(s, w, h);
    changed();
    LeaveCriticalSection(&g_lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE s_trim(Surface *s, const RECT *r, UINT n) { (void)s; (void)r; (void)n; return S_OK; }

static const void *const g_surface_vtbl[] = {
    (void *)s_qi, (void *)s_addref, (void *)s_release, (void *)s_begin, (void *)s_end, (void *)s_suspend, (void *)s_suspend,
    (void *)s_scroll, (void *)s_resize, (void *)s_trim,
};

/* ---- Rectangle clips and effect groups ----------------------------------- */
static HRESULT simple_qi(void *o, LONG *refs, const GUID *iid, const GUID *a, const GUID *b, void **out)
{
    if (!out) return E_POINTER;
    if (novadx_same(iid, &NIID_IUnknown) || novadx_same(iid, a) || novadx_same(iid, b)) {
        InterlockedIncrement(refs);
        *out = o;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE k_qi(Clip *c, const GUID *iid, void **out)
{ return simple_qi(c, &c->refs, iid, &NIID_IDCompositionClip, &NIID_IDCompositionRectangleClip, out); }
static ULONG STDMETHODCALLTYPE k_addref(Clip *c) { return (ULONG)InterlockedIncrement(&c->refs); }
static ULONG STDMETHODCALLTYPE k_release(Clip *c)
{
    LONG r = InterlockedDecrement(&c->refs);
    if (!r) HeapFree(GetProcessHeap(), 0, c);
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE k_anim(Clip *c, IUnknown *a) { (void)c; (void)a; return S_OK; }
static HRESULT STDMETHODCALLTYPE k_radius(Clip *c, float f) { (void)c; (void)f; return S_OK; }
static HRESULT k_set(Clip *c, int i, float f)
{
    EnterCriticalSection(&g_lock);
    c->r[i] = f;
    changed();
    LeaveCriticalSection(&g_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE k_left(Clip *c, float f) { return k_set(c, 0, f); }
static HRESULT STDMETHODCALLTYPE k_top(Clip *c, float f) { return k_set(c, 1, f); }
static HRESULT STDMETHODCALLTYPE k_right(Clip *c, float f) { return k_set(c, 2, f); }
static HRESULT STDMETHODCALLTYPE k_bottom(Clip *c, float f) { return k_set(c, 3, f); }

/* (each property: animated, then a value) */
static const void *const g_clip_vtbl[] = {
    (void *)k_qi, (void *)k_addref, (void *)k_release,
    (void *)k_anim, (void *)k_left, (void *)k_anim, (void *)k_top, (void *)k_anim, (void *)k_right, (void *)k_anim, (void *)k_bottom,
    (void *)k_anim, (void *)k_radius, (void *)k_anim, (void *)k_radius, (void *)k_anim, (void *)k_radius, (void *)k_anim, (void *)k_radius,
    (void *)k_anim, (void *)k_radius, (void *)k_anim, (void *)k_radius, (void *)k_anim, (void *)k_radius, (void *)k_anim, (void *)k_radius,
};

static HRESULT STDMETHODCALLTYPE e_qi(Effect *e, const GUID *iid, void **out)
{ return simple_qi(e, &e->refs, iid, &NIID_IDCompositionEffect, &NIID_IDCompositionEffectGroup, out); }
static ULONG STDMETHODCALLTYPE e_addref(Effect *e) { return (ULONG)InterlockedIncrement(&e->refs); }
static ULONG STDMETHODCALLTYPE e_release(Effect *e)
{
    LONG r = InterlockedDecrement(&e->refs);
    if (!r) HeapFree(GetProcessHeap(), 0, e);
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE e_anim(Effect *e, IUnknown *a) { (void)e; (void)a; return S_OK; }
static HRESULT STDMETHODCALLTYPE e_opacity(Effect *e, float f)
{
    EnterCriticalSection(&g_lock);
    e->opacity = f;
    changed();
    LeaveCriticalSection(&g_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE e_transform(Effect *e, IUnknown *t) { (void)e; (void)t; return S_OK; }
static const void *const g_effect_vtbl[] = { (void *)e_qi, (void *)e_addref, (void *)e_release, (void *)e_anim, (void *)e_opacity, (void *)e_transform };

/* ---- Visuals ------------------------------------------------------------- */
static void visual_set_content(Visual *v, IUnknown *content)
{
    if (v->chain) {
        v->chain->lpVtbl->SetSink(v->chain, NULL, NULL);
        v->chain->lpVtbl->Release(v->chain);
        v->chain = NULL;
    }
    v->surface = NULL;
    if (v->content) v->content->lpVtbl->Release(v->content);
    v->content = content;
    if (!content) return;
    content->lpVtbl->AddRef(content);
    void *c = NULL;
    if ((v->surface = surface_of(content))) return;
    if (SUCCEEDED(content->lpVtbl->QueryInterface(content, &NIID_INovaSwapChain, &c))) {
        v->chain = c;
        v->chain->lpVtbl->SetSink(v->chain, visual_sink, v);
    }
}

static HRESULT STDMETHODCALLTYPE v_qi(Visual *v, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (novadx_same(iid, &NIID_IUnknown) || novadx_same(iid, &NIID_IDCompositionVisual) ||
        novadx_same(iid, &NIID_IDCompositionVisual2) || novadx_same(iid, &NIID_IDCompositionVisualDebug) ||
        novadx_same(iid, &NIID_IDCompositionVisual3)) {
        InterlockedIncrement(&v->refs);
        *out = v;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE v_addref(Visual *v) { return (ULONG)InterlockedIncrement(&v->refs); }
static HRESULT STDMETHODCALLTYPE v_removeall(Visual *v);
static ULONG STDMETHODCALLTYPE v_release(Visual *v)
{
    LONG r = InterlockedDecrement(&v->refs);
    if (!r) {
        EnterCriticalSection(&g_lock);
        visual_set_content(v, NULL);
        v_removeall(v);
        if (v->clip) k_release(v->clip);
        if (v->effect) e_release(v->effect);
        LeaveCriticalSection(&g_lock);
        HeapFree(GetProcessHeap(), 0, v);
    }
    return (ULONG)r;
}
/* (a property's value, under the lock: the tree changed) */
#define SET(stmt) do { EnterCriticalSection(&g_lock); stmt; changed(); LeaveCriticalSection(&g_lock); return S_OK; } while (0)
static HRESULT STDMETHODCALLTYPE v_ignore(Visual *v, IUnknown *u) { (void)v; (void)u; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_ignoreptr(Visual *v, const void *p) { (void)v; (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_ignoremode(Visual *v, UINT m) { (void)v; (void)m; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_ignorefloat(Visual *v, float f) { (void)v; (void)f; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_nothing(Visual *v) { (void)v; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_setx(Visual *v, float x) { SET(v->ox = x); }
static HRESULT STDMETHODCALLTYPE v_sety(Visual *v, float y) { SET(v->oy = y); }
/* a transform object: none is made here, so only NULL (no transform) counts */
static HRESULT STDMETHODCALLTYPE v_transformobj(Visual *v, IUnknown *t) { if (t) return S_OK; SET(v->tx = v->ty = 0); }
static HRESULT STDMETHODCALLTYPE v_transform(Visual *v, const float *m)                /* D2D_MATRIX_3X2_F: its translation */
{
    if (!m) return E_INVALIDARG;
    SET((v->tx = m[4], v->ty = m[5]));
}
static HRESULT STDMETHODCALLTYPE v_transform3d(Visual *v, const float *m)              /* D2D_MATRIX_4X4_F: its translation */
{
    if (!m) return E_INVALIDARG;
    SET((v->tx = m[12], v->ty = m[13]));
}
static HRESULT STDMETHODCALLTYPE v_effect(Visual *v, IUnknown *e)
{
    Effect *eg = effect_of(e);
    if (eg) e_addref(eg);
    SET((v->effect ? (void)e_release(v->effect) : (void)0, v->effect = eg));
}
static HRESULT STDMETHODCALLTYPE v_clipobj(Visual *v, IUnknown *c)
{
    Clip *k = clip_of(c);
    if (k) k_addref(k);
    SET((v->clip ? (void)k_release(v->clip) : (void)0, v->clip = k));
}
static HRESULT STDMETHODCALLTYPE v_clip(Visual *v, const float *r)                     /* D2D_RECT_F */
{
    if (!r) return E_INVALIDARG;
    SET((__builtin_memcpy(v->rect_clip, r, sizeof(v->rect_clip)), v->has_rect_clip = 1));
}
static HRESULT STDMETHODCALLTYPE v_opacity(Visual *v, float f) { SET(v->opacity = f); }
static HRESULT STDMETHODCALLTYPE v_visible(Visual *v, BOOL on) { SET(v->hidden = !on); }
static HRESULT STDMETHODCALLTYPE v_setcontent(Visual *v, IUnknown *content) { SET(visual_set_content(v, content)); }
static HRESULT STDMETHODCALLTYPE v_add(Visual *v, Visual *kid, BOOL above, Visual *ref)
{
    kid = visual_of(kid);
    ref = visual_of(ref);
    if (!kid || kid->parent || kid->target || kid == v) return E_INVALIDARG;
    EnterCriticalSection(&g_lock);
    HRESULT hr = E_OUTOFMEMORY;
    if (v->nkids < MAX_KIDS) {
        /* (kids are drawn in order, the last on top: above @ref is after
         * it, else before it; with no @ref, FALSE puts it above all its
         * siblings and TRUE below them, as Windows does) */
        int at = above ? 0 : v->nkids;
        for (int i = 0; ref && i < v->nkids; i++) if (v->kids[i] == ref) at = above ? i + 1 : i;
        for (int j = v->nkids; j > at; j--) v->kids[j] = v->kids[j - 1];
        v_addref(kid);
        kid->parent = v;
        v->kids[at] = kid;
        v->nkids++;
        changed();
        hr = S_OK;
    }
    LeaveCriticalSection(&g_lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE v_remove(Visual *v, Visual *kid)
{
    EnterCriticalSection(&g_lock);
    HRESULT hr = E_INVALIDARG;
    for (int i = 0; i < v->nkids; i++) {
        if (v->kids[i] != kid) continue;
        for (int j = i; j + 1 < v->nkids; j++) v->kids[j] = v->kids[j + 1];
        v->nkids--;
        kid->parent = NULL;
        changed();
        hr = S_OK;
        break;
    }
    LeaveCriticalSection(&g_lock);
    if (hr == S_OK) v_release(kid);
    return hr;
}
static HRESULT STDMETHODCALLTYPE v_removeall(Visual *v)
{
    while (v->nkids) v_remove(v, v->kids[v->nkids - 1]);
    return S_OK;
}

/* IDCompositionVisual3's methods (each property: animated, then a value) */
static const void *const g_visual_vtbl[] = {
    (void *)v_qi, (void *)v_addref, (void *)v_release,
    /* IDCompositionVisual */
    (void *)v_ignore, (void *)v_setx, (void *)v_ignore, (void *)v_sety, (void *)v_transformobj, (void *)v_transform,
    (void *)v_ignore /* SetTransformParent */, (void *)v_effect, (void *)v_ignoremode /* SetBitmapInterpolationMode */,
    (void *)v_ignoremode /* SetBorderMode */, (void *)v_clipobj, (void *)v_clip, (void *)v_setcontent, (void *)v_add,
    (void *)v_remove, (void *)v_removeall, (void *)v_ignoremode /* SetCompositeMode */,
    /* IDCompositionVisual2: SetOpacityMode, SetBackFaceVisibility */
    (void *)v_ignoremode, (void *)v_ignoremode,
    /* IDCompositionVisualDebug: EnableHeatMap, DisableHeatMap, EnableRedrawRegions, DisableRedrawRegions */
    (void *)v_ignoreptr, (void *)v_nothing, (void *)v_nothing, (void *)v_nothing,
    /* IDCompositionVisual3: SetDepthMode, SetOffsetZ, SetOpacity, SetTransform (3D), SetVisible */
    (void *)v_ignoremode, (void *)v_ignore, (void *)v_ignorefloat, (void *)v_ignore, (void *)v_opacity, (void *)v_ignore,
    (void *)v_transform3d, (void *)v_visible,
};

/* ---- Targets ------------------------------------------------------------- */
static HRESULT STDMETHODCALLTYPE t_qi(Target *t, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (novadx_same(iid, &NIID_IUnknown) || novadx_same(iid, &NIID_IDCompositionTarget)) {
        InterlockedIncrement(&t->refs);
        *out = t;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE t_addref(Target *t) { return (ULONG)InterlockedIncrement(&t->refs); }
static HRESULT STDMETHODCALLTYPE t_setroot(Target *t, Visual *v);
static ULONG STDMETHODCALLTYPE t_release(Target *t)
{
    LONG r = InterlockedDecrement(&t->refs);
    if (!r) {
        t_setroot(t, NULL);
        EnterCriticalSection(&g_lock);
        for (int i = 0; i < MAX_TARGETS; i++) if (g_targets[i] == t) g_targets[i] = NULL;
        LeaveCriticalSection(&g_lock);
        HeapFree(GetProcessHeap(), 0, t);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE t_setroot(Target *t, Visual *v)
{
    if (v && (!visual_of(v) || v->parent || (v->target && v->target != t))) return E_INVALIDARG;
    EnterCriticalSection(&g_lock);
    Visual *old = t->root;
    if (v) { v_addref(v); v->target = t; }
    t->root = v;
    if (old && old != v) old->target = NULL;
    changed();
    LeaveCriticalSection(&g_lock);
    if (old) v_release(old);
    return S_OK;
}
static const void *const g_target_vtbl[] = { (void *)t_qi, (void *)t_addref, (void *)t_release, (void *)t_setroot };

/* ---- The device ---------------------------------------------------------- */
/* One object for every version: IDCompositionDevice (DCompositionCreateDevice's),
 * and IDCompositionDevice2 as IDCompositionDesktopDevice and as
 * IDCompositionDevice3 (DCompositionCreateDevice2 and 3's) */
struct Dc {
    const void *const *v1, *const *desktop, *const *dev3;
    LONG refs;
    int version;
    IUnknown *d3d, *ctx;                /* the rendering device (ID3D11Device) and its immediate context, for surfaces */
};
static const void *const g_v1_vtbl[], *const g_desktop_vtbl[], *const g_dev3_vtbl[];

static Dc *dc_of(void *p)
{
    const void *vt = *(const void **)p;
    if (vt == g_desktop_vtbl) return (Dc *)((BYTE *)p - offsetof(Dc, desktop));
    if (vt == g_dev3_vtbl) return (Dc *)((BYTE *)p - offsetof(Dc, dev3));
    return p;
}

static HRESULT STDMETHODCALLTYPE d_qi(void *p, const GUID *iid, void **out)
{
    Dc *d = dc_of(p);
    if (!out) return E_POINTER;
    *out = NULL;
    if (novadx_same(iid, &NIID_IUnknown)) *out = d->version == 1 ? (void *)&d->v1 : (void *)&d->desktop;
    else if (d->version == 1 && novadx_same(iid, &NIID_IDCompositionDevice)) *out = &d->v1;
    else if (d->version > 1 && (novadx_same(iid, &NIID_IDCompositionDevice2) || novadx_same(iid, &NIID_IDCompositionDesktopDevice)))
        *out = &d->desktop;
    else if (d->version > 1 && novadx_same(iid, &NIID_IDCompositionDevice3)) *out = &d->dev3;
    if (!*out) return E_NOINTERFACE;
    InterlockedIncrement(&d->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE d_addref(void *p) { return (ULONG)InterlockedIncrement(&dc_of(p)->refs); }
static ULONG STDMETHODCALLTYPE d_release(void *p)
{
    Dc *d = dc_of(p);
    LONG r = InterlockedDecrement(&d->refs);
    if (!r) {
        if (d->ctx) d->ctx->lpVtbl->Release(d->ctx);
        if (d->d3d) d->d3d->lpVtbl->Release(d->d3d);
        HeapFree(GetProcessHeap(), 0, d);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE d_commit(void *p) { (void)p; commit(); return S_OK; }
static HRESULT STDMETHODCALLTYPE d_wait(void *p) { (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_stats(void *p, void *out)
{
    (void)p;
    if (!out) return E_INVALIDARG;
    struct { LARGE_INTEGER last; UINT num, den; LARGE_INTEGER now, freq, next; } *s = out;   /* DCOMPOSITION_FRAME_STATISTICS */
    QueryPerformanceCounter(&s->now);
    QueryPerformanceFrequency(&s->freq);
    s->num = 60; s->den = 1;
    s->last = s->now;
    s->next.QuadPart = s->now.QuadPart + s->freq.QuadPart / 60;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE d_target(void *p, HWND h, BOOL topmost, void **out)
{
    (void)p; (void)topmost;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsWindow(h)) return E_INVALIDARG;
    Target *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t) return E_OUTOFMEMORY;
    t->vtbl = g_target_vtbl;
    t->refs = 1;
    t->hwnd = h;
    EnterCriticalSection(&g_lock);
    int slot = -1;
    for (int i = 0; i < MAX_TARGETS && slot < 0; i++) if (!g_targets[i]) slot = i;
    if (slot >= 0) g_targets[slot] = t;
    LeaveCriticalSection(&g_lock);
    if (slot < 0) { HeapFree(GetProcessHeap(), 0, t); return E_OUTOFMEMORY; }
    *out = t;
    return S_OK;
}
static void *make(size_t size, const void *const *vtbl)
{
    void **o = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!o) return NULL;
    o[0] = (void *)vtbl;
    ((LONG *)(o + 1))[0] = 1;                                /* (refs, after the vtable) */
    return o;
}
static HRESULT STDMETHODCALLTYPE d_visual(void *p, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    Visual *v = make(sizeof(Visual), g_visual_vtbl);
    if (!v) { *out = NULL; return E_OUTOFMEMORY; }
    v->opacity = 1.0f;
    *out = v;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE d_surface(void *p, UINT w, UINT h, UINT format, UINT alpha, void **out)
{
    Dc *d = dc_of(p);
    (void)alpha;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!d->d3d || !d->ctx) return E_INVALIDARG;           /* (no rendering device) */
    if (!w || !h || !surface_format(format)) return E_INVALIDARG;
    Surface *s = make(sizeof(Surface), g_surface_vtbl);
    if (!s) return E_OUTOFMEMORY;
    s->format = format;
    s->dev = d->d3d; s->dev->lpVtbl->AddRef(s->dev);
    s->ctx = d->ctx; s->ctx->lpVtbl->AddRef(s->ctx);
    HRESULT hr = surface_make(s, w, h);
    if (FAILED(hr)) { s_release(s); return hr; }
    *out = s;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE d_clip(void *p, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    Clip *c = make(sizeof(Clip), g_clip_vtbl);
    if (!c) { *out = NULL; return E_OUTOFMEMORY; }
    c->r[0] = c->r[1] = -FAR_AWAY;
    c->r[2] = c->r[3] = FAR_AWAY;
    *out = c;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE d_effectgroup(void *p, void **out)
{
    (void)p;
    if (!out) return E_POINTER;
    Effect *e = make(sizeof(Effect), g_effect_vtbl);
    if (!e) { *out = NULL; return E_OUTOFMEMORY; }
    e->opacity = 1.0f;
    *out = e;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE d_none1(void *p, void **out) { (void)p; if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_none2(void *p, HANDLE h, void **out) { (void)p; (void)h; if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_none2w(void *p, HWND h, void **out) { (void)p; (void)h; if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_none2u(void *p, IUnknown *u, void **out) { (void)p; (void)u; if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_none3(void *p, void **a, UINT n, void **out) { (void)p; (void)a; (void)n; if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_state(void *p, BOOL *ok) { (void)p; if (!ok) return E_INVALIDARG; *ok = TRUE; return S_OK; }

/* IDCompositionDevice */
static const void *const g_v1_vtbl[] = {
    (void *)d_qi, (void *)d_addref, (void *)d_release, (void *)d_commit, (void *)d_wait, (void *)d_stats, (void *)d_target,
    (void *)d_visual, (void *)d_surface, (void *)d_surface /* CreateVirtualSurface */, (void *)d_none2, (void *)d_none2w,
    (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none3, (void *)d_none1,
    (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none3, (void *)d_effectgroup, (void *)d_clip, (void *)d_none1,
    (void *)d_state,
};

/* IDCompositionDevice2's methods, then IDCompositionDesktopDevice's or
 * IDCompositionDevice3's */
#define DEVICE2 \
    (void *)d_qi, (void *)d_addref, (void *)d_release, (void *)d_commit, (void *)d_wait, (void *)d_stats, (void *)d_visual, \
    (void *)d_none2u /* CreateSurfaceFactory */, (void *)d_surface, (void *)d_surface /* CreateVirtualSurface */, \
    (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none3, (void *)d_none1, \
    (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none3, (void *)d_effectgroup, (void *)d_clip, (void *)d_none1
static const void *const g_desktop_vtbl[] = { DEVICE2, (void *)d_target, (void *)d_none2, (void *)d_none2w };
static const void *const g_dev3_vtbl[] = {                  /* (its thirteen effects: not made) */
    DEVICE2, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1,
    (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1, (void *)d_none1,
};

/* A device of @version; @rendering: the device surfaces are made on (an
 * IDXGIDevice of a Direct3D 11 device; NULL: no surfaces) */
static HRESULT create(int version, IUnknown *rendering, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    Dc *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) return E_OUTOFMEMORY;
    d->v1 = g_v1_vtbl; d->desktop = g_desktop_vtbl; d->dev3 = g_dev3_vtbl;
    d->refs = 1;
    d->version = version;
    void *dev = NULL;
    if (rendering && SUCCEEDED(rendering->lpVtbl->QueryInterface(rendering, &NIID_ID3D11Device, &dev))) {
        d->d3d = dev;
        ((void (STDMETHODCALLTYPE *)(IUnknown *, IUnknown **))((void **)*(void **)d->d3d)[40])(d->d3d, &d->ctx);  /* GetImmediateContext */
        if (!d->ctx) { d->d3d->lpVtbl->Release(d->d3d); d->d3d = NULL; }
    }
    HRESULT hr = d_qi(d, iid, out);
    d_release(d);
    return hr;
}

DCOMPAPI HRESULT WINAPI DCompositionCreateDevice(IUnknown *dxgi, const GUID *iid, void **out) { return create(1, dxgi, iid, out); }
DCOMPAPI HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering, const GUID *iid, void **out) { return create(2, rendering, iid, out); }
DCOMPAPI HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering, const GUID *iid, void **out) { return create(3, rendering, iid, out); }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) InitializeCriticalSection(&g_lock);
    return TRUE;
}
