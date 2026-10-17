/* d2dctxtest.exe — Direct2D 1.1's devices and device contexts (d2d1.dll),
 * as the XAML framework draws its text and shapes: an ID2D1Factory1 and
 * its ID2D1Multithread, an ID2D1Device on a WARP Direct3D 11 device
 * (ID2D1Factory1::CreateDevice and D2D1CreateDevice), its device contexts
 * drawing into a bitmap of their own (CreateBitmap with
 * D2D1_BITMAP_PROPERTIES1, read through a CPU-readable copy's Map) and into
 * a Direct3D texture (CreateBitmapFromDxgiSurface, D2D1CreateDeviceContext;
 * the texture's pixels read back), D2D1_UNIT_MODE_PIXELS; and the surfaces
 * DirectComposition makes on an ID2D1Device (DCompositionCreateDevice2 and
 * IDCompositionSurfaceFactory), whose BeginDraw hands out a device context,
 * shown in a window.
 *
 * The software Direct3D device is 64-bit only, so this test is too.
 *
 *   d2dctxtest           the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <novadx.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define VT(o, i, T) ((T)((void **)*(void **)(o))[i])
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(void *);
typedef HRESULT (STDMETHODCALLTYPE *QI_t)(void *, const GUID *, void **);
typedef void (STDMETHODCALLTYPE *Void_t)(void *);
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)

static const GUID IID_ID2D1Factory1_T = { 0xbb12d362, 0xdaee, 0x4b9a, { 0xaa, 0x1d, 0x14, 0xba, 0x40, 0x1c, 0xfa, 0x1f } };
static const GUID IID_ID2D1Multithread_T = { 0x31e6e7bc, 0xe0ff, 0x4d46, { 0x8c, 0x64, 0xa0, 0xa8, 0xc4, 0x1c, 0x15, 0xd3 } };
static const GUID IID_ID2D1RenderTarget_T = { 0x2cd90694, 0x12e2, 0x11dc, { 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9 } };
static const GUID IID_ID2D1Bitmap1_T = { 0xa898a84c, 0x3873, 0x4588, { 0xb0, 0x8b, 0xeb, 0xbf, 0x97, 0x8d, 0xf0, 0x41 } };

typedef struct { float r, g, b, a; } ColorF;
typedef struct { float l, t, r, b; } RectF;
typedef struct { float m11, m12, m21, m22, dx, dy; } Mat;
typedef struct { struct { UINT format, alpha; } fmt; float dpix, dpiy; UINT options; void *cc; } BmpProps1;
typedef struct { UINT pitch; BYTE *bits; } Mapped;
typedef struct { UINT x, y; } PtU;
typedef struct { float x, y; } PtF;
typedef struct { PtF c; float rx, ry; } EllipseF;

/* ID2D1RenderTarget */
enum { RT_SOLID = 8, RT_FILL_RECT = 17, RT_FILL_ELLIPSE = 21, RT_SET_TRANSFORM = 30, RT_CLEAR = 47, RT_BEGIN = 48,
       RT_END = 49, RT_FORMAT = 50, RT_SET_DPI = 51 };
/* ID2D1DeviceContext */
enum { DC_BITMAP1 = 57, DC_FROM_SURFACE = 62, DC_FORMAT_OK = 68, DC_GET_DEVICE = 73, DC_SET_TARGET = 74, DC_GET_TARGET = 75,
       DC_SET_UNIT = 80, DC_GET_UNIT = 81 };
enum { OPT_TARGET = 1, OPT_CANNOT_DRAW = 2, OPT_CPU_READ = 4 };

typedef HRESULT (WINAPI *D2DFactory_t)(UINT, const GUID *, const void *, void **);
typedef HRESULT (WINAPI *D2DDevice_t)(void *, const void *, void **);
typedef HRESULT (WINAPI *D3DDevice_t)(void *, UINT, HMODULE, UINT, const UINT *, UINT, UINT, void **, UINT *, void **);
typedef HRESULT (WINAPI *DComp_t)(void *, const GUID *, void **);

static void *g_ctx;                     /* the D3D device's immediate context */
static void *g_d3d;

