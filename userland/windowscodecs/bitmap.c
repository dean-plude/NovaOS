/*
 * windowscodecs.dll — the objects that hold pixels or bytes: bitmaps and
 * their locks (also over a mapped section, an HBITMAP or an HICON),
 * palettes, streams and colour contexts.
 */
#include "wic.h"

/* ================= palette ================= */
typedef struct {
    const void *const *vtbl;
    LONG ref;
    CRITICAL_SECTION lock;
    WICColor *colors;
    UINT count;
    WICBitmapPaletteType type;
} Palette;

static HRESULT STDMETHODCALLTYPE pl_qi(Palette *p, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICPalette)) {
        *out = p;
        InterlockedIncrement(&p->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pl_addref(Palette *p) { return (ULONG)InterlockedIncrement(&p->ref); }
static ULONG STDMETHODCALLTYPE pl_release(Palette *p)
{
    LONG r = InterlockedDecrement(&p->ref);
    if (!r) { w_free(p->colors); DeleteCriticalSection(&p->lock); w_free(p); dll_release(); }
    return (ULONG)r;
}
static HRESULT pl_store(Palette *p, const WICColor *c, UINT n, WICBitmapPaletteType type)
{
    WICColor *nc = 0;
    if (n && !(nc = w_alloc(n * sizeof *nc))) return E_OUTOFMEMORY;
    if (n) memcpy(nc, c, n * sizeof *nc);
    EnterCriticalSection(&p->lock);
    w_free(p->colors);
    p->colors = nc; p->count = n; p->type = type;
    LeaveCriticalSection(&p->lock);
    return S_OK;
}

static UINT cube(WICColor *o, UINT n, UINT lr, UINT lg, UINT lb)
{
    for (UINT b = 0; b < lb; b++)
        for (UINT g = 0; g < lg; g++)
            for (UINT r = 0; r < lr; r++)
                o[n++] = 0xFF000000u | (r * 255 / (lr - 1)) << 16 | (g * 255 / (lg - 1)) << 8 | (b * 255 / (lb - 1));
    return n;
}
/* the 16 VGA colours the halftone palettes add when the cube lacks them */
static UINT add_system(WICColor *o, UINT n)
{
    static const WICColor sys[16] = {
        0xFF000000, 0xFF800000, 0xFF008000, 0xFF808000, 0xFF000080, 0xFF800080, 0xFF008080, 0xFFC0C0C0,
        0xFF808080, 0xFFFF0000, 0xFF00FF00, 0xFFFFFF00, 0xFF0000FF, 0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF,
    };
    for (UINT i = 0; i < 16; i++) {
        UINT k = 0;
        while (k < n && o[k] != sys[i]) k++;
        if (k == n) o[n++] = sys[i];
    }
    return n;
}

static HRESULT STDMETHODCALLTYPE pl_predefined(Palette *p, WICBitmapPaletteType type, BOOL transparent)
{
    WICColor c[257];
    UINT n = 0;
    switch (type) {
    case WICBitmapPaletteTypeFixedBW:          c[0] = 0xFF000000; c[1] = 0xFFFFFFFF; n = 2; break;
    case WICBitmapPaletteTypeFixedHalftone8:   n = add_system(c, cube(c, 0, 2, 2, 2)); break;
    case WICBitmapPaletteTypeFixedHalftone27:  n = add_system(c, cube(c, 0, 3, 3, 3)); break;
    case WICBitmapPaletteTypeFixedHalftone64:  n = add_system(c, cube(c, 0, 4, 4, 4)); break;
    case WICBitmapPaletteTypeFixedHalftone125: n = add_system(c, cube(c, 0, 5, 5, 5)); break;
    case WICBitmapPaletteTypeFixedHalftone216: n = add_system(c, cube(c, 0, 6, 6, 6)); break;
    case WICBitmapPaletteTypeFixedHalftone252: n = cube(c, 0, 6, 7, 6); break;
    case WICBitmapPaletteTypeFixedHalftone256: n = cube(c, 0, 8, 8, 4); break;
    case WICBitmapPaletteTypeFixedGray4:
    case WICBitmapPaletteTypeFixedGray16:
    case WICBitmapPaletteTypeFixedGray256: {
        n = type == WICBitmapPaletteTypeFixedGray4 ? 4 : type == WICBitmapPaletteTypeFixedGray16 ? 16 : 256;
        for (UINT i = 0; i < n; i++) { UINT v = i * 255 / (n - 1); c[i] = 0xFF000000u | v << 16 | v << 8 | v; }
        break;
    }
    default: return E_INVALIDARG;
    }
    if (transparent) {
        if (n < 256) c[n++] = 0;
        else c[255] = 0;
    }
    return pl_store(p, c, n, type);
}
static HRESULT STDMETHODCALLTYPE pl_custom(Palette *p, WICColor *c, UINT n)
{
    if (!c && n) return E_INVALIDARG;
    return pl_store(p, c, n, WICBitmapPaletteTypeCustom);
}

/* a palette for a bitmap: the most used colours, at 5 bits per channel */
static HRESULT STDMETHODCALLTYPE pl_from_bitmap(Palette *p, IWICBitmapSource *src, UINT count, BOOL transparent)
{
    if (!src || count < 2 || count > 256) return E_INVALIDARG;
    IWICBitmapSource *bgra = 0;
    HRESULT hr = WICConvertBitmapSource(&GUID_WICPixelFormat32bppBGRA, src, &bgra);
    if (FAILED(hr)) return hr;
    UINT w, h;
    bgra->lpVtbl->GetSize(bgra, &w, &h);
    UINT *hist = w_alloc(32768 * sizeof *hist);
    BYTE *row = w_alloc((SIZE_T)w * 4 + 4);
    if (!hist || !row) { w_free(hist); w_free(row); bgra->lpVtbl->Release(bgra); return E_OUTOFMEMORY; }
    for (UINT y = 0; y < h && SUCCEEDED(hr); y++) {
        WICRect r = { 0, (INT)y, (INT)w, 1 };
        hr = bgra->lpVtbl->CopyPixels(bgra, &r, w * 4, w * 4, row);
        for (UINT x = 0; x < w && SUCCEEDED(hr); x++)
            hist[(row[x * 4 + 2] >> 3) << 10 | (row[x * 4 + 1] >> 3) << 5 | row[x * 4] >> 3]++;
    }
    bgra->lpVtbl->Release(bgra);
    w_free(row);
    if (FAILED(hr)) { w_free(hist); return hr; }
    WICColor c[256];
    UINT n = 0, want = transparent ? count - 1 : count;
    while (n < want) {
        UINT best = 0, bi = 0;
        for (UINT i = 0; i < 32768; i++) if (hist[i] > best) { best = hist[i]; bi = i; }
        if (!best) break;
        hist[bi] = 0;
        UINT r = (bi >> 10) & 31, g = (bi >> 5) & 31, b = bi & 31;
        c[n++] = 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
    }
    w_free(hist);
    if (transparent) c[n++] = 0;
    return pl_store(p, c, n, WICBitmapPaletteTypeMedianCut);
}
static HRESULT STDMETHODCALLTYPE pl_from_palette(Palette *p, IWICPalette *src)
{
    if (!src) return E_INVALIDARG;
    UINT n = 0;
    WICBitmapPaletteType t = WICBitmapPaletteTypeCustom;
    HRESULT hr = src->lpVtbl->GetColorCount(src, &n);
    if (FAILED(hr)) return hr;
    WICColor *c = w_alloc((n ? n : 1) * sizeof *c);
    if (!c) return E_OUTOFMEMORY;
    if (n) hr = src->lpVtbl->GetColors(src, n, c, &n);
    src->lpVtbl->GetType(src, &t);
    if (SUCCEEDED(hr)) hr = pl_store(p, c, n, t);
    w_free(c);
    return hr;
}
static HRESULT STDMETHODCALLTYPE pl_type(Palette *p, WICBitmapPaletteType *t)
{
    if (!t) return E_INVALIDARG;
    *t = p->type;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pl_count(Palette *p, UINT *n)
{
    if (!n) return E_INVALIDARG;
    *n = p->count;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pl_colors(Palette *p, UINT n, WICColor *c, UINT *actual)
{
    if (!c || !actual) return E_INVALIDARG;
    EnterCriticalSection(&p->lock);
    UINT k = n < p->count ? n : p->count;
    memcpy(c, p->colors, k * sizeof *c);
    *actual = k;
    LeaveCriticalSection(&p->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pl_is_bw(Palette *p, BOOL *r)
{
    if (!r) return E_INVALIDARG;
    *r = p->type == WICBitmapPaletteTypeFixedBW;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pl_is_gray(Palette *p, BOOL *r)
{
    if (!r) return E_INVALIDARG;
    BOOL g = p->count > 0;
    for (UINT i = 0; i < p->count && g; i++) {
        WICColor c = p->colors[i];
        g = ((c >> 16) & 255) == ((c >> 8) & 255) && ((c >> 8) & 255) == (c & 255);
    }
    *r = g;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pl_has_alpha(Palette *p, BOOL *r)
{
    if (!r) return E_INVALIDARG;
    *r = FALSE;
    for (UINT i = 0; i < p->count; i++) if ((p->colors[i] >> 24) != 255) *r = TRUE;
    return S_OK;
}
static const void *const pl_vtbl[] = {
    pl_qi, pl_addref, pl_release, pl_predefined, pl_custom, pl_from_bitmap, pl_from_palette, pl_type,
    pl_count, pl_colors, pl_is_bw, pl_is_gray, pl_has_alpha,
};
HRESULT palette_create(IWICPalette **out)
{
    if (!out) return E_INVALIDARG;
    Palette *p = w_alloc(sizeof *p);
    if (!p) { *out = 0; return E_OUTOFMEMORY; }
    p->vtbl = pl_vtbl;
    p->ref = 1;
    p->type = WICBitmapPaletteTypeCustom;
    InitializeCriticalSection(&p->lock);
    dll_addref();
    *out = (IWICPalette *)p;
    return S_OK;
}
HRESULT palette_set(IWICPalette *p, const WICColor *c, UINT n)
{
    return p->lpVtbl->InitializeCustom(p, (WICColor *)c, n);
}
HRESULT palette_colors(IWICPalette *p, WICColor *c, UINT max, UINT *n)
{
    return p->lpVtbl->GetColors(p, max, c, n);
}

/* ================= bitmap and lock ================= */
typedef struct {
    const void *const *vtbl;
    LONG ref;
    CRITICAL_SECTION cs;
    UINT w, h, stride;
    const PixelFormat *f;
    BYTE *data;
    void *view;                        /* a mapped section to unmap, or NULL */
    double dpix, dpiy;
    WICColor *pal;
    UINT npal;
    LONG locks;                        /* read locks, or -1 while locked for writing */
} Bitmap;

typedef struct {
    const void *const *vtbl;
    LONG ref;
    Bitmap *bmp;
    WICRect r;
    BOOL write;
} Lock;

static HRESULT STDMETHODCALLTYPE lk_qi(Lock *l, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICBitmapLock)) {
        *out = l;
        InterlockedIncrement(&l->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE lk_addref(Lock *l) { return (ULONG)InterlockedIncrement(&l->ref); }
static ULONG STDMETHODCALLTYPE lk_release(Lock *l)
{
    LONG r = InterlockedDecrement(&l->ref);
    if (!r) {
        Bitmap *b = l->bmp;
        EnterCriticalSection(&b->cs);
        if (l->write) b->locks = 0; else b->locks--;
        LeaveCriticalSection(&b->cs);
        ((IUnknown *)b)->lpVtbl->Release((IUnknown *)b);
        w_free(l);
        dll_release();
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE lk_size(Lock *l, UINT *w, UINT *h)
{
    if (!w || !h) return E_INVALIDARG;
    *w = (UINT)l->r.Width; *h = (UINT)l->r.Height;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE lk_stride(Lock *l, UINT *s)
{
    if (!s) return E_INVALIDARG;
    *s = l->bmp->stride;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE lk_data(Lock *l, UINT *size, BYTE **data)
{
    if (!size || !data) return E_INVALIDARG;
    Bitmap *b = l->bmp;
    *data = b->data + (SIZE_T)l->r.Y * b->stride + ((SIZE_T)l->r.X * b->f->bpp) / 8;
    *size = b->stride * (UINT)(l->r.Height - 1) + pf_stride(b->f, (UINT)l->r.Width);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE lk_format(Lock *l, GUID *g)
{
    if (!g) return E_INVALIDARG;
    *g = *l->bmp->f->guid;
    return S_OK;
}
static const void *const lk_vtbl[] = { lk_qi, lk_addref, lk_release, lk_size, lk_stride, lk_data, lk_format };

static HRESULT STDMETHODCALLTYPE bm_qi(Bitmap *b, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICBitmapSource) || IsEqualIID(riid, &IID_IWICBitmap)) {
        *out = b;
        InterlockedIncrement(&b->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE bm_addref(Bitmap *b) { return (ULONG)InterlockedIncrement(&b->ref); }
static ULONG STDMETHODCALLTYPE bm_release(Bitmap *b)
{
    LONG r = InterlockedDecrement(&b->ref);
    if (!r) {
        if (b->view) UnmapViewOfFile(b->view);
        else w_free(b->data);
        w_free(b->pal);
        DeleteCriticalSection(&b->cs);
        w_free(b);
        dll_release();
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE bm_size(Bitmap *b, UINT *w, UINT *h)
{
    if (!w || !h) return E_INVALIDARG;
    *w = b->w; *h = b->h;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE bm_format(Bitmap *b, GUID *g)
{
    if (!g) return E_INVALIDARG;
    *g = *b->f->guid;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE bm_res(Bitmap *b, double *x, double *y)
{
    if (!x || !y) return E_INVALIDARG;
    *x = b->dpix; *y = b->dpiy;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE bm_palette(Bitmap *b, IWICPalette *p)
{
    if (!p) return E_INVALIDARG;
    if (!b->npal) return WINCODEC_ERR_PALETTEUNAVAILABLE;
    return palette_set(p, b->pal, b->npal);
}
static HRESULT STDMETHODCALLTYPE bm_copy(Bitmap *b, const WICRect *rc, UINT stride, UINT size, BYTE *buf)
{
    return src_copy_from_buffer(b->data, b->stride, b->w, b->h, b->f, rc, stride, size, buf);
}
static HRESULT STDMETHODCALLTYPE bm_lock(Bitmap *b, const WICRect *rc, DWORD flags, IWICBitmapLock **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    WICRect r;
    if (!(flags & (WICBitmapLockRead | WICBitmapLockWrite))) return E_INVALIDARG;
    if (FAILED(src_check_rect(rc, b->w, b->h, &r))) return E_INVALIDARG;
    BOOL write = (flags & WICBitmapLockWrite) != 0;
    EnterCriticalSection(&b->cs);
    if (b->locks < 0 || (write && b->locks > 0)) { LeaveCriticalSection(&b->cs); return WINCODEC_ERR_ALREADYLOCKED; }
    Lock *l = w_alloc(sizeof *l);
    if (!l) { LeaveCriticalSection(&b->cs); return E_OUTOFMEMORY; }
    b->locks = write ? -1 : b->locks + 1;
    LeaveCriticalSection(&b->cs);
    l->vtbl = lk_vtbl;
    l->ref = 1;
    l->bmp = b;
    l->r = r;
    l->write = write;
    InterlockedIncrement(&b->ref);
    dll_addref();
    *out = (IWICBitmapLock *)l;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE bm_set_palette(Bitmap *b, IWICPalette *p)
{
    if (!p) return E_INVALIDARG;
    UINT n = 0;
    HRESULT hr = p->lpVtbl->GetColorCount(p, &n);
    if (FAILED(hr)) return hr;
    WICColor *c = w_alloc((n ? n : 1) * sizeof *c);
    if (!c) return E_OUTOFMEMORY;
    if (n && FAILED(hr = p->lpVtbl->GetColors(p, n, c, &n))) { w_free(c); return hr; }
    EnterCriticalSection(&b->cs);
    w_free(b->pal);
    b->pal = c; b->npal = n;
    LeaveCriticalSection(&b->cs);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE bm_set_res(Bitmap *b, double x, double y)
{
    b->dpix = x; b->dpiy = y;
    return S_OK;
}
static const void *const bm_vtbl[] = {
    bm_qi, bm_addref, bm_release, bm_size, bm_format, bm_res, bm_palette, bm_copy, bm_lock, bm_set_palette, bm_set_res,
};

HRESULT bitmap_create(UINT w, UINT h, REFGUID fmt, UINT stride, const BYTE *mem, void *view, BYTE *viewdata, IWICBitmap **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    const PixelFormat *f = pf_find(fmt);
    if (!f || !f->bpp) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (!w || !h) return E_INVALIDARG;
    UINT row = pf_stride(f, w);
    if ((UINT64)row * h > 0x7FFFFFFF) return WINCODEC_ERR_VALUEOVERFLOW;
    Bitmap *b = w_alloc(sizeof *b);
    if (!b) return E_OUTOFMEMORY;
    if (view) {
        b->view = view;
        b->data = viewdata;
        b->stride = stride;
    } else {
        b->stride = (row + 3) & ~3u;              /* rows on 4-byte boundaries, as Windows' */
        b->data = w_alloc((SIZE_T)b->stride * h);
        if (!b->data) { w_free(b); return E_OUTOFMEMORY; }
        if (mem) for (UINT y = 0; y < h; y++) memcpy(b->data + (SIZE_T)y * b->stride, mem + (SIZE_T)y * stride, row);
    }
    b->vtbl = bm_vtbl;
    b->ref = 1;
    b->w = w; b->h = h; b->f = f;
    b->dpix = b->dpiy = 96.0;
    InitializeCriticalSection(&b->cs);
    dll_addref();
    *out = (IWICBitmap *)b;
    return S_OK;
}

HRESULT bitmap_from_source(IWICBitmapSource *s, const WICRect *rc, IWICBitmap **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!s) return E_INVALIDARG;
    UINT w, h;
    GUID g;
    WICRect r;
    HRESULT hr = s->lpVtbl->GetSize(s, &w, &h);
    if (SUCCEEDED(hr)) hr = s->lpVtbl->GetPixelFormat(s, &g);
    if (FAILED(hr)) return hr;
    if (FAILED(src_check_rect(rc, w, h, &r)) || !r.Width || !r.Height) return E_INVALIDARG;
    IWICBitmap *bmp;
    if (FAILED(hr = bitmap_create((UINT)r.Width, (UINT)r.Height, &g, 0, 0, 0, 0, &bmp))) return hr;
    Bitmap *b = (Bitmap *)bmp;
    hr = s->lpVtbl->CopyPixels(s, &r, b->stride, b->stride * b->h, b->data);
    if (SUCCEEDED(hr)) {
        double x = 96, y = 96;
        if (SUCCEEDED(s->lpVtbl->GetResolution(s, &x, &y))) { b->dpix = x; b->dpiy = y; }
        if (b->f->kind == PK_INDEXED) b->pal = src_palette(s, &b->npal);
    }
    if (FAILED(hr)) { bmp->lpVtbl->Release(bmp); return hr; }
    *out = bmp;
    return S_OK;
}

/* the bitmap over a mapped section (InteropBitmap's memory): the pixels
 * stay in the section, so later writes to it show */
static HRESULT from_section(UINT w, UINT h, REFGUID fmt, HANDLE section, UINT stride, UINT offset, BOOL write, IWICBitmap **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    const PixelFormat *f = pf_find(fmt);
    if (!f || !f->bpp) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (!section || !w || !h || stride < pf_stride(f, w)) return E_INVALIDARG;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    UINT gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 65536;
    UINT base = offset - offset % gran;
    SIZE_T len = (SIZE_T)(offset - base) + (SIZE_T)stride * (h - 1) + pf_stride(f, w);
    BYTE *view = MapViewOfFile(section, write ? FILE_MAP_WRITE : FILE_MAP_READ, 0, base, len);
    if (!view) return HRESULT_FROM_WIN32(GetLastError());
    HRESULT hr = bitmap_create(w, h, fmt, stride, 0, view, view + (offset - base), out);
    if (FAILED(hr)) UnmapViewOfFile(view);
    return hr;
}
__declspec(dllexport) HRESULT WINAPI WICCreateBitmapFromSection(UINT w, UINT h, REFGUID fmt, HANDLE section, UINT stride,
                                                               UINT offset, IWICBitmap **out)
{
    return from_section(w, h, fmt, section, stride, offset, TRUE, out);
}
__declspec(dllexport) HRESULT WINAPI WICCreateBitmapFromSectionEx(UINT w, UINT h, REFGUID fmt, HANDLE section, UINT stride,
                                                                 UINT offset, int access, IWICBitmap **out)
{
    return from_section(w, h, fmt, section, stride, offset, (access & 2) != 0 /* WICSectionAccessLevelReadWrite */, out);
}

/* an HBITMAP's pixels, 32 bits per pixel, top-down */
static BYTE *dib_bits(HBITMAP hbm, UINT *w, UINT *h, UINT *bpp)
{
    BITMAP bm;
    if (!GetObjectW(hbm, sizeof bm, &bm)) return 0;
    *w = (UINT)bm.bmWidth; *h = (UINT)(bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight); *bpp = bm.bmBitsPixel;
    if (!*w || !*h) return 0;
    BYTE *bits = w_alloc((SIZE_T)*w * *h * 4);
    if (!bits) return 0;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = (LONG)*w;
    bi.bmiHeader.biHeight = -(LONG)*h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(0);
    int got = GetDIBits(dc, hbm, 0, *h, bits, &bi, DIB_RGB_COLORS);
    DeleteDC(dc);
    if (!got) { w_free(bits); return 0; }
    return bits;
}

HRESULT bitmap_from_hbitmap(HBITMAP hbm, HPALETTE hpal, WICBitmapAlphaChannelOption opt, IWICBitmap **out)
{
    (void)hpal;
    if (!out) return E_INVALIDARG;
    *out = 0;
    UINT w, h, bpp;
    BYTE *bits = dib_bits(hbm, &w, &h, &bpp);
    if (!bits) return E_INVALIDARG;
    if (bpp < 32) for (UINT i = 0; i < w * h; i++) bits[i * 4 + 3] = 255;
    const GUID *fmt = opt == WICBitmapUsePremultipliedAlpha ? &GUID_WICPixelFormat32bppPBGRA
                    : opt == WICBitmapIgnoreAlpha ? &GUID_WICPixelFormat32bppBGR : &GUID_WICPixelFormat32bppBGRA;
    HRESULT hr = bitmap_create(w, h, fmt, w * 4, bits, 0, 0, out);
    w_free(bits);
    return hr;
}

HRESULT bitmap_from_hicon(HICON icon, IWICBitmap **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    ICONINFO ii;
    if (!icon || !GetIconInfo(icon, &ii)) return E_INVALIDARG;
    UINT w = 0, h = 0, bpp = 0, mw = 0, mh = 0, mbpp = 0;
    BYTE *mask = ii.hbmMask ? dib_bits(ii.hbmMask, &mw, &mh, &mbpp) : 0;
    BYTE *color = ii.hbmColor ? dib_bits(ii.hbmColor, &w, &h, &bpp) : 0;
    HRESULT hr = E_INVALIDARG;
    BYTE *px = 0;
    if (color) {
        BOOL alpha = FALSE;
        if (bpp == 32) for (UINT i = 0; i < w * h && !alpha; i++) alpha = color[i * 4 + 3] != 0;
        if (!alpha)
            for (UINT i = 0; i < w * h; i++)     /* AND mask: white = transparent */
                color[i * 4 + 3] = mask && i < mw * mh && mask[i * 4] ? 0 : 255;
        px = color; color = 0;
    } else if (mask && mh >= 2) {                /* monochrome: AND over XOR */
        w = mw; h = mh / 2;
        px = w_alloc((SIZE_T)w * h * 4);
        if (px)
            for (UINT i = 0; i < w * h; i++) {
                BYTE a = mask[i * 4], x = mask[(i + w * h) * 4];
                px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = x ? 255 : 0;
                px[i * 4 + 3] = a && !x ? 0 : 255;
            }
    }
    if (px) hr = bitmap_create(w, h, &GUID_WICPixelFormat32bppBGRA, w * 4, px, 0, 0, out);
    w_free(px); w_free(color); w_free(mask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return hr;
}

/* ================= stream ================= */
enum { ST_NONE, ST_MEMORY, ST_STREAM, ST_FILE, ST_REGION };
typedef struct {
    const void *const *vtbl;
    LONG ref;
    int kind;
    BYTE *mem;
    ULONGLONG size, pos;             /* memory and region streams */
    ULONGLONG off;                   /* region start in the inner stream */
    IStream *inner;
    HANDLE file;
} Stream;

static HRESULT STDMETHODCALLTYPE st_qi(Stream *s, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ISequentialStream) || IsEqualIID(riid, &IID_IStream)
        || IsEqualIID(riid, &IID_IWICStream)) {
        *out = s;
        InterlockedIncrement(&s->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE st_addref(Stream *s) { return (ULONG)InterlockedIncrement(&s->ref); }
static ULONG STDMETHODCALLTYPE st_release(Stream *s)
{
    LONG r = InterlockedDecrement(&s->ref);
    if (!r) {
        COM_RELEASE(s->inner);
        if (s->file) CloseHandle(s->file);
        w_free(s);
        dll_release();
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE st_read(Stream *s, void *pv, ULONG cb, ULONG *read)
{
    ULONG got = 0;
    HRESULT hr = S_OK;
    if (!pv) return STG_E_INVALIDPOINTER;
    switch (s->kind) {
    case ST_MEMORY:
        got = s->pos >= s->size ? 0 : (ULONG)(s->size - s->pos < cb ? s->size - s->pos : cb);
        memcpy(pv, s->mem + s->pos, got);
        s->pos += got;
        break;
    case ST_STREAM:
        return s->inner->lpVtbl->Read(s->inner, pv, cb, read);
    case ST_REGION: {
        ULONG want = s->pos >= s->size ? 0 : (ULONG)(s->size - s->pos < cb ? s->size - s->pos : cb);
        LARGE_INTEGER at; at.QuadPart = (LONGLONG)(s->off + s->pos);
        hr = s->inner->lpVtbl->Seek(s->inner, at, STREAM_SEEK_SET, 0);
        if (SUCCEEDED(hr) && want) hr = s->inner->lpVtbl->Read(s->inner, pv, want, &got);
        s->pos += got;
        break;
    }
    case ST_FILE: {
        DWORD n = 0;
        if (!ReadFile(s->file, pv, cb, &n, 0)) hr = HRESULT_FROM_WIN32(GetLastError());
        got = n;
        break;
    }
    default: return WINCODEC_ERR_NOTINITIALIZED;
    }
    if (read) *read = got;
    return FAILED(hr) ? hr : got < cb ? S_FALSE : S_OK;
}
static HRESULT STDMETHODCALLTYPE st_write(Stream *s, const void *pv, ULONG cb, ULONG *written)
{
    ULONG put = 0;
    HRESULT hr = S_OK;
    if (!pv) return STG_E_INVALIDPOINTER;
    switch (s->kind) {
    case ST_MEMORY:
        if (s->pos >= s->size || s->size - s->pos < cb) return STG_E_MEDIUMFULL;
        memcpy(s->mem + s->pos, pv, cb);
        s->pos += cb;
        put = cb;
        break;
    case ST_STREAM:
        return s->inner->lpVtbl->Write(s->inner, pv, cb, written);
    case ST_REGION: {
        if (s->pos >= s->size || s->size - s->pos < cb) return STG_E_MEDIUMFULL;
        LARGE_INTEGER at; at.QuadPart = (LONGLONG)(s->off + s->pos);
        hr = s->inner->lpVtbl->Seek(s->inner, at, STREAM_SEEK_SET, 0);
        if (SUCCEEDED(hr)) hr = s->inner->lpVtbl->Write(s->inner, pv, cb, &put);
        s->pos += put;
        break;
    }
    case ST_FILE: {
        DWORD n = 0;
        if (!WriteFile(s->file, pv, cb, &n, 0)) hr = HRESULT_FROM_WIN32(GetLastError());
        put = n;
        break;
    }
    default: return WINCODEC_ERR_NOTINITIALIZED;
    }
    if (written) *written = put;
    return hr;
}
static HRESULT STDMETHODCALLTYPE st_seek(Stream *s, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *pos)
{
    if (s->kind == ST_STREAM) return s->inner->lpVtbl->Seek(s->inner, move, origin, pos);
    if (s->kind == ST_FILE) {
        LARGE_INTEGER np;
        if (!SetFilePointerEx(s->file, move, &np, origin)) return HRESULT_FROM_WIN32(GetLastError());
        if (pos) pos->QuadPart = (ULONGLONG)np.QuadPart;
        return S_OK;
    }
    if (s->kind == ST_NONE) return WINCODEC_ERR_NOTINITIALIZED;
    LONGLONG base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? (LONGLONG)s->pos
                  : origin == STREAM_SEEK_END ? (LONGLONG)s->size : -1;
    if (base < 0) return STG_E_INVALIDFUNCTION;
    LONGLONG np = base + move.QuadPart;
    if (np < 0) return STG_E_INVALIDFUNCTION;
    if ((ULONGLONG)np > s->size) return WINCODEC_ERR_VALUEOVERFLOW;
    s->pos = (ULONGLONG)np;
    if (pos) pos->QuadPart = s->pos;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE st_setsize(Stream *s, ULARGE_INTEGER size)
{
    if (s->kind == ST_STREAM) return s->inner->lpVtbl->SetSize(s->inner, size);
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE st_copyto(Stream *s, IStream *to, ULARGE_INTEGER cb, ULARGE_INTEGER *read, ULARGE_INTEGER *written)
{
    if (s->kind == ST_STREAM) return s->inner->lpVtbl->CopyTo(s->inner, to, cb, read, written);
    BYTE buf[4096];
    ULONGLONG left = cb.QuadPart, r = 0, w = 0;
    HRESULT hr = S_OK;
    while (left && SUCCEEDED(hr)) {
        ULONG got = 0, put = 0;
        hr = st_read(s, buf, left < sizeof buf ? (ULONG)left : sizeof buf, &got);
        if (FAILED(hr) || !got) break;
        r += got;
        hr = to->lpVtbl->Write(to, buf, got, &put);
        w += put;
        left -= got;
    }
    if (read) read->QuadPart = r;
    if (written) written->QuadPart = w;
    return FAILED(hr) ? hr : S_OK;
}
static HRESULT STDMETHODCALLTYPE st_commit(Stream *s, DWORD flags)
{
    if (s->kind == ST_STREAM || s->kind == ST_REGION) return s->inner->lpVtbl->Commit(s->inner, flags);
    if (s->kind == ST_FILE) FlushFileBuffers(s->file);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE st_revert(Stream *s)
{
    if (s->kind == ST_STREAM) return s->inner->lpVtbl->Revert(s->inner);
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE st_lockregion(Stream *s, ULARGE_INTEGER off, ULARGE_INTEGER cb, DWORD type)
{
    if (s->kind == ST_STREAM) return s->inner->lpVtbl->LockRegion(s->inner, off, cb, type);
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE st_unlockregion(Stream *s, ULARGE_INTEGER off, ULARGE_INTEGER cb, DWORD type)
{
    if (s->kind == ST_STREAM) return s->inner->lpVtbl->UnlockRegion(s->inner, off, cb, type);
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE st_stat(Stream *s, STATSTG *st, DWORD flags)
{
    if (!st) return STG_E_INVALIDPOINTER;
    if (s->kind == ST_STREAM) return s->inner->lpVtbl->Stat(s->inner, st, flags);
    memset(st, 0, sizeof *st);
    st->type = STGTY_STREAM;
    if (s->kind == ST_FILE) {
        LARGE_INTEGER sz;
        if (GetFileSizeEx(s->file, &sz)) st->cbSize.QuadPart = (ULONGLONG)sz.QuadPart;
    } else st->cbSize.QuadPart = s->size;
    return s->kind == ST_NONE ? WINCODEC_ERR_NOTINITIALIZED : S_OK;
}
static HRESULT STDMETHODCALLTYPE st_clone(Stream *s, IStream **out)
{
    if (out) *out = 0;
    if (s->kind == ST_STREAM) return s->inner->lpVtbl->Clone(s->inner, out);
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE st_from_istream(Stream *s, IStream *inner)
{
    if (!inner) return E_INVALIDARG;
    if (s->kind) return WINCODEC_ERR_WRONGSTATE;
    inner->lpVtbl->AddRef(inner);
    s->inner = inner;
    s->kind = ST_STREAM;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE st_from_file(Stream *s, LPCWSTR name, DWORD access)
{
    if (!name) return E_INVALIDARG;
    if (s->kind) return WINCODEC_ERR_WRONGSTATE;
    BOOL wr = (access & GENERIC_WRITE) != 0;
    HANDLE h = CreateFileW(name, wr ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ, 0,
                           wr ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    s->file = h;
    s->kind = ST_FILE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE st_from_memory(Stream *s, BYTE *mem, DWORD size)
{
    if (!mem) return E_INVALIDARG;
    if (s->kind) return WINCODEC_ERR_WRONGSTATE;
    s->mem = mem; s->size = size; s->pos = 0;
    s->kind = ST_MEMORY;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE st_from_region(Stream *s, IStream *inner, ULARGE_INTEGER off, ULARGE_INTEGER max)
{
    if (!inner) return E_INVALIDARG;
    if (s->kind) return WINCODEC_ERR_WRONGSTATE;
    inner->lpVtbl->AddRef(inner);
    s->inner = inner; s->off = off.QuadPart; s->size = max.QuadPart; s->pos = 0;
    s->kind = ST_REGION;
    return S_OK;
}
static const void *const st_vtbl[] = {
    st_qi, st_addref, st_release, st_read, st_write, st_seek, st_setsize, st_copyto, st_commit, st_revert,
    st_lockregion, st_unlockregion, st_stat, st_clone, st_from_istream, st_from_file, st_from_memory, st_from_region,
};
/* a stream reading @file, which it closes */
HRESULT stream_on_handle(HANDLE file, IWICStream **out)
{
    HRESULT hr = stream_create(out);
    if (SUCCEEDED(hr)) { ((Stream *)*out)->file = file; ((Stream *)*out)->kind = ST_FILE; }
    return hr;
}
HRESULT stream_create(IWICStream **out)
{
    if (!out) return E_INVALIDARG;
    Stream *s = w_alloc(sizeof *s);
    if (!s) { *out = 0; return E_OUTOFMEMORY; }
    s->vtbl = st_vtbl;
    s->ref = 1;
    dll_addref();
    *out = (IWICStream *)s;
    return S_OK;
}

/* ================= colour context ================= */
typedef struct {
    const void *const *vtbl;
    LONG ref;
    WICColorContextType type;
    BYTE *profile;
    UINT size, exif;
} ColorContext;

static HRESULT STDMETHODCALLTYPE cc_qi(ColorContext *c, REFIID riid, void **out)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICColorContext)) {
        *out = c;
        InterlockedIncrement(&c->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cc_addref(ColorContext *c) { return (ULONG)InterlockedIncrement(&c->ref); }
static ULONG STDMETHODCALLTYPE cc_release(ColorContext *c)
{
    LONG r = InterlockedDecrement(&c->ref);
    if (!r) { w_free(c->profile); w_free(c); dll_release(); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE cc_from_memory(ColorContext *c, const BYTE *buf, UINT size)
{
    if (!buf || !size) return E_INVALIDARG;
    if (c->type) return WINCODEC_ERR_WRONGSTATE;
    if (!(c->profile = w_alloc(size))) return E_OUTOFMEMORY;
    memcpy(c->profile, buf, size);
    c->size = size;
    c->type = WICColorContextProfile;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cc_from_file(ColorContext *c, LPCWSTR name)
{
    if (!name) return E_INVALIDARG;
    HANDLE h = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER sz;
    HRESULT hr = E_FAIL;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (64 << 20)) {
        BYTE *b = w_alloc((SIZE_T)sz.QuadPart);
        DWORD got = 0;
        if (b && ReadFile(h, b, (DWORD)sz.QuadPart, &got, 0)) hr = cc_from_memory(c, b, got);
        w_free(b);
    }
    CloseHandle(h);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cc_from_exif(ColorContext *c, UINT v)
{
    if (c->type) return WINCODEC_ERR_WRONGSTATE;
    c->exif = v;
    c->type = WICColorContextExifColorSpace;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cc_type(ColorContext *c, WICColorContextType *t)
{
    if (!t) return E_INVALIDARG;
    *t = c->type;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cc_profile(ColorContext *c, UINT size, BYTE *buf, UINT *actual)
{
    if (!actual) return E_INVALIDARG;
    if (c->type != WICColorContextProfile) return WINCODEC_ERR_NOTINITIALIZED;
    *actual = c->size;
    if (buf && size >= c->size) memcpy(buf, c->profile, c->size);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cc_exif(ColorContext *c, UINT *v)
{
    if (!v) return E_INVALIDARG;
    *v = c->type == WICColorContextExifColorSpace ? c->exif : 0xFFFFFFFF;
    return S_OK;
}
static const void *const cc_vtbl[] = {
    cc_qi, cc_addref, cc_release, cc_from_file, cc_from_memory, cc_from_exif, cc_type, cc_profile, cc_exif,
};
HRESULT color_context_create(IWICColorContext **out)
{
    if (!out) return E_INVALIDARG;
    ColorContext *c = w_alloc(sizeof *c);
    if (!c) { *out = 0; return E_OUTOFMEMORY; }
    c->vtbl = cc_vtbl;
    c->ref = 1;
    dll_addref();
    *out = (IWICColorContext *)c;
    return S_OK;
}
