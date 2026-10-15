/*
 * windowscodecs.dll — the decoders: PNG, JPEG, BMP and GIF through
 * stb_image (third_party/stb, public domain / MIT) and ICO (directory,
 * PNG or DIB entries with their AND masks) here.  A decoder reads its
 * stream once; each frame holds its decoded pixels.  Metadata readers,
 * thumbnails and previews are not supported (the documented errors).
 */
#include "wic.h"

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#include "../../third_party/stb/stb_image.h"
#pragma clang diagnostic pop

static const BYTE sig_png[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
static const BYTE sig_jpg[] = { 0xFF, 0xD8, 0xFF };
static const BYTE sig_bmp[] = { 'B', 'M' };
static const BYTE sig_gif[] = { 'G', 'I', 'F', '8' };
static const BYTE sig_ico[] = { 0, 0, 1, 0 };

static const CodecDesc codecs[] = {
    { &CLSID_WICPngDecoder,  &GUID_ContainerFormatPng,  L"PNG Decoder",  L"image/png", L".png", FALSE, FALSE, TRUE, sig_png, sizeof sig_png },
    { &CLSID_WICJpegDecoder, &GUID_ContainerFormatJpeg, L"JPEG Decoder", L"image/jpeg,image/jpe,image/jpg",
      L".jpeg,.jpe,.jpg,.jfif,.exif", FALSE, FALSE, FALSE, sig_jpg, sizeof sig_jpg },
    { &CLSID_WICBmpDecoder,  &GUID_ContainerFormatBmp,  L"BMP Decoder",  L"image/bmp", L".bmp,.dib,.rle", FALSE, FALSE, TRUE, sig_bmp, sizeof sig_bmp },
    { &CLSID_WICGifDecoder,  &GUID_ContainerFormatGif,  L"GIF Decoder",  L"image/gif", L".gif", TRUE, TRUE, FALSE, sig_gif, sizeof sig_gif },
    { &CLSID_WICIcoDecoder,  &GUID_ContainerFormatIco,  L"ICO Decoder",  L"image/ico,image/x-icon", L".ico,.icon", FALSE, TRUE, TRUE, sig_ico, sizeof sig_ico },
};
#define NCODECS (sizeof codecs / sizeof codecs[0])
enum { C_PNG, C_JPG, C_BMP, C_GIF, C_ICO };

const CodecDesc *codec_at(UINT i) { return i < NCODECS ? &codecs[i] : 0; }
const CodecDesc *codec_by_container(REFGUID g)
{
    for (UINT i = 0; i < NCODECS; i++) if (IsEqualGUID(codecs[i].container, g)) return &codecs[i];
    return 0;
}
const CodecDesc *codec_by_clsid(REFGUID g)
{
    if (IsEqualGUID(g, &CLSID_WICPngDecoder2)) return &codecs[C_PNG];
    for (UINT i = 0; i < NCODECS; i++) if (IsEqualGUID(codecs[i].clsid, g)) return &codecs[i];
    return 0;
}
static const CodecDesc *codec_sniff(const BYTE *d, SIZE_T n)
{
    for (UINT i = 0; i < NCODECS; i++)
        if (n >= codecs[i].siglen && !memcmp(d, codecs[i].sig, codecs[i].siglen)) return &codecs[i];
    if (n >= 4 && !d[0] && !d[1] && d[2] == 2 && !d[3]) return &codecs[C_ICO];     /* a cursor file */
    return 0;
}

/* the whole stream (from its start) in memory */
static HRESULT read_all(IStream *s, BYTE **out, SIZE_T *len)
{
    LARGE_INTEGER zero = { 0 };
    s->lpVtbl->Seek(s, zero, STREAM_SEEK_SET, 0);
    SIZE_T cap = 1 << 16, n = 0;
    BYTE *b = w_alloc(cap);
    if (!b) return E_OUTOFMEMORY;
    for (;;) {
        if (cap - n < 1 << 15) {
            if (cap >= ((SIZE_T)1 << 30)) { w_free(b); return WINCODEC_ERR_IMAGESIZEOUTOFRANGE; }
            BYTE *nb = w_alloc(cap * 2);
            if (!nb) { w_free(b); return E_OUTOFMEMORY; }
            memcpy(nb, b, n);
            w_free(b);
            b = nb; cap *= 2;
        }
        ULONG got = 0;
        HRESULT hr = s->lpVtbl->Read(s, b + n, (ULONG)(cap - n), &got);
        if (FAILED(hr)) { w_free(b); return hr; }
        n += got;
        if (!got || hr == S_FALSE) break;
    }
    *out = b;
    *len = n;
    return S_OK;
}

static UINT be32(const BYTE *p) { return (UINT)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static UINT le32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT)p[3] << 24; }
static UINT le16(const BYTE *p) { return p[0] | p[1] << 8; }