static void *solid(void *rt, float r, float g, float b)
{
    ColorF c = { r, g, b, 1 };
    void *brush = 0;
    VT(rt, RT_SOLID, HRESULT (STDMETHODCALLTYPE *)(void *, const ColorF *, const void *, void **))(rt, &c, NULL, &brush);
    return brush;
}
static void clear(void *rt, float r, float g, float b)
{
    ColorF c = { r, g, b, 1 };
    VT(rt, RT_CLEAR, void (STDMETHODCALLTYPE *)(void *, const ColorF *))(rt, &c);
}
static void fill_rect(void *rt, float l, float t, float r, float b, void *brush)
{
    RectF rc = { l, t, r, b };
    VT(rt, RT_FILL_RECT, void (STDMETHODCALLTYPE *)(void *, const RectF *, void *))(rt, &rc, brush);
}
static HRESULT end_draw(void *rt) { return VT(rt, RT_END, HRESULT (STDMETHODCALLTYPE *)(void *, UINT64 *, UINT64 *))(rt, NULL, NULL); }
static void set_target(void *dc, void *bmp) { VT(dc, DC_SET_TARGET, void (STDMETHODCALLTYPE *)(void *, void *))(dc, bmp); }

static void *bitmap1(void *dc, UINT w, UINT h, UINT options)
{
    BmpProps1 p = { { 87, 1 }, 96, 96, options, NULL };
    PtU size = { w, h };
    void *b = 0;
    VT(dc, DC_BITMAP1, HRESULT (STDMETHODCALLTYPE *)(void *, PtU, const void *, UINT, const BmpProps1 *, void **))(dc, size, NULL, 0, &p, &b);
    return b;
}

/* the pixel (@x, @y) of @target, a bitmap of @dc's device, through a CPU-readable copy */
static DWORD bitmap_pixel(void *dc, void *target, UINT w, UINT h, UINT x, UINT y)
{
    void *copy = bitmap1(dc, w, h, OPT_CPU_READ | OPT_CANNOT_DRAW);
    DWORD v = 0xDEADBEEF;
    if (!copy) return v;
    PtU at = { 0, 0 };
    VT(copy, 8, HRESULT (STDMETHODCALLTYPE *)(void *, const PtU *, void *, const void *))(copy, &at, target, NULL);   /* CopyFromBitmap */
    Mapped m = { 0, 0 };
    if (SUCCEEDED(VT(copy, 14, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, Mapped *))(copy, 1 /* READ */, &m)) && m.bits) {
        v = *(DWORD *)(m.bits + (size_t)y * m.pitch + x * 4);
        VT(copy, 15, HRESULT (STDMETHODCALLTYPE *)(void *))(copy);
    }
    RELEASE(copy);
    return v;
}

/* a @w x @h B8G8R8A8 render-target texture, and the pixel (@x, @y) of one */
static void *texture(UINT w, UINT h)
{
    ND3D11_TEXTURE2D_DESC td = { w, h, 1, 1, NDXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, 0, 0x20 | 0x8, 0, 0 };
    void *t = 0;
    VT(g_d3d, 5, HRESULT (STDMETHODCALLTYPE *)(void *, const void *, const void *, void **))(g_d3d, &td, NULL, &t);
    return t;
}
static DWORD texture_pixel(void *tex, UINT w, UINT h, UINT x, UINT y)
{
    ND3D11_TEXTURE2D_DESC td = { w, h, 1, 1, NDXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, 3 /* STAGING */, 0, 0x20000 /* READ */, 0 };
    void *st = 0;
    DWORD v = 0xDEADBEEF;
    VT(g_d3d, 5, HRESULT (STDMETHODCALLTYPE *)(void *, const void *, const void *, void **))(g_d3d, &td, NULL, &st);
    if (!st) return v;
    VT(g_ctx, 47, void (STDMETHODCALLTYPE *)(void *, void *, void *))(g_ctx, st, tex);                   /* CopyResource */
    ND3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(VT(g_ctx, 14, HRESULT (STDMETHODCALLTYPE *)(void *, void *, UINT, UINT, UINT, ND3D11_MAPPED_SUBRESOURCE *))(
            g_ctx, st, 0, 1, 0, &m)) && m.pData) {
        v = ((DWORD *)((BYTE *)m.pData + (size_t)y * m.RowPitch))[x];
        VT(g_ctx, 15, void (STDMETHODCALLTYPE *)(void *, void *, UINT))(g_ctx, st, 0);
    }
    RELEASE(st);
    return v;
}

