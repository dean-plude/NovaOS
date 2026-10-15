/*
 * windowscodecs.dll — the Windows Imaging Component on NovaOS: the imaging
 * factory (CoCreateInstance(CLSID_WICImagingFactory) or
 * WICCreateImagingFactory_Proxy, which WPF calls), its class objects,
 * the component information of the decoders, the pixel formats and the
 * format converter, and the metadata name helpers.
 *
 * The decoders, bitmaps and transforms are in decode.c, bitmap.c and
 * convert.c; the IWIC*_Proxy exports WPF calls through are in proxy.c.
 * There are no encoders and no metadata readers yet: those calls return
 * WINCODEC_ERR_COMPONENTNOTFOUND or WINCODEC_ERR_UNSUPPORTEDOPERATION,
 * which programs are written to handle.
 */
#include "wic.h"

int _fltused = 0x9875;      /* floating point in use (the compiler references it) */

static LONG g_objects;
void dll_addref(void) { InterlockedIncrement(&g_objects); }
void dll_release(void) { InterlockedDecrement(&g_objects); }
void *w_alloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1); }
void w_free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

HRESULT string_out(const WCHAR *s, UINT cch, WCHAR *buf, UINT *actual)
{
    UINT len = (UINT)lstrlenW(s) + 1;
    if (!buf && !actual) return E_INVALIDARG;
    if (actual) *actual = len;
    if (!buf) return cch ? E_INVALIDARG : S_OK;
    if (cch < len) return WINCODEC_ERR_INSUFFICIENTBUFFER;
    memcpy(buf, s, len * sizeof(WCHAR));
    return S_OK;
}

/* ================= pixel format and converter information ================= */
typedef struct {
    const void *const *vtbl;
    LONG ref;
    const PixelFormat *f;              /* NULL: the format converter */
} Info;

static HRESULT STDMETHODCALLTYPE in_qi(Info *i, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    BOOL ok = IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICComponentInfo);
    if (i->f) ok = ok || IsEqualIID(riid, &IID_IWICPixelFormatInfo) || IsEqualIID(riid, &IID_IWICPixelFormatInfo2);
    else ok = ok || IsEqualIID(riid, &IID_IWICFormatConverterInfo);
    if (!ok) { *out = 0; return E_NOINTERFACE; }
    *out = i;
    InterlockedIncrement(&i->ref);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE in_addref(Info *i) { return (ULONG)InterlockedIncrement(&i->ref); }
