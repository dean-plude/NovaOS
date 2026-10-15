/*
 * device.c — Direct2D 1.1's devices: an ID2D1Device on a Direct3D 11
 * device (ID2D1Factory1::CreateDevice, D2D1CreateDevice), bitmaps on its
 * textures (ID2D1DeviceContext::CreateBitmapFromDxgiSurface), and
 * D2D1CreateDeviceContext.  The device contexts themselves are render
 * targets (target.c, TGT_CONTEXT).
 *
 * Drawing is still done in software.  A bitmap on a DXGI surface keeps
 * its pixels in memory like any other bitmap: a device context drawing
 * into it reads the texture back (through a staging copy) at BeginDraw
 * and writes its pixels into the texture (UpdateSubresource) at Flush and
 * EndDraw, so it works on any Direct3D 11 device, NovaOS's software one
 * as well as DXVK's on a GPU.  B8G8R8A8, B8G8R8X8 and R8G8B8A8 textures
 * can be drawn into.
 *
 * The device answers NovaOS's private INovaD2DDevice (novadx.h) with its
 * Direct3D 11 device, which DirectComposition and Windows.UI.Composition
 * use when they are given an ID2D1Device to make their surfaces on.
 */
#include "d2d_int.h"
#include <novadx.h>
#include <stddef.h>

#define D3D11_BIND_RENDER_TARGET_   0x20
#define D3D11_USAGE_STAGING_        3
#define D3D11_CPU_ACCESS_READ_      0x20000

typedef struct {
    const void *vtbl;
    const void *nova_vtbl;          /* INovaD2DDevice */
    LONG ref;
    void *factory;
    IUnknown *dxgi;                 /* the IDXGIDevice (counted) */
    IUnknown *d3d;                  /* its ID3D11Device (counted) */
    UINT64 max_memory;
} Device;

static const void *const dev_vtbl[];
#define DEV_OF_NOVA(p) ((Device *)((char *)(p) - offsetof(Device, nova_vtbl)))

