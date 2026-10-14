/*
 * camera.c — Media Foundation's video capture devices: MFEnumDeviceSources
 * and MFCreateDeviceSource (mf.dll's exports), the media source of a
 * camera, and its presentation and stream descriptors
 *
 * A camera is a device interface of class KSCATEGORY_VIDEO_CAMERA, as on
 * Windows 10 and 11; NovaOS's USB Video Class driver (kernel/nt/
 * usbvideo.c) turns one on for each camera it runs.  MFEnumDeviceSources
 * gives an activation object per camera with the attributes Windows'
 * have (the friendly name, the symbolic link, the category); activating
 * one opens the camera.  Its one stream's native media types are the
 * driver's modes: each format and frame size the camera offers, at its
 * default frame rate.  Choosing one (IMFMediaTypeHandler's
 * SetCurrentMediaType, or the source reader's) chooses the mode the
 * stream starts in.
 *
 * Frames are read through the source reader (reader.c), which is how
 * programs read a camera.  The media source's own streaming (Start, its
 * IMFMediaStream and events, which a media session or a program's own
 * pipeline would use) is not done yet: Start says E_NOTIMPL.  No audio
 * capture sources are listed yet (MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_
 * AUDCAP_GUID lists none).
 */
#include "mfpriv.h"

__declspec(dllimport) DWORD WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) DWORD WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);

struct Camera {
    IMFMediaSource   iface;
    LONG             ref;
    CRITICAL_SECTION cs;
    HANDLE           dev;
    NovaCamInfo      info;
    UINT32           mode;             /* the native mode the stream starts in */
    BOOL             streaming, shut;
    BYTE            *frame;            /* NovaCamFrame, then the frame */
    UINT32           frame_cap;
};

static const IMFMediaSourceVtbl g_src_vtbl;

Camera *camera_of(IMFMediaSource *s) { return s && s->lpVtbl == &g_src_vtbl ? (Camera *)s : NULL; }
UINT32 camera_mode_count(Camera *c) { return c->info.nmodes; }
const NovaCamMode *camera_mode(Camera *c, UINT32 i) { return i < c->info.nmodes ? &c->info.modes[i] : NULL; }
UINT32 camera_current_mode(Camera *c) { return c->mode; }
DWORD camera_characteristics(Camera *c) { (void)c; return MFMEDIASOURCE_IS_LIVE; }

/* The media subtype of a FOURCC (its GUID: the FOURCC, then MEDIASUBTYPE's base) */
const GUID *camera_subtype(UINT32 fourcc, GUID *g)
{
    *g = MFVideoFormat_YUY2;
    g->Data1 = fourcc;
    return g;
}

HRESULT camera_native_type(Camera *c, UINT32 i, IMFMediaType **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (i >= c->info.nmodes) return MF_E_NO_MORE_TYPES;
    const NovaCamMode *m = &c->info.modes[i];
    HRESULT hr = mf_create_media_type(out);
    if (FAILED(hr)) return hr;
    GUID g;
    mf_fill_video_type(*out, camera_subtype(m->fourcc, &g), m->width, m->height, m->interval, m->min_interval,
                       m->fourcc == NOVACAM_MJPG);
    return S_OK;
}

HRESULT camera_set_mode(Camera *c, UINT32 i)
{
    if (i >= c->info.nmodes) return MF_E_INVALIDMEDIATYPE;
    EnterCriticalSection(&c->cs);
    if (c->mode != i && c->streaming) camera_stop(c);
    c->mode = i;
    LeaveCriticalSection(&c->cs);
    return S_OK;
}

static HRESULT last_error(void)
{
    DWORD e = GetLastError();
    return e ? HRESULT_FROM_WIN32(e) : E_FAIL;
}

