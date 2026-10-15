/*
 * camtest.exe — a USB camera through Media Foundation, as a Windows
 * program captures one
 *
 * Runs in the devices self-test boot "camera": a fake UVC 1.1 camera
 * (tools/campeer.py, 1209:000a "NovaOS Test Camera") behind a QEMU
 * usb-redir device.  NovaOS's UVC class driver (kernel/nt/usbvideo.c)
 * runs it, and mfplat.dll's camera source reads it.  The camera has three
 * modes: YUY2 640x480 (30 or 15 frames a second), YUY2 320x240 and NV12
 * 640x480.  Each frame is a test card: eight colour bars (white, yellow,
 * cyan, green, magenta, red, blue, black) over the top 7/8 and, beneath,
 * the frame's number as sixteen blocks (lowest bit on the left, white for
 * a one).  "camtest" checks:
 *  - MFEnumDeviceSources listing the camera with its friendly name and
 *    symbolic link, and nothing for audio capture;
 *  - the media source's presentation descriptor and type handler, and the
 *    source reader's native types (each mode, with its frame size and
 *    frame rate);
 *  - frames read through IMFSourceReader::ReadSample in each mode, and as
 *    RGB32, NV12 from YUY2 and YUY2 from NV12 (the reader's converters):
 *    the bars' colours and frame numbers that go up;
 *  - the asynchronous reader (MF_SOURCE_READER_ASYNC_CALLBACK) and Flush;
 *  - a second reader on the same camera refused while the first streams.
 *
 * "camtest --iso" runs in the boot "isocam" instead, on tools/campeer.py
 * --iso: 1209:000b "NovaOS Isochronous Camera", which streams on an
 * isochronous endpoint (two alternate settings; the UVC driver picks one
 * by the payload size) in MJPEG 640x480 and 320x240 and YUY2 160x120.
 * The same checks, its MJPEG frames read as they come (JPEG images) and
 * decoded by the source reader into RGB32, NV12 and YUY2.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

static int g_pass, g_fail;

static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("%s: ", ok ? "pass" : "FAIL");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    if (ok) g_pass++; else g_fail++;
}

#define BARS 8
static const BYTE g_bars[BARS][3] = {                               /* R, G, B */
    {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0}, {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0},
};

static void yuv(const BYTE *rgb, int *y, int *u, int *v)
{
    double r = rgb[0], g = rgb[1], b = rgb[2];
    *y = (int)(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255 + 0.5);
    *u = (int)(128 + (-37.797 * r - 74.203 * g + 112.0 * b) / 255 + 128.5) - 128;
    *v = (int)(128 + (112.0 * r - 93.786 * g - 18.214 * b) / 255 + 128.5) - 128;
}

static int near(int a, int b, int tol) { return a - b <= tol && b - a <= tol; }

/* The pixel at (x, y) as Y, U, V or R, G, B, from a frame of @sub */
static void pixel(const GUID *sub, const BYTE *p, UINT32 w, UINT32 h, LONG stride, UINT32 x, UINT32 y, int out[3])
{
    if (IsEqualGUID(sub, &MFVideoFormat_RGB32)) {
        const BYTE *q = p + (LONG)y * stride + x * 4;
        out[0] = q[2]; out[1] = q[1]; out[2] = q[0];
    } else if (IsEqualGUID(sub, &MFVideoFormat_YUY2)) {
        const BYTE *q = p + (LONG)y * stride + (x & ~1u) * 2;
        out[0] = q[(x & 1) * 2]; out[1] = q[1]; out[2] = q[3];
    } else {                                                           /* NV12 */
        const BYTE *uv = p + (size_t)stride * h + (size_t)(y / 2) * stride + (x & ~1u);
        out[0] = p[(LONG)y * stride + x]; out[1] = uv[0]; out[2] = uv[1];
    }
}