static HRESULT STDMETHODCALLTYPE dv_qi(Device *d, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ID2D1Resource) || IsEqualIID(riid, &IID_ID2D1Device)) {
        *out = d;
        InterlockedIncrement(&d->ref);
        return S_OK;
    }
    if (IsEqualIID(riid, &NIID_INovaD2DDevice)) {
        *out = &d->nova_vtbl;
        InterlockedIncrement(&d->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE dv_addref(Device *d) { return InterlockedIncrement(&d->ref); }
static ULONG STDMETHODCALLTYPE dv_release(Device *d)
{
    LONG r = InterlockedDecrement(&d->ref);
    if (!r) {
        COM_RELEASE(d->d3d);
        COM_RELEASE(d->dxgi);
        factory_release(d->factory);
        d_free(d);
    }
    return r;
}
static void STDMETHODCALLTYPE dv_factory(Device *d, void **f) { *f = d->factory; factory_addref(d->factory); }

static HRESULT STDMETHODCALLTYPE dv_context(Device *d, UINT32 options, void **out)
{
    (void)options;
    if (!out) return E_POINTER;
    *out = 0;
    RTPROPS p = { 0, { 87, 1 }, 96, 96, 0, 0 };
    Target *t;
    HRESULT hr = target_create(d->factory, TGT_CONTEXT, &p, &t);
    if (FAILED(hr)) return hr;
    t->device = d;
    dv_addref(d);
    *out = t;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dv_print(Device *d, void *wic, void *target, const void *props, void **out)
{
    (void)d; (void)wic; (void)target; (void)props;
    if (out) *out = 0;
    return E_NOTIMPL;
}
static void STDMETHODCALLTYPE dv_set_memory(Device *d, UINT64 n) { d->max_memory = n; }
static UINT64 STDMETHODCALLTYPE dv_get_memory(Device *d) { return d->max_memory; }
static void STDMETHODCALLTYPE dv_clear(Device *d, UINT32 ms) { (void)d; (void)ms; }
static const void *const dev_vtbl[] = { dv_qi, dv_addref, dv_release, dv_factory, dv_context, dv_print, dv_set_memory,
                                        dv_get_memory, dv_clear };

static HRESULT STDMETHODCALLTYPE nv_qi(void *p, REFIID riid, void **out) { return dv_qi(DEV_OF_NOVA(p), riid, out); }
static ULONG STDMETHODCALLTYPE nv_addref(void *p) { return dv_addref(DEV_OF_NOVA(p)); }
static ULONG STDMETHODCALLTYPE nv_release(void *p) { return dv_release(DEV_OF_NOVA(p)); }
static IUnknown *STDMETHODCALLTYPE nv_d3d(void *p)
{
    IUnknown *d = DEV_OF_NOVA(p)->d3d;
    d->lpVtbl->AddRef(d);
    return d;
}
static const void *const nova_vtbl[] = { nv_qi, nv_addref, nv_release, nv_d3d };

HRESULT device_create(void *factory, IUnknown *dxgi, void **out)
{
    *out = 0;
    IUnknown *d3d = NULL;
    if (FAILED(dxgi->lpVtbl->QueryInterface(dxgi, &NIID_ID3D11Device, (void **)&d3d)) || !d3d) return E_INVALIDARG;
    Device *d = d_alloc(sizeof(*d));
    if (!d) { d3d->lpVtbl->Release(d3d); return E_OUTOFMEMORY; }
    d->vtbl = dev_vtbl;
    d->nova_vtbl = nova_vtbl;
    d->ref = 1;
    d->factory = factory;
    factory_addref(factory);
    d->dxgi = dxgi;
    dxgi->lpVtbl->AddRef(dxgi);
    d->d3d = d3d;
    d->max_memory = 0x8000000;
    *out = d;
    return S_OK;
}

/* ---- bitmaps on DXGI surfaces ---- */
typedef HRESULT (STDMETHODCALLTYPE *CreateTexture2D_t)(IUnknown *, const ND3D11_TEXTURE2D_DESC *, const void *, IUnknown **);
typedef void (STDMETHODCALLTYPE *GetDesc_t)(IUnknown *, ND3D11_TEXTURE2D_DESC *);
typedef void (STDMETHODCALLTYPE *GetDevice_t)(IUnknown *, IUnknown **);
typedef void (STDMETHODCALLTYPE *UpdateSubresource_t)(IUnknown *, IUnknown *, UINT, const ND3D11_BOX *, const void *, UINT, UINT);

static int rgba(UINT32 f) { return f == NDXGI_FORMAT_R8G8B8A8_UNORM || f == NDXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }

void bitmap_pull(Bitmap *b)
{
    if (!b->tex || !b->staging) return;
    novadx_read_back(b->d3dctx, b->staging, b->tex, b->dxgi_format, b->s.w, b->s.h, (BYTE *)b->s.px, NULL);
    if (b->dxgi_format == NDXGI_FORMAT_B8G8R8X8_UNORM || b->fmt.alphaMode == 3)
        for (size_t i = 0; i < (size_t)b->s.w * b->s.h; i++) b->s.px[i] |= 0xFF000000u;
}

void bitmap_push(Bitmap *b)
{
    if (!b->tex) return;
    const UINT32 *src = b->s.px;
    UINT32 *swapped = NULL;
    if (rgba(b->dxgi_format)) {
        swapped = d_alloc(sizeof(UINT32) * b->s.w * b->s.h);
        if (!swapped) return;
        for (size_t i = 0; i < (size_t)b->s.w * b->s.h; i++) {
            UINT32 v = b->s.px[i];
            swapped[i] = (v & 0xFF00FF00u) | (v >> 16 & 0xFF) | (v & 0xFF) << 16;
        }
        src = swapped;
    }
    ((UpdateSubresource_t)(*(void ***)b->d3dctx)[48])(b->d3dctx, b->tex, 0, NULL, src, b->s.w * 4, 0);
    d_free(swapped);
}

void bitmap_drop_surface(Bitmap *b)
{
    COM_RELEASE(b->staging);
    COM_RELEASE(b->d3dctx);
    COM_RELEASE(b->tex);
    b->staging = b->d3dctx = b->tex = NULL;
}

HRESULT bitmap_from_surface(void *factory, IUnknown *surface, const BMPPROPS1 *props, Bitmap **out)
{
    *out = 0;
    if (!surface) return E_INVALIDARG;
    IUnknown *tex = NULL, *dev = NULL, *ctx = NULL, *staging = NULL;
    if (FAILED(surface->lpVtbl->QueryInterface(surface, &NIID_ID3D11Texture2D, (void **)&tex)) || !tex) return E_INVALIDARG;
    ND3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof(td));
    ((GetDesc_t)(*(void ***)tex)[10])(tex, &td);
    UINT32 f = td.Format, fmt;
    if (f == NDXGI_FORMAT_B8G8R8A8_UNORM || f == NDXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == NDXGI_FORMAT_B8G8R8X8_UNORM) fmt = 87;
    else if (rgba(f)) fmt = 28;
    else { tex->lpVtbl->Release(tex); return D2DERR_UNSUPPORTED_PIXEL_FORMAT; }
    ((GetDevice_t)(*(void ***)tex)[3])(tex, &dev);                        /* ID3D11DeviceChild::GetDevice */
    if (dev) ((GetDevice_t)(*(void ***)dev)[40])(dev, &ctx);              /* GetImmediateContext */
    ND3D11_TEXTURE2D_DESC sd = td;
    sd.MipLevels = sd.ArraySize = 1;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Usage = D3D11_USAGE_STAGING_;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ_;
    sd.MiscFlags = 0;
    HRESULT hr = dev && ctx ? ((CreateTexture2D_t)(*(void ***)dev)[5])(dev, &sd, NULL, &staging) : E_FAIL;
    COM_RELEASE(dev);
    if (FAILED(hr)) { COM_RELEASE(ctx); tex->lpVtbl->Release(tex); return hr; }
    BMPPROPS bp = { { fmt, f == NDXGI_FORMAT_B8G8R8X8_UNORM ? 3u : 1u }, 96, 96 };
    if (props) {
        if (props->pixelFormat.alphaMode) bp.pixelFormat.alphaMode = props->pixelFormat.alphaMode;
        if (props->dpiX > 0 && props->dpiY > 0) { bp.dpiX = props->dpiX; bp.dpiY = props->dpiY; }
    }
    if (bp.pixelFormat.alphaMode == 2) bp.pixelFormat.alphaMode = 1;   /* (stored premultiplied) */
    SZU size = { td.Width, td.Height };
    Bitmap *b;
    hr = bitmap_create(factory, size, NULL, 0, &bp, &b);
    if (FAILED(hr)) { staging->lpVtbl->Release(staging); ctx->lpVtbl->Release(ctx); tex->lpVtbl->Release(tex); return hr; }
    b->tex = tex;
    b->d3dctx = ctx;
    b->staging = staging;
    b->dxgi_format = f;
    /* without properties the options follow the texture's: a render
       target can be drawn into; a CPU-readable texture can be mapped */
    b->options = props ? props->bitmapOptions :
                 (td.BindFlags & D3D11_BIND_RENDER_TARGET_ ? 1u : 0u) | (td.CPUAccessFlags & D3D11_CPU_ACCESS_READ_ ? 6u : 0u);
    bitmap_pull(b);
    *out = b;
    return S_OK;
}

/* ---- the exports ---- */
/* D2D1_CREATION_PROPERTIES */
typedef struct { UINT32 threadingMode, debugLevel, options; } CREATIONPROPS;

D2D HRESULT WINAPI D2D1CreateDevice(IUnknown *dxgi, const CREATIONPROPS *props, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (!dxgi) return E_INVALIDARG;
    Factory *f = factory_new(props && props->threadingMode == 1 ? 1 : 0);
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = device_create(f, dxgi, out);
    factory_release(f);
    return hr;
}

D2D HRESULT WINAPI D2D1CreateDeviceContext(IUnknown *surface, const CREATIONPROPS *props, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (!surface) return E_INVALIDARG;
    IUnknown *tex = NULL, *d3d = NULL, *dxgi = NULL;
    if (FAILED(surface->lpVtbl->QueryInterface(surface, &NIID_ID3D11Texture2D, (void **)&tex)) || !tex) return E_INVALIDARG;
    ((GetDevice_t)(*(void ***)tex)[3])(tex, &d3d);
    tex->lpVtbl->Release(tex);
    if (!d3d) return E_INVALIDARG;
    HRESULT hr = d3d->lpVtbl->QueryInterface(d3d, &NIID_IDXGIDevice, (void **)&dxgi);
    d3d->lpVtbl->Release(d3d);
    if (FAILED(hr)) return hr;
    void *dev = NULL, *ctx = NULL;
    hr = D2D1CreateDevice(dxgi, props, &dev);
    dxgi->lpVtbl->Release(dxgi);
    if (FAILED(hr)) return hr;
    hr = dv_context(dev, props ? props->options : 0, &ctx);
    dv_release(dev);
    if (FAILED(hr)) return hr;
    Bitmap *b;
    hr = bitmap_from_surface(((Target *)ctx)->factory, surface, NULL, &b);
    if (FAILED(hr)) { COM_RELEASE(ctx); return hr; }
    context_set_target(ctx, b);
    COM_RELEASE(b);
    *out = ctx;
    return S_OK;
}