static HWND g_hwnd;
static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
}
static int near(int a, int b) { return a - b <= 3 && b - a <= 3; }
/* waits (up to five seconds) for the window's pixel (@x, @y) to be @want */
static int shows(int x, int y, COLORREF want)
{
    COLORREF got = 0;
    for (int i = 0; i < 250; i++) {
        pump();
        HDC dc = GetDC(g_hwnd);
        got = GetPixel(dc, x, y);
        ReleaseDC(g_hwnd, dc);
        if (near(GetRValue(got), GetRValue(want)) && near(GetGValue(got), GetGValue(want)) && near(GetBValue(got), GetBValue(want)))
            return 1;
        Sleep(20);
    }
    printf("     (%d, %d) shows %06lx, not %06lx\n", x, y, (unsigned long)got, (unsigned long)want);
    return 0;
}

static DWORD WINAPI watchdog(void *arg)
{
    (void)arg;
    Sleep(150000);
    printf("FAIL: d2dctxtest hung\n");
    fflush(stdout);
    ExitProcess(1);
}

/* a device context drawing into a bitmap of its own, read through Map */
static void own_bitmap(void *dc)
{
    void *target = bitmap1(dc, 64, 64, OPT_TARGET);
    CHECK("CreateBitmap (D2D1_BITMAP_PROPERTIES1, TARGET)", target != 0);
    if (!target) return;
    void *b1 = 0;
    CHECK("the bitmap's ID2D1Bitmap1", SUCCEEDED(VT(target, 0, QI_t)(target, &IID_ID2D1Bitmap1_T, &b1)) && b1);
    if (b1) CHECK("GetOptions", VT(b1, 12, UINT (STDMETHODCALLTYPE *)(void *))(b1) == OPT_TARGET);
    RELEASE(b1);
    set_target(dc, target);
    void *got = 0;
    VT(dc, DC_GET_TARGET, void (STDMETHODCALLTYPE *)(void *, void **))(dc, &got);
    CHECK("GetTarget", got == target);
    RELEASE(got);
    void *red = solid(dc, 1, 0, 0);
    VT(dc, RT_BEGIN, Void_t)(dc);
    clear(dc, 1, 1, 1);
    fill_rect(dc, 8, 8, 24, 24, red);
    CHECK("EndDraw", end_draw(dc) == S_OK);
    DWORD in = bitmap_pixel(dc, target, 64, 64, 10, 10), out = bitmap_pixel(dc, target, 64, 64, 40, 40);
    printf("     own bitmap: %08lx inside, %08lx outside\n", (unsigned long)in, (unsigned long)out);
    CHECK("the rectangle red, read through a CPU_READ copy's Map", in == 0xFFFF0000);
    CHECK("the rest white", out == 0xFFFFFFFF);
    void *noread = bitmap1(dc, 4, 4, OPT_TARGET);
    Mapped m = { 0, 0 };
    CHECK("Map without CPU_READ fails", noread && FAILED(VT(noread, 14, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, Mapped *))(noread, 1, &m)));
    RELEASE(noread);

    /* D2D1_UNIT_MODE_PIXELS at 192 DPI: four DIPs are eight pixels, four pixels are four */
    VT(dc, RT_SET_DPI, void (STDMETHODCALLTYPE *)(void *, float, float))(dc, 192, 192);
    VT(dc, RT_BEGIN, Void_t)(dc);
    clear(dc, 1, 1, 1);
    fill_rect(dc, 0, 0, 4, 4, red);
    VT(dc, DC_SET_UNIT, void (STDMETHODCALLTYPE *)(void *, UINT))(dc, 1);
    fill_rect(dc, 32, 32, 36, 36, red);
    end_draw(dc);
    CHECK("GetUnitMode", VT(dc, DC_GET_UNIT, UINT (STDMETHODCALLTYPE *)(void *))(dc) == 1);
    CHECK("DIPs at 192 DPI: twice the pixels", bitmap_pixel(dc, target, 64, 64, 7, 7) == 0xFFFF0000 &&
                                               bitmap_pixel(dc, target, 64, 64, 9, 9) == 0xFFFFFFFF);
    CHECK("D2D1_UNIT_MODE_PIXELS: pixels", bitmap_pixel(dc, target, 64, 64, 35, 35) == 0xFFFF0000 &&
                                           bitmap_pixel(dc, target, 64, 64, 37, 37) == 0xFFFFFFFF);
    VT(dc, DC_SET_UNIT, void (STDMETHODCALLTYPE *)(void *, UINT))(dc, 0);
    VT(dc, RT_SET_DPI, void (STDMETHODCALLTYPE *)(void *, float, float))(dc, 96, 96);
    set_target(dc, NULL);
    RELEASE(red);
    RELEASE(target);
}

