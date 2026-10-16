/*
 * digitest.exe — USB touch screens, pens and precision touchpads through
 * the HID class driver, as on Windows
 *
 * Runs in the devices self-test boot "touch", which has a USB digitizer
 * on xHCI (tools/padpeer.py --kind digitizer: a touch screen, a pen, a
 * precision touchpad with its mouse and configuration collections, one
 * device).  The checks:
 *  - the tree: its five collections (HID\VID_1209&PID_000B&Col01-05) run
 *    on hiddigi (the touch screen, the pen, the touchpad's fingers, its
 *    configuration; class HIDClass) and mouhid (the touchpad's mouse,
 *    class Mouse), each with its USB device on HidUsb;
 *  - Raw Input lists the digitizer collections as HID devices (usage page
 *    0x0D, usages 2, 4, 5 and 0x0E), as Windows does, and not the mouse;
 *  - GetPointerDevices lists a pen;
 *  - "digitest step 1": the test touches the screen a quarter of the way
 *    across, halfway down, and lifts: WM_POINTERDOWN (PT_TOUCH) there,
 *    WM_POINTERUP;
 *  - "digitest step 2": the pen hovers at three quarters across, touches
 *    with half pressure and lifts: WM_POINTERDOWN (PT_PEN) there,
 *    GetPointerPenInfo's pressure about 512 of 1024;
 *  - "digitest step 3": a finger moves 40 mm right on the touchpad (which
 *    hiddigi put in touchpad mode): the pointer goes right;
 *  - "digitest step 4": a tap on the touchpad: WM_LBUTTONDOWN and UP;
 *  - "digitest step 5": two fingers move down the touchpad: WM_MOUSEWHEEL,
 *    away from the user (the content follows the fingers);
 *  - "digitest step 6": tap and drag: a tap, then a finger touches again
 *    at once and moves 40 mm right: two left clicks, the second one held
 *    while the pointer moves right and let go when the finger lifts;
 *  - "digitest step 7": two fingers flick 30 mm down the touchpad (ten
 *    notches) and lift while moving: the scrolling coasts on past the ten;
 *  - "digitest step 8": the same flick, then a finger touches and moves
 *    right: the touch stops the coasting (no more WM_MOUSEWHEEL once the
 *    pointer has moved);
 *  - "digitest step 9": the test unplugs the digitizer: its collections
 *    leave, and Raw Input and GetPointerDevices no longer list them.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW(PULONG len, LPCWSTR filter, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_ListW(LPCWSTR filter, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Parent(PDWORD parent, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Registry_PropertyW(DWORD dn, ULONG prop, PULONG type, PVOID buf, PULONG len, ULONG flags);

#define CM_DRP_SERVICE   0x05
#define CM_DRP_CLASS     0x08
#define DN_STARTED       0x08

#define WM_POINTERUPDATE_ 0x0245
#define WM_POINTERDOWN_   0x0246
#define WM_POINTERUP_     0x0247
#define PT_TOUCH_ 2
#define PT_PEN_   3

typedef struct {
    DWORD pointerType; UINT32 pointerId, frameId, pointerFlags; HANDLE sourceDevice; HWND hwndTarget;
    POINT ptPixelLocation, ptHimetricLocation, ptPixelLocationRaw, ptHimetricLocationRaw;
    DWORD dwTime; UINT32 historyCount; INT32 InputData; DWORD dwKeyStates; UINT64 PerformanceCount; int ButtonChangeType;
} PtrInfo;
typedef struct { PtrInfo pointerInfo; UINT32 penFlags, penMask, pressure, rotation; INT32 tiltX, tiltY; } PtrPenInfo;
typedef struct {
    DWORD displayOrientation; HANDLE device; int pointerDeviceType; HMONITOR monitor; ULONG startingCursorId;
    USHORT maxActiveContacts; WCHAR productString[520];
} PtrDevice;
typedef BOOL (WINAPI *GetPointerTypeFn)(UINT32, DWORD *);
typedef BOOL (WINAPI *GetPointerPenInfoFn)(UINT32, PtrPenInfo *);
typedef BOOL (WINAPI *GetPointerDevicesFn)(UINT32 *, PtrDevice *);

static GetPointerTypeFn pGetPointerType;
static GetPointerPenInfoFn pGetPointerPenInfo;
static GetPointerDevicesFn pGetPointerDevices;

static int g_pass, g_fail, sw, sh;

static void check(BOOL ok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("%s: ", ok ? "PASS" : "FAIL");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    if (ok) g_pass++; else g_fail++;
}

static void step(int n, const char *what)
{
    printf("digitest step %d: %s\n", n, what);
    fflush(stdout);
}

static void prop(DWORD dn, ULONG which, WCHAR *out, ULONG cap)
{
    ULONG type, len = cap * sizeof(WCHAR);
    if (CM_Get_DevNode_Registry_PropertyW(dn, which, &type, out, &len, 0)) out[0] = 0;
}

/* The digitizer's started collections: how many, and how many run on
 * hiddigi (class HIDClass) or mouhid (class Mouse), their USB device on HidUsb */