static ULONG STDMETHODCALLTYPE in_release(Info *i)
{
    LONG r = InterlockedDecrement(&i->ref);
    if (!r) { w_free(i); dll_release(); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE in_type(Info *i, WICComponentType *t)
{
    if (!t) return E_INVALIDARG;
    *t = i->f ? WICPixelFormat : WICPixelFormatConverter;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE in_clsid(Info *i, CLSID *c)
{
    if (!c) return E_INVALIDARG;
    *c = i->f ? *i->f->guid : CLSID_WICDefaultFormatConverter;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE in_signing(Info *i, DWORD *s) { (void)i; if (!s) return E_INVALIDARG; *s = WICComponentSigned; return S_OK; }
static HRESULT STDMETHODCALLTYPE in_author(Info *i, UINT n, WCHAR *b, UINT *a) { (void)i; return string_out(L"NovaOS", n, b, a); }
static HRESULT STDMETHODCALLTYPE in_vendor(Info *i, GUID *g) { (void)i; if (!g) return E_INVALIDARG; *g = GUID_VendorMicrosoft; return S_OK; }
static HRESULT STDMETHODCALLTYPE in_version(Info *i, UINT n, WCHAR *b, UINT *a) { (void)i; return string_out(L"1.0.0.0", n, b, a); }
static HRESULT STDMETHODCALLTYPE in_name(Info *i, UINT n, WCHAR *b, UINT *a)
{
    WCHAR name[64];
    const char *s = i->f ? i->f->name : "Default Format Converter";
    UINT k = 0;
    for (; s[k] && k < 63; k++) name[k] = (WCHAR)s[k];
    name[k] = 0;
    return string_out(name, n, b, a);
}
static HRESULT STDMETHODCALLTYPE pi_format(Info *i, GUID *g) { if (!g) return E_INVALIDARG; *g = *i->f->guid; return S_OK; }
static HRESULT STDMETHODCALLTYPE pi_context(Info *i, IWICColorContext **out)
{
    (void)i;
    if (!out) return E_INVALIDARG;
    HRESULT hr = color_context_create(out);
    if (SUCCEEDED(hr)) (*out)->lpVtbl->InitializeFromExifColorSpace(*out, 1);     /* sRGB */
    return hr;
}
static HRESULT STDMETHODCALLTYPE pi_bpp(Info *i, UINT *b) { if (!b) return E_INVALIDARG; *b = i->f->bpp; return S_OK; }
static HRESULT STDMETHODCALLTYPE pi_channels(Info *i, UINT *c) { if (!c) return E_INVALIDARG; *c = i->f->channels; return S_OK; }
static HRESULT STDMETHODCALLTYPE pi_mask(Info *i, UINT ch, UINT size, BYTE *m, UINT *a) { return pf_channel_mask(i->f, ch, size, m, a); }
static HRESULT STDMETHODCALLTYPE pi_transparency(Info *i, BOOL *r) { if (!r) return E_INVALIDARG; *r = i->f->alpha; return S_OK; }
static HRESULT STDMETHODCALLTYPE pi_numeric(Info *i, WICPixelFormatNumericRepresentation *r)
{
    if (!r) return E_INVALIDARG;
    *r = (WICPixelFormatNumericRepresentation)i->f->rep;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ci_formats(Info *i, UINT n, GUID *g, UINT *a)
{
    (void)i;
    if (!a) return E_INVALIDARG;
    UINT k = 0;
    for (const PixelFormat *f; (f = pf_at(k)); k++) ;
    *a = k - 1;                                  /* every format but DontCare */
    if (!g) return S_OK;
    if (n < k - 1) return WINCODEC_ERR_INSUFFICIENTBUFFER;
    for (UINT j = 1; j < k; j++) g[j - 1] = *pf_at(j)->guid;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ci_create(Info *i, IWICFormatConverter **out) { (void)i; return converter_create(out); }

static const void *const pi_vtbl[] = {
    in_qi, in_addref, in_release, in_type, in_clsid, in_signing, in_author, in_vendor, in_version, in_version, in_name,
    pi_format, pi_context, pi_bpp, pi_channels, pi_mask, pi_transparency, pi_numeric,
};
static const void *const ci_vtbl[] = {
    in_qi, in_addref, in_release, in_type, in_clsid, in_signing, in_author, in_vendor, in_version, in_version, in_name,
    ci_formats, ci_create,
};

static HRESULT info_new(const PixelFormat *f, IWICComponentInfo **out)
{
    Info *i = w_alloc(sizeof *i);
    if (!i) { *out = 0; return E_OUTOFMEMORY; }
    i->vtbl = f ? pi_vtbl : ci_vtbl;
    i->ref = 1;
    i->f = f;
    dll_addref();
    *out = (IWICComponentInfo *)i;
    return S_OK;
}

HRESULT component_info_create(REFCLSID clsid, IWICComponentInfo **out)
{
    if (!clsid || !out) return E_INVALIDARG;
    *out = 0;
    const CodecDesc *c = codec_by_clsid(clsid);
    if (c) return decoder_info_create(c->container, (IWICBitmapDecoderInfo **)out);
    if (IsEqualGUID(clsid, &CLSID_WICDefaultFormatConverter)) return info_new(0, out);
    const PixelFormat *f = pf_find(clsid);
    if (f) return info_new(f, out);
    return WINCODEC_ERR_COMPONENTNOTFOUND;
}

/* ================= component enumerator ================= */
typedef struct {
    const void *const *vtbl;
    LONG ref;
    IUnknown **items;
    ULONG n, at;
} Enum;

static HRESULT STDMETHODCALLTYPE en_qi(Enum *e, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumUnknown)) {
        *out = e;
        InterlockedIncrement(&e->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE en_addref(Enum *e) { return (ULONG)InterlockedIncrement(&e->ref); }
static ULONG STDMETHODCALLTYPE en_release(Enum *e)
{
    LONG r = InterlockedDecrement(&e->ref);
    if (!r) {
        for (ULONG i = 0; i < e->n; i++) COM_RELEASE(e->items[i]);
        w_free(e->items);
        w_free(e);
        dll_release();
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE en_next(Enum *e, ULONG count, IUnknown **items, ULONG *fetched)
{
    if (!items) return E_INVALIDARG;
    ULONG k = 0;
    while (k < count && e->at < e->n) {
        items[k] = e->items[e->at++];
        COM_ADDREF(items[k]);
        k++;
    }
    if (fetched) *fetched = k;
    return k == count ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE en_skip(Enum *e, ULONG count)
{
    ULONG left = e->n - e->at;
    e->at += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE en_reset(Enum *e) { e->at = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE en_clone(Enum *e, IEnumUnknown **out);
static const void *const en_vtbl[] = { en_qi, en_addref, en_release, en_next, en_skip, en_reset, en_clone };

static Enum *enum_new(ULONG cap)
{
    Enum *e = w_alloc(sizeof *e);
    if (!e) return 0;
    if (!(e->items = w_alloc((cap ? cap : 1) * sizeof *e->items))) { w_free(e); return 0; }
    e->vtbl = en_vtbl;
    e->ref = 1;
    dll_addref();
    return e;
}
static HRESULT STDMETHODCALLTYPE en_clone(Enum *e, IEnumUnknown **out)
{
    if (!out) return E_INVALIDARG;
    Enum *c = enum_new(e->n);
    if (!c) { *out = 0; return E_OUTOFMEMORY; }
    for (ULONG i = 0; i < e->n; i++) { c->items[i] = e->items[i]; COM_ADDREF(c->items[i]); }
    c->n = e->n; c->at = e->at;
    *out = (IEnumUnknown *)c;
    return S_OK;
}

HRESULT component_enum_create(DWORD types, IEnumUnknown **out)
{
    if (!out) return E_INVALIDARG;
    Enum *e = enum_new(128);
    if (!e) { *out = 0; return E_OUTOFMEMORY; }
    IWICComponentInfo *ci;
    if (types & WICDecoder)
        for (UINT i = 0; codec_at(i); i++)
            if (SUCCEEDED(decoder_info_create(codec_at(i)->container, (IWICBitmapDecoderInfo **)&ci))) e->items[e->n++] = (IUnknown *)ci;
    if ((types & WICPixelFormatConverter) && SUCCEEDED(info_new(0, &ci))) e->items[e->n++] = (IUnknown *)ci;
    if (types & WICPixelFormat)
        for (UINT i = 1; pf_at(i) && e->n < 128; i++)
            if (SUCCEEDED(info_new(pf_at(i), &ci))) e->items[e->n++] = (IUnknown *)ci;
    *out = (IEnumUnknown *)e;
    return S_OK;
}

/* ================= the imaging factory ================= */
typedef struct { const void *const *vtbl; } Fact;
static Fact g_fact, g_fact2;

static HRESULT STDMETHODCALLTYPE f_qi(Fact *f, REFIID riid, void **out)
{
    (void)f;
    return factory_get(riid, out);
}
static ULONG STDMETHODCALLTYPE f_addref(Fact *f) { (void)f; dll_addref(); return 2; }
static ULONG STDMETHODCALLTYPE f_release(Fact *f) { (void)f; dll_release(); return 1; }

static HRESULT STDMETHODCALLTYPE f_from_stream(Fact *f, IStream *s, const GUID *vendor, WICDecodeOptions opt, IWICBitmapDecoder **out)
{
    (void)f; (void)opt;
    return decoder_from_stream(s, vendor, out);
}
static HRESULT STDMETHODCALLTYPE f_from_name(Fact *f, LPCWSTR name, const GUID *vendor, DWORD access, WICDecodeOptions opt,
                                             IWICBitmapDecoder **out)
{
    (void)access;
    if (!out) return E_INVALIDARG;
    *out = 0;
    IWICStream *s;
    HRESULT hr = stream_create(&s);
    if (FAILED(hr)) return hr;
    hr = s->lpVtbl->InitializeFromFilename(s, name, GENERIC_READ);
    if (SUCCEEDED(hr)) hr = f_from_stream(f, (IStream *)s, vendor, opt, out);
    s->lpVtbl->Release(s);
    return hr;
}
static HRESULT STDMETHODCALLTYPE f_from_handle(Fact *f, ULONG_PTR file, const GUID *vendor, WICDecodeOptions opt,
                                               IWICBitmapDecoder **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    HANDLE dup;
    if (!DuplicateHandle(GetCurrentProcess(), (HANDLE)file, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
    /* the stream owns the duplicate; the file position is shared, so it is put back */
    LARGE_INTEGER zero = { 0 }, at;
    SetFilePointerEx(dup, zero, &at, FILE_CURRENT);
    IWICStream *s;
    HRESULT hr = stream_on_handle(dup, &s);
    if (FAILED(hr)) { CloseHandle(dup); return hr; }
    hr = f_from_stream(f, (IStream *)s, vendor, opt, out);
    SetFilePointerEx(dup, at, 0, FILE_BEGIN);
    s->lpVtbl->Release(s);
    return hr;
}
static HRESULT STDMETHODCALLTYPE f_component_info(Fact *f, REFCLSID clsid, IWICComponentInfo **out) { (void)f; return component_info_create(clsid, out); }
static HRESULT STDMETHODCALLTYPE f_decoder(Fact *f, REFGUID fmt, const GUID *vendor, IWICBitmapDecoder **out)
{
    (void)f; (void)vendor;
    return decoder_create(fmt, out);
}
static HRESULT STDMETHODCALLTYPE f_encoder(Fact *f, REFGUID fmt, const GUID *vendor, IWICBitmapEncoder **out)
{
    (void)f; (void)fmt; (void)vendor;
    if (!out) return E_INVALIDARG;
    *out = 0;
    return WINCODEC_ERR_COMPONENTNOTFOUND;
}
static HRESULT STDMETHODCALLTYPE f_palette(Fact *f, IWICPalette **out) { (void)f; return palette_create(out); }
static HRESULT STDMETHODCALLTYPE f_converter(Fact *f, IWICFormatConverter **out) { (void)f; return converter_create(out); }
static HRESULT STDMETHODCALLTYPE f_scaler(Fact *f, IWICBitmapScaler **out) { (void)f; return scaler_create(out); }
static HRESULT STDMETHODCALLTYPE f_clipper(Fact *f, IWICBitmapClipper **out) { (void)f; return clipper_create(out); }
static HRESULT STDMETHODCALLTYPE f_fliprotator(Fact *f, IWICBitmapFlipRotator **out) { (void)f; return fliprotator_create(out); }
static HRESULT STDMETHODCALLTYPE f_stream(Fact *f, IWICStream **out) { (void)f; return stream_create(out); }
static HRESULT STDMETHODCALLTYPE f_color_context(Fact *f, IWICColorContext **out) { (void)f; return color_context_create(out); }
static HRESULT STDMETHODCALLTYPE f_color_transform(Fact *f, IWICColorTransform **out) { (void)f; return color_transform_create(out); }
static HRESULT STDMETHODCALLTYPE f_bitmap(Fact *f, UINT w, UINT h, REFGUID fmt, WICBitmapCreateCacheOption opt, IWICBitmap **out)
{
    (void)f; (void)opt;
    return bitmap_create(w, h, fmt, 0, 0, 0, 0, out);
}
static HRESULT STDMETHODCALLTYPE f_bitmap_from_source(Fact *f, IWICBitmapSource *s, WICBitmapCreateCacheOption opt, IWICBitmap **out)
{
    (void)f; (void)opt;
    return bitmap_from_source(s, 0, out);
}
static HRESULT STDMETHODCALLTYPE f_bitmap_from_rect(Fact *f, IWICBitmapSource *s, UINT x, UINT y, UINT w, UINT h, IWICBitmap **out)
{
    (void)f;
    WICRect r = { (INT)x, (INT)y, (INT)w, (INT)h };
    return bitmap_from_source(s, &r, out);
}
static HRESULT STDMETHODCALLTYPE f_bitmap_from_memory(Fact *f, UINT w, UINT h, REFGUID fmt, UINT stride, UINT size, BYTE *buf,
                                                      IWICBitmap **out)
{
    (void)f;
    if (!out) return E_INVALIDARG;
    *out = 0;
    const PixelFormat *pf = pf_find(fmt);
    if (!pf || !pf->bpp) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (!buf || !w || !h) return E_INVALIDARG;
    UINT row = pf_stride(pf, w);
    if (stride < row || (UINT64)stride * (h - 1) + row > size) return E_INVALIDARG;
    return bitmap_create(w, h, fmt, stride, buf, 0, 0, out);
}
static HRESULT STDMETHODCALLTYPE f_bitmap_from_hbitmap(Fact *f, HBITMAP hbm, HPALETTE pal, WICBitmapAlphaChannelOption opt, IWICBitmap **out)
{
    (void)f;
    return bitmap_from_hbitmap(hbm, pal, opt, out);
}
static HRESULT STDMETHODCALLTYPE f_bitmap_from_hicon(Fact *f, HICON icon, IWICBitmap **out) { (void)f; return bitmap_from_hicon(icon, out); }
static HRESULT STDMETHODCALLTYPE f_enum(Fact *f, DWORD types, DWORD opt, IEnumUnknown **out) { (void)f; (void)opt; return component_enum_create(types, out); }
static HRESULT STDMETHODCALLTYPE f_unsupported1(Fact *f, void *a, void **out)
{
    (void)f; (void)a;
    if (out) *out = 0;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}
static HRESULT STDMETHODCALLTYPE f_unsupported2(Fact *f, void *a, void *b, void **out)
{
    (void)f; (void)a; (void)b;
    if (out) *out = 0;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}
static HRESULT STDMETHODCALLTYPE f_metadata_reader(Fact *f, REFGUID fmt, const GUID *vendor, DWORD opt, IStream *s, void **out)
{
    (void)f; (void)fmt; (void)vendor; (void)opt; (void)s;
    if (out) *out = 0;
    return WINCODEC_ERR_COMPONENTNOTFOUND;
}
static HRESULT STDMETHODCALLTYPE f_metadata_writer(Fact *f, REFGUID fmt, const GUID *vendor, DWORD opt, void **out)
{
    (void)f; (void)fmt; (void)vendor; (void)opt;
    if (out) *out = 0;
    return WINCODEC_ERR_COMPONENTNOTFOUND;
}
static HRESULT STDMETHODCALLTYPE f_property_bag(Fact *f, PROPBAG2 *opts, UINT n, IPropertyBag2 **out)
{
    (void)f; (void)opts; (void)n;
    if (out) *out = 0;
    return E_NOTIMPL;
}

#define FACTORY_METHODS \
    f_qi, f_addref, f_release, f_from_name, f_from_stream, f_from_handle, f_component_info, f_decoder, f_encoder, \
    f_palette, f_converter, f_scaler, f_clipper, f_fliprotator, f_stream, f_color_context, f_color_transform, \
    f_bitmap, f_bitmap_from_source, f_bitmap_from_rect, f_bitmap_from_memory, f_bitmap_from_hbitmap, \
    f_bitmap_from_hicon, f_enum, f_unsupported1, f_unsupported1, f_unsupported2, f_unsupported2
/* IWICComponentFactory: IWICImagingFactory and the metadata methods */
static const void *const f_vtbl[] = {
    FACTORY_METHODS,
    f_metadata_reader, f_metadata_reader, f_metadata_writer, f_unsupported2, f_unsupported1, f_unsupported1, f_property_bag,
};
/* IWICImagingFactory2: IWICImagingFactory and CreateImageEncoder */
static const void *const f2_vtbl[] = { FACTORY_METHODS, f_unsupported1 };
static Fact g_fact = { f_vtbl }, g_fact2 = { f2_vtbl };

HRESULT factory_get(REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICImagingFactory) || IsEqualIID(riid, &IID_IWICComponentFactory))
        *out = &g_fact;
    else if (IsEqualIID(riid, &IID_IWICImagingFactory2)) *out = &g_fact2;
    else { *out = 0; return E_NOINTERFACE; }
    dll_addref();
    return S_OK;
}

__declspec(dllexport) HRESULT WINAPI WICCreateImagingFactory_Proxy(UINT version, IWICImagingFactory **out)
{
    (void)version;
    return factory_get(&IID_IWICImagingFactory, (void **)out);
}

/* ================= class objects ================= */
typedef struct { const IClassFactoryVtbl *vtbl; const GUID *clsid; } ClassObj;

static HRESULT STDMETHODCALLTYPE cf_qi(IClassFactory *This, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) { *out = This; dll_addref(); return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cf_addref(IClassFactory *This) { (void)This; dll_addref(); return 2; }
static ULONG STDMETHODCALLTYPE cf_release(IClassFactory *This) { (void)This; dll_release(); return 1; }
static HRESULT STDMETHODCALLTYPE cf_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (outer) return CLASS_E_NOAGGREGATION;
    const GUID *clsid = ((ClassObj *)This)->clsid;
    if (IsEqualCLSID(clsid, &CLSID_WICImagingFactory) || IsEqualCLSID(clsid, &CLSID_WICImagingFactory2))
        return factory_get(riid, out);
    IUnknown *obj = 0;
    HRESULT hr;
    const CodecDesc *c = codec_by_clsid(clsid);
    if (c) hr = decoder_create(c->container, (IWICBitmapDecoder **)&obj);
    else hr = converter_create((IWICFormatConverter **)&obj);
    if (FAILED(hr)) return hr;
    hr = obj->lpVtbl->QueryInterface(obj, riid, out);
    obj->lpVtbl->Release(obj);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cf_lock(IClassFactory *This, BOOL lock)
{
    (void)This;
    if (lock) dll_addref(); else dll_release();
    return S_OK;
}
static const IClassFactoryVtbl cf_vtbl = { cf_qi, cf_addref, cf_release, cf_create, cf_lock };
static const ClassObj classes[] = {
    { &cf_vtbl, &CLSID_WICImagingFactory }, { &cf_vtbl, &CLSID_WICImagingFactory2 },
    { &cf_vtbl, &CLSID_WICPngDecoder }, { &cf_vtbl, &CLSID_WICPngDecoder2 }, { &cf_vtbl, &CLSID_WICJpegDecoder },
    { &cf_vtbl, &CLSID_WICBmpDecoder }, { &cf_vtbl, &CLSID_WICGifDecoder }, { &cf_vtbl, &CLSID_WICIcoDecoder },
    { &cf_vtbl, &CLSID_WICDefaultFormatConverter },
};

__declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    for (UINT i = 0; i < sizeof classes / sizeof classes[0]; i++)
        if (IsEqualCLSID(clsid, classes[i].clsid)) return cf_qi((IClassFactory *)&classes[i], riid, out);
    return CLASS_E_CLASSNOTAVAILABLE;
}
__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void) { return g_objects ? S_FALSE : S_OK; }
/* the classes are in the registry from the start (the kernel's defaults) */
__declspec(dllexport) HRESULT WINAPI DllRegisterServer(void) { return S_OK; }
__declspec(dllexport) HRESULT WINAPI DllUnregisterServer(void) { return S_OK; }

/* ================= metadata names ================= */
static const struct { const GUID *g; const WCHAR *name; } short_names[] = {
    { &GUID_MetadataFormatUnknown, L"unknown" }, { &GUID_MetadataFormatIfd, L"ifd" }, { &GUID_MetadataFormatSubIfd, L"sub" },
    { &GUID_MetadataFormatExif, L"exif" }, { &GUID_MetadataFormatGps, L"gps" }, { &GUID_MetadataFormatInterop, L"interop" },
    { &GUID_MetadataFormatApp0, L"app0" }, { &GUID_MetadataFormatApp1, L"app1" }, { &GUID_MetadataFormatApp13, L"app13" },
    { &GUID_MetadataFormatIPTC, L"iptc" }, { &GUID_MetadataFormatIRB, L"irb" }, { &GUID_MetadataFormat8BIMIPTC, L"8bimiptc" },
    { &GUID_MetadataFormat8BIMResolutionInfo, L"8bimResInfo" }, { &GUID_MetadataFormat8BIMIPTCDigest, L"8bimiptcdigest" },
    { &GUID_MetadataFormatXMP, L"xmp" }, { &GUID_MetadataFormatThumbnail, L"thumb" }, { &GUID_MetadataFormatChunktEXt, L"tEXt" },
    { &GUID_MetadataFormatXMPStruct, L"xmpstruct" }, { &GUID_MetadataFormatXMPBag, L"xmpbag" },
    { &GUID_MetadataFormatXMPSeq, L"xmpseq" }, { &GUID_MetadataFormatXMPAlt, L"xmpalt" },
    { &GUID_MetadataFormatLSD, L"logscrdesc" }, { &GUID_MetadataFormatIMD, L"imgdesc" }, { &GUID_MetadataFormatGCE, L"grctlext" },
    { &GUID_MetadataFormatAPE, L"appext" }, { &GUID_MetadataFormatJpegChrominance, L"chrominance" },
    { &GUID_MetadataFormatJpegLuminance, L"luminance" }, { &GUID_MetadataFormatJpegComment, L"com" },
    { &GUID_MetadataFormatGifComment, L"commentext" }, { &GUID_MetadataFormatChunkgAMA, L"gAMA" },
    { &GUID_MetadataFormatChunkbKGD, L"bKGD" }, { &GUID_MetadataFormatChunkiTXt, L"iTXt" },
    { &GUID_MetadataFormatChunkcHRM, L"cHRM" }, { &GUID_MetadataFormatChunkhIST, L"hIST" },
    { &GUID_MetadataFormatChunkiCCP, L"iCCP" }, { &GUID_MetadataFormatChunksRGB, L"sRGB" },
    { &GUID_MetadataFormatChunktIME, L"tIME" },
};

__declspec(dllexport) HRESULT WINAPI WICMapGuidToShortName(REFGUID g, UINT cch, WCHAR *name, UINT *actual)
{
    if (!g || (!name && !actual) || (name && !cch)) return E_INVALIDARG;
    for (UINT i = 0; i < sizeof short_names / sizeof short_names[0]; i++)
        if (IsEqualGUID(short_names[i].g, g)) {
            UINT len = (UINT)lstrlenW(short_names[i].name) + 1;
            if (actual) *actual = len;
            if (!name) return S_OK;
            if (cch < len) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            memcpy(name, short_names[i].name, len * sizeof(WCHAR));
            return S_OK;
        }
    return WINCODEC_ERR_PROPERTYNOTFOUND;
}
__declspec(dllexport) HRESULT WINAPI WICMapShortNameToGuid(LPCWSTR name, GUID *g)
{
    if (!name || !g) return E_INVALIDARG;
    for (UINT i = 0; i < sizeof short_names / sizeof short_names[0]; i++)
        if (!lstrcmpiW(short_names[i].name, name)) { *g = *short_names[i].g; return S_OK; }
    return WINCODEC_ERR_PROPERTYNOTFOUND;
}
__declspec(dllexport) HRESULT WINAPI WICMapSchemaToName(REFGUID fmt, LPWSTR schema, UINT cch, WCHAR *name, UINT *actual)
{
    (void)fmt; (void)cch; (void)name;
    if (!schema || (!name && !actual)) return E_INVALIDARG;
    return WINCODEC_ERR_PROPERTYNOTFOUND;
}
