/*
 * windowscodecs.dll's private header: object helpers, the pixel format
 * table and the row converters every bitmap source shares.
 */
#pragma once
#define NOVA_WIC_BUILD
#include <wincodec.h>
#include <string.h>
#include <math.h>

#define COM_RELEASE(p) do { if (p) { ((IUnknown *)(p))->lpVtbl->Release((IUnknown *)(p)); (p) = 0; } } while (0)
#define COM_ADDREF(p)  ((IUnknown *)(p))->lpVtbl->AddRef((IUnknown *)(p))

void *w_alloc(SIZE_T n);                     /* zeroed */
void w_free(void *p);
void dll_addref(void);
void dll_release(void);

/* ---- pixel formats (convert.c) ---- */
enum { PK_INDEXED, PK_GRAY, PK_RGB, PK_BW, PK_SPECIAL };
typedef struct PixelFormat {
    const GUID *guid;
    const char *name;
    UINT bpp, channels;
    BYTE kind;               /* PK_* */
    BYTE rep;                /* WICPixelFormatNumericRepresentation */
    BYTE alpha;              /* has an alpha channel */
    BYTE premul;             /* alpha is premultiplied */
    BYTE comp;               /* component type: CT_* */
    char order[5];           /* channel order in memory, e.g. "BGRA" */
} PixelFormat;
enum { CT_U8 = 1, CT_U16, CT_F16, CT_F32, CT_FX16, CT_FX32, CT_U1, CT_U2, CT_U4,
       CT_555, CT_565, CT_5551, CT_101010, CT_CMYK8, CT_CMYK16, CT_RGBE, CT_NONE };

const PixelFormat *pf_find(REFGUID guid);
const PixelFormat *pf_at(UINT i);              /* 0.. until NULL */
BOOL pf_can_unpack(const PixelFormat *f);
BOOL pf_can_pack(const PixelFormat *f);
UINT pf_stride(const PixelFormat *f, UINT width);   /* tightly packed row, bytes */
HRESULT pf_channel_mask(const PixelFormat *f, UINT channel, UINT size, BYTE *mask, UINT *actual);

/* one pixel as four floats: straight (not premultiplied) R, G, B, A in
 * the sRGB-encoded 0..1 range of the integer formats */
typedef struct { float r, g, b, a; } Px;
void pf_unpack(const PixelFormat *f, const BYTE *row, UINT x0, UINT n, const WICColor *pal, UINT npal, Px *out);
void pf_pack(const PixelFormat *f, const Px *in, UINT n, BYTE *row, UINT x0, const WICColor *pal, UINT npal);
/* copies @n pixels of a row from bit/byte position @sx to @dx (same format) */
void pf_copy_pixels(UINT bpp, const BYTE *src, UINT sx, BYTE *dst, UINT dx, UINT n);

/* ---- shared bitmap-source helpers ---- */
HRESULT src_check_rect(const WICRect *rc, UINT w, UINT h, WICRect *out);
HRESULT src_copy_from_buffer(const BYTE *buf, UINT stride, UINT w, UINT h, const PixelFormat *f,
                             const WICRect *rc, UINT cbStride, UINT cbSize, BYTE *out);
/* the source's palette as an array (NULL when it has none) */
WICColor *src_palette(IWICBitmapSource *s, UINT *count);
HRESULT string_out(const WCHAR *s, UINT cch, WCHAR *buf, UINT *actual);

/* ---- objects ---- */
HRESULT palette_create(IWICPalette **out);
HRESULT palette_colors(IWICPalette *p, WICColor *colors, UINT max, UINT *count);
HRESULT palette_set(IWICPalette *p, const WICColor *colors, UINT count);
HRESULT stream_create(IWICStream **out);
HRESULT stream_on_handle(HANDLE file, IWICStream **out);
HRESULT color_context_create(IWICColorContext **out);
/* a bitmap over @mem (copied unless @view: a mapped section the bitmap unmaps) */
HRESULT bitmap_create(UINT w, UINT h, REFGUID fmt, UINT stride, const BYTE *mem, void *view, BYTE *viewdata,
                      IWICBitmap **out);
HRESULT bitmap_from_source(IWICBitmapSource *s, const WICRect *rc, IWICBitmap **out);
HRESULT bitmap_from_hbitmap(HBITMAP hbm, HPALETTE hpal, WICBitmapAlphaChannelOption opt, IWICBitmap **out);
HRESULT bitmap_from_hicon(HICON icon, IWICBitmap **out);
HRESULT converter_create(IWICFormatConverter **out);
HRESULT color_transform_create(IWICColorTransform **out);
HRESULT scaler_create(IWICBitmapScaler **out);
HRESULT clipper_create(IWICBitmapClipper **out);
HRESULT fliprotator_create(IWICBitmapFlipRotator **out);
HRESULT decoder_create(REFGUID container, IWICBitmapDecoder **out);
HRESULT decoder_from_stream(IStream *stream, const GUID *vendor, IWICBitmapDecoder **out);
HRESULT component_info_create(REFCLSID clsid, IWICComponentInfo **out);
HRESULT decoder_info_create(REFGUID container, IWICBitmapDecoderInfo **out);
HRESULT component_enum_create(DWORD types, IEnumUnknown **out);
HRESULT factory_get(REFIID riid, void **out);

/* decoders (decode.c) report their container formats here */
typedef struct CodecDesc {
    const GUID *clsid, *container;
    const WCHAR *name, *mimes, *exts;
    BOOL anim, multi, lossless;
    const BYTE *sig;
    UINT siglen;
} CodecDesc;
const CodecDesc *codec_at(UINT i);
const CodecDesc *codec_by_container(REFGUID g);
const CodecDesc *codec_by_clsid(REFGUID g);
