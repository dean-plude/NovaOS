/* comptest.exe — Windows.UI.Composition (dcomp.dll), as the XAML framework
 * and WinRT desktop programs draw with it: the Compositor from
 * RoActivateInstance (the registry's ActivatableClassId key names
 * dcomp.dll), a desktop window target (ICompositorDesktopInterop), a tree of
 * container and sprite visuals with colour brushes, checked in the window's
 * pixels: offsets, opacity, an anchor point, a rotation, an inset clip, a
 * scale through a parent, a hidden visual, InsertBelow, Remove and
 * RemoveAll; the objects' interfaces, runtime class names, Parent and
 * Compositor; and in 64 bits a drawing surface of a graphics device on a
 * WARP Direct3D 11 device (ICompositorInterop, BeginDraw/EndDraw), shown by
 * a surface brush stretched (Fill) and not (None), and one of a graphics
 * device on an ID2D1Device, drawn through BeginDraw's ID2D1DeviceContext.
 *
 * The compositor draws on its own thread after each change, so each check
 * waits (up to five seconds) for the pixel to show.
 *
 *   comptest            the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <roapi.h>
#include <novadx.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define VT(o, i, T) ((T)((void **)*(void **)(o))[i])
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(void *);
typedef HRESULT (STDMETHODCALLTYPE *QI_t)(void *, const GUID *, void **);
typedef HRESULT (STDMETHODCALLTYPE *Get_t)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *Put_t)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *Call_t)(void *);
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)

static const GUID IID_ICompositor_T = { 0xB403CA50, 0x7F8C, 0x4E83, { 0x98, 0x5F, 0xCC, 0x45, 0x06, 0x00, 0x36, 0xD8 } };
static const GUID IID_ICompositorInterop_T = { 0x25297D5C, 0x3AD4, 0x4C9C, { 0xB5, 0xCF, 0xE3, 0x6A, 0x38, 0x51, 0x23, 0x30 } };
static const GUID IID_ICompositorDesktopInterop_T = { 0x29E691FA, 0x4567, 0x4DCA, { 0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07 } };
static const GUID IID_ICompositionObject_T = { 0xBCB4AD45, 0x7609, 0x4550, { 0x93, 0x4F, 0x16, 0x00, 0x2A, 0x68, 0xFD, 0xED } };
static const GUID IID_IVisual_T = { 0x117E202D, 0xA859, 0x4C89, { 0x87, 0x3B, 0xC2, 0xAA, 0x56, 0x67, 0x88, 0xE3 } };
static const GUID IID_IContainerVisual_T = { 0x02F6BC74, 0xED20, 0x4773, { 0xAF, 0xE6, 0xD4, 0x9B, 0x4A, 0x93, 0xDB, 0x32 } };
static const GUID IID_ISpriteVisual_T = { 0x08E05581, 0x1AD1, 0x4F97, { 0x97, 0x57, 0x40, 0x2D, 0x76, 0xE4, 0x23, 0x3B } };
static const GUID IID_ICompositionTarget_T = { 0xA1BEA8BA, 0xD726, 0x4663, { 0x81, 0x29, 0x6B, 0x5E, 0x79, 0x27, 0xFF, 0xA6 } };
static const GUID IID_ICompositionDrawingSurfaceInterop_T = { 0xFD04E6E3, 0xFE0C, 0x4C3C, { 0xAB, 0x19, 0xA0, 0x76, 0x01, 0xA5, 0x76, 0xEE } };
static const GUID IID_ICompositionSurfaceBrush_T = { 0xAD016D79, 0x1E4C, 0x4C0D, { 0x9C, 0x29, 0x83, 0x33, 0x8C, 0x87, 0xC1, 0x62 } };

typedef struct { float X, Y; } Vec2;
typedef struct { float X, Y, Z; } Vec3;
typedef struct { BYTE A, R, G, B; } Color;
typedef struct { float Width, Height; } SizeF;

/* ICompositor */
enum { C_COLOR_BRUSH = 8, C_CONTAINER = 9, C_INSET_CLIP = 16, C_SPRITE = 22, C_SURFACE_BRUSH = 24 };
/* IVisual */
enum { V_ANCHOR = 7, V_CENTER = 13, V_CLIP = 15, V_VISIBLE = 19, V_OFFSET = 21, V_OPACITY = 23, V_PARENT = 26, V_DEGREES = 30,
       V_SCALE = 34, V_SIZE = 36 };
