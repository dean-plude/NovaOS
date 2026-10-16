/*
 * novadx.h — what NovaOS's dxgi.dll, d3d11.dll and dcomp.dll share for
 * Direct3D without a GPU driver: the few DXGI, Direct3D 11 and
 * DirectComposition types they pass each other, and NovaOS's private
 * interfaces between them.
 *
 * Windows always has an adapter: on a PC with no display driver, and next
 * to every real GPU, it lists the "Microsoft Basic Render Driver" (WARP,
 * VendorId 0x1414, DeviceId 0x8c).  Chromium's software compositor (the GPU
 * process of WebView2, Steam's and Galaxy's browsers) draws its frames into
 * a mapped Direct3D 11 staging texture on WARP, copies them to a DXGI 1.2
 * swap chain made for DirectComposition, and shows that through a
 * DirectComposition visual on its window.  Without DXVK, NovaOS's three DLLs
 * provide exactly that path:
 *   dxgi.dll   the factory (IDXGIFactory7), the Basic Render Driver adapter,
 *              and swap chains on a software device;
 *   d3d11.dll  the software device (64-bit): textures in memory that the CPU
 *              maps, updates and copies; it does not rasterize;
 *   dcomp.dll  devices, targets and visuals; a visual whose content is a swap
 *              chain gets its presents and draws them into the target's
 *              window with GDI.
 * With DXVK, dxgi.dll also makes composition swap chains on the GPU device
 * (DXVK makes none): the program renders into a texture of that device, and
 * each present reads the frame back for the visual.
 */
#pragma once
#include <windows.h>
#include <objbase.h>

#define NOVADX_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    static const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }

/* ---- Windows' interfaces ------------------------------------------------- */
NOVADX_GUID(NIID_IUnknown,              0x00000000, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
NOVADX_GUID(NIID_IDXGIObject,           0xaec22fb8, 0x76f3, 0x4639, 0x9b, 0xe0, 0x28, 0xeb, 0x43, 0xa6, 0x7a, 0x2e);
NOVADX_GUID(NIID_IDXGIDeviceSubObject,  0x3d3e0379, 0xf9de, 0x4d58, 0xbb, 0x6c, 0x18, 0xd6, 0x29, 0x92, 0xf1, 0xa6);
NOVADX_GUID(NIID_IDXGIFactory,          0x7b7166ec, 0x21c7, 0x44ae, 0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69);
NOVADX_GUID(NIID_IDXGIFactory1,         0x770aae78, 0xf26f, 0x4dba, 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87);
NOVADX_GUID(NIID_IDXGIFactory2,         0x50c83a1c, 0xe072, 0x4c48, 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0);
NOVADX_GUID(NIID_IDXGIFactory3,         0x25483823, 0xcd46, 0x4c7d, 0x86, 0xca, 0x47, 0xaa, 0x95, 0xb8, 0x37, 0xbd);
NOVADX_GUID(NIID_IDXGIFactory4,         0x1bc6ea02, 0xef36, 0x464f, 0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a);
NOVADX_GUID(NIID_IDXGIFactory5,         0x7632e1f5, 0xee65, 0x4dca, 0x87, 0xfd, 0x84, 0xcd, 0x75, 0xf8, 0x83, 0x8d);
NOVADX_GUID(NIID_IDXGIFactory6,         0xc1b6694f, 0xff09, 0x44a9, 0xb0, 0x3c, 0x77, 0x90, 0x0a, 0x0a, 0x1d, 0x17);
NOVADX_GUID(NIID_IDXGIFactory7,         0xa4966eed, 0x76db, 0x44da, 0x84, 0xc1, 0xee, 0x9a, 0x7a, 0xfb, 0x20, 0xa8);
NOVADX_GUID(NIID_IDXGIAdapter,          0x2411e7e1, 0x12ac, 0x4ccf, 0xbd, 0x14, 0x97, 0x98, 0xe8, 0x53, 0x4d, 0xc0);
NOVADX_GUID(NIID_IDXGIAdapter1,         0x29038f61, 0x3839, 0x4626, 0x91, 0xfd, 0x08, 0x68, 0x79, 0x01, 0x1a, 0x05);
NOVADX_GUID(NIID_IDXGIAdapter2,         0x0aa1ae0a, 0xfa0e, 0x4b84, 0x86, 0x44, 0xe0, 0x5f, 0xf8, 0xe5, 0xac, 0xb5);
NOVADX_GUID(NIID_IDXGIAdapter3,         0x645967a4, 0x1392, 0x4310, 0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd);
NOVADX_GUID(NIID_IDXGIAdapter4,         0x3c8d99d1, 0x4fbf, 0x4181, 0xa8, 0x2c, 0xaf, 0x66, 0xbf, 0x7b, 0xd2, 0x4e);
NOVADX_GUID(NIID_IDXGIDevice,           0x54ec77fa, 0x1377, 0x44e6, 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c);
NOVADX_GUID(NIID_IDXGIDevice1,          0x77db970f, 0x6276, 0x48ba, 0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c);
NOVADX_GUID(NIID_IDXGISwapChain,        0x310d36a0, 0xd2e7, 0x4c0a, 0xaa, 0x04, 0x6a, 0x9d, 0x23, 0xb8, 0x88, 0x6a);
NOVADX_GUID(NIID_IDXGISwapChain1,       0x790a45f7, 0x0d42, 0x4876, 0x98, 0x3a, 0x0a, 0x55, 0xcf, 0xe6, 0xf4, 0xaa);
NOVADX_GUID(NIID_IDXGISwapChain2,       0xa8be2ac4, 0x199f, 0x4946, 0xb3, 0x31, 0x79, 0x59, 0x9f, 0xb9, 0x8d, 0xe7);
NOVADX_GUID(NIID_IDXGISwapChain3,       0x94d99bdb, 0xf1f8, 0x4ab0, 0xb2, 0x36, 0x7d, 0xa0, 0x17, 0x0e, 0xda, 0xb1);
NOVADX_GUID(NIID_IDXGISwapChain4,       0x3d585d5a, 0xbd4a, 0x489e, 0xb1, 0xf4, 0x3d, 0xbc, 0xb6, 0x45, 0x2f, 0xfb);
NOVADX_GUID(NIID_ID3D11Device,          0xdb6f6ddb, 0xac77, 0x4e88, 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40);
NOVADX_GUID(NIID_ID3D11DeviceChild,     0x1841e5c8, 0x16b0, 0x489b, 0xbc, 0xc8, 0x44, 0xcf, 0xb0, 0xd5, 0xde, 0xae);
NOVADX_GUID(NIID_ID3D11DeviceContext,   0xc0bfa96c, 0xe089, 0x44fb, 0x8e, 0xaf, 0x26, 0xf8, 0x79, 0x61, 0x90, 0xda);
NOVADX_GUID(NIID_ID3D11Resource,        0xdc8e63f3, 0xd12b, 0x4952, 0xb4, 0x7b, 0x5e, 0x45, 0x02, 0x6a, 0x86, 0x2d);
NOVADX_GUID(NIID_IDXGISurface,          0xcafcb56c, 0x6ac3, 0x4889, 0xbf, 0x47, 0x9e, 0x23, 0xbb, 0xd2, 0x60, 0xec);
NOVADX_GUID(NIID_ID2D1Device,           0x47dd575d, 0xac05, 0x4cdd, 0x80, 0x49, 0x9b, 0x02, 0xcd, 0x16, 0xf4, 0x4c);
NOVADX_GUID(NIID_ID2D1DeviceContext,    0xe8f7fe7a, 0x191c, 0x466d, 0xad, 0x95, 0x97, 0x56, 0x78, 0xbd, 0xa9, 0x98);
NOVADX_GUID(NIID_ID3D11Texture2D,       0x6f15aaf2, 0xd208, 0x4e89, 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c);
NOVADX_GUID(NIID_IDCompositionDevice,   0xc37ea93a, 0xe7aa, 0x450d, 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3);
NOVADX_GUID(NIID_IDCompositionTarget,   0xeacdd04c, 0x117e, 0x4e17, 0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89);
NOVADX_GUID(NIID_IDCompositionVisual,   0x4d93059d, 0x097b, 0x4651, 0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3);
NOVADX_GUID(NIID_IDCompositionVisual2,  0xe8de1639, 0x4331, 0x4b26, 0xbc, 0x5f, 0x6a, 0x32, 0x1d, 0x34, 0x7a, 0x85);
NOVADX_GUID(NIID_IDCompositionVisualDebug, 0xfed2b808, 0x5eb4, 0x43a0, 0xae, 0xa3, 0x35, 0xf6, 0x52, 0x80, 0xf9, 0x1b);
NOVADX_GUID(NIID_IDCompositionVisual3,  0x2775f462, 0xb6c1, 0x4015, 0xb0, 0xbe, 0xb3, 0xe7, 0xd6, 0xa4, 0x97, 0x6d);
NOVADX_GUID(NIID_IDCompositionDevice2,  0x75f6468d, 0x1b8e, 0x447c, 0x9b, 0xc6, 0x75, 0xfe, 0xa8, 0x0b, 0x5b, 0x25);
NOVADX_GUID(NIID_IDCompositionDesktopDevice, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
NOVADX_GUID(NIID_IDCompositionDevice3,  0x0987cb06, 0xf916, 0x48bf, 0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9);
NOVADX_GUID(NIID_IDCompositionSurfaceFactory, 0xe334bc12, 0x3937, 0x4e02, 0x85, 0xeb, 0xfc, 0xf4, 0xeb, 0x30, 0xd2, 0xc8);
NOVADX_GUID(NIID_IDCompositionSurface,  0xbb8a4953, 0x2c99, 0x4f5a, 0x96, 0xf5, 0x48, 0x19, 0x02, 0x7f, 0xa3, 0xac);
NOVADX_GUID(NIID_IDCompositionVirtualSurface, 0xae471c51, 0x5f53, 0x4a24, 0x8d, 0x3e, 0xd0, 0xc3, 0x9c, 0x30, 0xb3, 0xf0);
NOVADX_GUID(NIID_IDCompositionClip,     0x64ac3703, 0x9d3f, 0x45ec, 0xa1, 0x09, 0x7c, 0xac, 0x0e, 0x7a, 0x13, 0xa7);
NOVADX_GUID(NIID_IDCompositionRectangleClip, 0x9842ad7d, 0xd9cf, 0x4908, 0xae, 0xd7, 0x48, 0xb5, 0x1d, 0xa5, 0xe7, 0xc2);
NOVADX_GUID(NIID_IDCompositionEffect,   0xec81b08f, 0xbfcb, 0x4e8d, 0xb1, 0x93, 0xa9, 0x15, 0x58, 0x79, 0x99, 0xe8);
NOVADX_GUID(NIID_IDCompositionEffectGroup, 0xa7929a74, 0xe6b2, 0x4bd6, 0x8b, 0x95, 0x40, 0x40, 0x11, 0x9c, 0xa3, 0x4d);

/* ---- NovaOS's own, between the three DLLs -------------------------------- */
NOVADX_GUID(NIID_INovaSoftDevice,       0x6e6f7661, 0x0001, 0x4458, 0x91, 0x00, 0x73, 0x6f, 0x66, 0x74, 0x64, 0x78);
NOVADX_GUID(NIID_INovaSoftTexture,      0x6e6f7661, 0x0002, 0x4458, 0x91, 0x00, 0x73, 0x6f, 0x66, 0x74, 0x64, 0x78);
NOVADX_GUID(NIID_INovaSwapChain,        0x6e6f7661, 0x0003, 0x4458, 0x91, 0x00, 0x73, 0x6f, 0x66, 0x74, 0x64, 0x78);
NOVADX_GUID(NIID_INovaSoftAdapter,      0x6e6f7661, 0x0004, 0x4458, 0x91, 0x00, 0x73, 0x6f, 0x66, 0x74, 0x64, 0x78);
NOVADX_GUID(NIID_INovaD2DDevice,        0x6e6f7661, 0x0005, 0x4458, 0x91, 0x00, 0x73, 0x6f, 0x66, 0x74, 0x64, 0x78);

static inline int novadx_same(const GUID *a, const GUID *b) { return !__builtin_memcmp(a, b, sizeof(GUID)); }

/* ---- Types --------------------------------------------------------------- */
#define NDXGI_FORMAT_R16G16B16A16_FLOAT   10
#define NDXGI_FORMAT_R10G10B10A2_UNORM    24
#define NDXGI_FORMAT_R8G8B8A8_TYPELESS    27
#define NDXGI_FORMAT_R8G8B8A8_UNORM       28
#define NDXGI_FORMAT_R8G8B8A8_UNORM_SRGB  29
#define NDXGI_FORMAT_R32_FLOAT            41
#define NDXGI_FORMAT_R8G8_UNORM           49
#define NDXGI_FORMAT_R8_UNORM             61
#define NDXGI_FORMAT_A8_UNORM             65
#define NDXGI_FORMAT_B8G8R8A8_UNORM       87
#define NDXGI_FORMAT_B8G8R8X8_UNORM       88
#define NDXGI_FORMAT_B8G8R8A8_TYPELESS    90
#define NDXGI_FORMAT_B8G8R8A8_UNORM_SRGB  91

#define NDXGI_ERROR_NOT_FOUND        ((HRESULT)0x887A0002L)
#define NDXGI_ERROR_MORE_DATA        ((HRESULT)0x887A0003L)
#define NDXGI_ERROR_UNSUPPORTED      ((HRESULT)0x887A0004L)
#define NDXGI_ERROR_INVALID_CALL     ((HRESULT)0x887A0001L)
#define NDXGI_ERROR_WAS_STILL_DRAWING ((HRESULT)0x887A000AL)

typedef struct { UINT Count, Quality; } NDXGI_SAMPLE_DESC;

typedef struct {                        /* D3D11_TEXTURE2D_DESC */
    UINT Width, Height, MipLevels, ArraySize, Format;
    NDXGI_SAMPLE_DESC SampleDesc;
    UINT Usage, BindFlags, CPUAccessFlags, MiscFlags;
} ND3D11_TEXTURE2D_DESC;

typedef struct { const void *pSysMem; UINT SysMemPitch, SysMemSlicePitch; } ND3D11_SUBRESOURCE_DATA;
typedef struct { void *pData; UINT RowPitch, DepthPitch; } ND3D11_MAPPED_SUBRESOURCE;
typedef struct { UINT left, top, front, right, bottom, back; } ND3D11_BOX;

typedef struct {                        /* DXGI_SWAP_CHAIN_DESC1 */
    UINT Width, Height, Format;
    BOOL Stereo;
    NDXGI_SAMPLE_DESC SampleDesc;
    UINT BufferUsage, BufferCount, Scaling, SwapEffect, AlphaMode, Flags;
} NDXGI_SWAP_CHAIN_DESC1;

typedef struct {                        /* DXGI_MODE_DESC */
    UINT Width, Height;
    struct { UINT Numerator, Denominator; } RefreshRate;
    UINT Format, ScanlineOrdering, Scaling;
} NDXGI_MODE_DESC;

typedef struct {                        /* DXGI_SWAP_CHAIN_DESC */
    NDXGI_MODE_DESC BufferDesc;
    NDXGI_SAMPLE_DESC SampleDesc;
    UINT BufferUsage, BufferCount;
    HWND OutputWindow;
    BOOL Windowed;
    UINT SwapEffect, Flags;
} NDXGI_SWAP_CHAIN_DESC;

typedef struct { UINT DirtyRectsCount; RECT *pDirtyRects; RECT *pScrollRect; POINT *pScrollOffset; } NDXGI_PRESENT_PARAMETERS;

typedef struct {                        /* DXGI_ADAPTER_DESC3 (DESC, DESC1 and DESC2 are its beginnings) */
    WCHAR Description[128];
    UINT VendorId, DeviceId, SubSysId, Revision;
    SIZE_T DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory;
    LUID AdapterLuid;
    UINT Flags;
    UINT GraphicsPreemptionGranularity, ComputePreemptionGranularity;
} NDXGI_ADAPTER_DESC3;

#define NDXGI_ADAPTER_FLAG_SOFTWARE 2

/* The Basic Render Driver's numbers, as Windows reports them */
#define NOVADX_WARP_VENDOR  0x1414
#define NOVADX_WARP_DEVICE  0x008c
#define NOVADX_WARP_NAME    L"Microsoft Basic Render Driver"

/* ---- The private interfaces ---------------------------------------------- */
/* A software texture (d3d11.dll): its pixels, rows @pitch bytes apart */
typedef struct INovaSoftTexture INovaSoftTexture;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(INovaSoftTexture *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(INovaSoftTexture *);
    ULONG   (STDMETHODCALLTYPE *Release)(INovaSoftTexture *);
    BYTE   *(STDMETHODCALLTYPE *Bits)(INovaSoftTexture *, UINT *pitch, ND3D11_TEXTURE2D_DESC *desc);
} INovaSoftTextureVtbl;
struct INovaSoftTexture { const INovaSoftTextureVtbl *lpVtbl; };

/* A swap chain (dxgi.dll) shown by a DirectComposition visual (dcomp.dll):
 * the visual sets a sink, which every present calls (@dirty: the part
 * that changed, or NULL for all of it).  A GPU's chain reads a frame back
 * only when Frame asks for it: dcomp.dll composites those on the GPU
 * (Texture) when it can */
typedef void (STDMETHODCALLTYPE *NovaPresentSink)(void *ctx, const RECT *dirty);
typedef struct INovaSwapChain INovaSwapChain;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(INovaSwapChain *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(INovaSwapChain *);
    ULONG   (STDMETHODCALLTYPE *Release)(INovaSwapChain *);
    void    (STDMETHODCALLTYPE *SetSink)(INovaSwapChain *, NovaPresentSink sink, void *ctx);
    /* the last presented frame: 32-bit BGRA pixels */
    BYTE   *(STDMETHODCALLTYPE *Frame)(INovaSwapChain *, UINT *pitch, UINT *width, UINT *height);
    /* a GPU's: its buffer, a texture of *@device (neither counted), its
     * format and size, and whether it was presented since it was made
     * (else its contents mean nothing yet); NULL for the software device's */
    IUnknown *(STDMETHODCALLTYPE *Texture)(INovaSwapChain *, IUnknown **device, UINT *format, UINT *width, UINT *height,
                                         BOOL *shown);
} INovaSwapChainVtbl;
struct INovaSwapChain { const INovaSwapChainVtbl *lpVtbl; };

/* A Direct2D device (d2d1.dll's ID2D1Device) answers this with the
 * Direct3D 11 device it was made on, so DirectComposition and
 * Windows.UI.Composition (dcomp.dll), given an ID2D1Device as their
 * rendering device, make their surfaces' textures on that device */
typedef struct INovaD2DDevice INovaD2DDevice;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(INovaD2DDevice *, const GUID *, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(INovaD2DDevice *);
    ULONG   (STDMETHODCALLTYPE *Release)(INovaD2DDevice *);
    IUnknown *(STDMETHODCALLTYPE *D3D11Device)(INovaD2DDevice *);     /* (counted) */
} INovaD2DDeviceVtbl;
struct INovaD2DDevice { const INovaD2DDeviceVtbl *lpVtbl; };

/* @rendering (an IDXGIDevice or ID3D11Device, or an ID2D1Device) as its
 * Direct3D 11 device (counted), and the ID2D1Device when it is one
 * (counted; else NULL) */
static inline IUnknown *novadx_rendering_d3d(IUnknown *rendering, IUnknown **d2d)
{
    IUnknown *d3d = NULL;
    *d2d = NULL;
    if (!rendering) return NULL;
    if (SUCCEEDED(rendering->lpVtbl->QueryInterface(rendering, &NIID_ID3D11Device, (void **)&d3d)) && d3d) return d3d;
    INovaD2DDevice *n = NULL;
    if (FAILED(rendering->lpVtbl->QueryInterface(rendering, &NIID_INovaD2DDevice, (void **)&n)) || !n) return NULL;
    d3d = n->lpVtbl->D3D11Device(n);
    n->lpVtbl->Release(n);
    if (d3d) {
        *d2d = rendering;
        rendering->lpVtbl->AddRef(rendering);
    }
    return d3d;
}

/* BeginDraw of a surface asked for an ID2D1DeviceContext: a device
 * context of @d2d (an ID2D1Device) drawing into @tex, the surface's
 * texture, already in BeginDraw, as Windows' DirectComposition hands it
 * out (the caller translates by the update offset itself).  EndDraw ends
 * the drawing and releases it */
static inline HRESULT novadx_d2d_begin(IUnknown *d2d, IUnknown *tex, IUnknown **dc)
{
    typedef HRESULT (STDMETHODCALLTYPE *CreateDC_t)(IUnknown *, UINT, IUnknown **);
    typedef HRESULT (STDMETHODCALLTYPE *FromSurface_t)(IUnknown *, IUnknown *, const void *, IUnknown **);
    typedef void    (STDMETHODCALLTYPE *SetTarget_t)(IUnknown *, IUnknown *);
    typedef void    (STDMETHODCALLTYPE *Call_t)(IUnknown *);
    IUnknown *c = NULL, *bmp = NULL;
    *dc = NULL;
    if (!d2d) return E_NOINTERFACE;
    HRESULT hr = ((CreateDC_t)(*(void ***)d2d)[4])(d2d, 0, &c);                 /* ID2D1Device::CreateDeviceContext */
    if (FAILED(hr)) return hr;
    hr = ((FromSurface_t)(*(void ***)c)[62])(c, tex, NULL, &bmp);              /* CreateBitmapFromDxgiSurface */
    if (FAILED(hr)) { c->lpVtbl->Release(c); return hr; }
    ((SetTarget_t)(*(void ***)c)[74])(c, bmp);                                  /* SetTarget */
    bmp->lpVtbl->Release(bmp);
    ((Call_t)(*(void ***)c)[48])(c);                                            /* BeginDraw */
    *dc = c;
    return S_OK;
}
static inline void novadx_d2d_end(IUnknown *dc)
{
    typedef HRESULT (STDMETHODCALLTYPE *EndDraw_t)(IUnknown *, UINT64 *, UINT64 *);
    typedef void    (STDMETHODCALLTYPE *SetTarget_t)(IUnknown *, IUnknown *);
    if (!dc) return;
    ((EndDraw_t)(*(void ***)dc)[49])(dc, NULL, NULL);                          /* EndDraw: the pixels reach the texture */
    ((SetTarget_t)(*(void ***)dc)[74])(dc, NULL);
    dc->lpVtbl->Release(dc);
}

/* The part @r (NULL: all) of @src, a @w x @h texture of a Direct3D 11
 * device whose immediate context is @ctx, read back into @frame (BGRA,
 * @w * 4 bytes a row) through @staging, a texture of the same size the CPU
 * reads; R8G8B8A8 pixels are swapped to BGRA */
static inline void novadx_read_back(IUnknown *ctx, IUnknown *staging, IUnknown *src, UINT format, UINT w, UINT h, BYTE *frame,
                                    const RECT *r)
{
    typedef void    (STDMETHODCALLTYPE *CopyRegion_t)(IUnknown *, IUnknown *, UINT, UINT, UINT, UINT, IUnknown *, UINT, const ND3D11_BOX *);
    typedef HRESULT (STDMETHODCALLTYPE *Map_t)(IUnknown *, IUnknown *, UINT, UINT, UINT, ND3D11_MAPPED_SUBRESOURCE *);
    typedef void    (STDMETHODCALLTYPE *Unmap_t)(IUnknown *, IUnknown *, UINT);
    void **vt = *(void ***)ctx;
    RECT a = { 0, 0, (LONG)w, (LONG)h };
    if (r) IntersectRect(&a, &a, r);
    if (IsRectEmpty(&a) || !frame) return;
    ND3D11_BOX box = { (UINT)a.left, (UINT)a.top, 0, (UINT)a.right, (UINT)a.bottom, 1 };
    ((CopyRegion_t)vt[46])(ctx, staging, 0, box.left, box.top, 0, src, 0, &box);       /* CopySubresourceRegion */
    ND3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(((Map_t)vt[14])(ctx, staging, 0, 1 /* D3D11_MAP_READ */, 0, &m)) || !m.pData) return;
    int rgba = format == NDXGI_FORMAT_R8G8B8A8_UNORM || format == NDXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    for (LONG y = a.top; y < a.bottom; y++) {
        const DWORD *s = (const DWORD *)((const BYTE *)m.pData + (size_t)y * m.RowPitch);
        DWORD *d = (DWORD *)(frame + (size_t)y * w * 4);
        for (LONG x = a.left; x < a.right; x++)
            d[x] = rgba ? (s[x] & 0xFF00FF00u) | (s[x] >> 16 & 0xFF) | (s[x] & 0xFF) << 16 : s[x];
    }
    ((Unmap_t)vt[15])(ctx, staging, 0);
}

/* Draw @w x @h BGRA pixels (rows @pitch bytes apart), the part @r of them
 * (NULL: all), at (@x, @y) of @dc */
static inline void novadx_blit(HDC dc, int x, int y, const BYTE *bits, UINT pitch, UINT w, UINT h, const RECT *r)
{
    RECT a = { 0, 0, (LONG)w, (LONG)h };
    if (r) {
        if (r->left > a.left) a.left = r->left;
        if (r->top > a.top) a.top = r->top;
        if (r->right < a.right) a.right = r->right;
        if (r->bottom < a.bottom) a.bottom = r->bottom;
    }
    if (a.right <= a.left || a.bottom <= a.top) return;
    BITMAPINFOHEADER bi;
    __builtin_memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = (LONG)(pitch / 4);
    bi.biHeight = -(a.bottom - a.top);                  /* (top-down: the rows from a.top on) */
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    StretchDIBits(dc, x + a.left, y + a.top, a.right - a.left, a.bottom - a.top, a.left, 0, a.right - a.left,
                  a.bottom - a.top, bits + (size_t)a.top * pitch, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
}