/* the resolution an image file records (96 dpi when it records none) */
static void file_dpi(int codec, const BYTE *d, SIZE_T n, double *x, double *y)
{
    *x = *y = 96.0;
    if (codec == C_PNG) {
        for (SIZE_T p = 8; p + 12 <= n;) {
            UINT len = be32(d + p);
            if (!memcmp(d + p + 4, "pHYs", 4) && len >= 9 && p + 8 + 9 <= n) {
                UINT px = be32(d + p + 8), py = be32(d + p + 12);
                if (d[p + 16] == 1 && px && py) { *x = px * 0.0254; *y = py * 0.0254; }
                return;
            }
            if (!memcmp(d + p + 4, "IDAT", 4) || len > n) return;
            p += 12 + (SIZE_T)len;
        }
    } else if (codec == C_JPG) {
        if (n >= 18 && d[2] == 0xFF && d[3] == 0xE0 && !memcmp(d + 6, "JFIF", 5)) {
            UINT unit = d[13], dx = (UINT)d[14] << 8 | d[15], dy = (UINT)d[16] << 8 | d[17];
            if (dx && dy && unit == 1) { *x = dx; *y = dy; }
            else if (dx && dy && unit == 2) { *x = dx * 2.54; *y = dy * 2.54; }
        }
    } else if (codec == C_BMP) {
        if (n >= 46 && le32(d + 14) >= 40) {
            UINT px = le32(d + 38), py = le32(d + 42);
            if (px && py) { *x = px * 0.0254; *y = py * 0.0254; }
        }
    }
}

/* ---- ICO entries ---- */
static BYTE *ico_dib(const BYTE *d, SIZE_T n, UINT *ow, UINT *oh)
{
    if (n < 40) return 0;
    UINT hs = le32(d), w = le32(d + 4), h2 = le32(d + 8), bpp = le16(d + 14), comp = le32(d + 16), used = le32(d + 32);
    if (hs < 40 || hs > n || !w || w > 4096 || (INT)h2 <= 0 || h2 > 8192 || (comp != 0 && comp != 3)) return 0;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return 0;
    UINT h = h2 / 2;
    if (!h) return 0;
    UINT ncol = bpp <= 8 ? (used ? used : 1u << bpp) : 0;
    if (ncol > 256) return 0;
    const BYTE *pal = d + hs;
    SIZE_T xstride = (((SIZE_T)w * bpp + 31) / 32) * 4, astride = ((w + 31) / 32) * 4;
    const BYTE *xor_ = pal + (SIZE_T)ncol * 4, *and_ = xor_ + xstride * h;
    BOOL has_and = (SIZE_T)(and_ - d) + astride * h <= n;
    if ((SIZE_T)(xor_ - d) + xstride * h > n) return 0;
    BYTE *px = w_alloc((SIZE_T)w * h * 4);
    if (!px) return 0;
    BOOL alpha = FALSE;
    for (UINT y = 0; y < h; y++) {
        const BYTE *s = xor_ + (SIZE_T)(h - 1 - y) * xstride;
        BYTE *o = px + (SIZE_T)y * w * 4;
        for (UINT x = 0; x < w; x++, o += 4) {
            if (bpp == 32) { o[0] = s[x * 4]; o[1] = s[x * 4 + 1]; o[2] = s[x * 4 + 2]; o[3] = s[x * 4 + 3]; if (o[3]) alpha = TRUE; }
            else if (bpp == 24) { o[0] = s[x * 3]; o[1] = s[x * 3 + 1]; o[2] = s[x * 3 + 2]; o[3] = 255; }
            else {
                UINT bit = x * bpp, i = (s[bit / 8] >> (8 - bpp - bit % 8)) & ((1u << bpp) - 1);
                if (i < ncol) { o[0] = pal[i * 4]; o[1] = pal[i * 4 + 1]; o[2] = pal[i * 4 + 2]; }
                o[3] = 255;
            }
        }
    }
    if (!alpha && has_and)                /* the AND mask: a set bit is transparent */
        for (UINT y = 0; y < h; y++) {
            const BYTE *m = and_ + (SIZE_T)(h - 1 - y) * astride;
            for (UINT x = 0; x < w; x++)
                if ((m[x / 8] >> (7 - x % 8)) & 1) px[((SIZE_T)y * w + x) * 4 + 3] = 0;
        }
    if (!alpha && !has_and) for (SIZE_T i = 0; i < (SIZE_T)w * h; i++) px[i * 4 + 3] = 255;
    *ow = w; *oh = h;
    return px;
}

