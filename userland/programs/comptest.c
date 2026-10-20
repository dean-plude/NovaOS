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
 * Then the interfaces beyond the first versions, as the XAML framework asks
 * for them (Microsoft.UI.Composition's IIDs among them): ICompositor2, 5,
 * 6 and 7, IVisual2 and 3 (relative offset and size adjustments, checked in
 * the pixels), ICompositionObject2 and 4, a visual's Properties (a property
 * set: insert, TryGet's statuses), a rectangle clip, a Vector3 key frame
 * animation of Offset in a scoped batch whose Completed is raised, an
 * animation group fading a visual out with a cubic Bezier easing function,
 * an expression animation started and stopped, and the easing function
 * statics.
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

/* ---- the interfaces beyond the first versions, and animations ---------------- */
#define G(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) { a, b, c, { d0, d1, d2, d3, d4, d5, d6, d7 } }
static const GUID IID_ICompositor2_T = G(0x735081DC, 0x5E24, 0x45DA, 0xA3, 0x8F, 0xE3, 0x2C, 0xC3, 0x49, 0xA9, 0xA0);
static const GUID IID_ICompositor5_T = G(0x48EA31AD, 0x7FCD, 0x4076, 0xA7, 0x9C, 0x90, 0xCC, 0x4B, 0x85, 0x2C, 0x9B);
static const GUID IID_ICompositor7_T = G(0xD3483FAD, 0x9A12, 0x53BA, 0xBF, 0xC8, 0x88, 0xB7, 0xFF, 0x79, 0x77, 0xC6);
static const GUID IID_MUI_ICompositor_T = G(0x95213C13, 0xC4CB, 0x57DE, 0xB2, 0x67, 0xD2, 0x1A, 0xB9, 0x01, 0xAE, 0x38);
static const GUID IID_MUI_ICompositor2_T = G(0xA9FFEDAD, 0x3982, 0x576D, 0xA3, 0x8A, 0xC8, 0x88, 0xFF, 0x60, 0x58, 0x19);
static const GUID IID_MUI_ICompositor5_T = G(0xBB52D644, 0xA030, 0x5C19, 0xB8, 0x83, 0x57, 0x7D, 0xED, 0x73, 0x9A, 0xE7);
static const GUID IID_MUI_ICompositor6_T = G(0x5FD20200, 0xC188, 0x5737, 0xB5, 0x67, 0x5E, 0x61, 0x74, 0x1A, 0xF1, 0x9C);
static const GUID IID_MUI_ICompositorSwapChainInterop_T = G(0xA0B03594, 0xC413, 0x5F5D, 0xBC, 0x70, 0xCE, 0x93, 0x82, 0xEB, 0x5E, 0x83);
static const GUID IID_MUI_ICompositionObject_T = G(0x0E583D49, 0xFB5E, 0x5481, 0xA4, 0x26, 0xD3, 0xC4, 0x1E, 0x05, 0x9A, 0x5A);
static const GUID IID_MUI_IVisual_T = G(0xC0EEAB6C, 0xC897, 0x5AC6, 0xA1, 0xC9, 0x63, 0xAB, 0xD5, 0x05, 0x5B, 0x9B);
static const GUID IID_IVisual2_T = G(0x3052B611, 0x56C3, 0x4C3E, 0x8B, 0xF3, 0xF6, 0xE1, 0xAD, 0x47, 0x3F, 0x06);
static const GUID IID_IVisual3_T = G(0x30BE580D, 0xF4B6, 0x4AB7, 0x80, 0xDD, 0x37, 0x38, 0xCB, 0xAC, 0x9F, 0x2C);
static const GUID IID_ICompositionObject2_T = G(0xEF874EA1, 0x5CFF, 0x4B68, 0x9E, 0x30, 0xA1, 0x51, 0x9D, 0x08, 0xBA, 0x03);
static const GUID IID_ICompositionObject4_T = G(0x0BB3784C, 0x346B, 0x4A7C, 0x96, 0x6B, 0x73, 0x10, 0x96, 0x65, 0x53, 0xD5);
static const GUID IID_ICompositionPropertySet2_T = G(0xDE80731E, 0xA211, 0x4455, 0x88, 0x80, 0x7D, 0x0F, 0x3F, 0x6A, 0x44, 0xFD);
static const GUID IID_IKeyFrameAnimation_T = G(0x126E7F22, 0x3AE9, 0x4540, 0x9A, 0x8A, 0xDE, 0xAE, 0x8A, 0x4A, 0x4A, 0x84);
static const GUID IID_ICompositionAnimation2_T = G(0x369B603E, 0xA80F, 0x4948, 0x93, 0xE3, 0xED, 0x23, 0xFB, 0x38, 0xC6, 0xCB);
static const GUID IID_ICompositionEasingFunctionStatics_MUI_T = G(0x17A766B6, 0x2936, 0x53EA, 0xB5, 0xAF, 0xC6, 0x42, 0xF4, 0xA6, 0x10, 0x83);
#undef G

/* ICompositor (beyond C_*) and the interfaces' members used here */
enum { C_CUBIC = 10, C_EXPRESSION_WITH = 14, C_PROPERTY_SET = 18, C_SCALAR_KF = 20, C_SCOPED_BATCH = 21, C_VECTOR3_KF = 27 };
enum { CO_PROPERTIES = 8, CO_START = 9, CO_STOP = 10 };
enum { PS_INSERT_SCALAR = 10, PS_INSERT_VECTOR3 = 12, PS_TRYGET_SCALAR = 18, PS_TRYGET_VECTOR3 = 20 };
enum { KA_PUT_DURATION = 9, KA_KEYFRAME_COUNT = 14, KA_INSERT_EXPRESSION = 17, KF_INSERT = 6 };
enum { SB_IS_ACTIVE = 6, SB_END = 8, SB_ADD_COMPLETED = 11 };

/* a TypedEventHandler<Object, CompositionBatchCompletedEventArgs>: sets an event */
typedef struct { const void *const *vt; HANDLE ev; } Done;
static HRESULT STDMETHODCALLTYPE done_qi(void *p, const GUID *iid, void **out) { (void)iid; *out = p; return S_OK; }
static ULONG STDMETHODCALLTYPE done_ref(void *p) { (void)p; return 1; }
static HRESULT STDMETHODCALLTYPE done_invoke(void *p, void *sender, void *args)
{
    (void)args;
    if (sender) SetEvent(((Done *)p)->ev);
    return S_OK;
}
static const void *const done_vt[] = { (void *)done_qi, (void *)done_ref, (void *)done_ref, (void *)done_invoke };

static void *qi(void *o, const GUID *iid)
{
    void *r = 0;
    if (o) VT(o, 0, QI_t)(o, iid, &r);
    return r;
}
static HSTRING hs(const WCHAR *s)
{
    HSTRING h = 0;
    WindowsCreateString(s, (UINT32)wcslen(s), &h);
    return h;
}
static int waits(HANDLE ev)
{
    for (int i = 0; i < 250; i++) {
        pump();
        if (WaitForSingleObject(ev, 20) == WAIT_OBJECT_0) return 1;
    }
    return 0;
}

static void versions(void *children)
{
    /* the compositor's interfaces, in both namespaces */
    void *c2 = qi(g_comp, &IID_ICompositor2_T), *c5 = qi(g_comp, &IID_ICompositor5_T), *c7 = qi(g_comp, &IID_ICompositor7_T);
    void *mc = qi(g_comp, &IID_MUI_ICompositor_T), *mc2 = qi(g_comp, &IID_MUI_ICompositor2_T);
    void *mc5 = qi(g_comp, &IID_MUI_ICompositor5_T), *mc6 = qi(g_comp, &IID_MUI_ICompositor6_T);
    void *swap = qi(g_comp, &IID_MUI_ICompositorSwapChainInterop_T);
    CHECK("ICompositor2, ICompositor5, ICompositor7", c2 && c5 && c7);
    CHECK("Microsoft.UI.Composition: ICompositor, 2, 5, 6, ICompositorSwapChainInterop", mc && mc2 && mc5 && mc6 && swap);

    /* a sprite made through Microsoft.UI's ICompositor, seen through both namespaces */
    void *ms = 0, *mobj = 0, *mcomp = 0;
    if (mc) VT(mc, C_SPRITE, Get_t)(mc, &ms);
    CHECK("Microsoft.UI ICompositor::CreateSpriteVisual", ms != 0);
    void *mv = qi(ms, &IID_MUI_IVisual_T), *v2 = qi(ms, &IID_IVisual2_T), *v3 = qi(ms, &IID_IVisual3_T);
    void *o2 = qi(ms, &IID_ICompositionObject2_T), *o4 = qi(ms, &IID_ICompositionObject4_T);
    CHECK("a visual: Microsoft.UI IVisual, IVisual2, IVisual3, ICompositionObject2, ICompositionObject4", mv && v2 && v3 && o2 && o4);
    mobj = qi(ms, &IID_MUI_ICompositionObject_T);
    if (mobj) VT(mobj, 6, Get_t)(mobj, &mcomp);                                /* Compositor */
    CHECK("Microsoft.UI ICompositionObject::Compositor: Microsoft.UI's ICompositor", mcomp && mcomp == mc);
    BYTE hit = 0;
    if (v3) VT(v3, 6, Get_t)(v3, &hit);
    CHECK("IsHitTestVisible: true", hit == 1);
    RELEASE(mcomp); RELEASE(mobj); RELEASE(mv); RELEASE(v3); RELEASE(o4);

    /* Properties: a property set */
    void *obj = qi(ms, &IID_ICompositionObject_T), *props = 0, *props2 = 0;
    if (obj) VT(obj, CO_PROPERTIES, Get_t)(obj, &props);
    CHECK("Properties", props != 0);
    HSTRING foo = hs(L"Foo"), bar = hs(L"Bar");
    if (props) {
        float f = 0;
        int st = -1;
        Vec3 v = { 0 };
        CHECK("InsertScalar", VT(props, PS_INSERT_SCALAR, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, float))(props, foo, 2.5f) == S_OK);
        VT(props, PS_TRYGET_SCALAR, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, float *, int *))(props, foo, &f, &st);
        CHECK("TryGetScalar: the value", st == 0 && f == 2.5f);
        VT(props, PS_TRYGET_VECTOR3, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, Vec3 *, int *))(props, foo, &v, &st);
        CHECK("TryGetVector3 of a scalar: TypeMismatch", st == 1);
        VT(props, PS_TRYGET_SCALAR, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, float *, int *))(props, bar, &f, &st);
        CHECK("TryGetScalar of none: NotFound", st == 2);
        CHECK("InsertVector3 over a scalar: E_INVALIDARG",
              VT(props, PS_INSERT_VECTOR3, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, Vec3))(props, foo, v) == E_INVALIDARG);
        props2 = qi(props, &IID_ICompositionPropertySet2_T);
        BYTE b = 0;
        if (props2) {
            VT(props2, 6, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, BYTE))(props2, bar, 1);
            VT(props2, 7, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, BYTE *, int *))(props2, bar, &b, &st);
        }
        CHECK("InsertBoolean, TryGetBoolean", props2 && b == 1 && st == 0);
        void *again = 0;
        VT(obj, CO_PROPERTIES, Get_t)(obj, &again);
        CHECK("Properties: the same set each time", again == props);
        RELEASE(again);
    }
    void *ps = 0;
    VT(g_comp, C_PROPERTY_SET, Get_t)(g_comp, &ps);
    CHECK("CreatePropertySet", ps != 0);
    RELEASE(ps);

    /* RelativeOffsetAdjustment and RelativeSizeAdjustment: a band a quarter
     * of the root wide, from its middle */
    void *band_v, *band = sprite(0, 128, 0, 0, 20, 0, 200, &band_v), *band2 = qi(band, &IID_IVisual2_T);
    Vec3 half = { 0.5f, 0, 0 };
    Vec2 quarter = { 0.25f, 0 };
    if (band2) {
        VT(band2, 9, HRESULT (STDMETHODCALLTYPE *)(void *, Vec3))(band2, half);              /* RelativeOffsetAdjustment */
        VT(band2, 11, HRESULT (STDMETHODCALLTYPE *)(void *, Vec2))(band2, quarter);          /* RelativeSizeAdjustment */
    }
    VT(children, VC_INSERT_TOP, Put_t)(children, band_v);
    CHECK("RelativeOffsetAdjustment, RelativeSizeAdjustment",
          shows(200, 210, RGB(0, 128, 0)) && shows(150, 210, RGB(255, 255, 255)) && shows(250, 210, RGB(255, 255, 255)));

    /* a rectangle clip (ICompositor7): the left half of a blue sprite cut off */
    void *rv, *r = sprite(0, 0, 255, 100, 40, 10, 10, &rv), *rclip = 0;
    if (c7) VT(c7, 9, HRESULT (STDMETHODCALLTYPE *)(void *, float, float, float, float, void **))(c7, 50, 0, 100, 40, &rclip);
    CHECK("CreateRectangleClip", rclip != 0);
    VT(rv, V_CLIP, Put_t)(rv, rclip);
    VT(children, VC_INSERT_TOP, Put_t)(children, rv);
    CHECK("RectangleClip (left 50)", shows(30, 20, RGB(255, 255, 255)) && shows(80, 20, RGB(0, 0, 255)));

    /* a key frame animation of Offset in a scoped batch: the sprite moves,
     * and the batch completes */
    void *batch = 0, *anim = 0, *ka = 0;
    VT(g_comp, C_SCOPED_BATCH, HRESULT (STDMETHODCALLTYPE *)(void *, int, void **))(g_comp, 1, &batch);
    CHECK("CreateScopedBatch", batch != 0);
    BYTE active = 0;
    if (batch) VT(batch, SB_IS_ACTIVE, Get_t)(batch, &active);
    CHECK("IsActive", active == 1);
    Done done = { done_vt, CreateEventW(0, TRUE, FALSE, 0) };
    EventRegistrationToken tok = { 0 };
    if (batch) VT(batch, SB_ADD_COMPLETED, HRESULT (STDMETHODCALLTYPE *)(void *, void *, EventRegistrationToken *))(batch, &done, &tok);
    VT(g_comp, C_VECTOR3_KF, Get_t)(g_comp, &anim);
    CHECK("CreateVector3KeyFrameAnimation", anim != 0);
    ka = qi(anim, &IID_IKeyFrameAnimation_T);
    Vec3 to = { 200, 100, 0 };
    HSTRING start = hs(L"this.StartingValue"), offset = hs(L"Offset"), nonsense = hs(L"Nonsense");
    INT32 count = 0;
    if (ka) {
        VT(ka, KA_INSERT_EXPRESSION, HRESULT (STDMETHODCALLTYPE *)(void *, float, HSTRING))(ka, 0.0f, start);
        VT(anim, KF_INSERT, HRESULT (STDMETHODCALLTYPE *)(void *, float, Vec3))(anim, 1.0f, to);
        VT(ka, KA_PUT_DURATION, HRESULT (STDMETHODCALLTYPE *)(void *, INT64))(ka, 3000000);       /* 300 ms */
        VT(ka, KA_KEYFRAME_COUNT, Get_t)(ka, &count);
    }
    CHECK("InsertExpressionKeyFrame, InsertKeyFrame: KeyFrameCount 2", count == 2);
    void *mover_v, *mover = sprite(255, 0, 0, 30, 30, 10, 100, &mover_v), *mobj2 = qi(mover, &IID_ICompositionObject_T);
    VT(children, VC_INSERT_TOP, Put_t)(children, mover_v);
    CHECK("StartAnimation(Offset)", mobj2 && VT(mobj2, CO_START, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, void *))(mobj2, offset, anim) == S_OK);
    CHECK("StartAnimation of no such property: E_INVALIDARG",
          VT(mobj2, CO_START, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, void *))(mobj2, nonsense, anim) == E_INVALIDARG);
    if (batch) VT(batch, SB_END, Call_t)(batch);
    CHECK("the batch's Completed", waits(done.ev));
    Vec3 at = { 0 };
    VT(mover_v, V_OFFSET - 1, HRESULT (STDMETHODCALLTYPE *)(void *, Vec3 *))(mover_v, &at);            /* get_Offset */
    CHECK("the animation's end: Offset (200, 100)", at.X == 200 && at.Y == 100);
    CHECK("... drawn there", shows(215, 115, RGB(255, 0, 0)) && shows(25, 115, RGB(255, 255, 255)));

    /* an animation group (ICompositor2) on a visual's Opacity, through
     * StartAnimationGroup and an animation's Target, with a cubic Bezier
     * easing function */
    void *group = 0, *fade = 0, *fade2 = 0, *ease = 0, *mo2 = qi(mover, &IID_ICompositionObject2_T);
    if (c2) VT(c2, 7, Get_t)(c2, &group);
    VT(g_comp, C_SCALAR_KF, Get_t)(g_comp, &fade);
    Vec2 p1 = { 0.4f, 0 }, p2 = { 0.6f, 1 };
    VT(g_comp, C_CUBIC, HRESULT (STDMETHODCALLTYPE *)(void *, Vec2, Vec2, void **))(g_comp, p1, p2, &ease);
    CHECK("CreateAnimationGroup, CreateScalarKeyFrameAnimation, CreateCubicBezierEasingFunction", group && fade && ease);
    HSTRING opacity = hs(L"Opacity");
    fade2 = qi(fade, &IID_ICompositionAnimation2_T);
    void *fka = qi(fade, &IID_IKeyFrameAnimation_T);
    if (fade2 && fka && group && mo2) {
        VT(fade2, 8, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING))(fade2, opacity);                   /* put_Target */
        VT(fade, KF_INSERT + 1, HRESULT (STDMETHODCALLTYPE *)(void *, float, float, void *))(fade, 1.0f, 0.0f, ease);
        VT(fka, KA_PUT_DURATION, HRESULT (STDMETHODCALLTYPE *)(void *, INT64))(fka, 2000000);
        VT(group, 7, Put_t)(group, fade);                                                           /* Add */
        CHECK("StartAnimationGroup", VT(mo2, 10, Put_t)(mo2, group) == S_OK);
    }
    float op = 1;
    for (int i = 0; i < 250 && op != 0.0f; i++) {                    /* (the animation ends after 200 ms) */
        pump();
        Sleep(20);
        VT(mover_v, V_OPACITY - 1, HRESULT (STDMETHODCALLTYPE *)(void *, float *))(mover_v, &op);
    }
    CHECK("the group faded the sprite out to Opacity 0", op == 0.0f);
    CHECK("... not drawn", shows(215, 115, RGB(255, 255, 255)));

    /* an expression animation: accepted on a property and stopped */
    void *expr = 0;
    HSTRING text = hs(L"this.Target.Size.X / 2"), offx = hs(L"Offset.X");
    VT(g_comp, C_EXPRESSION_WITH, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, void **))(g_comp, text, &expr);
    CHECK("CreateExpressionAnimation", expr != 0);
    CHECK("StartAnimation(Offset.X) with it", VT(mobj2, CO_START, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, void *))(mobj2, offx, expr) == S_OK);
    CHECK("StopAnimation", VT(mobj2, CO_STOP, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING))(mobj2, offx) == S_OK);

    /* CompositionEasingFunction's statics (Microsoft.UI's) and a step easing function */
    HSTRING ecls = hs(L"Microsoft.UI.Composition.CompositionEasingFunction");
    void *statics = 0, *linear = 0, *step = 0;
    RoGetActivationFactory(ecls, &IID_ICompositionEasingFunctionStatics_MUI_T, &statics);
    if (statics) VT(statics, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(statics, g_comp, &linear);
    CHECK("CompositionEasingFunction statics: CreateLinearEasingFunction", linear != 0);
    INT32 steps = 0;
    if (c2) VT(c2, 18, HRESULT (STDMETHODCALLTYPE *)(void *, int, void **))(c2, 4, &step);
    if (step) VT(step, 14, Get_t)(step, &steps);
    CHECK("CreateStepEasingFunction(4): StepCount 4", steps == 4);

    VT(children, VC_REMOVE_ALL, Call_t)(children);
    WindowsDeleteString(foo); WindowsDeleteString(bar); WindowsDeleteString(start); WindowsDeleteString(offset);
    WindowsDeleteString(nonsense); WindowsDeleteString(opacity); WindowsDeleteString(text); WindowsDeleteString(offx);
    WindowsDeleteString(ecls);
    CloseHandle(done.ev);
    RELEASE(statics); RELEASE(linear); RELEASE(step); RELEASE(expr); RELEASE(fka); RELEASE(fade2); RELEASE(ease); RELEASE(fade);
    RELEASE(group); RELEASE(mo2); RELEASE(mobj2); RELEASE(mover_v); RELEASE(mover); RELEASE(ka); RELEASE(anim); RELEASE(batch);
    RELEASE(rclip); RELEASE(rv); RELEASE(r); RELEASE(band2); RELEASE(band_v); RELEASE(band); RELEASE(props2); RELEASE(props);
    RELEASE(obj); RELEASE(o2); RELEASE(v2); RELEASE(ms); RELEASE(swap); RELEASE(mc6); RELEASE(mc5); RELEASE(mc2); RELEASE(mc);
    RELEASE(c7); RELEASE(c5); RELEASE(c2);
}

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

    versions(children);

    RELEASE(a); RELEASE(b); RELEASE(c); RELEASE(d); RELEASE(e); RELEASE(hid); RELEASE(j); RELEASE(g); RELEASE(f); RELEASE(f_kids);
    RELEASE(a_v); RELEASE(b_v); RELEASE(c_v); RELEASE(d_v); RELEASE(e_v); RELEASE(h_v); RELEASE(j_v); RELEASE(g_v); RELEASE(f_v);
    RELEASE(clip); RELEASE(children); RELEASE(root_v); RELEASE(root); RELEASE(ct); RELEASE(target); RELEASE(desktop);
    RELEASE(g_comp);
    DestroyWindow(g_hwnd);
    printf("comptest: %d passed, %d failed (%d-bit)\n", pass, fail, (int)sizeof(void *) * 8);
    return fail ? 1 : 0;
}
