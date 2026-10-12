/* dcomptest.exe — Direct3D without a GPU, the way Chromium's software
 * compositor (the GPU process of WebView2, Steam's and Galaxy's browsers)
 * draws: dxgi.dll's factory and its one adapter, the Microsoft Basic Render
 * Driver; a WARP device from d3d11.dll; a DirectComposition device, target
 * and visual (dcomp.dll) showing a swap chain made for composition; frames
 * drawn into a mapped staging texture, copied into the swap chain's buffer
 * and presented with dirty rectangles; a swap chain on a window; and the
 * window Chromium composites into, made by the GPU process as a child of a
 * hidden window of its own and parented in the browser's window by the
 * browser process (SetParent across processes).
 *
 *   dcomptest            runs the checks
 *   dcomptest dxvk       runs them with DXVK installed: programs' factory
 *                        and hardware devices are DXVK's (the GPU), while
 *                        a WARP device stays the software device, whose
 *                        swap chains (for composition too) that factory
 *                        still makes (WebView2's GPU process on Venus);
 *                        and a composition swap chain on the GPU device,
 *                        rendered on the GPU and shown by a visual; and
 *                        DirectComposition surfaces on the GPU device,
 *                        with the factory of the device's adapter
 *   dcomptest life       with DXVK: a composition swap chain and a surface
 *                        through window resizes, teardown while frames
 *                        are presented on another thread, and the GPU
 *                        process's window composited on the GPU, resized
 *                        and its process killed while presenting
 *   dcomptest direct     with DXVK on Venus: a composition swap chain on
 *                        the GPU device composited on the GPU, its frames
 *                        shown straight from the host while the window is
 *                        alone ("dcomp: quadrants at X Y W H": the
 *                        self-test checks the screen), through the GDI
 *                        while another window covers it, the GDI way again
 *                        once a swap chain of another device joins the tree
 *                        ("dcomp: yellow at X Y W H": the screen shows that
 *                        frame, not the last one from the host), and the
 *                        GPU process's window, embedded in the browser's,
 *                        shown straight from the host too
 *   dcomptest gpu HWND [life|direct]  the "GPU process": makes the child
 *                        window, posts it to HWND, and draws into it once
 *                        it is parented (life, direct: on the GPU, through
 *                        a visual)
 *
 * dxvk and life check the frames GDI's way (GetPixel on the window), so
 * they keep every frame on the GDI way (NOVA_PRESENT=gdi): one shown
 * straight from the host is on the screen only, not in the window's pixels.
 * The software device is 64-bit only; the 32-bit run checks the factory,
 * the adapter and the cross-process parenting (drawing with GDI). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <novadx.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)

/* method @i of COM object @o, as type @T */
#define VT(o, i, T) ((T)((void **)*(void **)(o))[i])
typedef HRESULT (STDMETHODCALLTYPE *QI_t)(void *, const GUID *, void **);
typedef ULONG   (STDMETHODCALLTYPE *Rel_t)(void *);
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)

#define DC_HELLO  (WM_APP + 1)      /* gpu -> host: wParam = its child window */
#define DC_DRAW   (WM_APP + 2)      /* host -> gpu (to the child window): wParam = the parent it should have */
#define DC_RESULT (WM_APP + 3)      /* gpu -> host: wParam = checks passed, lParam = failed ones (bits) */
#define DC_RESIZE (WM_APP + 4)      /* host -> gpu: wParam = the new width, lParam = height */
#define DC_SPIN   (WM_APP + 5)      /* host -> gpu: present frames on another thread until killed */
#define DC_SPINNING (WM_APP + 6)    /* gpu -> host: frames are being presented */

static const char *g_step = "start";
static HMODULE g_dxgi, g_d3d11, g_dcomp;
static int g_dxvk;                  /* "dcomptest dxvk": DXVK is installed */
static int g_life;                  /* "dcomptest life" (DXVK installed too) */
static int g_direct;                /* "dcomptest direct" (DXVK and Venus) */

static DWORD WINAPI watchdog(void *arg)
{
    (void)arg;
    Sleep(g_life || g_direct ? 300000 : 120000);
    printf("FAIL: hung at: %s\n", g_step);
    fflush(stdout);
    ExitProcess(3);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    do {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(10);
    } while ((LONG)(end - GetTickCount()) > 0);
}

static COLORREF pixel(HWND h, int x, int y)
{
    HDC dc = GetDC(h);
    COLORREF c = GetPixel(dc, x, y);
    ReleaseDC(h, dc);
    return c;
}

/* The kernel's display counters (NtNovaGpuCtl 14); [4]: frames shown by
 * direct presentation */
#define ST_DIRECT 4
typedef LONG_PTR (WINAPI *NtNovaGpuCtl_t)(INT_PTR, ULONG, ULONG_PTR, void *);
static int gpu_stats(unsigned long long st[8])
{
    static NtNovaGpuCtl_t ctl;
    memset(st, 0, 8 * sizeof(st[0]));
    if (!ctl) ctl = (NtNovaGpuCtl_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtNovaGpuCtl");
    return ctl && ctl(0, 14, 0, st) == 0;
}

/* The pointer out of the way (over a window it stops direct presentation) */
static void pointer_away(void) { SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 1, 0); }

/* until @cond, pumping messages, at most @ms */
#define WAIT_FOR(cond, ms) do { for (int w_ = 0; w_ < (ms) / 20 && !(cond); w_++) pump(20); } while (0)

/* ---- Direct3D helpers ----------------------------------------------------- */
typedef HRESULT (WINAPI *CreateFactory_t)(const GUID *, void **);
typedef HRESULT (WINAPI *CreateDevice_t)(void *, UINT, HMODULE, UINT, const UINT *, UINT, UINT, void **, UINT *, void **);
typedef HRESULT (WINAPI *CreateDeviceSC_t)(void *, UINT, HMODULE, UINT, const UINT *, UINT, UINT, const void *, void **,
                                           void **, UINT *, void **);
typedef HRESULT (WINAPI *CreateDComp_t)(void *, const GUID *, void **);
typedef HRESULT (STDMETHODCALLTYPE *Tex2D_t)(void *, const ND3D11_TEXTURE2D_DESC *, const void *, void **);
typedef HRESULT (STDMETHODCALLTYPE *Map_t)(void *, void *, UINT, UINT, UINT, ND3D11_MAPPED_SUBRESOURCE *);
typedef void    (STDMETHODCALLTYPE *Unmap_t)(void *, void *, UINT);
typedef void    (STDMETHODCALLTYPE *CopyRegion_t)(void *, void *, UINT, UINT, UINT, UINT, void *, UINT, const ND3D11_BOX *);
typedef HRESULT (STDMETHODCALLTYPE *GetBuffer_t)(void *, UINT, const GUID *, void **);
typedef HRESULT (STDMETHODCALLTYPE *Present_t)(void *, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *Present1_t)(void *, UINT, UINT, const NDXGI_PRESENT_PARAMETERS *);

#define D3D_DRIVER_TYPE_HARDWARE 1
#define D3D_DRIVER_TYPE_WARP     5
#define D3D11_SDK_VERSION        7
#define D3D11_USAGE_STAGING      3
#define D3D11_CPU_ACCESS_RW      0x30000
#define D3D11_MAP_READ_WRITE     3

/* The pieces of Chromium's software output device */
typedef struct { void *dev, *ctx, *factory, *dcomp, *target, *visual, *chain, *staging; } Out;

static void out_free(Out *o)
{
    RELEASE(o->staging); RELEASE(o->visual); RELEASE(o->target); RELEASE(o->chain);
    RELEASE(o->dcomp); RELEASE(o->factory); RELEASE(o->ctx); RELEASE(o->dev);
}

/* OutputDeviceBacking::GetOrCreateDXObjects and
 * SoftwareOutputDeviceWinSwapChain::ResizeDelegated, on @hwnd */