static int collections(int *digi, int *mouse)
{
    ULONG len = 0;
    int n = 0;
    *digi = *mouse = 0;
    if (CM_Get_Device_ID_List_SizeW(&len, L"HID", 0x101) || len < 2) return 0;
    WCHAR *list = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (list && !CM_Get_Device_ID_ListW(L"HID", list, len, 0x101))
        for (WCHAR *id = list; *id; id += lstrlenW(id) + 1) {
            DWORD dn, parent;
            ULONG status = 0, problem = 0;
            WCHAR s[64], c[64], p[64];
            if (_wcsnicmp(id, L"HID\\VID_1209&PID_000B&Col", 25) || CM_Locate_DevNodeW(&dn, id, 0)) continue;
            if (CM_Get_DevNode_Status(&status, &problem, dn, 0) || !(status & DN_STARTED)) continue;
            n++;
            prop(dn, CM_DRP_SERVICE, s, 64);
            prop(dn, CM_DRP_CLASS, c, 64);
            p[0] = 0;
            if (!CM_Get_Parent(&parent, dn, 0)) prop(parent, CM_DRP_SERVICE, p, 64);
            if (!lstrcmpiW(s, L"hiddigi") && !lstrcmpiW(c, L"HIDClass") && !lstrcmpiW(p, L"HidUsb")) (*digi)++;
            else if (!lstrcmpiW(s, L"mouhid") && !lstrcmpiW(c, L"Mouse") && !lstrcmpiW(p, L"HidUsb")) (*mouse)++;
            else printf("  %ls: driver %ls, class %ls, its USB device's %ls\n", id, s, c, p);
        }
    if (list) HeapFree(GetProcessHeap(), 0, list);
    return n;
}

/* Raw Input's HID devices from the digitizer: how many, and a bit per
 * digitizer usage (1 << usage) */
