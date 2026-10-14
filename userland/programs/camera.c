/*
 * camera.exe — the Camera app: the first camera's picture in a window,
 * through Media Foundation as a Windows camera app reads one
 *
 * MFEnumDeviceSources finds the camera, a source reader hands its frames
 * over as RGB32 (converted from what the camera sends), and each one is
 * drawn with StretchDIBits, scaled to the window.  The bar beneath names
 * the camera, the frame size and the frames a second.  With no camera the
 * window says so.  "camera --frames N" closes after N frames (the devices
 * suite's "camera" boot); it reports what it shows with OutputDebugString
 * ("camera: showing ...", "camera: showed N frames").
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#define BAR_H 32

static IMFSourceReader *g_reader;
static IMFActivate *g_act;
static BYTE *g_pixels;
static UINT32 g_w, g_h;
static WCHAR g_name[128] = L"";
static char g_status[160] = "Looking for a camera...";
static int g_frames;
static DWORD g_t0;

static void say(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

/* The first camera, reading RGB32 at its first mode's size */
static BOOL open_camera(void)
{
    IMFAttributes *a;
    IMFActivate **devs = NULL;
    UINT32 n = 0;
    if (FAILED(MFCreateAttributes(&a, 1))) return FALSE;
    a->lpVtbl->SetGUID(a, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    HRESULT hr = MFEnumDeviceSources(a, &devs, &n);
    a->lpVtbl->Release(a);
    if (FAILED(hr) || !n) {
        snprintf(g_status, sizeof(g_status), "No camera found. Plug in a USB camera and start Camera again.");
        return FALSE;
    }
    g_act = devs[0];
    for (UINT32 k = 1; k < n; k++) devs[k]->lpVtbl->Release(devs[k]);
    CoTaskMemFree(devs);
    WCHAR *name = NULL;
    UINT32 len;
    if (SUCCEEDED(g_act->lpVtbl->GetAllocatedString(g_act, &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len))) {
        lstrcpynW(g_name, name, 128);
        CoTaskMemFree(name);
    }
    IMFMediaSource *src = NULL;
    hr = g_act->lpVtbl->ActivateObject(g_act, &IID_IMFMediaSource, (void **)&src);
    if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(src, NULL, &g_reader);
    if (src) src->lpVtbl->Release(src);
    IMFMediaType *t = NULL;
    if (SUCCEEDED(hr)) hr = g_reader->lpVtbl->GetNativeMediaType(g_reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &t);
    if (SUCCEEDED(hr)) {
        MFGetAttributeSize((IMFAttributes *)t, &MF_MT_FRAME_SIZE, &g_w, &g_h);
        t->lpVtbl->SetGUID(t, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
        hr = g_reader->lpVtbl->SetCurrentMediaType(g_reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, t);
        t->lpVtbl->Release(t);
    }
    if (SUCCEEDED(hr) && !(g_pixels = calloc((size_t)g_w * g_h, 4))) hr = E_OUTOFMEMORY;
    if (FAILED(hr)) {
        snprintf(g_status, sizeof(g_status), "The camera could not be started (error %08lx).", (unsigned long)hr);
        return FALSE;
    }
    snprintf(g_status, sizeof(g_status), "%ls  -  %ux%u", g_name, g_w, g_h);
    return TRUE;
}

/* One frame into g_pixels; FALSE when the camera stopped */
static BOOL next_frame(void)
{
    DWORD idx, flags = 0;
    LONGLONG ts;
    IMFSample *s = NULL;
    HRESULT hr = g_reader->lpVtbl->ReadSample(g_reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &ts, &s);
    if (FAILED(hr) || !s) {
        snprintf(g_status, sizeof(g_status), "%ls stopped (error %08lx).", g_name, (unsigned long)hr);
        return FALSE;
    }
    IMFMediaBuffer *b = NULL;
    BYTE *p;
    DWORD len = 0;
    if (SUCCEEDED(s->lpVtbl->ConvertToContiguousBuffer(s, &b)) && SUCCEEDED(b->lpVtbl->Lock(b, &p, NULL, &len))) {
        memcpy(g_pixels, p, len < g_w * g_h * 4 ? len : g_w * g_h * 4);
        b->lpVtbl->Unlock(b);
    }
    if (b) b->lpVtbl->Release(b);
    s->lpVtbl->Release(s);
    if (!g_frames++) g_t0 = GetTickCount();
    DWORD ms = GetTickCount() - g_t0;
    if (g_frames > 1 && ms)
        snprintf(g_status, sizeof(g_status), "%ls  -  %ux%u  -  %u.%u frames a second", g_name, g_w, g_h,
                 (unsigned)((g_frames - 1) * 1000u / ms), (unsigned)((g_frames - 1) * 10000u / ms % 10));
    return TRUE;
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int vh = rc.bottom - BAR_H > 0 ? rc.bottom - BAR_H : 0;
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    if (g_frames && g_pixels) {
        /* the picture, as large as fits with its shape kept */
        int dw = rc.right, dh = (int)((long long)rc.right * g_h / g_w);
        if (dh > vh) { dh = vh; dw = (int)((long long)vh * g_w / g_h); }
        int dx = (rc.right - dw) / 2, dy = (vh - dh) / 2;
        RECT l = { 0, 0, dx, vh }, r = { dx + dw, 0, rc.right, vh }, t = { dx, 0, dx + dw, dy }, bt = { dx, dy + dh, dx + dw, vh };
        FillRect(dc, &l, black); FillRect(dc, &r, black); FillRect(dc, &t, black); FillRect(dc, &bt, black);
        BITMAPINFO bi = {0};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = (LONG)g_w;
        bi.bmiHeader.biHeight = -(LONG)g_h;                         /* (top-down, as RGB32 samples are) */
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(dc, dx, dy, dw, dh, 0, 0, (int)g_w, (int)g_h, g_pixels, &bi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        RECT v = { 0, 0, rc.right, vh };
        FillRect(dc, &v, black);
    }
    DeleteObject(black);
    RECT bar = { 0, vh, rc.right, rc.bottom };
    HBRUSH bb = CreateSolidBrush(RGB(0x20, 0x20, 0x28));
    FillRect(dc, &bar, bb);
    DeleteObject(bb);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0xF0, 0xF0, 0xF0));
    TextOutA(dc, 12, vh + 8, g_status, (int)strlen(g_status));
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(int argc, char **argv)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    int limit = 0;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--frames")) limit = atoi(argv[i + 1]);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"NovaCamera";
    RegisterClassW(&wc);
    RECT r = { 0, 0, 640, 480 + BAR_H };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, L"NovaCamera", L"Camera", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MFStartup(MF_VERSION, MFSTARTUP_FULL);
    BOOL live = open_camera();
    InvalidateRect(hwnd, NULL, FALSE);
    if (live) say("camera: showing %ls, %ux%u\n", g_name, g_w, g_h);
    else say("camera: %s\n", g_status);

    MSG m;
    for (;;) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) goto out;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (live) {
            live = next_frame();
            InvalidateRect(hwnd, NULL, FALSE);
            if (g_frames == 1) say("camera: first frame\n");
            if (limit && g_frames >= limit) {
                say("camera: showed %d frames\n", g_frames);
                DestroyWindow(hwnd);
            }
        } else {
            if (limit) { DestroyWindow(hwnd); limit = 0; }
            WaitMessage();
        }
    }
out:
    if (g_reader) g_reader->lpVtbl->Release(g_reader);
    if (g_act) { g_act->lpVtbl->ShutdownObject(g_act); g_act->lpVtbl->Release(g_act); }
    MFShutdown();
    free(g_pixels);
    return (int)m.wParam;
}
