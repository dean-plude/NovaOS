/*
 * reader.c — the Media Foundation source reader (mfreadwrite.dll's
 * MFCreateSourceReaderFromMediaSource) over a camera's media source
 *
 * The reader has the camera's one video stream, selected.  Its native
 * types are the camera's modes; its current type is first the camera's
 * current mode, and SetCurrentMediaType picks another mode by subtype and
 * frame size, or asks for frames converted from an uncompressed one:
 * RGB32 and ARGB32 (top-down, as Media Foundation's RGB is), NV12 and
 * YUY2 from YUY2 or NV12 (BT.601), as Windows' video processor does for
 * a reader made with MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING (NovaOS
 * converts whether or not it was asked to).  A frame rate in the type
 * asks the camera for that interval.
 *
 * ReadSample starts the camera's stream on the first read and returns
 * each frame as a sample of one buffer (with IMF2DBuffer), its time in
 * 100 ns units from the first frame and its duration the frame interval.
 * With MF_SOURCE_READER_ASYNC_CALLBACK, ReadSample returns at once and a
 * thread of the reader calls OnReadSample for each request; Flush drops
 * the requests not yet served and calls OnFlush.  Releasing the reader
 * shuts the media source down, as Windows' does.
 *
 * Only a camera's media source can be read: MFCreateSourceReaderFromURL
 * and byte streams (files) say E_NOTIMPL, and so do sources from
 * elsewhere.
 */
#include "mfpriv.h"

typedef enum { CONV_NONE, CONV_RGB32, CONV_NV12, CONV_YUY2 } Conv;

typedef struct {
    IMFSourceReader  iface;
    LONG             ref;
    CRITICAL_SECTION cs;
    IMFMediaSource  *src;
    Camera          *c;
    BOOL             selected;
    IMFMediaType    *current;          /* what the reader hands out */
    UINT32           mode;             /* the camera mode it comes from */
    Conv             conv;
    UINT32           interval;         /* the interval asked for (0: the mode's) */
    LONGLONG         t0;               /* the first frame's time (-1: none yet) */
    LARGE_INTEGER    freq;
    /* asynchronous mode */
    IMFSourceReaderCallback *cb;
    HANDLE           thread, wake;
    DWORD            thread_id;
    LONG             pending;
    BOOL             quit, flush;
} Reader;

static Reader *rd(IMFSourceReader *r) { return (Reader *)r; }

/* ---- converting frames ---- */