static int raw_digitizer(UINT32 *usages, int *other)
{
    UINT n = 0;
    *usages = 0;
    *other = 0;
    if (GetRawInputDeviceList(NULL, &n, sizeof(RAWINPUTDEVICELIST)) || !n) return -1;
    RAWINPUTDEVICELIST l[64];
    if (n > 64) n = 64;
    n = GetRawInputDeviceList(l, &n, sizeof(l[0]));
    int k = 0;
    for (UINT i = 0; i < n && n != (UINT)-1; i++) {
        if (l[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info = { sizeof(info) };
        UINT size = sizeof(info);
        if (GetRawInputDeviceInfoW(l[i].hDevice, RIDI_DEVICEINFO, &info, &size) == (UINT)-1 ||
            info.hid.dwVendorId != 0x1209 || info.hid.dwProductId != 0x000B)
            continue;
        k++;
        if (info.hid.usUsagePage == 0x0D && info.hid.usUsage < 32) *usages |= 1u << info.hid.usUsage;
        else (*other)++;
    }
    return k;
}

static int pens(void)
{
    PtrDevice dev[4];
    UINT32 n = 4;
    int k = 0;
    if (pGetPointerDevices && pGetPointerDevices(&n, dev))
        for (UINT32 i = 0; i < n && i < 4; i++) if (dev[i].pointerDeviceType == 1 || dev[i].pointerDeviceType == 2) k++;
    return k;
}

/* What the window saw */
static int touch_down, touch_up, pen_down, pen_up, lbdown, lbup, wheel, dragmoves;
static POINT touch_at, pen_at, lbdown_at, lbup_at;
static UINT32 pen_pressure;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_POINTERDOWN_: case WM_POINTERUP_: {
        UINT32 id = LOWORD(wp);
        DWORD type = 0;
        pGetPointerType(id, &type);
        POINT at = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (type == PT_TOUCH_ && m == WM_POINTERDOWN_) { touch_down++; touch_at = at; }
        if (type == PT_TOUCH_ && m == WM_POINTERUP_) touch_up++;
        if (type == PT_PEN_ && m == WM_POINTERDOWN_) {
            PtrPenInfo pi;
            pen_down++;
            pen_at = at;
            if (pGetPointerPenInfo(id, &pi)) pen_pressure = pi.pressure;
        }
        if (type == PT_PEN_ && m == WM_POINTERUP_) pen_up++;
        return 0;                                    /* (no mouse messages from these) */
    }
    case WM_LBUTTONDOWN: lbdown++; lbdown_at.x = (short)LOWORD(lp); lbdown_at.y = (short)HIWORD(lp); return 0;
    case WM_LBUTTONUP: lbup++; lbup_at.x = (short)LOWORD(lp); lbup_at.y = (short)HIWORD(lp); return 0;
    case WM_MOUSEMOVE: if (wp & MK_LBUTTON) dragmoves++; return 0;
    case WM_MOUSEWHEEL: wheel += (short)HIWORD(wp); return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

static BOOL pump(BOOL (*done)(void), int secs)
{
    DWORD end = GetTickCount() + secs * 1000;
    while (GetTickCount() < end) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (done()) return TRUE;
        Sleep(20);
    }
    return done();
}

static POINT cursor_start;
static BOOL touch_done(void) { return touch_up >= 1; }
static BOOL pen_done(void) { return pen_up >= 1; }
static BOOL moved_done(void) { POINT p; GetCursorPos(&p); return p.x - cursor_start.x >= 200; }
static BOOL tap_done(void) { return lbup >= 1; }
static BOOL wheel_done(void) { return wheel >= 3 * WHEEL_DELTA; }
static BOOL drag_done(void) { return lbup >= 2; }
static BOOL flicked(void) { return wheel >= 10 * WHEEL_DELTA; }
static BOOL stop_moved(void) { POINT p; GetCursorPos(&p); return p.x - cursor_start.x >= 100; }

/* Pump until the wheel has not turned for @quiet ms (at most @secs s) */
static void settle(int quiet, int secs)
{
    DWORD end = GetTickCount() + secs * 1000, still = GetTickCount();
    int last = wheel;
    while (GetTickCount() < end && GetTickCount() - still < (DWORD)quiet) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (wheel != last) { last = wheel; still = GetTickCount(); }
        Sleep(20);
    }
}