/* The test card's bars and frame number in one frame; the number, or -1 */
static int card(const GUID *sub, const BYTE *p, UINT32 w, UINT32 h, LONG stride, const char *what)
{
    BOOL rgb = IsEqualGUID(sub, &MFVideoFormat_RGB32);
    int bad = 0;
    for (int k = 0; k < BARS; k++) {
        int px[3], want[3];
        pixel(sub, p, w, h, stride, (UINT32)(k * (w / BARS) + w / BARS / 2), h / 3, px);
        if (rgb) { want[0] = g_bars[k][0]; want[1] = g_bars[k][1]; want[2] = g_bars[k][2]; }
        else yuv(g_bars[k], &want[0], &want[1], &want[2]);
        if (!near(px[0], want[0], 6) || !near(px[1], want[1], 6) || !near(px[2], want[2], 6)) {
            if (!bad++) printf("  %s bar %d: %d,%d,%d, expected %d,%d,%d\n", what, k, px[0], px[1], px[2], want[0], want[1], want[2]);
        }
    }
    if (bad) return -1;
    int n = 0, last_row = 0;
    UINT32 band = h * 7 / 8 & ~1u;
    for (int k = 0; k < 16; k++) {
        int px[3];
        pixel(sub, p, w, h, stride, (UINT32)(k * (w / 16) + w / 32), band + (h - band) / 2, px);
        if (px[0] > 128) n |= 1 << k;
        pixel(sub, p, w, h, stride, (UINT32)(k * (w / 16) + w / 32), h - 2, px);
        if (px[0] > 128) last_row |= 1 << k;
    }
    if (last_row != n) {                                  /* (the bottom rows of another frame: one torn) */
        printf("  %s: frame %d with the bottom rows of %d\n", what, n, last_row);
        return -1;
    }
    return n;
}

/* Reads @count frames of the reader's current type; the last number (-1:
 * failed).  MJPEG frames, which have no picture to check, count up from 0
 * when they are JPEG images. */
static int read_frames(IMFSourceReader *r, int count, const char *what)
{
    IMFMediaType *t = NULL;
    if (FAILED(r->lpVtbl->GetCurrentMediaType(r, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &t))) return -1;
    GUID sub;
    UINT32 w = 0, h = 0;
    t->lpVtbl->GetGUID(t, &MF_MT_SUBTYPE, &sub);
    MFGetAttributeSize((IMFAttributes *)t, &MF_MT_FRAME_SIZE, &w, &h);
    t->lpVtbl->Release(t);
    int last = -1, ok = 1;
    LONGLONG prev_ts = -1;
    for (int i = 0; i < count && ok; i++) {
        DWORD idx = 99, flags = 0;
        LONGLONG ts = 0;
        IMFSample *s = NULL;
        HRESULT hr = r->lpVtbl->ReadSample(r, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &ts, &s);
        if (FAILED(hr) || !s || (flags & MF_SOURCE_READERF_ERROR)) {
            printf("  %s: ReadSample %08lx flags %lx\n", what, (unsigned long)hr, (unsigned long)flags);
            ok = 0;
            break;
        }
        IMFMediaBuffer *b = NULL;
        IMF2DBuffer *b2 = NULL;
        BYTE *p = NULL;
        LONG stride = 0;
        DWORD len = 0;
        if (IsEqualGUID(&sub, &MFVideoFormat_MJPG)) {
            if (SUCCEEDED(s->lpVtbl->ConvertToContiguousBuffer(s, &b)) && SUCCEEDED(b->lpVtbl->Lock(b, &p, NULL, &len))) {
                if (len < 4 || p[0] != 0xFF || p[1] != 0xD8 || p[len - 2] != 0xFF || p[len - 1] != 0xD9) {
                    printf("  %s: not a JPEG image (%lu bytes)\n", what, (unsigned long)len);
                    ok = 0;
                }
                b->lpVtbl->Unlock(b);
                last++;
            } else {
                ok = 0;
            }
        } else if (SUCCEEDED(s->lpVtbl->ConvertToContiguousBuffer(s, &b)) &&
            SUCCEEDED(b->lpVtbl->QueryInterface(b, &IID_IMF2DBuffer, (void **)&b2)) &&
            SUCCEEDED(b2->lpVtbl->Lock2D(b2, &p, &stride))) {
            int n = card(&sub, p, w, h, stride, what);
            b2->lpVtbl->Unlock2D(b2);
            if (n < 0 || (last >= 0 && n <= last) || ts < prev_ts || idx != 0) {
                printf("  %s: frame %d after %d, time %lld after %lld, stream %lu\n", what, n, last, ts, prev_ts,
                       (unsigned long)idx);
                ok = 0;
            }
            last = n;
            prev_ts = ts;
        } else {
            printf("  %s: no 2D buffer\n", what);
            ok = 0;
        }
        if (b2) b2->lpVtbl->Release(b2);
        if (b) b->lpVtbl->Release(b);
        s->lpVtbl->Release(s);
    }
    return ok ? last : -1;
}