HRESULT camera_start(Camera *c, UINT32 interval)
{
    HRESULT hr = S_OK;
    EnterCriticalSection(&c->cs);
    if (c->shut) hr = MF_E_SHUTDOWN;
    else if (!c->streaming) {
        const NovaCamMode *m = &c->info.modes[c->mode];
        UINT32 cap = (UINT32)sizeof(NovaCamFrame) + m->max_bytes;
        if (cap > c->frame_cap) {
            BYTE *f = c->frame ? HeapReAlloc(GetProcessHeap(), 0, c->frame, cap) : HeapAlloc(GetProcessHeap(), 0, cap);
            if (!f) hr = E_OUTOFMEMORY;
            else c->frame = f, c->frame_cap = cap;
        }
        NovaCamStart s = { c->mode, interval };
        DWORD n = 0;
        if (SUCCEEDED(hr) && !DeviceIoControl(c->dev, IOCTL_NOVACAM_START, &s, sizeof(s), NULL, 0, &n, NULL))
            hr = GetLastError() == ERROR_BUSY ? MF_E_HW_MFT_FAILED_START_STREAMING : last_error();
        if (SUCCEEDED(hr)) c->streaming = TRUE;
    }
    LeaveCriticalSection(&c->cs);
    return hr;
}

void camera_stop(Camera *c)
{
    EnterCriticalSection(&c->cs);
    if (c->streaming && !c->shut) {
        DWORD n = 0;
        DeviceIoControl(c->dev, IOCTL_NOVACAM_STOP, NULL, 0, NULL, 0, &n, NULL);
    }
    c->streaming = FALSE;
    LeaveCriticalSection(&c->cs);
}

/* The next frame (in the camera's own buffer, until the next read) */
HRESULT camera_read(Camera *c, BYTE **frame, UINT32 *bytes, UINT32 *seq)
{
    if (c->shut) return MF_E_SHUTDOWN;
    if (!c->streaming) return MF_E_INVALIDREQUEST;
    DWORD n = 0;
    if (!DeviceIoControl(c->dev, IOCTL_NOVACAM_READ_FRAME, NULL, 0, c->frame, c->frame_cap, &n, NULL)) return last_error();
    if (n < sizeof(NovaCamFrame)) return E_FAIL;
    NovaCamFrame *h = (NovaCamFrame *)c->frame;
    *frame = c->frame + sizeof(NovaCamFrame);
    *bytes = h->bytes;
    if (seq) *seq = h->seq;
    return S_OK;
}

/* =======================================================================
 * The media source
 * ======================================================================= */

static Camera *cam(IMFMediaSource *s) { return (Camera *)s; }