/* Pump for @ms whatever comes */
static void pump_for(int ms)
{
    DWORD end = GetTickCount() + ms;
    while (GetTickCount() < end) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        Sleep(20);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE u = GetModuleHandleA("user32.dll");
    pGetPointerType = (GetPointerTypeFn)GetProcAddress(u, "GetPointerType");
    pGetPointerPenInfo = (GetPointerPenInfoFn)GetProcAddress(u, "GetPointerPenInfo");
    pGetPointerDevices = (GetPointerDevicesFn)GetProcAddress(u, "GetPointerDevices");
    if (!pGetPointerType || !pGetPointerPenInfo || !pGetPointerDevices) {
        printf("FAIL: user32 lacks the pointer functions\n");
        return 1;
    }
    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);

    int digi = 0, mouse = 0, n = 0;
    for (int i = 0; i < 300 && (digi < 4 || mouse < 1); i++) {
        n = collections(&digi, &mouse);
        if (digi < 4 || mouse < 1) Sleep(100);
    }
    check(n == 5 && digi == 4, "the touch screen, pen, touchpad and configuration collections on hiddigi under HidUsb (%d of %d)",
          digi, n);
    check(mouse == 1, "the touchpad's mouse collection on mouhid (%d)", mouse);
    UINT32 usages;
    int other, raw = raw_digitizer(&usages, &other);
    check(raw == 4 && usages == ((1u << 2) | (1u << 4) | (1u << 5) | (1u << 0x0E)) && !other,
          "Raw Input lists the pen, touch screen, touchpad and configuration collections as HID devices, not the mouse "
          "(%d, usages 0x%x)", raw, usages);
    check(pens() >= 1, "GetPointerDevices lists a pen");

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "digitest";
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);
    HWND w = CreateWindowExA(0, "digitest", "digitest", WS_POPUP | WS_VISIBLE, 0, 0, sw, sh, NULL, NULL,
                             wc.hInstance, NULL);
    if (!w) { printf("FAIL: CreateWindowEx\n"); return 1; }
    SetForegroundWindow(w);
    MSG msg;
    for (int i = 0; i < 25; i++) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(20);
    }

    step(1, "touch a quarter across");
    pump(touch_done, 30);
    check(touch_down == 1 && touch_up == 1, "the touch screen: WM_POINTERDOWN and WM_POINTERUP, PT_TOUCH (%d, %d)",
          touch_down, touch_up);
    check(labs(touch_at.x - sw / 4) <= sw / 50 && labs(touch_at.y - sh / 2) <= sh / 50,
          "the contact where it touched (%ld,%ld, want %d,%d)", (long)touch_at.x, (long)touch_at.y, sw / 4, sh / 2);

    step(2, "the pen three quarters across");
    pump(pen_done, 30);
    check(pen_down == 1 && pen_up == 1, "the pen: WM_POINTERDOWN and WM_POINTERUP, PT_PEN (%d, %d)", pen_down, pen_up);
    check(labs(pen_at.x - 3 * sw / 4) <= sw / 50 && labs(pen_at.y - sh / 2) <= sh / 50,
          "the pen where it touched (%ld,%ld, want %d,%d)", (long)pen_at.x, (long)pen_at.y, 3 * sw / 4, sh / 2);
    check(pen_pressure >= 450 && pen_pressure <= 580, "GetPointerPenInfo: half pressure (%u of 1024)", pen_pressure);

    SetCursorPos(sw / 4, sh / 2);
    GetCursorPos(&cursor_start);
    lbdown = lbup = 0;
    step(3, "a finger right on the touchpad");
    pump(moved_done, 30);
    POINT now;
    GetCursorPos(&now);
    check(now.x - cursor_start.x >= 200 && labs(now.y - cursor_start.y) <= 20,
          "the touchpad moves the pointer right (from %ld,%ld to %ld,%ld)",
          (long)cursor_start.x, (long)cursor_start.y, (long)now.x, (long)now.y);
    check(lbdown == 0, "moving is no click (%d)", lbdown);

    step(4, "tap the touchpad");
    pump(tap_done, 30);
    check(lbdown == 1 && lbup == 1, "a tap is a left click (%d down, %d up)", lbdown, lbup);

    step(5, "two fingers down the touchpad");
    pump(wheel_done, 30);
    check(wheel >= 3 * WHEEL_DELTA, "two fingers scroll: WM_MOUSEWHEEL away from the user (%d)", wheel);

    settle(600, 10);
    SetCursorPos(sw / 4, sh / 2);
    GetCursorPos(&cursor_start);
    lbdown = lbup = dragmoves = 0;
    step(6, "tap, then touch again and drag right");
    pump(drag_done, 30);
    check(lbdown == 2 && lbup == 2, "tap and drag: the tap's click, then the button held for the drag (%d down, %d up)",
          lbdown, lbup);
    check(lbup_at.x - lbdown_at.x >= 200 && dragmoves > 0,
          "the drag moves the pointer with the left button down (from x %ld to %ld, %d moves)",
          (long)lbdown_at.x, (long)lbup_at.x, dragmoves);

    settle(600, 10);
    wheel = 0;
    step(7, "flick two fingers down the touchpad");
    pump(flicked, 30);
    settle(800, 15);
    check(wheel >= 12 * WHEEL_DELTA, "scrolling coasts on after the fingers lift (%d notches, the fingers moved 10)",
          wheel / WHEEL_DELTA);

    SetCursorPos(sw / 4, sh / 2);
    GetCursorPos(&cursor_start);
    wheel = 0;
    step(8, "flick, then touch the touchpad to stop");
    pump(stop_moved, 30);
    pump_for(300);
    int stopped_at = wheel;
    pump_for(800);
    check(stopped_at >= 10 * WHEEL_DELTA && wheel == stopped_at,
          "a touch stops the coasting (%d notches when the finger moved, %d after)",
          stopped_at / WHEEL_DELTA, wheel / WHEEL_DELTA);

    step(9, "unplug the digitizer");
    /* (the nodes go with the USB device at once; the drivers stop on a work
     * item after them, so the pen leaves GetPointerDevices a little later) */
    int np = pens();
    for (int i = 0; i < 300; i++) {
        n = collections(&digi, &mouse);
        np = pens();
        if (!n && !np) break;
        Sleep(100);
    }
    raw = raw_digitizer(&usages, &other);
    check(n == 0, "its collections leave (%d)", n);
    check(raw <= 0, "Raw Input no longer lists them (%d)", raw);
    check(np == 0, "GetPointerDevices lists no pen (%d)", np);

    DestroyWindow(w);
    printf("digitest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