/* a device context drawing into a Direct3D texture */
static void texture_target(void *dc)
{
    void *tex = texture(32, 32), *surf = 0, *bmp = 0;
    CHECK("a render-target texture", tex != 0);
    if (!tex) return;
    CHECK("its IDXGISurface", SUCCEEDED(VT(tex, 0, QI_t)(tex, &NIID_IDXGISurface, &surf)) && surf);
    HRESULT hr = surf ? VT(dc, DC_FROM_SURFACE, HRESULT (STDMETHODCALLTYPE *)(void *, void *, const void *, void **))(dc, surf, NULL, &bmp) : E_FAIL;
    CHECK("CreateBitmapFromDxgiSurface", hr == S_OK && bmp);
    if (bmp) {
        CHECK("its options follow the texture's: TARGET", VT(bmp, 12, UINT (STDMETHODCALLTYPE *)(void *))(bmp) == OPT_TARGET);
        set_target(dc, bmp);
        void *green = solid(dc, 0, 1, 0);
        EllipseF e = { { 16, 16 }, 8, 8 };
        VT(dc, RT_BEGIN, Void_t)(dc);
        clear(dc, 0, 0, 1);
        VT(dc, RT_FILL_ELLIPSE, void (STDMETHODCALLTYPE *)(void *, const EllipseF *, void *))(dc, &e, green);
        CHECK("EndDraw", end_draw(dc) == S_OK);
        DWORD c = texture_pixel(tex, 32, 32, 16, 16), k = texture_pixel(tex, 32, 32, 1, 1);
        printf("     texture: %08lx at the centre, %08lx in the corner\n", (unsigned long)c, (unsigned long)k);
        CHECK("the texture holds the drawing: the circle green", c == 0xFF00FF00);
        CHECK("... the rest blue", k == 0xFF0000FF);
        /* drawing again keeps what the texture holds */
        VT(dc, RT_BEGIN, Void_t)(dc);
        fill_rect(dc, 0, 0, 4, 4, green);
        end_draw(dc);
        CHECK("BeginDraw reads the texture back: the circle kept", texture_pixel(tex, 32, 32, 16, 16) == 0xFF00FF00 &&
                                                                  texture_pixel(tex, 32, 32, 2, 2) == 0xFF00FF00);
        set_target(dc, NULL);
        RELEASE(green);
    }
    RELEASE(bmp);

    /* D2D1CreateDeviceContext: a context on the surface's own device */
    HMODULE d2d = GetModuleHandleW(L"d2d1.dll");
    D2DDevice_t cdc = (D2DDevice_t)GetProcAddress(d2d, "D2D1CreateDeviceContext");
    void *dc2 = 0, *t2 = 0;
    hr = cdc && surf ? cdc(surf, NULL, &dc2) : E_FAIL;
    CHECK("D2D1CreateDeviceContext", hr == S_OK && dc2);
    if (dc2) {
        VT(dc2, DC_GET_TARGET, void (STDMETHODCALLTYPE *)(void *, void **))(dc2, &t2);
        CHECK("... its target the surface", t2 != 0);
        RELEASE(t2);
        VT(dc2, RT_BEGIN, Void_t)(dc2);
        clear(dc2, 1, 0, 0);
        end_draw(dc2);
        CHECK("... drawing into it", texture_pixel(tex, 32, 32, 16, 16) == 0xFFFF0000);
    }
    RELEASE(dc2);
    RELEASE(surf);
    RELEASE(tex);
}