/* IVisualCollection */
enum { VC_COUNT = 6, VC_INSERT_ABOVE = 7, VC_INSERT_BOTTOM = 8, VC_INSERT_TOP = 9, VC_INSERT_BELOW = 10, VC_REMOVE = 11, VC_REMOVE_ALL = 12 };

static void *g_comp;
static HWND g_hwnd;

static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
}
static int near(int a, int b) { return a - b <= 3 && b - a <= 3; }
static int same(COLORREF a, COLORREF b)
{
    return near(GetRValue(a), GetRValue(b)) && near(GetGValue(a), GetGValue(b)) && near(GetBValue(a), GetBValue(b));
}
/* waits for the window's pixel (@x, @y) to be @want */
static COLORREF g_last;
static int shows(int x, int y, COLORREF want)
{
    for (int i = 0; i < 250; i++) {
        pump();
        HDC dc = GetDC(g_hwnd);
        g_last = GetPixel(dc, x, y);
        ReleaseDC(g_hwnd, dc);
        if (same(g_last, want)) return 1;
        Sleep(20);
    }
    printf("comptest: (%d, %d) is %06lX, not %06lX\n", x, y, (unsigned long)g_last, (unsigned long)want);
    return 0;
}

/* a visual of the compositor's (its default interface) and its IVisual */
static void *sprite(BYTE r, BYTE g, BYTE b, float w, float h, float x, float y, void **visual)
{
    void *s = 0, *brush = 0;
    Color c = { 255, r, g, b };
    VT(g_comp, C_SPRITE, Get_t)(g_comp, &s);
    VT(g_comp, C_COLOR_BRUSH, HRESULT (STDMETHODCALLTYPE *)(void *, Color, void **))(g_comp, c, &brush);
    if (!s || !brush) { RELEASE(brush); *visual = 0; return s; }
    VT(s, 7, Put_t)(s, brush);                                             /* ISpriteVisual::put_Brush */
    RELEASE(brush);
    VT(s, 0, QI_t)(s, &IID_IVisual_T, visual);
    Vec2 size = { w, h };
    Vec3 at = { x, y, 0 };
    VT(*visual, V_SIZE, HRESULT (STDMETHODCALLTYPE *)(void *, Vec2))(*visual, size);
    VT(*visual, V_OFFSET, HRESULT (STDMETHODCALLTYPE *)(void *, Vec3))(*visual, at);
    return s;
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(h, m, wp, lp);
}

static DWORD WINAPI watchdog(void *arg)
{
    (void)arg;
    Sleep(150000);
    printf("FAIL: comptest hung\n");
    fflush(stdout);
    ExitProcess(1);
}

#ifdef _WIN64
/* a 32 x 32 drawing surface on a WARP device: the left half orange, the
 * right half purple, shown by a surface brush */