static int out_make(Out *o, HWND hwnd, UINT w, UINT h, int checks)
{
    memset(o, 0, sizeof(*o));
    CreateFactory_t cf = (CreateFactory_t)GetProcAddress(g_dxgi, "CreateDXGIFactory1");
    CreateDevice_t cd = (CreateDevice_t)GetProcAddress(g_d3d11, "D3D11CreateDevice");
    CreateDComp_t cdc = g_dcomp ? (CreateDComp_t)GetProcAddress(g_dcomp, "DCompositionCreateDevice") : NULL;
    int ok = cf && cd && cdc;
    ok = ok && SUCCEEDED(cf(&NIID_IDXGIFactory2, &o->factory));
    if (checks) CHECK("CreateDXGIFactory1(IDXGIFactory2)", ok);
    ok = ok && SUCCEEDED(cd(NULL, D3D_DRIVER_TYPE_WARP, NULL, 1 /* SINGLETHREADED */, NULL, 0, D3D11_SDK_VERSION, &o->dev, NULL, NULL));
    if (checks) CHECK("D3D11CreateDevice(WARP)", ok);
    ok = ok && SUCCEEDED(cdc(NULL, &NIID_IDCompositionDevice, &o->dcomp));
    if (checks) CHECK("DCompositionCreateDevice", ok);
    if (!ok) return 0;
    VT(o->dev, 40, void (STDMETHODCALLTYPE *)(void *, void **))(o->dev, &o->ctx);              /* GetImmediateContext */
    ok = SUCCEEDED(VT(o->dcomp, 6, HRESULT (STDMETHODCALLTYPE *)(void *, HWND, BOOL, void **))(o->dcomp, hwnd, TRUE, &o->target));
    ok = ok && SUCCEEDED(VT(o->dcomp, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(o->dcomp, &o->visual));
    ok = ok && SUCCEEDED(VT(o->target, 3, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(o->target, o->visual));   /* SetRoot */
    if (checks) CHECK("target, visual, SetRoot", ok);
    NDXGI_SWAP_CHAIN_DESC1 d = { w, h, NDXGI_FORMAT_B8G8R8A8_UNORM, FALSE, { 1, 0 }, 0, 2, 0 /* STRETCH */,
                                 3 /* FLIP_SEQUENTIAL */, 1 /* PREMULTIPLIED */, 0 };
    ok = ok && SUCCEEDED(VT(o->factory, 24, HRESULT (STDMETHODCALLTYPE *)(void *, void *, const void *, void *, void **))(
                   o->factory, o->dev, &d, NULL, &o->chain));                                   /* CreateSwapChainForComposition */
    if (checks) CHECK("CreateSwapChainForComposition", ok);
    ok = ok && SUCCEEDED(VT(o->visual, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(o->visual, o->chain));   /* SetContent */
    ok = ok && SUCCEEDED(VT(o->dcomp, 3, HRESULT (STDMETHODCALLTYPE *)(void *))(o->dcomp));    /* Commit */
    if (checks) CHECK("SetContent, Commit", ok);
    ND3D11_TEXTURE2D_DESC td = { w, h, 1, 1, NDXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_RW, 0 };
    ok = ok && SUCCEEDED(VT(o->dev, 5, Tex2D_t)(o->dev, &td, NULL, &o->staging));
    if (checks) CHECK("staging texture", ok);
    return ok;
}

/* BeginPaintDelegated / EndPaintDelegated: fill @r of the staging texture
 * with @bgra, copy that part into the swap chain's buffer, Present1 it */
static int out_frame(Out *o, RECT r, DWORD bgra)
{
    ND3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(VT(o->ctx, 14, Map_t)(o->ctx, o->staging, 0, D3D11_MAP_READ_WRITE, 0, &m)) || !m.pData) return 0;
    for (LONG y = r.top; y < r.bottom; y++)
        for (LONG x = r.left; x < r.right; x++) ((DWORD *)((BYTE *)m.pData + (size_t)y * m.RowPitch))[x] = bgra;
    VT(o->ctx, 15, Unmap_t)(o->ctx, o->staging, 0);
    void *buf = NULL;
    if (FAILED(VT(o->chain, 9, GetBuffer_t)(o->chain, 0, &NIID_ID3D11Texture2D, &buf))) return 0;
    ND3D11_BOX box = { (UINT)r.left, (UINT)r.top, 0, (UINT)r.right, (UINT)r.bottom, 1 };
    VT(o->ctx, 46, CopyRegion_t)(o->ctx, buf, 0, (UINT)r.left, (UINT)r.top, 0, o->staging, 0, &box);
    RELEASE(buf);
    NDXGI_PRESENT_PARAMETERS pp = { 1, &r, NULL, NULL };
    return SUCCEEDED(VT(o->chain, 22, Present1_t)(o->chain, 0, 0, &pp));
}

/* ---- The factory and the adapter (both widths) ---------------------------- */
static void check_adapter(void)
{
    g_step = "the factory";
    CreateFactory_t cf = (CreateFactory_t)GetProcAddress(g_dxgi, "CreateDXGIFactory1");
    void *f = NULL, *f7 = NULL, *a = NULL, *a4 = NULL, *x = NULL;
    CHECK("CreateDXGIFactory1", cf && SUCCEEDED(cf(&NIID_IDXGIFactory1, &f)));
    if (!f) return;
    CHECK("QueryInterface IDXGIFactory7", SUCCEEDED(VT(f, 0, QI_t)(f, &NIID_IDXGIFactory7, &f7)));
    typedef HRESULT (STDMETHODCALLTYPE *Enum_t)(void *, UINT, void **);
    CHECK("EnumAdapters1(0)", SUCCEEDED(VT(f, 12, Enum_t)(f, 0, &a)));
    CHECK("EnumAdapters1(1): one adapter", VT(f, 12, Enum_t)(f, 1, &x) == NDXGI_ERROR_NOT_FOUND && !x);
    if (!a) { RELEASE(f7); RELEASE(f); return; }
    NDXGI_ADAPTER_DESC3 d;
    memset(&d, 0, sizeof(d));
    CHECK("GetDesc1", SUCCEEDED(VT(a, 10, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(a, &d)));
    CHECK("the Microsoft Basic Render Driver", !wcscmp(d.Description, L"Microsoft Basic Render Driver"));
    CHECK("VendorId 0x1414, DeviceId 0x8c", d.VendorId == 0x1414 && d.DeviceId == 0x8c);
    CHECK("a software adapter (DXGI_ADAPTER_FLAG_SOFTWARE)", (d.Flags & NDXGI_ADAPTER_FLAG_SOFTWARE) != 0);
    CHECK("system memory, no video memory", d.SharedSystemMemory > 0 && !d.DedicatedVideoMemory);
    CHECK("EnumOutputs(0): render-only", VT(a, 7, Enum_t)(a, 0, &x) == NDXGI_ERROR_NOT_FOUND && !x);
    LARGE_INTEGER umd = { { 0, 0 } };
    CHECK("CheckInterfaceSupport(IDXGIDevice): the driver version",
          SUCCEEDED(VT(a, 9, HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, LARGE_INTEGER *))(a, &NIID_IDXGIDevice, &umd)) &&
          umd.HighPart >> 16 == 10);
    CHECK("QueryInterface IDXGIAdapter4", SUCCEEDED(VT(a, 0, QI_t)(a, &NIID_IDXGIAdapter4, &a4)));
    if (f7) {
        void *w = NULL, *l = NULL;
        CHECK("EnumWarpAdapter", SUCCEEDED(VT(f7, 27, HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, void **))(
                                      f7, &NIID_IDXGIAdapter1, &w)));
        CHECK("EnumAdapterByLuid", SUCCEEDED(VT(f7, 26, HRESULT (STDMETHODCALLTYPE *)(void *, LUID, const GUID *, void **))(
                                        f7, d.AdapterLuid, &NIID_IDXGIAdapter1, &l)));
        RELEASE(w); RELEASE(l);
    }
    RELEASE(a4); RELEASE(a); RELEASE(f7); RELEASE(f);

    g_step = "hardware devices";
    CreateDevice_t cd = (CreateDevice_t)GetProcAddress(g_d3d11, "D3D11CreateDevice");
    void *dev = NULL;
    CHECK("no hardware Direct3D 11 device without DXVK",
          cd && FAILED(cd(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, NULL)) && !dev);
    RELEASE(dev);
}

/* ---- With DXVK installed (the graphics suite) ------------------------------ */
/* The factory programs get is DXVK's, on the GPU, and a hardware device is
 * DXVK's; WARP stays the software device (check_composition) */
static void check_dxvk(void)
{
    g_step = "DXVK's factory";
    CreateFactory_t cf = (CreateFactory_t)GetProcAddress(g_dxgi, "CreateDXGIFactory1");
    void *f = NULL, *a = NULL, *dev = NULL;
    NDXGI_ADAPTER_DESC3 d;
    memset(&d, 0, sizeof(d));
    if (cf && SUCCEEDED(cf(&NIID_IDXGIFactory1, &f)) &&
        SUCCEEDED(VT(f, 12, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, void **))(f, 0, &a)))
        VT(a, 10, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(a, &d);
    CHECK("CreateDXGIFactory1's first adapter: the GPU, not the Basic Render Driver", a && d.VendorId != 0x1414);
    RELEASE(a); RELEASE(f);
    CreateDevice_t cd = (CreateDevice_t)GetProcAddress(g_d3d11, "D3D11CreateDevice");
    UINT level = 0;
    CHECK("a hardware Direct3D 11 device (DXVK), feature level 10_0 or higher",
          cd && SUCCEEDED(cd(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &level, NULL)) &&
          dev && level >= 0xa000);
    RELEASE(dev);
}

/* ---- A composition swap chain on the GPU (with DXVK, both widths) ---------- */
/* What Chromium's DirectComposition output does on a GPU: the device's
 * factory makes a swap chain for composition on the GPU device, a visual
 * shows it, and frames are rendered into its buffer on the GPU (cleared,
 * and a part copied in from another texture) and presented */
static void check_gpu_composition(HWND child)
{
    g_step = "a composition swap chain on the GPU";
    CreateFactory_t cf = (CreateFactory_t)GetProcAddress(g_dxgi, "CreateDXGIFactory1");
    CreateDevice_t cd = (CreateDevice_t)GetProcAddress(g_d3d11, "D3D11CreateDevice");
    CreateDComp_t cdc = g_dcomp ? (CreateDComp_t)GetProcAddress(g_dcomp, "DCompositionCreateDevice") : NULL;
    void *dev = NULL, *ctx = NULL, *f = NULL, *dc = NULL, *target = NULL, *visual = NULL, *chain = NULL;
    void *buf = NULL, *rtv = NULL, *part = NULL, *prtv = NULL, *owner = NULL, *got = NULL;
    int ok = cf && cd && cdc && SUCCEEDED(cd(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx)) &&
             SUCCEEDED(cf(&NIID_IDXGIFactory2, &f)) && SUCCEEDED(cdc(NULL, &NIID_IDCompositionDevice, &dc));
    CHECK("a hardware device, its factory, a DirectComposition device", ok);
    ok = ok && SUCCEEDED(VT(dc, 6, HRESULT (STDMETHODCALLTYPE *)(void *, HWND, BOOL, void **))(dc, child, TRUE, &target)) &&
         SUCCEEDED(VT(dc, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(dc, &visual)) &&
         SUCCEEDED(VT(target, 3, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(target, visual));
    NDXGI_SWAP_CHAIN_DESC1 d = { 200, 120, NDXGI_FORMAT_B8G8R8A8_UNORM, FALSE, { 1, 0 }, 0x20, 2, 0 /* STRETCH */,
                                 3 /* FLIP_SEQUENTIAL */, 1 /* PREMULTIPLIED */, 0 };
    ok = ok && SUCCEEDED(VT(f, 24, HRESULT (STDMETHODCALLTYPE *)(void *, void *, const void *, void *, void **))(f, dev, &d, NULL, &chain));
    CHECK("CreateSwapChainForComposition on the GPU device", ok);
    void *sc3 = NULL;
    UINT support = 0;
    ok = ok && SUCCEEDED(VT(chain, 0, QI_t)(chain, &NIID_IDXGISwapChain3, &sc3)) &&
         SUCCEEDED(VT(sc3, 37, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, UINT *))(sc3, 0, &support)) && (support & 1) &&
         SUCCEEDED(VT(sc3, 38, HRESULT (STDMETHODCALLTYPE *)(void *, UINT))(sc3, 0));   /* sRGB */
    CHECK("IDXGISwapChain3: CheckColorSpaceSupport, SetColorSpace1 (Chromium's)", ok);
    RELEASE(sc3);
    ok = ok && SUCCEEDED(VT(visual, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(visual, chain)) &&
         SUCCEEDED(VT(dc, 3, HRESULT (STDMETHODCALLTYPE *)(void *))(dc));
    CHECK("SetContent, Commit", ok);
    ok = ok && SUCCEEDED(VT(chain, 9, GetBuffer_t)(chain, 0, &NIID_ID3D11Texture2D, &buf));
    if (ok) VT(buf, 3, void (STDMETHODCALLTYPE *)(void *, void **))(buf, &owner);                 /* ID3D11DeviceChild::GetDevice */
    CHECK("its buffer: a texture of the GPU device", ok && owner == dev);
    ok = ok && SUCCEEDED(VT(chain, 7, HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, void **))(chain, &NIID_ID3D11Device, &got)) &&
         got == dev;
    CHECK("GetDevice: the GPU device", ok);
    typedef HRESULT (STDMETHODCALLTYPE *RTV_t)(void *, void *, const void *, void **);
    typedef void (STDMETHODCALLTYPE *Clear_t)(void *, void *, const float *);
    const float magenta[4] = { 1, 0, 1, 1 }, yellow[4] = { 1, 1, 0, 1 };
    ok = ok && SUCCEEDED(VT(dev, 9, RTV_t)(dev, buf, NULL, &rtv));
    if (ok) VT(ctx, 50, Clear_t)(ctx, rtv, magenta);                                              /* ClearRenderTargetView */
    CHECK("frame 1 rendered on the GPU and presented", ok && SUCCEEDED(VT(chain, 8, Present_t)(chain, 0, 0)));
    COLORREF c = pixel(child, 10, 10);
    CHECK("frame 1 in the window", c == RGB(255, 0, 255));
    if (c != RGB(255, 0, 255)) printf("pixel %06lx\n", (unsigned long)c);
    ND3D11_TEXTURE2D_DESC td = { 50, 50, 1, 1, NDXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, 0 /* DEFAULT */, 0x20 /* RENDER_TARGET */, 0, 0 };
    ok = ok && SUCCEEDED(VT(dev, 5, Tex2D_t)(dev, &td, NULL, &part)) && SUCCEEDED(VT(dev, 9, RTV_t)(dev, part, NULL, &prtv));
    RECT r = { 100, 50, 150, 100 };
    if (ok) {
        VT(ctx, 50, Clear_t)(ctx, prtv, yellow);
        VT(ctx, 46, CopyRegion_t)(ctx, buf, 0, 100, 50, 0, part, 0, NULL);
    }
    NDXGI_PRESENT_PARAMETERS pp = { 1, &r, NULL, NULL };
    CHECK("frame 2 (a dirty rectangle) presented", ok && SUCCEEDED(VT(chain, 22, Present1_t)(chain, 0, 0, &pp)));
    CHECK("frame 2: the rectangle drawn", pixel(child, 120, 70) == RGB(255, 255, 0));
    CHECK("frame 2: the rest kept", pixel(child, 10, 10) == RGB(255, 0, 255) && pixel(child, 160, 70) == RGB(255, 0, 255));
    RELEASE(prtv); RELEASE(part); RELEASE(rtv); RELEASE(got); RELEASE(owner); RELEASE(buf);
    RELEASE(chain); RELEASE(visual); RELEASE(target); RELEASE(dc); RELEASE(f); RELEASE(ctx); RELEASE(dev);
    CHECK("everything released", 1);
}

/* ---- Chromium's hardware DirectComposition path ---------------------------- */
/* What gl::InitializeDirectComposition, DCLayerTree and
 * DirectCompositionChildSurfaceWin do on ANGLE's device: an
 * IDCompositionDesktopDevice from DCompositionCreateDevice3 on the device,
 * also an IDCompositionDevice3; a target, a visual (IDCompositionVisual2,
 * also 3) and a DirectComposition surface drawn through BeginDraw and
 * EndDraw (the texture on the device, at the update rectangle's offset),
 * shown at Commit; a rectangle clip; and, on the GPU, the swap-chain
 * factory taken from the device's adapter (GetAdapter, GetParent), which
 * makes composition swap chains.  @gpu: on DXVK's hardware device, else on
 * the software device (WARP) */
static void fill(void *ctx, void *tex, LONG x, LONG y, LONG w, LONG h, DWORD bgra)
{
    typedef void (STDMETHODCALLTYPE *Update_t)(void *, void *, UINT, const ND3D11_BOX *, const void *, UINT, UINT);
    DWORD *px = malloc((size_t)w * h * 4);
    if (!px) return;
    for (LONG i = 0; i < w * h; i++) px[i] = bgra;
    ND3D11_BOX box = { (UINT)x, (UINT)y, 0, (UINT)(x + w), (UINT)(y + h), 1 };
    VT(ctx, 48, Update_t)(ctx, tex, 0, &box, px, (UINT)w * 4, 0);                      /* UpdateSubresource */
    free(px);
}

/* BeginDraw @r (NULL: all) of @surf, fill it with @bgra, EndDraw */
static int draw_surface(void *surf, void *dev, void *ctx, const RECT *r, DWORD bgra)
{
    typedef HRESULT (STDMETHODCALLTYPE *Begin_t)(void *, const RECT *, const GUID *, void **, POINT *);
    void *tex = NULL, *owner = NULL;
    POINT at = { -1, -1 };
    if (FAILED(VT(surf, 3, Begin_t)(surf, r, &NIID_ID3D11Texture2D, &tex, &at)) || !tex) return 0;
    VT(tex, 3, void (STDMETHODCALLTYPE *)(void *, void **))(tex, &owner);             /* ID3D11DeviceChild::GetDevice */
    int ok = owner == dev && at.x == (r ? r->left : 0) && at.y == (r ? r->top : 0);
    RELEASE(owner);
    RECT all = { 0, 0, 200, 120 };
    if (!r) r = &all;
    fill(ctx, tex, at.x, at.y, r->right - r->left, r->bottom - r->top, bgra);
    RELEASE(tex);
    return SUCCEEDED(VT(surf, 4, HRESULT (STDMETHODCALLTYPE *)(void *))(surf)) && ok;    /* EndDraw */
}

static void check_dcomp_surfaces(HWND child, int gpu)
{
    g_step = gpu ? "DirectComposition surfaces on the GPU" : "DirectComposition surfaces on the software device";
    CreateDevice_t cd = (CreateDevice_t)GetProcAddress(g_d3d11, "D3D11CreateDevice");
    CreateDComp_t cdc3 = g_dcomp ? (CreateDComp_t)GetProcAddress(g_dcomp, "DCompositionCreateDevice3") : NULL;
    CHECK("DCompositionCreateDevice3 exported", cdc3 != NULL);
    void *dev = NULL, *ctx = NULL, *dxgidev = NULL, *desk = NULL, *dev3 = NULL, *dev2 = NULL, *target = NULL, *visual = NULL;
    void *v3 = NULL, *surf = NULL, *clip = NULL;
    int ok = cd && cdc3 && SUCCEEDED(cd(NULL, gpu ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, NULL, 0, NULL, 0,
                                        D3D11_SDK_VERSION, &dev, NULL, &ctx)) &&
             SUCCEEDED(VT(dev, 0, QI_t)(dev, &NIID_IDXGIDevice, &dxgidev));
    CHECK("a device and its IDXGIDevice", ok);
    ok = ok && SUCCEEDED(cdc3(dxgidev, &NIID_IDCompositionDesktopDevice, &desk));
    CHECK("DCompositionCreateDevice3(IDCompositionDesktopDevice)", ok);
    ok = ok && SUCCEEDED(VT(desk, 0, QI_t)(desk, &NIID_IDCompositionDevice3, &dev3)) &&
         SUCCEEDED(VT(dev3, 0, QI_t)(dev3, &NIID_IDCompositionDevice2, &dev2));
    CHECK("QueryInterface IDCompositionDevice3, IDCompositionDevice2", ok);
    ok = ok && SUCCEEDED(VT(desk, 24, HRESULT (STDMETHODCALLTYPE *)(void *, HWND, BOOL, void **))(desk, child, TRUE, &target)) &&
         SUCCEEDED(VT(dev3, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(dev3, &visual)) &&
         SUCCEEDED(VT(visual, 0, QI_t)(visual, &NIID_IDCompositionVisual3, &v3)) &&
         SUCCEEDED(VT(target, 3, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(target, visual));
    CHECK("CreateTargetForHwnd, CreateVisual (IDCompositionVisual3), SetRoot", ok);
    ok = ok && SUCCEEDED(VT(dev3, 8, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, UINT, UINT, UINT, void **))(
                   dev3, 200, 120, NDXGI_FORMAT_B8G8R8A8_UNORM, 1 /* PREMULTIPLIED */, &surf)) &&
         SUCCEEDED(VT(visual, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(visual, surf));
    CHECK("CreateSurface, SetContent", ok);
    typedef HRESULT (STDMETHODCALLTYPE *Commit_t)(void *);
    ok = ok && draw_surface(surf, dev, ctx, NULL, 0xFFFFFF00);                            /* yellow */
    CHECK("BeginDraw: a texture on the device; EndDraw", ok);
    ok = ok && SUCCEEDED(VT(dev3, 3, Commit_t)(dev3));
    COLORREF c = pixel(child, 10, 10);
    CHECK("Commit: the surface in the window", ok && c == RGB(255, 255, 0));
    if (c != RGB(255, 255, 0)) printf("pixel %06lx\n", (unsigned long)c);
    RECT part = { 100, 50, 150, 100 };
    CHECK("BeginDraw of a rectangle: its offset", ok && draw_surface(surf, dev, ctx, &part, 0xFF00FFFF));   /* cyan */
    VT(dev3, 3, Commit_t)(dev3);
    CHECK("the rectangle drawn, the rest kept",
          pixel(child, 120, 70) == RGB(0, 255, 255) && pixel(child, 10, 10) == RGB(255, 255, 0) && pixel(child, 160, 70) == RGB(255, 255, 0));
    typedef HRESULT (STDMETHODCALLTYPE *SetF_t)(void *, float);
    ok = ok && SUCCEEDED(VT(dev3, 22, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(dev3, &clip));   /* CreateRectangleClip */
    if (ok) {
        VT(clip, 4, SetF_t)(clip, 0.0f); VT(clip, 6, SetF_t)(clip, 0.0f);              /* SetLeft, SetTop */
        VT(clip, 8, SetF_t)(clip, 100.0f); VT(clip, 10, SetF_t)(clip, 120.0f);         /* SetRight, SetBottom */
        ok = SUCCEEDED(VT(visual, 13, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(visual, clip));  /* SetClip */
    }
    ok = ok && draw_surface(surf, dev, ctx, NULL, 0xFFFF00FF) && SUCCEEDED(VT(dev3, 3, Commit_t)(dev3));   /* magenta */
    CHECK("a rectangle clip: drawn inside it, not outside",
          ok && pixel(child, 10, 10) == RGB(255, 0, 255) && pixel(child, 160, 10) == RGB(255, 255, 0));
    RELEASE(clip); RELEASE(surf); RELEASE(v3); RELEASE(visual); RELEASE(target); RELEASE(dev2); RELEASE(dev3); RELEASE(desk);

    if (gpu) {                                              /* the swap-chain factory of the device's adapter */
        g_step = "the factory of the device's adapter";
        void *a = NULL, *f = NULL, *chain = NULL, *dxvk = NULL, *f1 = NULL, *parent = NULL;
        ok = dxgidev && SUCCEEDED(VT(dxgidev, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(dxgidev, &a)) &&   /* GetAdapter */
             SUCCEEDED(VT(a, 6, HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, void **))(a, &NIID_IDXGIFactory2, &f));
        CHECK("the device's adapter: GetParent(IDXGIFactory2)", ok);
        NDXGI_SWAP_CHAIN_DESC1 d = { 64, 64, NDXGI_FORMAT_B8G8R8A8_UNORM, FALSE, { 1, 0 }, 0x20, 2, 0, 3, 1, 0 };
        CHECK("its CreateSwapChainForComposition on the GPU device",
              ok && SUCCEEDED(VT(f, 24, HRESULT (STDMETHODCALLTYPE *)(void *, void *, const void *, void *, void **))(f, dev, &d, NULL, &chain)));
        static const GUID dxvk_adapter = { 0x907bf281, 0xea3c, 0x43b4, { 0xa8, 0xe4, 0x9f, 0x23, 0x11, 0x07, 0xb4, 0xff } };
        CHECK("QueryInterface IDXGIDXVKAdapter: DXVK's", a && SUCCEEDED(VT(a, 0, QI_t)(a, &dxvk_adapter, &dxvk)));
        CreateFactory_t cf = (CreateFactory_t)GetProcAddress(g_dxgi, "CreateDXGIFactory1");
        void *ea = NULL;
        ok = cf && SUCCEEDED(cf(&NIID_IDXGIFactory1, &f1)) &&
             SUCCEEDED(VT(f1, 12, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, void **))(f1, 0, &ea)) &&
             SUCCEEDED(VT(ea, 6, HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, void **))(ea, &NIID_IDXGIFactory1, &parent));
        CHECK("EnumAdapters1's adapter: GetParent is its factory", ok && parent == f1);
        RELEASE(parent); RELEASE(ea); RELEASE(f1); RELEASE(dxvk); RELEASE(chain); RELEASE(f); RELEASE(a);
    }
    RELEASE(dxgidev); RELEASE(ctx); RELEASE(dev);
    CHECK("everything released", 1);
}

/* ---- Chromium's software output in this process (64-bit) ------------------ */
static void check_composition(HWND top, HWND child)
{
    g_step = "WARP devices";
    CreateDevice_t cd = (CreateDevice_t)GetProcAddress(g_d3d11, "D3D11CreateDevice");
    void *dev = NULL, *dxgidev = NULL, *a = NULL;
    UINT level = 0;
    CHECK("D3D11CreateDevice(WARP): feature level 9_1",
          SUCCEEDED(cd(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &level, NULL)) && level == 0x9100);
    if (dev) {
        CHECK("QueryInterface IDXGIDevice", SUCCEEDED(VT(dev, 0, QI_t)(dev, &NIID_IDXGIDevice, &dxgidev)));
        NDXGI_ADAPTER_DESC3 d;
        memset(&d, 0, sizeof(d));
        if (dxgidev && SUCCEEDED(VT(dxgidev, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(dxgidev, &a)))
            VT(a, 8, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(a, &d);
        CHECK("its adapter: the Basic Render Driver", d.VendorId == 0x1414 && d.DeviceId == 0x8c);
        RELEASE(a); RELEASE(dxgidev); RELEASE(dev);
    }
    const UINT angle[] = { 0xb000, 0xa000, 0x9300 };       /* ANGLE's levels: not this device */
    if (g_dxvk) {
        level = 0;
        CHECK("WARP at feature levels 9_3 and up: DXVK's device (ANGLE's WARP renderer)",
              SUCCEEDED(cd(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, angle, 3, D3D11_SDK_VERSION, &dev, &level, NULL)) && dev &&
              level >= 0xa000);
        RELEASE(dev);
    } else
        CHECK("WARP at feature levels 9_3 and up: refused (it does not rasterize)",
              FAILED(cd(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, angle, 3, D3D11_SDK_VERSION, &dev, NULL, NULL)) && !dev);

    g_step = "composition";
    Out o;
    int made = out_make(&o, child, 200, 120, 1);
    if (made) {
        RECT all = { 0, 0, 200, 120 }, part = { 100, 50, 150, 100 };
        CHECK("frame 1 presented", out_frame(&o, all, 0xFFFF0000));                 /* red */
        COLORREF c = pixel(child, 10, 10);
        CHECK("frame 1 in the window", c == RGB(255, 0, 0));
        if (c != RGB(255, 0, 0)) printf("pixel %06lx\n", (unsigned long)c);
        CHECK("frame 2 (a dirty rectangle) presented", out_frame(&o, part, 0xFF00FF00));   /* green */
        CHECK("frame 2: the rectangle drawn", pixel(child, 120, 70) == RGB(0, 255, 0));
        CHECK("frame 2: the rest kept", pixel(child, 10, 10) == RGB(255, 0, 0) && pixel(child, 160, 70) == RGB(255, 0, 0));
        void *v2 = NULL;
        VT(o.dcomp, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(o.dcomp, &v2);
        if (v2) {                                           /* a child visual at an offset, showing the same chain */
            VT(o.visual, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(o.visual, NULL);
            VT(v2, 4, HRESULT (STDMETHODCALLTYPE *)(void *, float))(v2, 20.0f);           /* SetOffsetX */
            VT(v2, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(v2, o.chain);
            VT(o.visual, 16, HRESULT (STDMETHODCALLTYPE *)(void *, void *, BOOL, void *))(o.visual, v2, TRUE, NULL);
            VT(o.dcomp, 3, HRESULT (STDMETHODCALLTYPE *)(void *))(o.dcomp);
            RECT strip = { 0, 0, 10, 120 };
            out_frame(&o, strip, 0xFF0000FF);                                               /* blue, at x 20..30 */
            CHECK("a child visual's offset", pixel(child, 25, 60) == RGB(0, 0, 255) && pixel(child, 15, 60) == RGB(255, 0, 0));
            RELEASE(v2);
        }
    }
    out_free(&o);
    CHECK("everything released", 1);

    g_step = "a swap chain on a window";
    CreateFactory_t cf = (CreateFactory_t)GetProcAddress(g_dxgi, "CreateDXGIFactory1");
    void *f = NULL, *chain = NULL, *buf = NULL, *ctx = NULL;
    cd(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx);
    cf(&NIID_IDXGIFactory2, &f);
    NDXGI_SWAP_CHAIN_DESC1 d1 = { 0, 0, NDXGI_FORMAT_B8G8R8A8_UNORM, FALSE, { 1, 0 }, 0x20, 2, 0, 3, 0, 0 };
    CHECK("CreateSwapChainForHwnd", dev && f && SUCCEEDED(VT(f, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *, HWND,
                                                     const void *, const void *, void *, void **))(f, dev, top, &d1, NULL, NULL, &chain)));
    if (chain) {
        NDXGI_SWAP_CHAIN_DESC1 got;
        VT(chain, 18, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(chain, &got);
        RECT cr;
        GetClientRect(top, &cr);
        CHECK("its size: the window's client area", got.Width == (UINT)cr.right && got.Height == (UINT)cr.bottom);
        VT(chain, 9, GetBuffer_t)(chain, 0, &NIID_ID3D11Texture2D, &buf);
        DWORD row[64];
        for (int i = 0; i < 64; i++) row[i] = 0xFF00FFFF;   /* cyan */
        ND3D11_BOX box = { 300, 10, 0, 364, 11, 1 };
        typedef void (STDMETHODCALLTYPE *Update_t)(void *, void *, UINT, const ND3D11_BOX *, const void *, UINT, UINT);
        for (UINT y = 10; y < 40; y++) { box.top = y; box.bottom = y + 1; VT(ctx, 48, Update_t)(ctx, buf, 0, &box, row, 256, 0); }
        CHECK("Present", SUCCEEDED(VT(chain, 8, Present_t)(chain, 0, 0)));
        CHECK("drawn in the window", pixel(top, 320, 20) == RGB(0, 255, 255));
    }
    RELEASE(buf); RELEASE(chain); RELEASE(f); RELEASE(ctx); RELEASE(dev);

    g_step = "D3D11CreateDeviceAndSwapChain";
    CreateDeviceSC_t cdsc = (CreateDeviceSC_t)GetProcAddress(g_d3d11, "D3D11CreateDeviceAndSwapChain");
    NDXGI_SWAP_CHAIN_DESC sd;
    memset(&sd, 0, sizeof(sd));
    sd.BufferDesc.Format = NDXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = 0x20;
    sd.BufferCount = 1;
    sd.OutputWindow = top;
    sd.Windowed = TRUE;
    CHECK("D3D11CreateDeviceAndSwapChain(WARP)",
          cdsc && SUCCEEDED(cdsc(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &chain, &dev, NULL, NULL)) &&
          chain && dev);
    RELEASE(chain); RELEASE(dev);
}


/* ---- Lifecycles (dcomptest life, both widths) ----------------------------- */
/* A composition swap chain on DXVK's device, shown by a visual of a target
 * on @hwnd: what Chromium's DirectComposition output makes */
typedef struct { void *dev, *ctx, *f, *dc, *target, *visual, *chain; } Gpu;
typedef HRESULT (STDMETHODCALLTYPE *ResizeBuffers_t)(void *, UINT, UINT, UINT, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *RTV_t)(void *, void *, const void *, void **);
typedef void (STDMETHODCALLTYPE *Clear_t)(void *, void *, const float *);
typedef HRESULT (STDMETHODCALLTYPE *Commit_t)(void *);

static void gpu_free(Gpu *g)
{
    RELEASE(g->chain); RELEASE(g->visual); RELEASE(g->target); RELEASE(g->dc); RELEASE(g->f); RELEASE(g->ctx); RELEASE(g->dev);
}

static int gpu_make(Gpu *g, HWND hwnd, UINT w, UINT h)
{
    memset(g, 0, sizeof(*g));
    CreateFactory_t cf = (CreateFactory_t)GetProcAddress(g_dxgi, "CreateDXGIFactory1");
    CreateDevice_t cd = (CreateDevice_t)GetProcAddress(g_d3d11, "D3D11CreateDevice");
    CreateDComp_t cdc = g_dcomp ? (CreateDComp_t)GetProcAddress(g_dcomp, "DCompositionCreateDevice") : NULL;
    NDXGI_SWAP_CHAIN_DESC1 d = { w, h, NDXGI_FORMAT_B8G8R8A8_UNORM, FALSE, { 1, 0 }, 0x20, 2, 0, 3, 1, 0 };
    return cf && cd && cdc && SUCCEEDED(cd(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &g->dev, NULL, &g->ctx)) &&
           SUCCEEDED(cf(&NIID_IDXGIFactory2, &g->f)) && SUCCEEDED(cdc(NULL, &NIID_IDCompositionDevice, &g->dc)) &&
           SUCCEEDED(VT(g->dc, 6, HRESULT (STDMETHODCALLTYPE *)(void *, HWND, BOOL, void **))(g->dc, hwnd, TRUE, &g->target)) &&
           SUCCEEDED(VT(g->dc, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(g->dc, &g->visual)) &&
           SUCCEEDED(VT(g->target, 3, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(g->target, g->visual)) &&
           SUCCEEDED(VT(g->f, 24, HRESULT (STDMETHODCALLTYPE *)(void *, void *, const void *, void *, void **))(g->f, g->dev, &d, NULL, &g->chain)) &&
           SUCCEEDED(VT(g->visual, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(g->visual, g->chain)) &&
           SUCCEEDED(VT(g->dc, 3, Commit_t)(g->dc));
}

/* the whole buffer cleared to @rgba on the GPU, on @ctx (NULL: the
 * device's), and presented */
static int gpu_frame(Gpu *g, void *ctx, const float *rgba)
{
    void *buf = NULL, *rtv = NULL;
    int ok = SUCCEEDED(VT(g->chain, 9, GetBuffer_t)(g->chain, 0, &NIID_ID3D11Texture2D, &buf)) &&
             SUCCEEDED(VT(g->dev, 9, RTV_t)(g->dev, buf, NULL, &rtv));
    if (ok) VT(ctx ? ctx : g->ctx, 50, Clear_t)(ctx ? ctx : g->ctx, rtv, rgba);
    RELEASE(rtv); RELEASE(buf);
    return ok && SUCCEEDED(VT(g->chain, 8, Present_t)(g->chain, 0, 0));
}

static const float c_red[4] = { 1, 0, 0, 1 }, c_green[4] = { 0, 1, 0, 1 }, c_blue[4] = { 0, 0, 1, 1 };
static const float c_magenta[4] = { 1, 0, 1, 1 }, c_cyan[4] = { 0, 1, 1, 1 }, c_yellow[4] = { 1, 1, 0, 1 };
static const float c_white[4] = { 1, 1, 1, 1 };

static void size_window(HWND h, int w, int ht)
{
    SetWindowPos(h, NULL, 0, 0, w, ht, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
    pump(50);
}

/* ---- Composited on the GPU (dcomptest direct, both widths) ----------------- */
/* @g's buffer as four quadrants, red, green / blue, white (@part: a texture
 * a quarter of its size, @prtv its view), presented */
static int quad_frame(Gpu *g, void *part, void *prtv, UINT w, UINT h)
{
    static const float *const c[4] = { c_red, c_green, c_blue, c_white };
    void *buf = NULL;
    if (FAILED(VT(g->chain, 9, GetBuffer_t)(g->chain, 0, &NIID_ID3D11Texture2D, &buf))) return 0;
    for (int i = 0; i < 4; i++) {
        VT(g->ctx, 50, Clear_t)(g->ctx, prtv, c[i]);
        VT(g->ctx, 46, CopyRegion_t)(g->ctx, buf, 0, (i & 1) * w / 2, (i >> 1) * h / 2, 0, part, 0, NULL);
    }
    RELEASE(buf);
    return SUCCEEDED(VT(g->chain, 8, Present_t)(g->chain, 0, 0));
}

/* Frames presented for @ms (the quadrants, or all @rgba); the frames, and
 * in *@direct those shown straight from the host */
static unsigned long present_for(Gpu *g, void *part, void *prtv, UINT w, UINT h, const float *rgba, DWORD ms, unsigned long *direct)
{
    unsigned long long a[8], b[8];
    unsigned long frames = 0;
    gpu_stats(a);
    for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < ms; frames++) {
        pump(0);
        if (!(rgba ? gpu_frame(g, NULL, rgba) : quad_frame(g, part, prtv, w, h))) break;
    }
    gpu_stats(b);
    *direct = (unsigned long)(b[ST_DIRECT] - a[ST_DIRECT]);
    return frames;
}

/* What Chromium's GPU DirectComposition output does, composited on the
 * GPU: a composition swap chain on the GPU device, the window's only
 * content, shown from the host, then through the GDI while a window covers
 * it, then from the host again; then the GDI way (the kernel shows the
 * window's own pixels again) once a swap chain of another device joins the
 * tree */
static void check_direct(HWND top, HWND child)
{
    enum { W = 200, H = 120 };
    g_step = "composited on the GPU";
    unsigned long long st[8];
    CHECK("the kernel's counters", gpu_stats(st));
    Gpu g;
    void *part = NULL, *prtv = NULL;
    int ok = gpu_make(&g, child, W, H);
    CHECK("a composition swap chain on the GPU device, the window's only content", ok);
    ND3D11_TEXTURE2D_DESC td = { W / 2, H / 2, 1, 1, NDXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, 0, 0x20 /* RENDER_TARGET */, 0, 0 };
    ok = ok && SUCCEEDED(VT(g.dev, 5, Tex2D_t)(g.dev, &td, NULL, &part)) && SUCCEEDED(VT(g.dev, 9, RTV_t)(g.dev, part, NULL, &prtv));
    if (!ok) { RELEASE(prtv); RELEASE(part); gpu_free(&g); return; }
    POINT o = { 0, 0 };
    ClientToScreen(child, &o);
    unsigned long d = 0, f;
    pointer_away();
    present_for(&g, part, prtv, W, H, NULL, 1000, &d);      /* (the window is shown and active) */
    printf("dcomp: quadrants at %ld %ld %d %d\n", o.x, o.y, W, H);
    fflush(stdout);
    f = present_for(&g, part, prtv, W, H, NULL, 6000, &d);
    printf("alone: %lu of %lu frames shown from the host\n", d, f);
    CHECK("frames shown from the host while the window is alone", f > 0 && d * 10 >= f * 9);

    g_step = "composited on the GPU, covered";
    HWND cover = CreateWindowExW(0, L"dcomptest", L"Cover", WS_OVERLAPPEDWINDOW | WS_VISIBLE, o.x + W / 2, o.y + H / 2, 200, 150,
                                 NULL, NULL, GetModuleHandleW(NULL), NULL);
    present_for(&g, part, prtv, W, H, NULL, 1000, &d);      /* (the window is drawn) */
    f = present_for(&g, part, prtv, W, H, NULL, 3000, &d);
    printf("covered: %lu of %lu frames shown from the host\n", d, f);
    CHECK("frames go through the GDI while a window covers it", cover && f > 0 && d == 0);
    COLORREF p1 = pixel(child, 10, 10), p2 = pixel(child, 150, 10), p3 = pixel(child, 10, 100);
    CHECK("... into the window's pixels", p1 == RGB(255, 0, 0) && p2 == RGB(0, 255, 0) && p3 == RGB(0, 0, 255));
    if (p1 != RGB(255, 0, 0) || p2 != RGB(0, 255, 0) || p3 != RGB(0, 0, 255))
        printf("pixels %06lx %06lx %06lx\n", (unsigned long)p1, (unsigned long)p2, (unsigned long)p3);
    DestroyWindow(cover);
    SetForegroundWindow(top);
    pointer_away();
    present_for(&g, part, prtv, W, H, NULL, 1000, &d);
    f = present_for(&g, part, prtv, W, H, NULL, 3000, &d);
    printf("uncovered: %lu of %lu frames shown from the host\n", d, f);
    CHECK("frames shown from the host again once it has gone", f > 0 && d * 10 >= f * 9);

    g_step = "composited on the GPU, then the GDI way";
    CreateDevice_t cd = (CreateDevice_t)GetProcAddress(g_d3d11, "D3D11CreateDevice");
    void *dev2 = NULL, *chain2 = NULL, *kid = NULL;
    NDXGI_SWAP_CHAIN_DESC1 d2 = { 20, 20, NDXGI_FORMAT_B8G8R8A8_UNORM, FALSE, { 1, 0 }, 0x20, 2, 0, 3, 1, 0 };
    ok = SUCCEEDED(cd(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev2, NULL, NULL)) &&
         SUCCEEDED(VT(g.f, 24, HRESULT (STDMETHODCALLTYPE *)(void *, void *, const void *, void *, void **))(g.f, dev2, &d2, NULL, &chain2)) &&
         SUCCEEDED(VT(g.dc, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(g.dc, &kid)) &&
         SUCCEEDED(VT(kid, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(kid, chain2)) &&
         SUCCEEDED(VT(g.visual, 16, HRESULT (STDMETHODCALLTYPE *)(void *, void *, BOOL, void *))(g.visual, kid, TRUE, NULL)) &&
         SUCCEEDED(VT(g.dc, 3, Commit_t)(g.dc));
    CHECK("a swap chain of another device joins the tree", ok);
    present_for(&g, part, prtv, W, H, c_yellow, 1000, &d);
    printf("dcomp: yellow at %ld %ld %d %d\n", o.x, o.y, W, H);
    fflush(stdout);
    f = present_for(&g, part, prtv, W, H, c_yellow, 6000, &d);
    printf("the GDI way: %lu of %lu frames shown from the host\n", d, f);
    CHECK("frames go through the GDI again", f > 0 && d == 0);
    CHECK("... into the window's pixels", pixel(child, 100, 60) == RGB(255, 255, 0));
    RELEASE(kid); RELEASE(chain2); RELEASE(dev2);
    RELEASE(prtv); RELEASE(part);
    gpu_free(&g);
}

/* Chromium on a resize of its window: the window sized, ResizeBuffers on
 * the composition swap chain, a frame at the new size; a virtual surface
 * resized (its pixels kept) */
static void check_resize(HWND child)
{
    g_step = "resizing a composition swap chain";
    Gpu g;
    int ok = gpu_make(&g, child, 200, 120) && gpu_frame(&g, NULL, c_magenta);
    CHECK("a composition swap chain on the GPU, frame 1 shown", ok && pixel(child, 10, 10) == RGB(255, 0, 255));
    size_window(child, 300, 160);
    ok = ok && SUCCEEDED(VT(g.chain, 13, ResizeBuffers_t)(g.chain, 0, 300, 160, 0, 0));
    NDXGI_SWAP_CHAIN_DESC1 got;
    memset(&got, 0, sizeof(got));
    if (ok) VT(g.chain, 18, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(g.chain, &got);
    CHECK("ResizeBuffers to 300x160", ok && got.Width == 300 && got.Height == 160);
    if (ok) {                                               /* (a change to the tree, so Commit draws all of it) */
        VT(g.visual, 4, HRESULT (STDMETHODCALLTYPE *)(void *, float))(g.visual, 0.0f);
        VT(g.dc, 3, Commit_t)(g.dc);
    }
    COLORREF c = pixel(child, 10, 10);
    CHECK("a Commit before the next present: the last frame stays", ok && c == RGB(255, 0, 255));
    if (c != RGB(255, 0, 255)) printf("pixel %06lx\n", (unsigned long)c);
    ok = ok && gpu_frame(&g, NULL, c_cyan);
    CHECK("a frame at the new size, all of it shown",
          ok && pixel(child, 250, 140) == RGB(0, 255, 255) && pixel(child, 10, 10) == RGB(0, 255, 255));
    size_window(child, 120, 80);
    ok = ok && SUCCEEDED(VT(g.chain, 13, ResizeBuffers_t)(g.chain, 2, 120, 80, NDXGI_FORMAT_B8G8R8A8_UNORM, 0)) &&
         gpu_frame(&g, NULL, c_yellow);
    CHECK("smaller: ResizeBuffers to 120x80, a frame", ok && pixel(child, 60, 40) == RGB(255, 255, 0));
    memset(&got, 0, sizeof(got));
    ok = ok && FAILED(VT(g.chain, 13, ResizeBuffers_t)(g.chain, 0, 0, 0, 0, 0)) &&
         SUCCEEDED(VT(g.chain, 18, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(g.chain, &got)) && got.Width == 120;
    CHECK("ResizeBuffers to no size refused (a composition swap chain has no window to take it from)", ok);
    gpu_free(&g);

    g_step = "resizing a virtual surface";
    size_window(child, 300, 160);
    CreateDevice_t cd = (CreateDevice_t)GetProcAddress(g_d3d11, "D3D11CreateDevice");
    CreateDComp_t cdc3 = (CreateDComp_t)GetProcAddress(g_dcomp, "DCompositionCreateDevice3");
    void *dev = NULL, *ctx = NULL, *dxgidev = NULL, *desk = NULL, *target = NULL, *visual = NULL, *surf = NULL, *virt = NULL;
    ok = cd && cdc3 && SUCCEEDED(cd(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx)) &&
         SUCCEEDED(VT(dev, 0, QI_t)(dev, &NIID_IDXGIDevice, &dxgidev)) &&
         SUCCEEDED(cdc3(dxgidev, &NIID_IDCompositionDesktopDevice, &desk)) &&
         SUCCEEDED(VT(desk, 24, HRESULT (STDMETHODCALLTYPE *)(void *, HWND, BOOL, void **))(desk, child, TRUE, &target)) &&
         SUCCEEDED(VT(desk, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(desk, &visual)) &&
         SUCCEEDED(VT(target, 3, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(target, visual)) &&
         SUCCEEDED(VT(desk, 9, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, UINT, UINT, UINT, void **))(
             desk, 200, 120, NDXGI_FORMAT_B8G8R8A8_UNORM, 1, &surf)) &&                 /* CreateVirtualSurface */
         SUCCEEDED(VT(surf, 0, QI_t)(surf, &NIID_IDCompositionVirtualSurface, &virt)) &&
         SUCCEEDED(VT(visual, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(visual, surf));
    CHECK("CreateVirtualSurface (IDCompositionVirtualSurface), SetContent", ok);
    ok = ok && draw_surface(surf, dev, ctx, NULL, 0xFFFFFF00) && SUCCEEDED(VT(desk, 3, Commit_t)(desk));   /* yellow */
    CHECK("drawn and committed", ok && pixel(child, 10, 10) == RGB(255, 255, 0));
    typedef HRESULT (STDMETHODCALLTYPE *VResize_t)(void *, UINT, UINT);
    ok = ok && SUCCEEDED(VT(virt, 8, VResize_t)(virt, 300, 160));
    RECT strip = { 200, 0, 300, 160 };
    ok = ok && draw_surface(surf, dev, ctx, &strip, 0xFF00FFFF) && SUCCEEDED(VT(desk, 3, Commit_t)(desk));   /* cyan */
    c = pixel(child, 10, 10);
    CHECK("Resize to 300x160: the new part drawn, the old pixels kept", ok && pixel(child, 250, 60) == RGB(0, 255, 255) &&
                                                                     c == RGB(255, 255, 0));
    if (c != RGB(255, 255, 0)) printf("pixel %06lx\n", (unsigned long)c);
    ok = ok && SUCCEEDED(VT(virt, 8, VResize_t)(virt, 100, 60)) && SUCCEEDED(VT(desk, 3, Commit_t)(desk));
    CHECK("Resize to 100x60: kept", ok && pixel(child, 50, 30) == RGB(255, 255, 0));
    typedef HRESULT (STDMETHODCALLTYPE *Begin_t)(void *, const RECT *, const GUID *, void **, POINT *);
    RECT big = { 0, 0, 200, 120 };
    void *tex = NULL;
    POINT at;
    CHECK("BeginDraw outside the new size refused", ok && FAILED(VT(surf, 3, Begin_t)(surf, &big, &NIID_ID3D11Texture2D, &tex, &at)) && !tex);
    RELEASE(tex);
    RELEASE(virt); RELEASE(surf); RELEASE(visual); RELEASE(target); RELEASE(desk); RELEASE(dxgidev); RELEASE(ctx); RELEASE(dev);
    size_window(child, 200, 120);
}

/* frames presented on another thread until @stop (the thread that starts
 * it does no Direct3D meanwhile: one immediate context) */
typedef struct { Gpu *g; void *ctx; volatile LONG stop, presents, failed; } Spin;

static DWORD WINAPI spin_thread(void *arg)
{
    Spin *sp = arg;
    const float *colors[3] = { c_red, c_green, c_blue };
    for (LONG i = 0; !sp->stop; i++) {
        if (gpu_frame(sp->g, sp->ctx, colors[i % 3])) InterlockedIncrement(&sp->presents);
        else InterlockedIncrement(&sp->failed);
        Sleep(1);
    }
    return 0;
}

/* @n more presents on @sp's thread, at most 20 s */
static int spun(Spin *sp, LONG n)
{
    LONG want = sp->presents + n;
    for (int i = 0; i < 2000 && sp->presents < want; i++) Sleep(10);
    return sp->presents >= want;
}

static HANDLE spin_start(Spin *sp, Gpu *g)
{
    memset((void *)sp, 0, sizeof(*sp));
    sp->g = g;
    sp->ctx = g->ctx;
    return CreateThread(NULL, 0, spin_thread, sp, 0, NULL);
}

static int spin_stop(Spin *sp, HANDLE t)
{
    sp->stop = 1;
    int ended = t && WaitForSingleObject(t, 20000) == WAIT_OBJECT_0;
    if (t) CloseHandle(t);
    return ended;
}

/* What a closing browser tab or a lost GPU device does: a visual, its
 * target, the DirectComposition device, the Direct3D device and the window
 * go away while another thread is still presenting into the swap chain */
static void check_teardown(HWND top, HWND child)
{
    g_step = "teardown while frames are presented";
    HWND w = CreateWindowExW(0, L"dcomptest_bare", L"", WS_CHILD | WS_VISIBLE, 240, 0, 200, 120, top, NULL, GetModuleHandleW(NULL), NULL);
    Gpu g;
    Spin sp;
    int ok = w && gpu_make(&g, w, 200, 120) && gpu_frame(&g, NULL, c_magenta);
    CHECK("a composition swap chain on a second window", ok);
    HANDLE t = ok ? spin_start(&sp, &g) : NULL;
    CHECK("frames presented on another thread", t && spun(&sp, 3));
    RELEASE(g.target);                                      /* the target first, its visual still held */
    CHECK("the target released, frames go on", spun(&sp, 3));
    VT(g.dc, 3, Commit_t)(g.dc);
    RELEASE(g.visual);                                      /* the visual: its swap chain's sink taken back */
    CHECK("the visual released, frames go on", spun(&sp, 3));
    VT(g.dc, 3, Commit_t)(g.dc);
    RELEASE(g.dc);
    RELEASE(g.f);
    CHECK("the DirectComposition device released, frames go on", spun(&sp, 3));
    CHECK("the presenting thread ends", spin_stop(&sp, t));
    CHECK("no present failed", sp.failed == 0);
    RELEASE(g.ctx);
    RELEASE(g.dev);                                         /* the device before the swap chain made on it */
    void *kept = NULL;
    ok = SUCCEEDED(VT(g.chain, 7, HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, void **))(g.chain, &NIID_ID3D11Device, &kept)) &&
         kept && SUCCEEDED(VT(g.chain, 8, Present_t)(g.chain, 0, 0));
    CHECK("the Direct3D device released before its swap chain: the swap chain keeps it and presents", ok);
    RELEASE(kept);
    RELEASE(g.chain);

    g_step = "a window destroyed while frames are presented";
    ok = gpu_make(&g, w, 200, 120) && gpu_frame(&g, NULL, c_cyan) && pixel(w, 10, 10) == RGB(0, 255, 255);
    CHECK("a swap chain on the window again", ok);
    t = ok ? spin_start(&sp, &g) : NULL;
    ok = t && spun(&sp, 2);
    DestroyWindow(w);
    CHECK("the window destroyed, frames go on", ok && spun(&sp, 5));
    CHECK("Commit with the window gone", SUCCEEDED(VT(g.dc, 3, Commit_t)(g.dc)));
    CHECK("the thread ends, no present failed", spin_stop(&sp, t) && sp.failed == 0);
    gpu_free(&g);

    g_step = "composition after the teardowns";
    ok = gpu_make(&g, child, 200, 120) && gpu_frame(&g, NULL, c_green);
    CHECK("a new swap chain on another window shown", ok && pixel(child, 10, 10) == RGB(0, 255, 0));
    gpu_free(&g);
    CHECK("everything released", 1);
}

/* ---- The GPU process: its compositing window, parented by the browser ----- */
static HWND g_host, g_child;
static int g_tries;
static Gpu g_gpu;                       /* (life: the window's swap chain) */
static Spin g_spin;

static LRESULT CALLBACK gpu_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == DC_RESIZE) {                                   /* the browser's window resized: ours, its buffers, a frame */
        int w = (int)wp, ht = (int)lp, ok = 0, bad = 0, bit = 0;
#define C(cond) do { if (cond) ok++; else bad |= 1 << bit; bit++; } while (0)
        C(SetWindowPos(h, NULL, 0, 0, w, ht, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOOWNERZORDER | SWP_NOZORDER));  /* bit 0 */
        C(g_gpu.chain && SUCCEEDED(VT(g_gpu.chain, 13, ResizeBuffers_t)(g_gpu.chain, 0, (UINT)w, (UINT)ht, 0, 0)));   /* bit 1 */
        C(g_gpu.chain && gpu_frame(&g_gpu, NULL, c_green));                                                            /* bit 2 */
        C(pixel(h, w - 10, ht - 10) == RGB(0, 255, 0));                                                               /* bit 3 */
#undef C
        PostMessageW(g_host, DC_RESULT, (WPARAM)ok, (LPARAM)bad);
        return 0;
    }
    if (m == DC_SPIN) {                                     /* (until the browser kills this process) */
        HANDLE t = g_gpu.chain ? spin_start(&g_spin, &g_gpu) : NULL;
        if (t) spun(&g_spin, 3);
        PostMessageW(g_host, DC_SPINNING, (WPARAM)(t != NULL), (LPARAM)g_spin.presents);
        return 0;
    }
    if (m != DC_DRAW) return DefWindowProcW(h, m, wp, lp);
    HWND parent = (HWND)wp;
    if (GetParent(h) != parent && ++g_tries < 200) {         /* (the browser's SetParent reaches us on its own) */
        Sleep(20);
        PostMessageW(h, DC_DRAW, wp, lp);
        return 0;
    }
    int ok = 0, bad = 0, bit = 0;
#define C(cond) do { if (cond) ok++; else bad |= 1 << bit; bit++; } while (0)
    C(GetParent(h) == parent);                              /* bit 0: WS_CHILD of the browser's window */
    C(GetAncestor(h, GA_PARENT) == parent);                 /* bit 1 */
    C(IsWindowVisible(h));                                  /* bit 2 */
    /* SoftwareOutputDeviceWinSwapChain::UpdateWindowSize */
    C(SetWindowPos(h, NULL, 0, 0, 200, 120, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOOWNERZORDER | SWP_NOZORDER));   /* bit 3 */
    RECT cr;
    C(GetClientRect(h, &cr) && cr.right == 200 && cr.bottom == 120);   /* bit 4 */
    RECT all = { 0, 0, 200, 120 };
#ifdef _WIN64
    Out o;
#endif
    if (g_life || g_direct) C(gpu_make(&g_gpu, h, 200, 120) && gpu_frame(&g_gpu, NULL, c_red));   /* bit 5: composited on the GPU, red */
    else {
#ifdef _WIN64
        C(out_make(&o, h, 200, 120, 0) && out_frame(&o, all, 0xFFFF0000));      /* bit 5: composited, red */
#else
        HDC dc = GetDC(h);
        HBRUSH red = CreateSolidBrush(RGB(255, 0, 0));
        C(FillRect(dc, &all, red));                         /* bit 5: drawn with GDI, red */
        DeleteObject(red);
        ReleaseDC(h, dc);
#endif
    }
    if (g_direct) {                                         /* bit 6: shown straight from the host, embedded as it is */
        unsigned long long a[8], b[8];
        pointer_away();
        gpu_stats(a);
        for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < 2000;) gpu_frame(&g_gpu, NULL, c_red);
        gpu_stats(b);
        C(b[ST_DIRECT] > a[ST_DIRECT]);
    } else
        C(pixel(h, 100, 60) == RGB(255, 0, 0));             /* bit 6 */
#undef C
    PostMessageW(g_host, DC_RESULT, (WPARAM)ok, (LPARAM)bad);
#ifdef _WIN64
    if (!g_life && !g_direct) out_free(&o);
#endif
    return 0;
}

static int gpu_main(HWND host)
{
    g_host = host;
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = gpu_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"Intermediate D3D Window";
    RegisterClassW(&wc);
    /* gl::ChildWindowWin: a hidden popup, and the child that the browser
     * then parents in its own window */
    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW, L"Intermediate D3D Window", L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    HWND child = CreateWindowExW(WS_EX_NOPARENTNOTIFY | WS_EX_LAYERED | WS_EX_TRANSPARENT | 0x00200000 /* NOREDIRECTIONBITMAP */,
                                 L"Intermediate D3D Window", L"", WS_CHILD | WS_DISABLED | WS_VISIBLE, 0, 0, 1, 1, popup, NULL,
                                 wc.hInstance, NULL);
    if (!popup || !child) return 2;
    if (!PostMessageW(host, DC_HELLO, (WPARAM)child, 0)) return 4;
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}

/* ---- The browser ----------------------------------------------------------- */
static int g_result_ok = -1, g_result_bad = -1, g_spinning;

/* a window that draws nothing of its own (Chromium's draws only through DirectComposition) */
static LRESULT CALLBACK bare_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_PAINT) { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); return 0; }
    if (m == WM_ERASEBKGND) return 1;
    return DefWindowProcW(h, m, wp, lp);
}

static LRESULT CALLBACK host_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case DC_HELLO: g_child = (HWND)wp; return 0;
    case DC_RESULT: g_result_ok = (int)wp; g_result_bad = (int)lp; return 0;
    case DC_SPINNING: g_spinning = wp ? 1 : 2; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}


/* The GPU process's window composited on the GPU (life): seen from the
 * browser's process, resized when the browser's is, and the
 * process killed while it presents (a GPU process crash): its window goes,
 * the browser's lives on and draws its own again */
static void check_cross_process_life(HWND browser, HANDLE gpu)
{
    g_step = "the GPU process's window, composited on the GPU";
    COLORREF c = pixel(g_child, 100, 60);                   /* (another process's window: what it shows now) */
    CHECK("its frame, seen from the browser's process", c == RGB(255, 0, 0));
    if (c != RGB(255, 0, 0)) printf("pixel %06lx\n", (unsigned long)c);
    g_step = "the GPU process's window resized";
    g_result_ok = g_result_bad = -1;
    PostMessageW(g_child, DC_RESIZE, 300, 160);
    WAIT_FOR(g_result_bad != -1, 20000);
    CHECK("the GPU process sizes its window and buffers and presents", g_result_bad == 0);
    if (g_result_bad) printf("gpu resize checks: %d passed, failed bits %#x\n", g_result_ok, g_result_bad);
    RECT r = { 0, 0, 0, 0 };
    GetWindowRect(g_child, &r);
    CHECK("the browser sees it 300x160", r.right - r.left == 300 && r.bottom - r.top == 160);
    RECT cr = { 0, 0, 0, 0 };
    GetClientRect(g_child, &cr);
    CHECK("its client area too (GetClientRect from another process)", cr.right == 300 && cr.bottom == 160);
    if (cr.right != 300 || cr.bottom != 160) printf("client %ldx%ld\n", cr.right, cr.bottom);
    c = pixel(g_child, 250, 140);
    CHECK("the new frame seen from the browser's process, the new part too", c == RGB(0, 255, 0) && pixel(g_child, 10, 10) == RGB(0, 255, 0));
    if (c != RGB(0, 255, 0)) printf("pixel %06lx\n", (unsigned long)c);
    g_step = "the GPU process killed while presenting";
    PostMessageW(g_child, DC_SPIN, 0, 0);
    WAIT_FOR(g_spinning, 20000);
    CHECK("the GPU process presents frames on another thread", g_spinning == 1);
    TerminateProcess(gpu, 9);
    CHECK("it ends", WaitForSingleObject(gpu, 20000) == WAIT_OBJECT_0);
    WAIT_FOR(!IsWindow(g_child), 5000);
    CHECK("its window goes with it", !IsWindow(g_child));
    CHECK("the browser's window lives on", IsWindow(browser) && IsWindowVisible(browser));
    InvalidateRect(browser, NULL, TRUE);
    UpdateWindow(browser);
    pump(200);
    c = pixel(browser, 100, 60);
    CHECK("the browser's window drawn again where the GPU process's was", c == RGB(128, 128, 128));
    if (c != RGB(128, 128, 128)) printf("pixel %06lx\n", (unsigned long)c);
}

static void check_cross_process(HWND browser)
{
    g_step = "starting the GPU process";
    char self[MAX_PATH], cl[MAX_PATH + 64];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    snprintf(cl, sizeof(cl), "\"%s\" gpu %llx%s", self, (unsigned long long)(ULONG_PTR)browser,
             g_life ? " life" : g_direct ? " direct" : "");
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(self, cl, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CHECK("CreateProcess", 0); return; }
    for (int i = 0; i < 1000 && !g_child; i++) pump(20);
    CHECK("the GPU process's child window arrived", g_child != NULL);
    if (g_child) {
        g_step = "SetParent across processes";
        SetParent(g_child, browser);                        /* gfx::RenderingWindowManager::RegisterChild */
        PostMessageW(g_child, DC_DRAW, (WPARAM)browser, 0);
        for (int i = 0; i < 1000 && g_result_bad == -1; i++) pump(20);
        CHECK("the GPU process sees its parent, sizes and draws its window", g_result_bad == 0);
        if (g_result_bad) printf("gpu checks: %d passed, failed bits %#x\n", g_result_ok, g_result_bad);
        POINT o = { 0, 0 };
        ClientToScreen(browser, &o);
        RECT r = { 0, 0, 0, 0 };
        int placed = 0;
        for (int i = 0; i < 100 && !placed; i++) {
            placed = GetWindowRect(g_child, &r) && r.left == o.x && r.top == o.y && r.right - r.left == 200 && r.bottom - r.top == 120;
            if (!placed) pump(30);
        }
        CHECK("the window is over the browser's client area", placed);
        if (!placed) printf("child at %ld,%ld %ldx%ld, wanted %ld,%ld 200x120\n", r.left, r.top, r.right - r.left, r.bottom - r.top, o.x, o.y);
        if (g_life) check_cross_process_life(browser, pi.hProcess);
    }
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int main(int argc, char **argv)
{
    g_dxgi = LoadLibraryW(L"dxgi.dll");
    g_d3d11 = LoadLibraryW(L"d3d11.dll");
    g_dcomp = LoadLibraryW(L"dcomp.dll");
    if (argc >= 3 && !strcmp(argv[1], "gpu")) {
        g_life = argc >= 4 && !strcmp(argv[3], "life");
        g_direct = argc >= 4 && !strcmp(argv[3], "direct");
        return gpu_main((HWND)(ULONG_PTR)strtoull(argv[2], NULL, 16));
    }
    g_life = argc >= 2 && !strcmp(argv[1], "life");
    g_direct = argc >= 2 && !strcmp(argv[1], "direct");
    if (!g_direct) SetEnvironmentVariableA("NOVA_PRESENT", "gdi");   /* (GetPixel sees the frames: see the top) */
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    CHECK("dxgi.dll, d3d11.dll and dcomp.dll load", g_dxgi && g_d3d11 && g_dcomp);
    if (!g_dxgi || !g_d3d11) { printf("dcomptest: %d passed, %d failed\n", pass, fail); return 1; }
    g_dxvk = g_life || g_direct || (argc >= 2 && !strcmp(argv[1], "dxvk"));
    if (!g_life && !g_direct && g_dxvk) check_dxvk();
    else if (!g_life && !g_direct) check_adapter();

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = host_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"dcomptest";
    wc.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
    RegisterClassW(&wc);
    HWND top = CreateWindowExW(0, L"dcomptest", L"dcomptest", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 480, 320, NULL, NULL,
                               wc.hInstance, NULL);
    WNDCLASSW bc = { 0 };
    bc.lpfnWndProc = bare_proc;
    bc.hInstance = wc.hInstance;
    bc.lpszClassName = L"dcomptest_bare";
    RegisterClassW(&bc);
    HWND child = CreateWindowExW(0, L"dcomptest_bare", L"", WS_CHILD | WS_VISIBLE, 0, 0, 200, 120, top, NULL, wc.hInstance, NULL);
    pump(200);
    if (g_direct)
        check_direct(top, child);
    else if (g_life) {
        check_resize(child);
        check_teardown(top, child);
    } else {
#ifdef _WIN64
        check_composition(top, child);
        check_dcomp_surfaces(child, 0);
#endif
        if (g_dxvk) {
            check_gpu_composition(child);
            check_dcomp_surfaces(child, 1);
        }
    }
    DestroyWindow(child);
    pump(100);
    check_cross_process(top);
    DestroyWindow(top);
    printf("dcomptest: %d passed, %d failed (%d-bit)\n", pass, fail, (int)sizeof(void *) * 8);
    return fail ? 1 : 0;
}