static BYTE clip(int v) { return (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void yuv_bgra(int y, int u, int v, BYTE *o)
{
    int c = 298 * (y - 16), d = u - 128, e = v - 128;
    o[0] = clip((c + 516 * d + 128) >> 8);
    o[1] = clip((c - 100 * d - 208 * e + 128) >> 8);
    o[2] = clip((c + 409 * e + 128) >> 8);
    o[3] = 255;
}

/* @src (@fourcc, @w x @h) into @dst as @conv; the bytes written */
static DWORD convert(Conv conv, UINT32 fourcc, UINT32 w, UINT32 h, const BYTE *src, BYTE *dst)
{
    if (conv == CONV_RGB32) {
        for (UINT32 y = 0; y < h; y++) {
            BYTE *o = dst + (size_t)y * w * 4;
            if (fourcc == NOVACAM_YUY2) {
                const BYTE *s = src + (size_t)y * w * 2;
                for (UINT32 x = 0; x + 1 < w; x += 2, s += 4, o += 8) {
                    yuv_bgra(s[0], s[1], s[3], o);
                    yuv_bgra(s[2], s[1], s[3], o + 4);
                }
            } else {                                                   /* NV12 */
                const BYTE *yl = src + (size_t)y * w, *uv = src + (size_t)w * h + (size_t)(y / 2) * w;
                for (UINT32 x = 0; x < w; x++, o += 4) yuv_bgra(yl[x], uv[x & ~1u], uv[(x & ~1u) + 1], o);
            }
        }
        return w * h * 4;
    }
    if (conv == CONV_NV12) {                                           /* (from YUY2) */
        BYTE *uv = dst + (size_t)w * h;
        for (UINT32 y = 0; y < h; y++) {
            const BYTE *s = src + (size_t)y * w * 2;
            for (UINT32 x = 0; x < w; x++) dst[(size_t)y * w + x] = s[2 * x];
            if (!(y & 1))
                for (UINT32 x = 0; x + 1 < w; x += 2) {
                    uv[(size_t)(y / 2) * w + x] = s[2 * x + 1];
                    uv[(size_t)(y / 2) * w + x + 1] = s[2 * x + 3];
                }
        }
        return w * h * 3 / 2;
    }
    /* CONV_YUY2 (from NV12) */
    for (UINT32 y = 0; y < h; y++) {
        const BYTE *yl = src + (size_t)y * w, *uv = src + (size_t)w * h + (size_t)(y / 2) * w;
        BYTE *o = dst + (size_t)y * w * 2;
        for (UINT32 x = 0; x + 1 < w; x += 2, o += 4) {
            o[0] = yl[x]; o[1] = uv[x]; o[2] = yl[x + 1]; o[3] = uv[x + 1];
        }
    }
    return w * h * 2;
}

/* ---- reading ---- */

static BOOL video_stream(DWORD i) { return i == 0 || i == MF_SOURCE_READER_FIRST_VIDEO_STREAM; }

/* One frame as a sample */
static HRESULT read_one(Reader *r, DWORD *flags, LONGLONG *ts, IMFSample **out)
{
    *flags = 0;
    *ts = 0;
    *out = NULL;
    EnterCriticalSection(&r->cs);
    const NovaCamMode *m = camera_mode(r->c, r->mode);
    HRESULT hr = camera_start(r->c, r->interval);
    BYTE *frame = NULL;
    UINT32 bytes = 0;
    if (SUCCEEDED(hr)) hr = camera_read(r->c, &frame, &bytes, NULL);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG t = (LONGLONG)((double)now.QuadPart * 10000000.0 / (double)r->freq.QuadPart);
    IMFMediaBuffer *b = NULL;
    IMFSample *s = NULL;
    if (SUCCEEDED(hr)) {
        UINT32 w = m->width, h = m->height;
        DWORD len = r->conv == CONV_RGB32 ? w * h * 4 : r->conv == CONV_NV12 ? w * h * 3 / 2 :
                    r->conv == CONV_YUY2 ? w * h * 2 : bytes;
        LONG stride = r->conv == CONV_RGB32 ? (LONG)w * 4 : r->conv == CONV_NV12 ? (LONG)w :
                      r->conv == CONV_YUY2 ? (LONG)w * 2 : m->fourcc == NOVACAM_YUY2 ? (LONG)w * 2 :
                      m->fourcc == NOVACAM_NV12 ? (LONG)w : 0;
        hr = mf_create_video_buffer(len, stride, h, &b);
        if (SUCCEEDED(hr)) {
            BYTE *d;
            b->lpVtbl->Lock(b, &d, NULL, NULL);
            DWORD n = r->conv == CONV_NONE ? (memcpy(d, frame, bytes), bytes) : convert(r->conv, m->fourcc, w, h, frame, d);
            b->lpVtbl->Unlock(b);
            b->lpVtbl->SetCurrentLength(b, n);
            hr = mf_create_sample(&s);
        }
        if (SUCCEEDED(hr)) {
            if (r->t0 < 0) r->t0 = t;
            s->lpVtbl->AddBuffer(s, b);
            s->lpVtbl->SetSampleTime(s, t - r->t0);
            s->lpVtbl->SetSampleDuration(s, r->interval ? r->interval : m->interval);
            *ts = t - r->t0;
            *out = s;
        }
        if (b) b->lpVtbl->Release(b);
    } else {
        *flags = MF_SOURCE_READERF_ERROR;
    }
    LeaveCriticalSection(&r->cs);
    return hr;
}

static DWORD WINAPI async_thread(void *arg)
{
    Reader *r = arg;
    for (;;) {
        WaitForSingleObject(r->wake, INFINITE);
        for (;;) {
            if (r->quit) return 0;
            if (r->flush) {
                r->flush = FALSE;
                InterlockedExchange(&r->pending, 0);
                r->cb->lpVtbl->OnFlush(r->cb, MF_SOURCE_READER_ALL_STREAMS);
                continue;
            }
            if (InterlockedCompareExchange(&r->pending, 0, 0) <= 0) break;
            InterlockedDecrement(&r->pending);
            DWORD flags;
            LONGLONG ts;
            IMFSample *s;
            HRESULT hr = read_one(r, &flags, &ts, &s);
            if (r->flush || r->quit) {                 /* (a flushed request is not answered) */
                if (s) s->lpVtbl->Release(s);
                continue;
            }
            r->cb->lpVtbl->OnReadSample(r->cb, hr, 0, flags, ts, s);
            if (s) s->lpVtbl->Release(s);
        }
    }
}

/* ---- IMFSourceReader ---- */

static HRESULT STDMETHODCALLTYPE r_qi(IMFSourceReader *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFSourceReader)) {
        *ppv = This;
        InterlockedIncrement(&rd(This)->ref);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE r_addref(IMFSourceReader *This) { return (ULONG)InterlockedIncrement(&rd(This)->ref); }

static ULONG STDMETHODCALLTYPE r_release(IMFSourceReader *This)
{
    Reader *r = rd(This);
    LONG n = InterlockedDecrement(&r->ref);
    if (n) return (ULONG)n;
    if (r->thread) {
        r->quit = TRUE;
        SetEvent(r->wake);
        if (GetCurrentThreadId() != r->thread_id) WaitForSingleObject(r->thread, INFINITE);
        CloseHandle(r->thread);
        CloseHandle(r->wake);
    }
    if (r->cb) r->cb->lpVtbl->Release(r->cb);
    camera_stop(r->c);
    r->src->lpVtbl->Shutdown(r->src);
    r->src->lpVtbl->Release(r->src);
    if (r->current) r->current->lpVtbl->Release(r->current);
    DeleteCriticalSection(&r->cs);
    HeapFree(GetProcessHeap(), 0, r);
    return 0;
}

static HRESULT STDMETHODCALLTYPE r_GetStreamSelection(IMFSourceReader *This, DWORD i, BOOL *sel)
{
    if (!sel) return E_POINTER;
    if (!video_stream(i)) return MF_E_INVALIDSTREAMNUMBER;
    *sel = rd(This)->selected;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_SetStreamSelection(IMFSourceReader *This, DWORD i, BOOL sel)
{
    if (!video_stream(i) && i != MF_SOURCE_READER_ALL_STREAMS) return MF_E_INVALIDSTREAMNUMBER;
    rd(This)->selected = sel;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_GetNativeMediaType(IMFSourceReader *This, DWORD i, DWORD k, IMFMediaType **t)
{
    if (!t) return E_POINTER;
    *t = NULL;
    if (!video_stream(i)) return MF_E_INVALIDSTREAMNUMBER;
    return camera_native_type(rd(This)->c, k == MF_SOURCE_READER_CURRENT_TYPE_INDEX ? rd(This)->mode : k, t);
}

static HRESULT STDMETHODCALLTYPE r_GetCurrentMediaType(IMFSourceReader *This, DWORD i, IMFMediaType **t)
{
    Reader *r = rd(This);
    if (!t) return E_POINTER;
    *t = NULL;
    if (!video_stream(i)) return MF_E_INVALIDSTREAMNUMBER;
    HRESULT hr = mf_create_media_type(t);
    if (FAILED(hr)) return hr;
    EnterCriticalSection(&r->cs);
    hr = attr_copy_into((AttrObj *)r->current, (IMFAttributes *)*t);
    LeaveCriticalSection(&r->cs);
    if (FAILED(hr)) { (*t)->lpVtbl->Release(*t); *t = NULL; }
    return hr;
}

/* The current type for camera mode @mode delivered as @conv */
static HRESULT make_current(Reader *r, UINT32 mode, Conv conv, UINT32 interval, IMFMediaType **out)
{
    const NovaCamMode *m = camera_mode(r->c, mode);
    HRESULT hr = mf_create_media_type(out);
    if (FAILED(hr)) return hr;
    GUID g;
    const GUID *sub = conv == CONV_RGB32 ? &MFVideoFormat_RGB32 : conv == CONV_NV12 ? &MFVideoFormat_NV12 :
                      conv == CONV_YUY2 ? &MFVideoFormat_YUY2 : camera_subtype(m->fourcc, &g);
    mf_fill_video_type(*out, sub, m->width, m->height, interval ? interval : m->interval, m->min_interval,
                       conv == CONV_NONE && m->fourcc == NOVACAM_MJPG);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_SetCurrentMediaType(IMFSourceReader *This, DWORD i, DWORD *reserved, IMFMediaType *t)
{
    Reader *r = rd(This);
    (void)reserved;
    if (!t) return E_POINTER;
    if (!video_stream(i)) return MF_E_INVALIDSTREAMNUMBER;
    IMFAttributes *a = (IMFAttributes *)t;
    GUID sub, major;
    if (SUCCEEDED(a->lpVtbl->GetGUID(a, &MF_MT_MAJOR_TYPE, &major)) && !IsEqualGUID(&major, &MFMediaType_Video))
        return MF_E_INVALIDMEDIATYPE;
    if (FAILED(a->lpVtbl->GetGUID(a, &MF_MT_SUBTYPE, &sub))) return MF_E_INVALIDMEDIATYPE;
    EnterCriticalSection(&r->cs);
    const NovaCamMode *cur = camera_mode(r->c, r->mode);
    UINT64 size = (UINT64)cur->width << 32 | cur->height;
    a->lpVtbl->GetUINT64(a, &MF_MT_FRAME_SIZE, &size);
    UINT64 rate = 0;
    UINT32 interval = 0;
    if (SUCCEEDED(a->lpVtbl->GetUINT64(a, &MF_MT_FRAME_RATE, &rate)) && (rate >> 32) && (UINT32)rate)
        interval = (UINT32)(10000000ull * (UINT32)rate / (rate >> 32));

    /* a native mode of that subtype and size; else one to convert from */
    Conv conv = IsEqualGUID(&sub, &MFVideoFormat_RGB32) || IsEqualGUID(&sub, &MFVideoFormat_ARGB32) ? CONV_RGB32 :
                IsEqualGUID(&sub, &MFVideoFormat_NV12) ? CONV_NV12 : IsEqualGUID(&sub, &MFVideoFormat_YUY2) ? CONV_YUY2 :
                CONV_NONE;
    int pick = -1;
    Conv how = CONV_NONE;
    for (UINT32 k = 0; k < camera_mode_count(r->c) && pick < 0; k++) {
        const NovaCamMode *m = camera_mode(r->c, k);
        GUID g;
        if (IsEqualGUID(&sub, camera_subtype(m->fourcc, &g)) && size == ((UINT64)m->width << 32 | m->height)) pick = (int)k;
    }
    if (pick < 0 && conv != CONV_NONE) {
        UINT32 want = conv == CONV_NV12 ? NOVACAM_YUY2 : conv == CONV_YUY2 ? NOVACAM_NV12 : 0;
        for (int pass = 0; pass < 2 && pick < 0; pass++)          /* (the current mode first) */
            for (UINT32 k = 0; k < camera_mode_count(r->c) && pick < 0; k++) {
                const NovaCamMode *m = camera_mode(r->c, k);
                if (!pass && k != r->mode) continue;
                if (size != ((UINT64)m->width << 32 | m->height)) continue;
                if (want ? m->fourcc == want : (m->fourcc == NOVACAM_YUY2 || m->fourcc == NOVACAM_NV12)) pick = (int)k;
            }
        how = conv;
    }
    HRESULT hr = MF_E_INVALIDMEDIATYPE;
    if (pick >= 0) {
        IMFMediaType *n;
        hr = make_current(r, (UINT32)pick, how, interval, &n);
        if (SUCCEEDED(hr)) {
            if ((UINT32)pick != r->mode || interval != r->interval) camera_stop(r->c);
            hr = camera_set_mode(r->c, (UINT32)pick);
            r->current->lpVtbl->Release(r->current);
            r->current = n;
            r->mode = (UINT32)pick;
            r->conv = how;
            r->interval = interval;
        }
    }
    LeaveCriticalSection(&r->cs);
    return hr;
}

static HRESULT STDMETHODCALLTYPE r_SetCurrentPosition(IMFSourceReader *This, REFGUID fmt, const PROPVARIANT *pos)
{
    (void)This; (void)fmt; (void)pos;
    return MF_E_INVALIDREQUEST;                        /* (a live source cannot seek) */
}

static HRESULT STDMETHODCALLTYPE r_ReadSample(IMFSourceReader *This, DWORD i, DWORD control, DWORD *actual, DWORD *flags,
                                              LONGLONG *ts, IMFSample **sample)
{
    Reader *r = rd(This);
    (void)control;
    if (!video_stream(i) && i != MF_SOURCE_READER_ANY_STREAM) return MF_E_INVALIDSTREAMNUMBER;
    if (r->cb) {
        if (actual || flags || ts || sample) return E_INVALIDARG;
        if (!r->selected) return MF_E_INVALIDREQUEST;
        InterlockedIncrement(&r->pending);
        SetEvent(r->wake);
        return S_OK;
    }
    if (!flags || !sample) return E_POINTER;
    *sample = NULL;
    if (actual) *actual = 0;
    if (!r->selected) return MF_E_INVALIDREQUEST;
    LONGLONG t = 0;
    HRESULT hr = read_one(r, flags, &t, sample);
    if (ts) *ts = t;
    return hr;
}

static HRESULT STDMETHODCALLTYPE r_Flush(IMFSourceReader *This, DWORD i)
{
    Reader *r = rd(This);
    if (!video_stream(i) && i != MF_SOURCE_READER_ALL_STREAMS) return MF_E_INVALIDSTREAMNUMBER;
    if (r->cb) {
        r->flush = TRUE;
        SetEvent(r->wake);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_GetServiceForStream(IMFSourceReader *This, DWORD i, REFGUID svc, REFIID riid, void **ppv)
{
    (void)This; (void)i; (void)svc; (void)riid;
    if (ppv) *ppv = NULL;
    return MF_E_UNSUPPORTED_SERVICE;
}

static HRESULT STDMETHODCALLTYPE r_GetPresentationAttribute(IMFSourceReader *This, DWORD i, REFGUID key, PROPVARIANT *v)
{
    if (!v) return E_POINTER;
    if (i == MF_SOURCE_READER_MEDIASOURCE && IsEqualGUID(key, &MF_SOURCE_READER_MEDIASOURCE_CHARACTERISTICS)) {
        memset(v, 0, sizeof(*v));
        v->vt = VT_UI4;
        v->ulVal = camera_characteristics(rd(This)->c);
        return S_OK;
    }
    if (i != MF_SOURCE_READER_MEDIASOURCE && !video_stream(i)) return MF_E_INVALIDSTREAMNUMBER;
    return MF_E_ATTRIBUTENOTFOUND;
}

static const IMFSourceReaderVtbl g_reader_vtbl = {
    r_qi, r_addref, r_release, r_GetStreamSelection, r_SetStreamSelection, r_GetNativeMediaType,
    r_GetCurrentMediaType, r_SetCurrentMediaType, r_SetCurrentPosition, r_ReadSample, r_Flush,
    r_GetServiceForStream, r_GetPresentationAttribute,
};

HRESULT nova_create_source_reader(IMFMediaSource *source, IMFAttributes *attrs, IMFSourceReader **out)
{
    if (!source || !out) return E_POINTER;
    *out = NULL;
    Camera *c = camera_of(source);
    if (!c) return E_NOTIMPL;                          /* (only cameras can be read so far) */
    Reader *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
    if (!r) return E_OUTOFMEMORY;
    r->iface.lpVtbl = &g_reader_vtbl;
    r->ref = 1;
    r->src = source;
    r->c = c;
    r->selected = TRUE;
    r->mode = camera_current_mode(c);
    r->t0 = -1;
    QueryPerformanceFrequency(&r->freq);
    InitializeCriticalSection(&r->cs);
    HRESULT hr = make_current(r, r->mode, CONV_NONE, 0, &r->current);
    if (SUCCEEDED(hr) && attrs &&
        SUCCEEDED(attrs->lpVtbl->GetUnknown(attrs, &MF_SOURCE_READER_ASYNC_CALLBACK, &IID_IMFSourceReaderCallback, (void **)&r->cb))) {
        r->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        r->thread = r->wake ? CreateThread(NULL, 0, async_thread, r, 0, &r->thread_id) : NULL;
        if (!r->thread) hr = E_OUTOFMEMORY;
    }
    if (FAILED(hr)) {
        if (r->cb) r->cb->lpVtbl->Release(r->cb);
        if (r->wake) CloseHandle(r->wake);
        if (r->current) r->current->lpVtbl->Release(r->current);
        DeleteCriticalSection(&r->cs);
        HeapFree(GetProcessHeap(), 0, r);
        return hr;
    }
    source->lpVtbl->AddRef(source);
    *out = &r->iface;
    return S_OK;
}