static HRESULT set_type(IMFSourceReader *r, const GUID *sub, UINT32 w, UINT32 h, UINT32 fps)
{
    IMFMediaType *t;
    HRESULT hr = MFCreateMediaType(&t);
    if (FAILED(hr)) return hr;
    t->lpVtbl->SetGUID(t, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    t->lpVtbl->SetGUID(t, &MF_MT_SUBTYPE, sub);
    MFSetAttributeSize((IMFAttributes *)t, &MF_MT_FRAME_SIZE, w, h);
    if (fps) MFSetAttributeRatio((IMFAttributes *)t, &MF_MT_FRAME_RATE, fps, 1);
    hr = r->lpVtbl->SetCurrentMediaType(r, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, t);
    t->lpVtbl->Release(t);
    return hr;
}

/* ---- the asynchronous reader's callback ---- */
typedef struct {
    IMFSourceReaderCallback iface;
    LONG ref, samples, flushed;
    HANDLE ev;
} Callback;

static HRESULT STDMETHODCALLTYPE cb_qi(IMFSourceReaderCallback *This, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFSourceReaderCallback)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cb_addref(IMFSourceReaderCallback *This) { return (ULONG)InterlockedIncrement(&((Callback *)This)->ref); }
static ULONG STDMETHODCALLTYPE cb_release(IMFSourceReaderCallback *This) { return (ULONG)InterlockedDecrement(&((Callback *)This)->ref); }
static HRESULT STDMETHODCALLTYPE cb_OnReadSample(IMFSourceReaderCallback *This, HRESULT hr, DWORD i, DWORD flags,
                                                 LONGLONG ts, IMFSample *s)
{
    Callback *c = (Callback *)This;
    (void)i; (void)flags; (void)ts;
    if (SUCCEEDED(hr) && s) InterlockedIncrement(&c->samples);
    SetEvent(c->ev);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cb_OnFlush(IMFSourceReaderCallback *This, DWORD i)
{
    Callback *c = (Callback *)This;
    (void)i;
    InterlockedIncrement(&c->flushed);
    SetEvent(c->ev);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cb_OnEvent(IMFSourceReaderCallback *This, DWORD i, IMFMediaEvent *e)
{
    (void)This; (void)i; (void)e;
    return S_OK;
}
static IMFSourceReaderCallbackVtbl g_cb_vtbl = { cb_qi, cb_addref, cb_release, cb_OnReadSample, cb_OnFlush, cb_OnEvent };

static WCHAR g_name[128];

/* What the camera of the boot offers and what is read from it */
typedef struct { const GUID *sub; UINT32 w, h, fps; } Type;
typedef struct { const GUID *sub; UINT32 w, h, fps; const char *what; } Run;

static const Type g_bulk_types[3] = {
    { &MFVideoFormat_YUY2, 640, 480, 30 }, { &MFVideoFormat_YUY2, 320, 240, 30 }, { &MFVideoFormat_NV12, 640, 480, 30 },
};
static const Run g_bulk_runs[] = {
    { &MFVideoFormat_YUY2,  640, 480, 30, "YUY2 640x480" },
    { &MFVideoFormat_YUY2,  320, 240, 15, "YUY2 320x240 at 15 a second" },
    { &MFVideoFormat_NV12,  640, 480, 30, "NV12 640x480" },
    { &MFVideoFormat_RGB32, 640, 480, 0,  "RGB32 640x480 from NV12" },
    { &MFVideoFormat_RGB32, 320, 240, 0,  "RGB32 320x240 from YUY2" },
    { &MFVideoFormat_NV12,  320, 240, 0,  "NV12 320x240 from YUY2" },
    { &MFVideoFormat_YUY2,  640, 480, 0,  "YUY2 640x480" },
    { NULL },
};
static const Type g_iso_types[3] = {
    { &MFVideoFormat_MJPG, 640, 480, 30 }, { &MFVideoFormat_MJPG, 320, 240, 30 }, { &MFVideoFormat_YUY2, 160, 120, 15 },
};
static const Run g_iso_runs[] = {
    { &MFVideoFormat_MJPG,  640, 480, 30, "MJPEG 640x480" },
    { &MFVideoFormat_RGB32, 640, 480, 0,  "RGB32 640x480 from MJPEG" },
    { &MFVideoFormat_RGB32, 640, 480, 15, "RGB32 640x480 from MJPEG at 15 a second" },
    { &MFVideoFormat_NV12,  320, 240, 0,  "NV12 320x240 from MJPEG" },
    { &MFVideoFormat_YUY2,  320, 240, 0,  "YUY2 320x240 from MJPEG" },
    { &MFVideoFormat_YUY2,  160, 120, 15, "YUY2 160x120" },
    { &MFVideoFormat_RGB32, 160, 120, 0,  "RGB32 160x120 from YUY2" },
    { &MFVideoFormat_MJPG,  320, 240, 30, "MJPEG 320x240" },
    { NULL },
};

static IMFMediaSource *open_camera(IMFActivate **act_out)
{
    IMFAttributes *a;
    IMFActivate **devs = NULL;
    UINT32 n = 0;
    MFCreateAttributes(&a, 1);
    a->lpVtbl->SetGUID(a, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    HRESULT hr = MFEnumDeviceSources(a, &devs, &n);
    a->lpVtbl->Release(a);
    if (FAILED(hr) || !n) return NULL;
    IMFMediaSource *src = NULL;
    devs[0]->lpVtbl->ActivateObject(devs[0], &IID_IMFMediaSource, (void **)&src);
    for (UINT32 k = 1; k < n; k++) devs[k]->lpVtbl->Release(devs[k]);
    *act_out = devs[0];
    CoTaskMemFree(devs);
    return src;
}

int main(int argc, char **argv)
{
    BOOL iso = argc > 1 && !strcmp(argv[1], "--iso");
    const WCHAR *want_name = iso ? L"NovaOS Isochronous Camera" : L"NovaOS Test Camera";
    const Type *want = iso ? g_iso_types : g_bulk_types;
    const Run *runs = iso ? g_iso_runs : g_bulk_runs;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("camtest: a USB camera through Media Foundation (%d-bit%s)\n", (int)sizeof(void *) * 8,
           iso ? ", isochronous" : "");
    check(SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL)), "MFStartup");

    /* ---- enumeration ---- */
    IMFAttributes *a;
    IMFActivate **devs = NULL;
    UINT32 n = 0;
    MFCreateAttributes(&a, 1);
    a->lpVtbl->SetGUID(a, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    HRESULT hr = MFEnumDeviceSources(a, &devs, &n);
    check(SUCCEEDED(hr) && n == 1, "MFEnumDeviceSources: %u video capture device(s) (hr %08lx)", n, (unsigned long)hr);
    if (n) {
        WCHAR *name = NULL, *link = NULL;
        UINT32 len = 0;
        devs[0]->lpVtbl->GetAllocatedString(devs[0], &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len);
        devs[0]->lpVtbl->GetAllocatedString(devs[0], &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &len);
        check(name && !wcscmp(name, want_name), "friendly name \"%ls\"", name ? name : L"");
        check(link && wcslen(link) > 4, "symbolic link %ls", link ? link : L"");
        if (name) lstrcpynW(g_name, name, 128);
        CoTaskMemFree(name);
        CoTaskMemFree(link);
        for (UINT32 k = 0; k < n; k++) devs[k]->lpVtbl->Release(devs[k]);
        CoTaskMemFree(devs);
    }
    devs = NULL;
    n = 99;
    a->lpVtbl->SetGUID(a, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
    hr = MFEnumDeviceSources(a, &devs, &n);
    check(SUCCEEDED(hr) && n == 0, "no audio capture sources (%u)", n);
    if (devs) CoTaskMemFree(devs);
    a->lpVtbl->Release(a);

    /* ---- the media source ---- */
    IMFActivate *act = NULL;
    IMFMediaSource *src = open_camera(&act);
    check(src != NULL, "ActivateObject: a media source");
    if (!src) goto done;
    DWORD ch = 0;
    src->lpVtbl->GetCharacteristics(src, &ch);
    check(ch & MFMEDIASOURCE_IS_LIVE, "the source is live (%lx)", (unsigned long)ch);
    IMFPresentationDescriptor *pd = NULL;
    hr = src->lpVtbl->CreatePresentationDescriptor(src, &pd);
    DWORD streams = 0, types = 0;
    if (SUCCEEDED(hr)) {
        pd->lpVtbl->GetStreamDescriptorCount(pd, &streams);
        BOOL sel = FALSE;
        IMFStreamDescriptor *sd = NULL;
        IMFMediaTypeHandler *th = NULL;
        GUID major = {0};
        if (SUCCEEDED(pd->lpVtbl->GetStreamDescriptorByIndex(pd, 0, &sel, &sd)) &&
            SUCCEEDED(sd->lpVtbl->GetMediaTypeHandler(sd, &th))) {
            th->lpVtbl->GetMediaTypeCount(th, &types);
            th->lpVtbl->GetMajorType(th, &major);
            th->lpVtbl->Release(th);
        }
        if (sd) sd->lpVtbl->Release(sd);
        pd->lpVtbl->Release(pd);
        check(streams == 1 && sel && types == 3 && IsEqualGUID(&major, &MFMediaType_Video),
              "presentation descriptor: %lu stream(s), %lu video type(s)", (unsigned long)streams, (unsigned long)types);
    } else {
        check(0, "CreatePresentationDescriptor %08lx", (unsigned long)hr);
    }

    /* ---- the source reader ---- */
    IMFSourceReader *r = NULL;
    hr = MFCreateSourceReaderFromMediaSource(src, NULL, &r);
    check(SUCCEEDED(hr) && r, "MFCreateSourceReaderFromMediaSource (%08lx)", (unsigned long)hr);
    if (!r) goto done;
    int types_ok = 1;
    for (DWORD k = 0;; k++) {
        IMFMediaType *t = NULL;
        hr = r->lpVtbl->GetNativeMediaType(r, MF_SOURCE_READER_FIRST_VIDEO_STREAM, k, &t);
        if (FAILED(hr)) {
            if (k != 3 || hr != MF_E_NO_MORE_TYPES) types_ok = 0;
            break;
        }
        GUID sub;
        UINT32 w = 0, h = 0, num = 0, den = 0;
        t->lpVtbl->GetGUID(t, &MF_MT_SUBTYPE, &sub);
        MFGetAttributeSize((IMFAttributes *)t, &MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio((IMFAttributes *)t, &MF_MT_FRAME_RATE, &num, &den);
        printf("  native type %lu: %.4s %ux%u, %u/%u frames a second\n", (unsigned long)k, (const char *)&sub.Data1, w, h,
               num, den);
        if (k >= 3 || !IsEqualGUID(&sub, want[k].sub) || w != want[k].w || h != want[k].h || !den || num / den != want[k].fps)
            types_ok = 0;
        t->lpVtbl->Release(t);
    }
    check(types_ok, iso ? "native types: MJPG 640x480, MJPG 320x240, YUY2 160x120" :
                          "native types: YUY2 640x480, YUY2 320x240, NV12 640x480");

    for (int k = 0; runs[k].sub; k++) {
        hr = set_type(r, runs[k].sub, runs[k].w, runs[k].h, runs[k].fps);
        int last = SUCCEEDED(hr) ? read_frames(r, 5, runs[k].what) : -1;
        check(last >= 0, "%s: 5 frames, the last number %d (hr %08lx)", runs[k].what, last, (unsigned long)hr);
    }

    /* the current mode streams, so a second reader on the camera is refused */
    IMFActivate *act2 = NULL;
    IMFMediaSource *src2 = open_camera(&act2);
    IMFSourceReader *r2 = NULL;
    if (src2) MFCreateSourceReaderFromMediaSource(src2, NULL, &r2);
    if (r2) {
        set_type(r2, want[0].sub, want[0].w, want[0].h, 0);
        DWORD idx, flags = 0;
        LONGLONG ts;
        IMFSample *s = NULL;
        hr = r2->lpVtbl->ReadSample(r2, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &ts, &s);
        check(hr == MF_E_HW_MFT_FAILED_START_STREAMING && !s, "a second reader while the first streams: %08lx", (unsigned long)hr);
        if (s) s->lpVtbl->Release(s);
        r2->lpVtbl->Release(r2);
    } else {
        check(0, "a second source on the camera");
    }
    if (src2) src2->lpVtbl->Release(src2);
    if (act2) { act2->lpVtbl->ShutdownObject(act2); act2->lpVtbl->Release(act2); }
    r->lpVtbl->Release(r);                                  /* (shuts the source down) */
    src->lpVtbl->Release(src);
    act->lpVtbl->ShutdownObject(act);
    act->lpVtbl->Release(act);

    /* ---- the asynchronous reader ---- */
    src = open_camera(&act);
    Callback cb = { { &g_cb_vtbl }, 1, 0, 0, CreateEventW(NULL, FALSE, FALSE, NULL) };
    MFCreateAttributes(&a, 1);
    a->lpVtbl->SetUnknown(a, &MF_SOURCE_READER_ASYNC_CALLBACK, (IUnknown *)&cb.iface);
    r = NULL;
    if (src) MFCreateSourceReaderFromMediaSource(src, a, &r);
    a->lpVtbl->Release(a);
    if (r) {
        set_type(r, &MFVideoFormat_RGB32, 320, 240, 0);
        DWORD start = GetTickCount();
        for (int k = 0; k < 4; k++) r->lpVtbl->ReadSample(r, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, NULL, NULL, NULL);
        while (cb.samples < 4 && GetTickCount() - start < 30000) WaitForSingleObject(cb.ev, 1000);
        check(cb.samples == 4, "asynchronous reader: %ld of 4 samples", cb.samples);
        r->lpVtbl->Flush(r, MF_SOURCE_READER_ALL_STREAMS);
        start = GetTickCount();
        while (!cb.flushed && GetTickCount() - start < 10000) WaitForSingleObject(cb.ev, 1000);
        check(cb.flushed == 1, "Flush answered by OnFlush");
        r->lpVtbl->Release(r);
    } else {
        check(0, "an asynchronous reader");
    }
    if (src) src->lpVtbl->Release(src);
    if (act) { act->lpVtbl->ShutdownObject(act); act->lpVtbl->Release(act); }

done:
    MFShutdown();
    printf("camtest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