/* DirectComposition surfaces on the ID2D1Device: BeginDraw's device context */
static void composition_surfaces(void *d2ddev)
{
    HMODULE dcomp = LoadLibraryW(L"dcomp.dll");
    DComp_t create = dcomp ? (DComp_t)GetProcAddress(dcomp, "DCompositionCreateDevice2") : NULL;
    void *dev = 0, *target = 0, *visual = 0, *surface = 0, *factory = 0, *fsurface = 0, *dc = 0;
    HRESULT hr = create ? create(d2ddev, &NIID_IDCompositionDesktopDevice, &dev) : E_FAIL;
    CHECK("DCompositionCreateDevice2(an ID2D1Device)", hr == S_OK && dev);
    if (!dev) return;
    VT(dev, 24, HRESULT (STDMETHODCALLTYPE *)(void *, HWND, BOOL, void **))(dev, g_hwnd, TRUE, &target);   /* CreateTargetForHwnd */
    VT(dev, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(dev, &visual);                             /* CreateVisual */
    hr = VT(dev, 8, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, UINT, UINT, UINT, void **))(dev, 64, 64, 87, 1, &surface);
    CHECK("CreateSurface", hr == S_OK && surface && target && visual);
    if (surface) {
        POINT off = { -1, -1 };
        RECT r = { 16, 16, 48, 48 };
        hr = VT(surface, 3, HRESULT (STDMETHODCALLTYPE *)(void *, const RECT *, const GUID *, void **, POINT *))(
            surface, NULL, &NIID_ID2D1DeviceContext, &dc, &off);
        CHECK("BeginDraw(ID2D1DeviceContext)", hr == S_OK && dc && off.x == 0 && off.y == 0);
        if (dc) {
            void *blue = solid(dc, 0, 0, 1);
            clear(dc, 1, 1, 0);
            fill_rect(dc, 32, 0, 64, 64, blue);
            RELEASE(blue);
            RELEASE(dc);
        }
        CHECK("EndDraw", VT(surface, 4, HRESULT (STDMETHODCALLTYPE *)(void *))(surface) == S_OK);
        /* an update rectangle: the context draws at its offset, which the caller applies */
        hr = VT(surface, 3, HRESULT (STDMETHODCALLTYPE *)(void *, const RECT *, const GUID *, void **, POINT *))(
            surface, &r, &NIID_ID2D1DeviceContext, &dc, &off);
        CHECK("BeginDraw(a rectangle, ID2D1DeviceContext)", hr == S_OK && dc && off.x == 16 && off.y == 16);
        if (dc) {
            void *red = solid(dc, 1, 0, 0);
            Mat m = { 1, 0, 0, 1, (float)off.x, (float)off.y };
            VT(dc, RT_SET_TRANSFORM, void (STDMETHODCALLTYPE *)(void *, const Mat *))(dc, &m);
            fill_rect(dc, 0, 0, 8, 8, red);
            RELEASE(red);
            RELEASE(dc);
        }
        VT(surface, 4, HRESULT (STDMETHODCALLTYPE *)(void *))(surface);
        VT(visual, 15, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(visual, surface);                 /* SetContent */
        VT(target, 3, HRESULT (STDMETHODCALLTYPE *)(void *, void *))(target, visual);                   /* SetRoot */
        VT(dev, 3, HRESULT (STDMETHODCALLTYPE *)(void *))(dev);                                         /* Commit */
        CHECK("the surface in the window: yellow, then blue", shows(8, 40, RGB(255, 255, 0)) && shows(56, 40, RGB(0, 0, 255)));
        CHECK("... the update rectangle's red at its offset", shows(20, 20, RGB(255, 0, 0)) && shows(26, 26, RGB(255, 255, 0)));
    }
    hr = VT(dev, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(dev, d2ddev, &factory);          /* CreateSurfaceFactory */
    CHECK("CreateSurfaceFactory(the ID2D1Device)", hr == S_OK && factory);
    if (factory) {
        hr = VT(factory, 3, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, UINT, UINT, UINT, void **))(factory, 16, 16, 87, 1, &fsurface);
        CHECK("... its CreateSurface", hr == S_OK && fsurface);
        POINT off;
        if (fsurface && SUCCEEDED(VT(fsurface, 3, HRESULT (STDMETHODCALLTYPE *)(void *, const RECT *, const GUID *, void **, POINT *))(
                fsurface, NULL, &NIID_ID2D1DeviceContext, &dc, &off)) && dc) {
            clear(dc, 0, 1, 0);
            RELEASE(dc);
            CHECK("... drawn with a device context", VT(fsurface, 4, HRESULT (STDMETHODCALLTYPE *)(void *))(fsurface) == S_OK);
        } else CHECK("... drawn with a device context", 0);
    }
    RELEASE(fsurface); RELEASE(factory); RELEASE(surface); RELEASE(visual); RELEASE(target); RELEASE(dev);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcW(h, m, wp, lp); }

int main(void)
{
    CloseHandle(CreateThread(0, 0, watchdog, 0, 0, 0));
#ifndef _WIN64
    printf("d2dctxtest: the software Direct3D device is 64-bit only\n");
    return 1;
#else
    HMODULE d2d = LoadLibraryW(L"d2d1.dll"), d3d = LoadLibraryW(L"d3d11.dll");
    D2DFactory_t cf = d2d ? (D2DFactory_t)GetProcAddress(d2d, "D2D1CreateFactory") : NULL;
    D2DDevice_t cdev = d2d ? (D2DDevice_t)GetProcAddress(d2d, "D2D1CreateDevice") : NULL;
    D3DDevice_t cd = d3d ? (D3DDevice_t)GetProcAddress(d3d, "D3D11CreateDevice") : NULL;
    void *f1 = 0, *mt = 0, *dxgi = 0, *dev = 0, *dev2 = 0, *dc = 0, *rt = 0, *got = 0;
    CHECK("D2D1CreateFactory(ID2D1Factory1)", cf && SUCCEEDED(cf(0, &IID_ID2D1Factory1_T, NULL, &f1)) && f1);
    if (f1) CHECK("ID2D1Multithread", SUCCEEDED(VT(f1, 0, QI_t)(f1, &IID_ID2D1Multithread_T, &mt)) && mt);
    if (mt) {
        VT(mt, 4, Void_t)(mt);                                                                  /* Enter */
        VT(mt, 5, Void_t)(mt);                                                                  /* Leave */
        CHECK("GetMultithreadProtected: not for a single-threaded factory", !VT(mt, 3, BOOL (STDMETHODCALLTYPE *)(void *))(mt));
    }
    RELEASE(mt);
    if (cd) cd(NULL, 5 /* WARP */, NULL, 0x20 /* BGRA_SUPPORT */, NULL, 0, 7, &g_d3d, NULL, &g_ctx);
    CHECK("a WARP Direct3D 11 device", g_d3d && g_ctx);
    if (g_d3d) VT(g_d3d, 0, QI_t)(g_d3d, &NIID_IDXGIDevice, &dxgi);
    CHECK("its IDXGIDevice", dxgi != 0);
    if (!f1 || !dxgi) { printf("d2dctxtest: %d passed, %d failed\n", pass, fail); return 1; }

    HRESULT hr = VT(f1, 17, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(f1, dxgi, &dev);         /* CreateDevice */
    CHECK("ID2D1Factory1::CreateDevice", hr == S_OK && dev);
    CHECK("D2D1CreateDevice", cdev && SUCCEEDED(cdev(dxgi, NULL, &dev2)) && dev2);
    RELEASE(dev2);
    if (!dev) { printf("d2dctxtest: %d passed, %d failed\n", pass, fail); return 1; }
    hr = VT(dev, 4, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, void **))(dev, 0, &dc);                      /* CreateDeviceContext */
    CHECK("CreateDeviceContext", hr == S_OK && dc);
    if (!dc) { printf("d2dctxtest: %d passed, %d failed\n", pass, fail); return 1; }
    CHECK("the context's ID2D1RenderTarget", SUCCEEDED(VT(dc, 0, QI_t)(dc, &IID_ID2D1RenderTarget_T, &rt)) && rt == dc);
    RELEASE(rt);
    VT(dc, DC_GET_DEVICE, void (STDMETHODCALLTYPE *)(void *, void **))(dc, &got);
    CHECK("GetDevice", got == dev);
    RELEASE(got);
    CHECK("IsDxgiFormatSupported(B8G8R8A8)", VT(dc, DC_FORMAT_OK, BOOL (STDMETHODCALLTYPE *)(void *, UINT))(dc, 87));

    own_bitmap(dc);
    texture_target(dc);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(0);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"d2dctxtest";
    RegisterClassW(&wc);
    RECT r = { 0, 0, 128, 96 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExW(0, L"d2dctxtest", L"d2dctxtest", WS_OVERLAPPEDWINDOW, 60, 60, r.right - r.left, r.bottom - r.top,
                             0, 0, wc.hInstance, 0);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    composition_surfaces(dev);
    DestroyWindow(g_hwnd);

    RELEASE(dc);
    RELEASE(dev);
    RELEASE(dxgi);
    RELEASE(g_ctx);
    RELEASE(g_d3d);
    RELEASE(f1);
    printf("d2dctxtest: %d passed, %d failed\n", pass, fail);
    return fail != 0;
#endif
}