static void drawing_surface(void *children)
{
    typedef HRESULT (WINAPI *CreateDevice_t)(void *, UINT, HMODULE, UINT, const UINT *, UINT, UINT, void **, UINT *, void **);
    HMODULE d3d = LoadLibraryW(L"d3d11.dll");
    CreateDevice_t cd = d3d ? (CreateDevice_t)GetProcAddress(d3d, "D3D11CreateDevice") : 0;
    void *dev = 0, *ctx = 0, *interop = 0, *gdev = 0, *surface = 0, *dsi = 0, *tex = 0, *brush = 0, *sb = 0, *v = 0;
    if (cd) cd(NULL, 5 /* WARP */, NULL, 0, NULL, 0, 7, &dev, NULL, &ctx);
    CHECK("a WARP device", dev && ctx);
    if (!dev) return;
    VT(g_comp, 0, QI_t)(g_comp, &IID_ICompositorInterop_T, &interop);
    CHECK("ICompositorInterop", interop != 0);
    if (interop) VT(interop, 5, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(interop, dev, &gdev);   /* CreateGraphicsDevice */
    CHECK("CreateGraphicsDevice", gdev != 0);
    SizeF sz = { 32, 32 };
    if (gdev) VT(gdev, 6, HRESULT (STDMETHODCALLTYPE *)(void *, SizeF, int, int, void **))(gdev, sz, 87 /* B8G8R8A8 */, 1 /* premultiplied */, &surface);
    CHECK("CreateDrawingSurface", surface != 0);
    if (surface) {
        SizeF got = { 0, 0 };
        VT(surface, 8, Get_t)(surface, &got);
        CHECK("its Size", got.Width == 32 && got.Height == 32);
        VT(surface, 0, QI_t)(surface, &IID_ICompositionDrawingSurfaceInterop_T, &dsi);
    }
    CHECK("ICompositionDrawingSurfaceInterop", dsi != 0);
    POINT off = { -1, -1 };
    HRESULT hr = dsi ? VT(dsi, 3, HRESULT (STDMETHODCALLTYPE *)(void *, const RECT *, const GUID *, void **, POINT *))(dsi, NULL, &NIID_ID3D11Texture2D, &tex, &off) : E_FAIL;
    CHECK("BeginDraw: the texture", hr == S_OK && tex && off.x == 0 && off.y == 0);
    if (tex) {
        DWORD row[32];
        for (int i = 0; i < 32; i++) row[i] = i < 16 ? 0xFFFF8000 : 0xFF800080;
        ND3D11_BOX box = { 0, 0, 0, 32, 1, 1 };
        typedef void (STDMETHODCALLTYPE *Update_t)(void *, void *, UINT, const ND3D11_BOX *, const void *, UINT, UINT);
        for (UINT y = 0; y < 32; y++) { box.top = y; box.bottom = y + 1; VT(ctx, 48, Update_t)(ctx, tex, 0, &box, row, 128, 0); }
        RELEASE(tex);
        CHECK("EndDraw", VT(dsi, 4, Call_t)(dsi) == S_OK);
        CHECK("EndDraw again: not drawing", VT(dsi, 4, Call_t)(dsi) != S_OK);
    }
    if (surface) VT(g_comp, C_SURFACE_BRUSH, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(g_comp, surface, &brush);
    CHECK("CreateSurfaceBrush(surface)", brush != 0);
    void *s = 0;
    if (brush) {
        VT(g_comp, C_SPRITE, Get_t)(g_comp, &s);
        VT(s, 7, Put_t)(s, brush);
        VT(s, 0, QI_t)(s, &IID_IVisual_T, &v);
        Vec2 size = { 64, 32 };
        Vec3 at = { 100, 200, 0 };
        VT(v, V_SIZE, HRESULT (STDMETHODCALLTYPE *)(void *, Vec2))(v, size);
        VT(v, V_OFFSET, HRESULT (STDMETHODCALLTYPE *)(void *, Vec3))(v, at);
        VT(children, VC_INSERT_TOP, Put_t)(children, v);
        CHECK("surface brush, stretched (Fill): the left half orange", shows(110, 210, RGB(255, 128, 0)));
        CHECK("... the right half purple", shows(160, 210, RGB(128, 0, 128)));
        VT(brush, 0, QI_t)(brush, &IID_ICompositionSurfaceBrush_T, &sb);
        if (sb) VT(sb, 11, HRESULT (STDMETHODCALLTYPE *)(void *, int))(sb, 0);                 /* Stretch None */
        CHECK("Stretch None: the surface's own size, centred", shows(105, 210, RGB(255, 255, 255)) && shows(120, 210, RGB(255, 128, 0)) &&
                                                               shows(140, 210, RGB(128, 0, 128)) && shows(155, 210, RGB(255, 255, 255)));
        SIZE half = { 16, 16 };
        CHECK("Resize", VT(dsi, 5, HRESULT (STDMETHODCALLTYPE *)(void *, SIZE))(dsi, half) == S_OK);
        SizeF got = { 0, 0 };
        VT(surface, 8, Get_t)(surface, &got);
        CHECK("Resize: the new size, the pixels kept", got.Width == 16 && shows(130, 210, RGB(255, 128, 0)));
    }
    RELEASE(v); RELEASE(s); RELEASE(sb); RELEASE(brush); RELEASE(dsi); RELEASE(surface); RELEASE(gdev);

    /* a graphics device on an ID2D1Device of the same Direct3D device: BeginDraw hands out a device context */
    typedef HRESULT (WINAPI *D2DDevice_t)(void *, const void *, void **);
    HMODULE d2d = LoadLibraryW(L"d2d1.dll");
    D2DDevice_t cdev = d2d ? (D2DDevice_t)GetProcAddress(d2d, "D2D1CreateDevice") : 0;
    void *dxgi = 0, *d2ddev = 0, *dc = 0;
    VT(dev, 0, QI_t)(dev, &NIID_IDXGIDevice, &dxgi);
    if (cdev && dxgi) cdev(dxgi, NULL, &d2ddev);
    CHECK("an ID2D1Device", d2ddev != 0);
    if (interop && d2ddev) VT(interop, 5, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(interop, d2ddev, &gdev);
    CHECK("CreateGraphicsDevice(the ID2D1Device)", gdev != 0);
    if (gdev) VT(gdev, 6, HRESULT (STDMETHODCALLTYPE *)(void *, SizeF, int, int, void **))(gdev, sz, 87, 1, &surface);
    if (surface) VT(surface, 0, QI_t)(surface, &IID_ICompositionDrawingSurfaceInterop_T, &dsi);
    hr = dsi ? VT(dsi, 3, HRESULT (STDMETHODCALLTYPE *)(void *, const RECT *, const GUID *, void **, POINT *))(dsi, NULL, &NIID_ID2D1DeviceContext, &dc, &off) : E_FAIL;
    CHECK("BeginDraw: an ID2D1DeviceContext", hr == S_OK && dc);
    if (dc) {
        struct { float r, g, b, a; } cyan = { 0, 1, 1, 1 };
        VT(dc, 47, void (STDMETHODCALLTYPE *)(void *, const void *))(dc, &cyan);                             /* Clear */
        RELEASE(dc);
        CHECK("EndDraw", VT(dsi, 4, Call_t)(dsi) == S_OK);
        VT(g_comp, C_SURFACE_BRUSH, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(g_comp, surface, &brush);
        if (brush) {
            VT(g_comp, C_SPRITE, Get_t)(g_comp, &s);
            VT(s, 7, Put_t)(s, brush);
            VT(s, 0, QI_t)(s, &IID_IVisual_T, &v);
            Vec2 size = { 32, 32 };
            Vec3 at = { 200, 200, 0 };
            VT(v, V_SIZE, HRESULT (STDMETHODCALLTYPE *)(void *, Vec2))(v, size);
            VT(v, V_OFFSET, HRESULT (STDMETHODCALLTYPE *)(void *, Vec3))(v, at);
            VT(children, VC_INSERT_TOP, Put_t)(children, v);
        }
        CHECK("the surface drawn with Direct2D, shown", shows(216, 216, RGB(0, 255, 255)));
    }
    RELEASE(v); RELEASE(s); RELEASE(brush); RELEASE(dsi); RELEASE(surface); RELEASE(gdev); RELEASE(d2ddev); RELEASE(dxgi);
    RELEASE(interop); RELEASE(ctx); RELEASE(dev);
}
#endif

int main(void)
{
    CloseHandle(CreateThread(0, 0, watchdog, 0, 0, 0));
    CHECK("RoInitialize", SUCCEEDED(RoInitialize(RO_INIT_SINGLETHREADED)));
    HSTRING cls = 0;
    WindowsCreateString(L"Windows.UI.Composition.Compositor", 33, &cls);
    void *insp = 0;
    HRESULT hr = RoActivateInstance(cls, &insp);
    WindowsDeleteString(cls);
    CHECK("RoActivateInstance(Windows.UI.Composition.Compositor)", hr == S_OK && insp);
    if (insp) VT(insp, 0, QI_t)(insp, &IID_ICompositor_T, &g_comp);
    RELEASE(insp);
    CHECK("ICompositor", g_comp != 0);
    if (!g_comp) { printf("comptest: %d passed, %d failed\n", pass, fail); return 1; }

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(0);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"comptest";
    RegisterClassW(&wc);
    RECT r = { 0, 0, 320, 240 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExW(0, L"comptest", L"comptest", WS_OVERLAPPEDWINDOW, 40, 40, r.right - r.left, r.bottom - r.top, 0, 0, wc.hInstance, 0);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    void *desktop = 0, *target = 0, *root = 0, *root_v = 0, *children = 0;
    VT(g_comp, 0, QI_t)(g_comp, &IID_ICompositorDesktopInterop_T, &desktop);
    CHECK("ICompositorDesktopInterop", desktop != 0);
    if (desktop) VT(desktop, 3, HRESULT (STDMETHODCALLTYPE *)(void *, HWND, BOOL, void **))(desktop, g_hwnd, FALSE, &target);
    CHECK("CreateDesktopWindowTarget", target != 0);
    void *ct = 0;
    if (target) VT(target, 0, QI_t)(target, &IID_ICompositionTarget_T, &ct);
    CHECK("its ICompositionTarget", ct != 0);
    if (!ct) { printf("comptest: %d passed, %d failed\n", pass, fail + 1); return 1; }
    VT(g_comp, C_CONTAINER, Get_t)(g_comp, &root);
    CHECK("CreateContainerVisual", root != 0);
    VT(root, 0, QI_t)(root, &IID_IVisual_T, &root_v);
    Vec2 full = { 320, 240 };
    VT(root_v, V_SIZE, HRESULT (STDMETHODCALLTYPE *)(void *, Vec2))(root_v, full);
    CHECK("Root = the container", VT(ct, 7, Put_t)(ct, root_v) == S_OK);
    VT(root, 6, Get_t)(root, &children);                                               /* IContainerVisual::get_Children */
    CHECK("Children", children != 0);
    if (!children) { printf("comptest: %d passed, %d failed\n", pass, fail); return 1; }

    void *a_v, *b_v, *c_v, *d_v, *e_v, *h_v, *j_v, *g_v, *f_v = 0;
    void *a = sprite(255, 0, 0, 100, 100, 10, 10, &a_v);
    void *b = sprite(0, 255, 0, 50, 50, 80, 80, &b_v);
    void *c = sprite(0, 0, 255, 40, 40, 200, 50, &c_v);
    void *d = sprite(255, 255, 0, 60, 20, 150, 120, &d_v);
    void *e = sprite(255, 0, 255, 80, 30, 10, 150, &e_v);
    void *hid = sprite(64, 64, 64, 320, 240, 0, 0, &h_v);
    void *j = sprite(0, 0, 128, 20, 20, 40, 40, &j_v);
    void *g = sprite(0, 255, 255, 30, 30, 0, 0, &g_v);
    void *f = 0;
    CHECK("CreateSpriteVisual and CreateColorBrush", a_v && b_v && c_v && d_v && e_v && h_v && j_v && g_v);
    VT(b_v, V_OPACITY, HRESULT (STDMETHODCALLTYPE *)(void *, float))(b_v, 0.5f);
    CHECK("Opacity 2: E_INVALIDARG", VT(b_v, V_OPACITY, HRESULT (STDMETHODCALLTYPE *)(void *, float))(b_v, 2.0f) == E_INVALIDARG);
    Vec2 mid = { 0.5f, 0.5f };
    VT(c_v, V_ANCHOR, HRESULT (STDMETHODCALLTYPE *)(void *, Vec2))(c_v, mid);
    VT(d_v, V_DEGREES, HRESULT (STDMETHODCALLTYPE *)(void *, float))(d_v, 90.0f);
    void *clip = 0;
    VT(g_comp, C_INSET_CLIP, HRESULT (STDMETHODCALLTYPE *)(void *, float, float, float, float, void **))(g_comp, 40, 0, 0, 0, &clip);
    CHECK("CreateInsetClip", clip != 0);
    VT(e_v, V_CLIP, Put_t)(e_v, clip);
    VT(h_v, V_VISIBLE, HRESULT (STDMETHODCALLTYPE *)(void *, BYTE))(h_v, 0);
    VT(g_comp, C_CONTAINER, Get_t)(g_comp, &f);
    void *f_kids = 0;
    if (f) { VT(f, 0, QI_t)(f, &IID_IVisual_T, &f_v); VT(f, 6, Get_t)(f, &f_kids); }
    Vec3 f_at = { 220, 120, 0 }, twice = { 2, 2, 1 };
    VT(f_v, V_OFFSET, HRESULT (STDMETHODCALLTYPE *)(void *, Vec3))(f_v, f_at);
    VT(g_v, V_SCALE, HRESULT (STDMETHODCALLTYPE *)(void *, Vec3))(g_v, twice);
    if (f_kids) VT(f_kids, VC_INSERT_TOP, Put_t)(f_kids, g_v);

    VT(children, VC_INSERT_TOP, Put_t)(children, a_v);
    VT(children, VC_INSERT_TOP, Put_t)(children, b_v);
    VT(children, VC_INSERT_TOP, Put_t)(children, c_v);
    VT(children, VC_INSERT_BOTTOM, Put_t)(children, d_v);
    VT(children, VC_INSERT_TOP, Put_t)(children, e_v);
    VT(children, VC_INSERT_TOP, Put_t)(children, f_v);
    VT(children, VC_INSERT_TOP, Put_t)(children, h_v);
    CHECK("InsertBelow(the navy one, the red one)", VT(children, VC_INSERT_BELOW, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *))
                                                    (children, j_v, a_v) == S_OK);
    CHECK("a visual with a parent: E_INVALIDARG", VT(children, VC_INSERT_TOP, Put_t)(children, a_v) == E_INVALIDARG);
    INT32 n = 0;
    VT(children, VC_COUNT, Get_t)(children, &n);
    CHECK("Count", n == 8);

    CHECK("the window's background where no visual is", shows(5, 5, RGB(255, 255, 255)));
    CHECK("a sprite with a colour brush", shows(30, 30, RGB(255, 0, 0)));
    CHECK("InsertBelow: under the red one", shows(50, 50, RGB(255, 0, 0)));
    CHECK("Opacity 0.5 over red", shows(100, 100, RGB(128, 128, 0)));
    CHECK("... over the background", shows(120, 120, RGB(128, 255, 128)));
    CHECK("AnchorPoint (0.5, 0.5): centred on its offset", shows(185, 35, RGB(0, 0, 255)) && shows(175, 35, RGB(255, 255, 255)));
    CHECK("RotationAngleInDegrees 90 (clockwise, about its corner)", shows(140, 170, RGB(255, 255, 0)) && shows(160, 125, RGB(255, 255, 255)));
    CHECK("InsetClip (left 40)", shows(20, 160, RGB(255, 255, 255)) && shows(70, 160, RGB(255, 0, 255)));
    CHECK("Scale 2 inside an offset container", shows(275, 175, RGB(0, 255, 255)) && shows(285, 175, RGB(255, 255, 255)));
    CHECK("IsVisible false: not drawn", shows(300, 220, RGB(255, 255, 255)));

    void *parent = 0, *comp2 = 0, *obj = 0;
    VT(b_v, V_PARENT, Get_t)(b_v, &parent);
    CHECK("Parent: the container", parent == root);
    RELEASE(parent);
    VT(b_v, 0, QI_t)(b_v, &IID_ICompositionObject_T, &obj);
    if (obj) VT(obj, 6, Get_t)(obj, &comp2);
    CHECK("ICompositionObject::Compositor", comp2 == g_comp);
    RELEASE(comp2); RELEASE(obj);
    void *sv = 0, *cv = 0;
    VT(a_v, 0, QI_t)(a_v, &IID_ISpriteVisual_T, &sv);
    VT(a_v, 0, QI_t)(a_v, &IID_IContainerVisual_T, &cv);
    CHECK("a sprite: ISpriteVisual, IContainerVisual", sv == a && cv != 0);
    RELEASE(sv); RELEASE(cv);
    HSTRING name = 0;
    VT(a, 4, Get_t)(a, &name);
    UINT32 len = 0;
    const WCHAR *nm = WindowsGetStringRawBuffer(name, &len);
    CHECK("its runtime class: Windows.UI.Composition.SpriteVisual", nm && !wcscmp(nm, L"Windows.UI.Composition.SpriteVisual"));
    WindowsDeleteString(name);

    CHECK("Remove", VT(children, VC_REMOVE, Put_t)(children, a_v) == S_OK);
    CHECK("Remove: the navy one shows", shows(50, 50, RGB(0, 0, 128)) && shows(30, 30, RGB(255, 255, 255)));
    VT(b_v, V_PARENT, Get_t)(b_v, &parent);
    RELEASE(parent);
    VT(a_v, V_PARENT, Get_t)(a_v, &parent);
    CHECK("removed: no Parent", parent == 0);
    RELEASE(parent);

#ifdef _WIN64
    drawing_surface(children);
#endif

    CHECK("RemoveAll", VT(children, VC_REMOVE_ALL, Call_t)(children) == S_OK);
    VT(children, VC_COUNT, Get_t)(children, &n);
    CHECK("RemoveAll: none left, the background again", n == 0 && shows(50, 50, RGB(255, 255, 255)) && shows(100, 100, RGB(255, 255, 255)));

    RELEASE(a); RELEASE(b); RELEASE(c); RELEASE(d); RELEASE(e); RELEASE(hid); RELEASE(j); RELEASE(g); RELEASE(f); RELEASE(f_kids);
    RELEASE(a_v); RELEASE(b_v); RELEASE(c_v); RELEASE(d_v); RELEASE(e_v); RELEASE(h_v); RELEASE(j_v); RELEASE(g_v); RELEASE(f_v);
    RELEASE(clip); RELEASE(children); RELEASE(root_v); RELEASE(root); RELEASE(ct); RELEASE(target); RELEASE(desktop);
    RELEASE(g_comp);
    DestroyWindow(g_hwnd);
    printf("comptest: %d passed, %d failed (%d-bit)\n", pass, fail, (int)sizeof(void *) * 8);
    return fail ? 1 : 0;
}