/* ================= frame ================= */
typedef struct Decoder Decoder;
typedef struct {
    const void *const *vtbl;
    LONG ref;
    Decoder *dec;
    UINT w, h, stride;
    const PixelFormat *f;
    BYTE *px;
    double dpix, dpiy;
} Frame;

struct Decoder {
    const void *const *vtbl;
    LONG ref;
    CRITICAL_SECTION lock;
    const CodecDesc *codec;
    BYTE *data;
    SIZE_T size;
    UINT nframes;
    double dpix, dpiy;
    BYTE *gif;                   /* every GIF frame, RGBA, composed by stb_image */
    int gw, gh;
};

static HRESULT STDMETHODCALLTYPE fd_qi(Frame *f, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICBitmapSource) || IsEqualIID(riid, &IID_IWICBitmapFrameDecode)) {
        *out = f;
        InterlockedIncrement(&f->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE fd_addref(Frame *f) { return (ULONG)InterlockedIncrement(&f->ref); }
static ULONG STDMETHODCALLTYPE fd_release(Frame *f)
{
    LONG r = InterlockedDecrement(&f->ref);
    if (!r) {
        ((IUnknown *)f->dec)->lpVtbl->Release((IUnknown *)f->dec);
        w_free(f->px);
        w_free(f);
        dll_release();
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE fd_size(Frame *f, UINT *w, UINT *h)
{
    if (!w || !h) return E_INVALIDARG;
    *w = f->w; *h = f->h;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fd_format(Frame *f, GUID *g)
{
    if (!g) return E_INVALIDARG;
    *g = *f->f->guid;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fd_res(Frame *f, double *x, double *y)
{
    if (!x || !y) return E_INVALIDARG;
    *x = f->dpix; *y = f->dpiy;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fd_palette(Frame *f, IWICPalette *p)
{
    (void)f;
    return p ? WINCODEC_ERR_PALETTEUNAVAILABLE : E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE fd_copy(Frame *f, const WICRect *rc, UINT stride, UINT size, BYTE *buf)
{
    return src_copy_from_buffer(f->px, f->stride, f->w, f->h, f->f, rc, stride, size, buf);
}
static HRESULT STDMETHODCALLTYPE fd_metadata(Frame *f, IWICMetadataQueryReader **out)
{
    (void)f;
    if (!out) return E_INVALIDARG;
    *out = 0;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}
static HRESULT STDMETHODCALLTYPE fd_contexts(Frame *f, UINT count, IWICColorContext **c, UINT *actual)
{
    (void)f; (void)count; (void)c;
    if (!actual) return E_INVALIDARG;
    *actual = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fd_thumbnail(Frame *f, IWICBitmapSource **out)
{
    (void)f;
    if (!out) return E_INVALIDARG;
    *out = 0;
    return WINCODEC_ERR_CODECNOTHUMBNAIL;
}
static const void *const fd_vtbl[] = {
    fd_qi, fd_addref, fd_release, fd_size, fd_format, fd_res, fd_palette, fd_copy, fd_metadata, fd_contexts, fd_thumbnail,
};

/* RGBA (stb_image's order) to BGRA, in place */
static void swap_rb(BYTE *p, SIZE_T n, UINT comp)
{
    for (SIZE_T i = 0; i < n; i++, p += comp) { BYTE t = p[0]; p[0] = p[2]; p[2] = t; }
}

static HRESULT frame_decode(Decoder *d, UINT index, Frame *f)
{
    int w = 0, h = 0, comp = 0;
    BYTE *px = 0;
    f->dpix = d->dpix; f->dpiy = d->dpiy;
    switch (d->codec - codecs) {
    case C_GIF: {
        SIZE_T one = (SIZE_T)d->gw * d->gh * 4;
        if (!(px = w_alloc(one))) return E_OUTOFMEMORY;
        memcpy(px, d->gif + one * index, one);
        swap_rb(px, (SIZE_T)d->gw * d->gh, 4);
        w = d->gw; h = d->gh; comp = 4;
        break;
    }
    case C_ICO: {
        const BYTE *e = d->data + 6 + 16 * index;
        UINT len = le32(e + 8), off = le32(e + 12);
        if (off >= d->size || len > d->size - off) return WINCODEC_ERR_BADIMAGE;
        const BYTE *img = d->data + off;
        if (len >= 8 && !memcmp(img, sig_png, 8)) {
            BYTE *s = stbi_load_from_memory(img, (int)len, &w, &h, &comp, 4);
            if (!s) return WINCODEC_ERR_BADIMAGE;
            px = w_alloc((SIZE_T)w * h * 4);
            if (px) { memcpy(px, s, (SIZE_T)w * h * 4); swap_rb(px, (SIZE_T)w * h, 4); }
            stbi_image_free(s);
            file_dpi(C_PNG, img, len, &f->dpix, &f->dpiy);
        } else {
            UINT uw = 0, uh = 0;
            px = ico_dib(img, len, &uw, &uh);
            if (!px) return WINCODEC_ERR_BADIMAGE;
            w = (int)uw; h = (int)uh;
        }
        if (!px) return E_OUTOFMEMORY;
        comp = 4;
        break;
    }
    default: {
        int c0 = 0;
        if (!stbi_info_from_memory(d->data, (int)d->size, &w, &h, &c0)) return WINCODEC_ERR_BADHEADER;
        int want = c0 == 1 ? 1 : c0 == 3 ? 3 : 4;
        BYTE *s = stbi_load_from_memory(d->data, (int)d->size, &w, &h, &comp, want);
        if (!s) return WINCODEC_ERR_BADIMAGE;
        comp = want;
        px = w_alloc((SIZE_T)w * h * comp);
        if (px) { memcpy(px, s, (SIZE_T)w * h * comp); if (comp >= 3) swap_rb(px, (SIZE_T)w * h, (UINT)comp); }
        stbi_image_free(s);
        if (!px) return E_OUTOFMEMORY;
        break;
    }
    }
    f->w = (UINT)w; f->h = (UINT)h;
    f->f = pf_find(comp == 1 ? &GUID_WICPixelFormat8bppGray : comp == 3 ? &GUID_WICPixelFormat24bppBGR : &GUID_WICPixelFormat32bppBGRA);
    f->stride = (UINT)w * (UINT)comp;
    f->px = px;
    return S_OK;
}

/* ================= decoder ================= */
static HRESULT STDMETHODCALLTYPE dc_qi(Decoder *d, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICBitmapDecoder)) {
        *out = d;
        InterlockedIncrement(&d->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE dc_addref(Decoder *d) { return (ULONG)InterlockedIncrement(&d->ref); }
static ULONG STDMETHODCALLTYPE dc_release(Decoder *d)
{
    LONG r = InterlockedDecrement(&d->ref);
    if (!r) {
        w_free(d->data);
        if (d->gif) stbi_image_free(d->gif);
        DeleteCriticalSection(&d->lock);
        w_free(d);
        dll_release();
    }
    return (ULONG)r;
}

static HRESULT peek(IStream *s, BYTE *buf, ULONG n, ULONG *got)
{
    LARGE_INTEGER zero = { 0 };
    ULARGE_INTEGER at;
    HRESULT hr = s->lpVtbl->Seek(s, zero, STREAM_SEEK_CUR, &at);
    if (FAILED(hr)) return hr;
    s->lpVtbl->Seek(s, zero, STREAM_SEEK_SET, 0);
    hr = s->lpVtbl->Read(s, buf, n, got);
    LARGE_INTEGER back; back.QuadPart = (LONGLONG)at.QuadPart;
    s->lpVtbl->Seek(s, back, STREAM_SEEK_SET, 0);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dc_query(Decoder *d, IStream *s, DWORD *cap)
{
    if (!s || !cap) return E_INVALIDARG;
    BYTE b[8];
    ULONG got = 0;
    HRESULT hr = peek(s, b, sizeof b, &got);
    if (FAILED(hr)) return hr;
    *cap = codec_sniff(b, got) == d->codec
         ? WICBitmapDecoderCapabilityCanDecodeAllImages | WICBitmapDecoderCapabilityCanDecodeSomeImages : 0;
    return S_OK;
}

/* takes @data (allocated with w_alloc) */
static HRESULT dc_load(Decoder *d, BYTE *data, SIZE_T n)
{
    if (codec_sniff(data, n) != d->codec) { w_free(data); return WINCODEC_ERR_UNKNOWNIMAGEFORMAT; }
    if (n > 0x7FFFFFFF) { w_free(data); return WINCODEC_ERR_IMAGESIZEOUTOFRANGE; }
    UINT frames = 1;
    switch (d->codec - codecs) {
    case C_ICO:
        frames = n >= 6 ? le16(data + 4) : 0;
        if (!frames || 6 + 16 * (SIZE_T)frames > n) { w_free(data); return WINCODEC_ERR_BADHEADER; }
        break;
    case C_GIF: {
        int *delays = 0, w = 0, h = 0, z = 0, comp = 0;
        BYTE *g = stbi_load_gif_from_memory(data, (int)n, &delays, &w, &h, &z, &comp, 4);
        if (delays) STBI_FREE(delays);
        if (!g || z < 1) { if (g) stbi_image_free(g); w_free(data); return WINCODEC_ERR_BADIMAGE; }
        d->gif = g; d->gw = w; d->gh = h;
        frames = (UINT)z;
        break;
    }
    default: {
        int w, h, c;
        if (!stbi_info_from_memory(data, (int)n, &w, &h, &c)) { w_free(data); return WINCODEC_ERR_BADHEADER; }
        break;
    }
    }
    d->data = data; d->size = n; d->nframes = frames;
    file_dpi((int)(d->codec - codecs), data, n, &d->dpix, &d->dpiy);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dc_initialize(Decoder *d, IStream *s, WICDecodeOptions opt)
{
    (void)opt;
    if (!s) return E_INVALIDARG;
    EnterCriticalSection(&d->lock);
    HRESULT hr = WINCODEC_ERR_WRONGSTATE;
    if (!d->data) {
        BYTE *data;
        SIZE_T n;
        hr = read_all(s, &data, &n);
        if (SUCCEEDED(hr)) hr = dc_load(d, data, n);
    }
    LeaveCriticalSection(&d->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE dc_container(Decoder *d, GUID *g)
{
    if (!g) return E_INVALIDARG;
    *g = *d->codec->container;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dc_info(Decoder *d, IWICBitmapDecoderInfo **out)
{
    return decoder_info_create(d->codec->container, out);
}
static HRESULT STDMETHODCALLTYPE dc_palette(Decoder *d, IWICPalette *p)
{
    (void)d;
    return p ? WINCODEC_ERR_PALETTEUNAVAILABLE : E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE dc_metadata(Decoder *d, IWICMetadataQueryReader **out)
{
    (void)d;
    if (!out) return E_INVALIDARG;
    *out = 0;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}
static HRESULT STDMETHODCALLTYPE dc_preview(Decoder *d, IWICBitmapSource **out)
{
    (void)d;
    if (!out) return E_INVALIDARG;
    *out = 0;
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}
static HRESULT STDMETHODCALLTYPE dc_contexts(Decoder *d, UINT count, IWICColorContext **c, UINT *actual)
{
    (void)d; (void)count; (void)c;
    if (!actual) return E_INVALIDARG;
    *actual = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dc_thumbnail(Decoder *d, IWICBitmapSource **out)
{
    (void)d;
    if (!out) return E_INVALIDARG;
    *out = 0;
    return WINCODEC_ERR_CODECNOTHUMBNAIL;
}
static HRESULT STDMETHODCALLTYPE dc_count(Decoder *d, UINT *n)
{
    if (!n) return E_INVALIDARG;
    if (!d->data) return WINCODEC_ERR_NOTINITIALIZED;
    *n = d->nframes;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dc_frame(Decoder *d, UINT index, IWICBitmapFrameDecode **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!d->data) return WINCODEC_ERR_NOTINITIALIZED;
    if (index >= d->nframes) return E_INVALIDARG;
    Frame *f = w_alloc(sizeof *f);
    if (!f) return E_OUTOFMEMORY;
    EnterCriticalSection(&d->lock);
    HRESULT hr = frame_decode(d, index, f);
    LeaveCriticalSection(&d->lock);
    if (FAILED(hr)) { w_free(f); return hr; }
    f->vtbl = fd_vtbl;
    f->ref = 1;
    f->dec = d;
    InterlockedIncrement(&d->ref);
    dll_addref();
    *out = (IWICBitmapFrameDecode *)f;
    return S_OK;
}
static const void *const dc_vtbl[] = {
    dc_qi, dc_addref, dc_release, dc_query, dc_initialize, dc_container, dc_info, dc_palette, dc_metadata,
    dc_preview, dc_contexts, dc_thumbnail, dc_count, dc_frame,
};

HRESULT decoder_create(REFGUID container, IWICBitmapDecoder **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    const CodecDesc *c = codec_by_container(container);
    if (!c) return WINCODEC_ERR_COMPONENTNOTFOUND;
    Decoder *d = w_alloc(sizeof *d);
    if (!d) return E_OUTOFMEMORY;
    d->vtbl = dc_vtbl;
    d->ref = 1;
    d->codec = c;
    InitializeCriticalSection(&d->lock);
    dll_addref();
    *out = (IWICBitmapDecoder *)d;
    return S_OK;
}

HRESULT decoder_from_stream(IStream *s, const GUID *vendor, IWICBitmapDecoder **out)
{
    (void)vendor;
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!s) return E_INVALIDARG;
    BYTE *data;
    SIZE_T n;
    HRESULT hr = read_all(s, &data, &n);
    if (FAILED(hr)) return hr;
    const CodecDesc *c = codec_sniff(data, n);
    if (!c) { w_free(data); return WINCODEC_ERR_COMPONENTNOTFOUND; }
    IWICBitmapDecoder *dec;
    if (FAILED(hr = decoder_create(c->container, &dec))) { w_free(data); return hr; }
    if (FAILED(hr = dc_load((Decoder *)dec, data, n))) { dec->lpVtbl->Release(dec); return hr; }
    *out = dec;
    return S_OK;
}

/* ================= decoder info ================= */
typedef struct {
    const void *const *vtbl;
    LONG ref;
    const CodecDesc *c;
} DecInfo;

static HRESULT STDMETHODCALLTYPE di_qi(DecInfo *i, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICComponentInfo) ||
        IsEqualIID(riid, &IID_IWICBitmapCodecInfo) || IsEqualIID(riid, &IID_IWICBitmapDecoderInfo)) {
        *out = i;
        InterlockedIncrement(&i->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE di_addref(DecInfo *i) { return (ULONG)InterlockedIncrement(&i->ref); }
static ULONG STDMETHODCALLTYPE di_release(DecInfo *i)
{
    LONG r = InterlockedDecrement(&i->ref);
    if (!r) { w_free(i); dll_release(); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE di_type(DecInfo *i, WICComponentType *t) { (void)i; if (!t) return E_INVALIDARG; *t = WICDecoder; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_clsid(DecInfo *i, CLSID *c) { if (!c) return E_INVALIDARG; *c = *i->c->clsid; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_signing(DecInfo *i, DWORD *s) { (void)i; if (!s) return E_INVALIDARG; *s = WICComponentSigned; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_author(DecInfo *i, UINT n, WCHAR *b, UINT *a) { (void)i; return string_out(L"NovaOS", n, b, a); }
static HRESULT STDMETHODCALLTYPE di_vendor(DecInfo *i, GUID *g) { (void)i; if (!g) return E_INVALIDARG; *g = GUID_VendorMicrosoft; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_version(DecInfo *i, UINT n, WCHAR *b, UINT *a) { (void)i; return string_out(L"1.0.0.0", n, b, a); }
static HRESULT STDMETHODCALLTYPE di_name(DecInfo *i, UINT n, WCHAR *b, UINT *a) { return string_out(i->c->name, n, b, a); }
static HRESULT STDMETHODCALLTYPE di_container(DecInfo *i, GUID *g) { if (!g) return E_INVALIDARG; *g = *i->c->container; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_formats(DecInfo *i, UINT n, GUID *g, UINT *a)
{
    static const GUID *const f[] = { &GUID_WICPixelFormat8bppGray, &GUID_WICPixelFormat24bppBGR, &GUID_WICPixelFormat32bppBGRA };
    (void)i;
    if (!a) return E_INVALIDARG;
    *a = 3;
    if (!g) return S_OK;
    if (n < 3) return WINCODEC_ERR_INSUFFICIENTBUFFER;
    for (UINT k = 0; k < 3; k++) g[k] = *f[k];
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di_empty(DecInfo *i, UINT n, WCHAR *b, UINT *a) { (void)i; return string_out(L"", n, b, a); }
static HRESULT STDMETHODCALLTYPE di_mimes(DecInfo *i, UINT n, WCHAR *b, UINT *a) { return string_out(i->c->mimes, n, b, a); }
static HRESULT STDMETHODCALLTYPE di_exts(DecInfo *i, UINT n, WCHAR *b, UINT *a) { return string_out(i->c->exts, n, b, a); }
static HRESULT STDMETHODCALLTYPE di_anim(DecInfo *i, BOOL *r) { if (!r) return E_INVALIDARG; *r = i->c->anim; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_chroma(DecInfo *i, BOOL *r) { if (!r) return E_INVALIDARG; *r = i->c->anim; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_lossless(DecInfo *i, BOOL *r) { if (!r) return E_INVALIDARG; *r = i->c->lossless; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_multi(DecInfo *i, BOOL *r) { if (!r) return E_INVALIDARG; *r = i->c->multi; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_matches_mime(DecInfo *i, LPCWSTR mime, BOOL *r)
{
    if (!mime || !r) return E_INVALIDARG;
    *r = FALSE;
    SIZE_T ml = lstrlenW(mime);
    for (const WCHAR *p = i->c->mimes; *p;) {
        const WCHAR *e = p;
        while (*e && *e != ',') e++;
        if ((SIZE_T)(e - p) == ml && CompareStringOrdinal(p, (int)ml, mime, (int)ml, TRUE) == CSTR_EQUAL) *r = TRUE;
        p = *e ? e + 1 : e;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di_patterns(DecInfo *i, UINT size, WICBitmapPattern *pat, UINT *count, UINT *actual)
{
    if (!count || !actual) return E_INVALIDARG;
    UINT need = sizeof(WICBitmapPattern) + 2 * i->c->siglen;
    *count = 1;
    *actual = need;
    if (!pat) return S_OK;
    if (size < need) return WINCODEC_ERR_INSUFFICIENTBUFFER;
    BYTE *bytes = (BYTE *)(pat + 1);
    memset(pat, 0, sizeof *pat);
    pat->Length = i->c->siglen;
    pat->Pattern = bytes;
    pat->Mask = bytes + i->c->siglen;
    memcpy(bytes, i->c->sig, i->c->siglen);
    memset(bytes + i->c->siglen, 0xFF, i->c->siglen);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di_matches(DecInfo *i, IStream *s, BOOL *r)
{
    if (!s || !r) return E_INVALIDARG;
    BYTE b[8];
    ULONG got = 0;
    HRESULT hr = peek(s, b, sizeof b, &got);
    if (FAILED(hr)) return hr;
    *r = codec_sniff(b, got) == i->c;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di_create(DecInfo *i, IWICBitmapDecoder **out) { return decoder_create(i->c->container, out); }
static const void *const di_vtbl[] = {
    di_qi, di_addref, di_release,
    di_type, di_clsid, di_signing, di_author, di_vendor, di_version, di_version, di_name,
    di_container, di_formats, di_version, di_empty, di_empty, di_mimes, di_exts, di_anim, di_chroma, di_lossless,
    di_multi, di_matches_mime,
    di_patterns, di_matches, di_create,
};

HRESULT decoder_info_create(REFGUID container, IWICBitmapDecoderInfo **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    const CodecDesc *c = codec_by_container(container);
    if (!c) return WINCODEC_ERR_COMPONENTNOTFOUND;
    DecInfo *i = w_alloc(sizeof *i);
    if (!i) return E_OUTOFMEMORY;
    i->vtbl = di_vtbl;
    i->ref = 1;
    i->c = c;
    dll_addref();
    *out = (IWICBitmapDecoderInfo *)i;
    return S_OK;
}