static HRESULT STDMETHODCALLTYPE src_qi(IMFMediaSource *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFMediaEventGenerator) ||
        IsEqualGUID(riid, &IID_IMFMediaSource)) {
        *ppv = This;
        InterlockedIncrement(&cam(This)->ref);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE src_addref(IMFMediaSource *This) { return (ULONG)InterlockedIncrement(&cam(This)->ref); }

static HRESULT STDMETHODCALLTYPE src_Shutdown(IMFMediaSource *This)
{
    Camera *c = cam(This);
    camera_stop(c);
    EnterCriticalSection(&c->cs);
    if (!c->shut) {
        c->shut = TRUE;
        CloseHandle(c->dev);
        c->dev = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&c->cs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE src_release(IMFMediaSource *This)
{
    Camera *c = cam(This);
    LONG n = InterlockedDecrement(&c->ref);
    if (!n) {
        src_Shutdown(This);
        DeleteCriticalSection(&c->cs);
        HeapFree(GetProcessHeap(), 0, c->frame);
        HeapFree(GetProcessHeap(), 0, c);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE src_GetEvent(IMFMediaSource *This, DWORD flags, IMFMediaEvent **e)
{
    (void)This; (void)flags;
    if (e) *e = NULL;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE src_BeginGetEvent(IMFMediaSource *This, IMFAsyncCallback *cb, IUnknown *state)
{
    (void)This; (void)cb; (void)state;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE src_EndGetEvent(IMFMediaSource *This, IMFAsyncResult *r, IMFMediaEvent **e)
{
    (void)This; (void)r;
    if (e) *e = NULL;
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE src_QueueEvent(IMFMediaSource *This, DWORD type, REFGUID ext, HRESULT st, const PROPVARIANT *v)
{
    (void)This; (void)type; (void)ext; (void)st; (void)v;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE src_GetCharacteristics(IMFMediaSource *This, DWORD *ch)
{
    if (!ch) return E_POINTER;
    if (cam(This)->shut) return MF_E_SHUTDOWN;
    *ch = camera_characteristics(cam(This));
    return S_OK;
}

static HRESULT create_pd(Camera *c, IMFPresentationDescriptor **out);

static HRESULT STDMETHODCALLTYPE src_CreatePresentationDescriptor(IMFMediaSource *This, IMFPresentationDescriptor **pd)
{
    if (!pd) return E_POINTER;
    if (cam(This)->shut) return MF_E_SHUTDOWN;
    return create_pd(cam(This), pd);
}

static HRESULT STDMETHODCALLTYPE src_Start(IMFMediaSource *This, IMFPresentationDescriptor *pd, const GUID *fmt, const PROPVARIANT *pos)
{
    (void)pd; (void)fmt; (void)pos;
    return cam(This)->shut ? MF_E_SHUTDOWN : E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE src_Stop(IMFMediaSource *This)  { return cam(This)->shut ? MF_E_SHUTDOWN : E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE src_Pause(IMFMediaSource *This) { return cam(This)->shut ? MF_E_SHUTDOWN : MF_E_INVALIDREQUEST; }

static const IMFMediaSourceVtbl g_src_vtbl = {
    src_qi, src_addref, src_release, src_GetEvent, src_BeginGetEvent, src_EndGetEvent, src_QueueEvent,
    src_GetCharacteristics, src_CreatePresentationDescriptor, src_Start, src_Stop, src_Pause, src_Shutdown,
};

/* Open the camera at @link: its media source */
static HRESULT open_camera(LPCWSTR link, IMFMediaSource **out)
{
    *out = NULL;
    HANDLE h = CreateFileW(link, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return last_error();
    Camera *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c) { CloseHandle(h); return E_OUTOFMEMORY; }
    DWORD n = 0;
    if (!DeviceIoControl(h, IOCTL_NOVACAM_GET_INFO, NULL, 0, &c->info, sizeof(c->info), &n, NULL) || !c->info.nmodes) {
        HRESULT hr = last_error();
        CloseHandle(h);
        HeapFree(GetProcessHeap(), 0, c);
        return hr;
    }
    c->iface.lpVtbl = &g_src_vtbl;
    c->ref = 1;
    c->dev = h;
    InitializeCriticalSection(&c->cs);
    *out = &c->iface;
    return S_OK;
}

/* =======================================================================
 * The stream's media type handler, the stream and presentation descriptors
 * ======================================================================= */

typedef struct {
    IMFMediaTypeHandler iface;
    LONG    ref;
    Camera *c;
} Handler;

static Handler *hdl(IMFMediaTypeHandler *h) { return (Handler *)h; }

static HRESULT STDMETHODCALLTYPE h_qi(IMFMediaTypeHandler *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFMediaTypeHandler)) {
        *ppv = This;
        InterlockedIncrement(&hdl(This)->ref);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE h_addref(IMFMediaTypeHandler *This) { return (ULONG)InterlockedIncrement(&hdl(This)->ref); }
static ULONG STDMETHODCALLTYPE h_release(IMFMediaTypeHandler *This)
{
    Handler *h = hdl(This);
    LONG n = InterlockedDecrement(&h->ref);
    if (!n) {
        h->c->iface.lpVtbl->Release(&h->c->iface);
        HeapFree(GetProcessHeap(), 0, h);
    }
    return (ULONG)n;
}

/* The native mode @t asks for (its subtype, and its frame size if it has
 * one): its index, or -1 */
static int match_mode(Camera *c, IMFMediaType *t)
{
    IMFAttributes *a = (IMFAttributes *)t;
    GUID sub;
    UINT64 size = 0;
    if (FAILED(a->lpVtbl->GetGUID(a, &MF_MT_SUBTYPE, &sub))) return -1;
    BOOL sized = SUCCEEDED(a->lpVtbl->GetUINT64(a, &MF_MT_FRAME_SIZE, &size));
    for (UINT32 i = 0; i < c->info.nmodes; i++) {
        const NovaCamMode *m = &c->info.modes[i];
        GUID g;
        if (!IsEqualGUID(&sub, camera_subtype(m->fourcc, &g))) continue;
        if (sized && size != ((UINT64)m->width << 32 | m->height)) continue;
        return (int)i;
    }
    return -1;
}

static HRESULT STDMETHODCALLTYPE h_IsMediaTypeSupported(IMFMediaTypeHandler *This, IMFMediaType *t, IMFMediaType **closest)
{
    if (closest) *closest = NULL;
    if (!t) return E_POINTER;
    return match_mode(hdl(This)->c, t) >= 0 ? S_OK : MF_E_INVALIDMEDIATYPE;
}

static HRESULT STDMETHODCALLTYPE h_GetMediaTypeCount(IMFMediaTypeHandler *This, DWORD *n)
{
    if (!n) return E_POINTER;
    *n = hdl(This)->c->info.nmodes;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE h_GetMediaTypeByIndex(IMFMediaTypeHandler *This, DWORD i, IMFMediaType **t)
{
    return camera_native_type(hdl(This)->c, i, t);
}

static HRESULT STDMETHODCALLTYPE h_SetCurrentMediaType(IMFMediaTypeHandler *This, IMFMediaType *t)
{
    if (!t) return E_POINTER;
    int i = match_mode(hdl(This)->c, t);
    return i < 0 ? MF_E_INVALIDMEDIATYPE : camera_set_mode(hdl(This)->c, (UINT32)i);
}

static HRESULT STDMETHODCALLTYPE h_GetCurrentMediaType(IMFMediaTypeHandler *This, IMFMediaType **t)
{
    return camera_native_type(hdl(This)->c, hdl(This)->c->mode, t);
}

static HRESULT STDMETHODCALLTYPE h_GetMajorType(IMFMediaTypeHandler *This, GUID *major)
{
    (void)This;
    if (!major) return E_POINTER;
    *major = MFMediaType_Video;
    return S_OK;
}

static const IMFMediaTypeHandlerVtbl g_h_vtbl = {
    h_qi, h_addref, h_release, h_IsMediaTypeSupported, h_GetMediaTypeCount, h_GetMediaTypeByIndex,
    h_SetCurrentMediaType, h_GetCurrentMediaType, h_GetMajorType,
};

/* The stream descriptor: attributes, its identifier, the handler */
typedef struct {
    AttrObj              a;
    IMFMediaTypeHandler *h;
} StreamDesc;

static HRESULT STDMETHODCALLTYPE sd_qi(IMFStreamDescriptor *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFAttributes) || IsEqualGUID(riid, &IID_IMFStreamDescriptor)) {
        *ppv = This;
        attr_AddRef((IMFAttributes *)This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sd_release(IMFStreamDescriptor *This)
{
    StreamDesc *s = (StreamDesc *)This;
    LONG n = InterlockedDecrement(&s->a.ref);
    if (!n) {
        s->h->lpVtbl->Release(s->h);
        attr_free(&s->a);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE sd_GetStreamIdentifier(IMFStreamDescriptor *This, DWORD *id)
{
    (void)This;
    if (!id) return E_POINTER;
    *id = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sd_GetMediaTypeHandler(IMFStreamDescriptor *This, IMFMediaTypeHandler **h)
{
    if (!h) return E_POINTER;
    *h = ((StreamDesc *)This)->h;
    (*h)->lpVtbl->AddRef(*h);
    return S_OK;
}

static const IMFStreamDescriptorVtbl g_sd_vtbl = {
    ATTR_VTBL(sd_qi, sd_release),
    .GetStreamIdentifier = sd_GetStreamIdentifier, .GetMediaTypeHandler = sd_GetMediaTypeHandler,
};

/* The presentation descriptor: attributes, the one stream and whether it is selected */
typedef struct {
    AttrObj              a;
    IMFStreamDescriptor *sd;
    BOOL                 selected;
} PresDesc;

static HRESULT STDMETHODCALLTYPE pd_qi(IMFPresentationDescriptor *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFAttributes) ||
        IsEqualGUID(riid, &IID_IMFPresentationDescriptor)) {
        *ppv = This;
        attr_AddRef((IMFAttributes *)This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pd_release(IMFPresentationDescriptor *This)
{
    PresDesc *p = (PresDesc *)This;
    LONG n = InterlockedDecrement(&p->a.ref);
    if (!n) {
        p->sd->lpVtbl->Release(p->sd);
        attr_free(&p->a);
        HeapFree(GetProcessHeap(), 0, p);
    }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE pd_GetStreamDescriptorCount(IMFPresentationDescriptor *This, DWORD *n)
{
    (void)This;
    if (!n) return E_POINTER;
    *n = 1;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pd_GetStreamDescriptorByIndex(IMFPresentationDescriptor *This, DWORD i, BOOL *sel,
                                                               IMFStreamDescriptor **sd)
{
    PresDesc *p = (PresDesc *)This;
    if (!sel || !sd) return E_POINTER;
    if (i) return E_INVALIDARG;
    *sel = p->selected;
    *sd = p->sd;
    (*sd)->lpVtbl->AddRef(*sd);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pd_SelectStream(IMFPresentationDescriptor *This, DWORD i)
{
    if (i) return E_INVALIDARG;
    ((PresDesc *)This)->selected = TRUE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pd_DeselectStream(IMFPresentationDescriptor *This, DWORD i)
{
    if (i) return E_INVALIDARG;
    ((PresDesc *)This)->selected = FALSE;
    return S_OK;
}

static HRESULT new_pd(IMFStreamDescriptor *sd, BOOL selected, IMFPresentationDescriptor **out);

static HRESULT STDMETHODCALLTYPE pd_Clone(IMFPresentationDescriptor *This, IMFPresentationDescriptor **out)
{
    PresDesc *p = (PresDesc *)This;
    if (!out) return E_POINTER;
    HRESULT hr = new_pd(p->sd, p->selected, out);
    if (SUCCEEDED(hr)) attr_copy_into(&p->a, (IMFAttributes *)*out);
    return hr;
}

static const IMFPresentationDescriptorVtbl g_pd_vtbl = {
    ATTR_VTBL(pd_qi, pd_release),
    .GetStreamDescriptorCount = pd_GetStreamDescriptorCount, .GetStreamDescriptorByIndex = pd_GetStreamDescriptorByIndex,
    .SelectStream = pd_SelectStream, .DeselectStream = pd_DeselectStream, .Clone = pd_Clone,
};

static HRESULT new_pd(IMFStreamDescriptor *sd, BOOL selected, IMFPresentationDescriptor **out)
{
    PresDesc *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p) return E_OUTOFMEMORY;
    attr_init(&p->a, &g_pd_vtbl);
    p->sd = sd;
    sd->lpVtbl->AddRef(sd);
    p->selected = selected;
    *out = (IMFPresentationDescriptor *)p;
    return S_OK;
}

static HRESULT create_pd(Camera *c, IMFPresentationDescriptor **out)
{
    Handler *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
    StreamDesc *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!h || !s) {
        HeapFree(GetProcessHeap(), 0, h);
        HeapFree(GetProcessHeap(), 0, s);
        return E_OUTOFMEMORY;
    }
    h->iface.lpVtbl = &g_h_vtbl;
    h->ref = 1;
    h->c = c;
    c->iface.lpVtbl->AddRef(&c->iface);
    attr_init(&s->a, &g_sd_vtbl);
    s->h = &h->iface;
    HRESULT hr = new_pd((IMFStreamDescriptor *)s, TRUE, out);
    sd_release((IMFStreamDescriptor *)s);
    return hr;
}

/* =======================================================================
 * Enumeration and activation
 * ======================================================================= */

typedef struct {
    AttrObj         a;
    IMFMediaSource *source;            /* (once activated) */
} Activate;

static HRESULT STDMETHODCALLTYPE act_qi(IMFActivate *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFAttributes) || IsEqualGUID(riid, &IID_IMFActivate)) {
        *ppv = This;
        attr_AddRef((IMFAttributes *)This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE act_release(IMFActivate *This)
{
    Activate *x = (Activate *)This;
    LONG n = InterlockedDecrement(&x->a.ref);
    if (!n) {
        if (x->source) x->source->lpVtbl->Release(x->source);
        attr_free(&x->a);
        HeapFree(GetProcessHeap(), 0, x);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE act_ActivateObject(IMFActivate *This, REFIID riid, void **ppv)
{
    Activate *x = (Activate *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!x->source) {
        HRESULT hr = nova_create_device_source((IMFAttributes *)This, &x->source);
        if (FAILED(hr)) return hr;
    }
    return x->source->lpVtbl->QueryInterface(x->source, riid, ppv);
}

static HRESULT STDMETHODCALLTYPE act_ShutdownObject(IMFActivate *This)
{
    Activate *x = (Activate *)This;
    if (x->source) {
        x->source->lpVtbl->Shutdown(x->source);
        x->source->lpVtbl->Release(x->source);
        x->source = NULL;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE act_DetachObject(IMFActivate *This)
{
    Activate *x = (Activate *)This;
    if (x->source) x->source->lpVtbl->Release(x->source);
    x->source = NULL;
    return S_OK;
}

static const IMFActivateVtbl g_act_vtbl = {
    ATTR_VTBL(act_qi, act_release),
    .ActivateObject = act_ActivateObject, .ShutdownObject = act_ShutdownObject, .DetachObject = act_DetachObject,
};

/* The camera interfaces' paths, as one multi-string (HeapFree it) */
static WCHAR *camera_links(void)
{
    GUID cat = KSCATEGORY_VIDEO_CAMERA;
    ULONG len = 0;
    if (CM_Get_Device_Interface_List_SizeW(&len, &cat, NULL, 0) || !len) return NULL;
    WCHAR *list = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (list && CM_Get_Device_Interface_ListW(&cat, NULL, list, len, 0)) {
        HeapFree(GetProcessHeap(), 0, list);
        list = NULL;
    }
    return list;
}

HRESULT nova_enum_device_sources(IMFAttributes *attrs, IMFActivate ***sources, UINT32 *count)
{
    if (!attrs || !sources || !count) return E_POINTER;
    *sources = NULL;
    *count = 0;
    GUID type, cat;
    if (FAILED(attrs->lpVtbl->GetGUID(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &type))) return E_INVALIDARG;
    if (!IsEqualGUID(&type, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) return S_OK;   /* (no audio capture sources yet) */
    if (SUCCEEDED(attrs->lpVtbl->GetGUID(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, &cat)) &&
        !IsEqualGUID(&cat, &KSCATEGORY_VIDEO_CAMERA))
        return S_OK;
    WCHAR *links = camera_links();
    if (!links) return S_OK;
    UINT32 n = 0;
    for (WCHAR *p = links; *p; p += lstrlenW(p) + 1) n++;
    IMFActivate **arr = n ? CoTaskMemAlloc(n * sizeof(*arr)) : NULL;
    if (n && !arr) { HeapFree(GetProcessHeap(), 0, links); return E_OUTOFMEMORY; }
    for (WCHAR *p = links; *p; p += lstrlenW(p) + 1) {
        HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        NovaCamInfo *info = HeapAlloc(GetProcessHeap(), 0, sizeof(*info));
        DWORD got = 0;
        BOOL ok = info && DeviceIoControl(h, IOCTL_NOVACAM_GET_INFO, NULL, 0, info, sizeof(*info), &got, NULL);
        CloseHandle(h);
        Activate *x = ok ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*x)) : NULL;
        if (x) {
            attr_init(&x->a, &g_act_vtbl);
            IMFAttributes *a = (IMFAttributes *)x;
            info->name[63] = 0;
            attr_SetGUID(a, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            attr_SetString(a, &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, (LPCWSTR)info->name);
            attr_SetString(a, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, p);
            attr_SetGUID(a, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, &KSCATEGORY_VIDEO_CAMERA);
            arr[(*count)++] = (IMFActivate *)x;
        }
        HeapFree(GetProcessHeap(), 0, info);
    }
    HeapFree(GetProcessHeap(), 0, links);
    if (!*count) { CoTaskMemFree(arr); arr = NULL; }
    *sources = arr;
    return S_OK;
}

HRESULT nova_create_device_source(IMFAttributes *attrs, IMFMediaSource **source)
{
    if (!attrs || !source) return E_POINTER;
    *source = NULL;
    GUID type;
    if (FAILED(attrs->lpVtbl->GetGUID(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &type))) return E_INVALIDARG;
    if (!IsEqualGUID(&type, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) return E_INVALIDARG;
    WCHAR *link = NULL;
    UINT32 n = 0;
    HRESULT hr = attrs->lpVtbl->GetAllocatedString(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &n);
    if (FAILED(hr)) {
        /* (no link: the first camera, as Windows does with only a type) */
        WCHAR *links = camera_links();
        if (!links || !*links) { HeapFree(GetProcessHeap(), 0, links); return MF_E_ATTRIBUTENOTFOUND; }
        hr = open_camera(links, source);
        HeapFree(GetProcessHeap(), 0, links);
        return hr;
    }
    hr = open_camera(link, source);
    CoTaskMemFree(link);
    return hr;
}
