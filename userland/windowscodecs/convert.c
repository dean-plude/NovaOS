/*
 * windowscodecs.dll — pixel formats and the bitmap sources that transform
 * another source: the format converter (and the colour transform, which
 * NovaOS, without colour management, runs as one), the scaler, the
 * clipper and the flip-rotator.
 *
 * Every conversion runs through one row layout: four floats per pixel,
 * straight alpha, in the sRGB-encoded range of the integer formats.  The
 * floating and fixed point formats are linear (scRGB), as in Windows, so
 * they are converted through the sRGB curve on the way.
 */
#include "wic.h"

/* ---- the pixel format table ---- */
#define U  WICPixelFormatNumericRepresentationUnsignedInteger
#define IX WICPixelFormatNumericRepresentationIndexed
#define FL WICPixelFormatNumericRepresentationFloat
#define FX WICPixelFormatNumericRepresentationFixed
#define UN WICPixelFormatNumericRepresentationUnspecified
static const PixelFormat formats[] = {
    { &GUID_WICPixelFormatDontCare,       "DontCare",        0, 0, PK_SPECIAL, UN, 0, 0, CT_NONE, "" },
    { &GUID_WICPixelFormat1bppIndexed,    "1bppIndexed",     1, 1, PK_INDEXED, IX, 1, 0, CT_U1, "I" },
    { &GUID_WICPixelFormat2bppIndexed,    "2bppIndexed",     2, 1, PK_INDEXED, IX, 1, 0, CT_U2, "I" },
    { &GUID_WICPixelFormat4bppIndexed,    "4bppIndexed",     4, 1, PK_INDEXED, IX, 1, 0, CT_U4, "I" },
    { &GUID_WICPixelFormat8bppIndexed,    "8bppIndexed",     8, 1, PK_INDEXED, IX, 1, 0, CT_U8, "I" },
    { &GUID_WICPixelFormatBlackWhite,     "BlackWhite",      1, 1, PK_BW,      U,  0, 0, CT_U1, "L" },
    { &GUID_WICPixelFormat2bppGray,       "2bppGray",        2, 1, PK_GRAY,    U,  0, 0, CT_U2, "L" },
    { &GUID_WICPixelFormat4bppGray,       "4bppGray",        4, 1, PK_GRAY,    U,  0, 0, CT_U4, "L" },
    { &GUID_WICPixelFormat8bppGray,       "8bppGray",        8, 1, PK_GRAY,    U,  0, 0, CT_U8, "L" },
    { &GUID_WICPixelFormat8bppAlpha,      "8bppAlpha",       8, 1, PK_RGB,     U,  1, 0, CT_U8, "A" },
    { &GUID_WICPixelFormat16bppGray,      "16bppGray",      16, 1, PK_GRAY,    U,  0, 0, CT_U16, "L" },
    { &GUID_WICPixelFormat16bppGrayHalf,  "16bppGrayHalf",  16, 1, PK_GRAY,    FL, 0, 0, CT_F16, "L" },
    { &GUID_WICPixelFormat16bppGrayFixedPoint, "16bppGrayFixedPoint", 16, 1, PK_GRAY, FX, 0, 0, CT_FX16, "L" },
    { &GUID_WICPixelFormat32bppGrayFloat, "32bppGrayFloat", 32, 1, PK_GRAY,    FL, 0, 0, CT_F32, "L" },
    { &GUID_WICPixelFormat32bppGrayFixedPoint, "32bppGrayFixedPoint", 32, 1, PK_GRAY, FX, 0, 0, CT_FX32, "L" },
    { &GUID_WICPixelFormat16bppBGR555,    "16bppBGR555",    16, 3, PK_SPECIAL, U,  0, 0, CT_555, "BGR" },
    { &GUID_WICPixelFormat16bppBGR565,    "16bppBGR565",    16, 3, PK_SPECIAL, U,  0, 0, CT_565, "BGR" },
    { &GUID_WICPixelFormat16bppBGRA5551,  "16bppBGRA5551",  16, 4, PK_SPECIAL, U,  1, 0, CT_5551, "BGRA" },
    { &GUID_WICPixelFormat24bppBGR,       "24bppBGR",       24, 3, PK_RGB,     U,  0, 0, CT_U8, "BGR" },
    { &GUID_WICPixelFormat24bppRGB,       "24bppRGB",       24, 3, PK_RGB,     U,  0, 0, CT_U8, "RGB" },
    { &GUID_WICPixelFormat32bppBGR,       "32bppBGR",       32, 3, PK_RGB,     U,  0, 0, CT_U8, "BGRX" },
    { &GUID_WICPixelFormat32bppBGRA,      "32bppBGRA",      32, 4, PK_RGB,     U,  1, 0, CT_U8, "BGRA" },
    { &GUID_WICPixelFormat32bppPBGRA,     "32bppPBGRA",     32, 4, PK_RGB,     U,  1, 1, CT_U8, "BGRA" },
    { &GUID_WICPixelFormat32bppRGB,       "32bppRGB",       32, 3, PK_RGB,     U,  0, 0, CT_U8, "RGBX" },
    { &GUID_WICPixelFormat32bppRGBA,      "32bppRGBA",      32, 4, PK_RGB,     U,  1, 0, CT_U8, "RGBA" },
    { &GUID_WICPixelFormat32bppPRGBA,     "32bppPRGBA",     32, 4, PK_RGB,     U,  1, 1, CT_U8, "RGBA" },
    { &GUID_WICPixelFormat32bppBGR101010, "32bppBGR101010", 32, 3, PK_SPECIAL, U,  0, 0, CT_101010, "BGR" },
    { &GUID_WICPixelFormat32bppCMYK,      "32bppCMYK",      32, 4, PK_SPECIAL, U,  0, 0, CT_CMYK8, "CMYK" },
    { &GUID_WICPixelFormat32bppRGBE,      "32bppRGBE",      32, 4, PK_SPECIAL, FL, 0, 0, CT_RGBE, "RGBE" },
    { &GUID_WICPixelFormat48bppRGB,       "48bppRGB",       48, 3, PK_RGB,     U,  0, 0, CT_U16, "RGB" },
    { &GUID_WICPixelFormat48bppBGR,       "48bppBGR",       48, 3, PK_RGB,     U,  0, 0, CT_U16, "BGR" },
    { &GUID_WICPixelFormat48bppRGBFixedPoint, "48bppRGBFixedPoint", 48, 3, PK_RGB, FX, 0, 0, CT_FX16, "RGB" },
    { &GUID_WICPixelFormat48bppBGRFixedPoint, "48bppBGRFixedPoint", 48, 3, PK_RGB, FX, 0, 0, CT_FX16, "BGR" },
    { &GUID_WICPixelFormat48bppRGBHalf,   "48bppRGBHalf",   48, 3, PK_RGB,     FL, 0, 0, CT_F16, "RGB" },
    { &GUID_WICPixelFormat64bppRGB,       "64bppRGB",       64, 3, PK_RGB,     U,  0, 0, CT_U16, "RGBX" },
    { &GUID_WICPixelFormat64bppRGBA,      "64bppRGBA",      64, 4, PK_RGB,     U,  1, 0, CT_U16, "RGBA" },
    { &GUID_WICPixelFormat64bppBGRA,      "64bppBGRA",      64, 4, PK_RGB,     U,  1, 0, CT_U16, "BGRA" },
    { &GUID_WICPixelFormat64bppPRGBA,     "64bppPRGBA",     64, 4, PK_RGB,     U,  1, 1, CT_U16, "RGBA" },
    { &GUID_WICPixelFormat64bppPBGRA,     "64bppPBGRA",     64, 4, PK_RGB,     U,  1, 1, CT_U16, "BGRA" },
    { &GUID_WICPixelFormat64bppRGBFixedPoint,  "64bppRGBFixedPoint",  64, 3, PK_RGB, FX, 0, 0, CT_FX16, "RGBX" },
    { &GUID_WICPixelFormat64bppRGBAFixedPoint, "64bppRGBAFixedPoint", 64, 4, PK_RGB, FX, 1, 0, CT_FX16, "RGBA" },
    { &GUID_WICPixelFormat64bppBGRAFixedPoint, "64bppBGRAFixedPoint", 64, 4, PK_RGB, FX, 1, 0, CT_FX16, "BGRA" },
    { &GUID_WICPixelFormat64bppRGBHalf,   "64bppRGBHalf",   64, 3, PK_RGB,     FL, 0, 0, CT_F16, "RGBX" },
    { &GUID_WICPixelFormat64bppRGBAHalf,  "64bppRGBAHalf",  64, 4, PK_RGB,     FL, 1, 0, CT_F16, "RGBA" },
    { &GUID_WICPixelFormat64bppPRGBAHalf, "64bppPRGBAHalf", 64, 4, PK_RGB,     FL, 1, 1, CT_F16, "RGBA" },
    { &GUID_WICPixelFormat64bppCMYK,      "64bppCMYK",      64, 4, PK_SPECIAL, U,  0, 0, CT_CMYK16, "CMYK" },
    { &GUID_WICPixelFormat96bppRGBFixedPoint,  "96bppRGBFixedPoint",  96, 3, PK_RGB, FX, 0, 0, CT_FX32, "RGB" },
    { &GUID_WICPixelFormat96bppRGBFloat,  "96bppRGBFloat",  96, 3, PK_RGB,     FL, 0, 0, CT_F32, "RGB" },
    { &GUID_WICPixelFormat128bppRGBFixedPoint,  "128bppRGBFixedPoint",  128, 3, PK_RGB, FX, 0, 0, CT_FX32, "RGBX" },
    { &GUID_WICPixelFormat128bppRGBAFixedPoint, "128bppRGBAFixedPoint", 128, 4, PK_RGB, FX, 1, 0, CT_FX32, "RGBA" },
    { &GUID_WICPixelFormat128bppRGBFloat,  "128bppRGBFloat",  128, 3, PK_RGB,  FL, 0, 0, CT_F32, "RGBX" },
    { &GUID_WICPixelFormat128bppRGBAFloat, "128bppRGBAFloat", 128, 4, PK_RGB,  FL, 1, 0, CT_F32, "RGBA" },
    { &GUID_WICPixelFormat128bppPRGBAFloat,"128bppPRGBAFloat",128, 4, PK_RGB,  FL, 1, 1, CT_F32, "RGBA" },
};
#define NFORMATS (sizeof formats / sizeof formats[0])

const PixelFormat *pf_find(REFGUID g)
{
    for (UINT i = 0; i < NFORMATS; i++)
        if (IsEqualGUID(formats[i].guid, g)) return &formats[i];
    return 0;
}
const PixelFormat *pf_at(UINT i) { return i < NFORMATS ? &formats[i] : 0; }
BOOL pf_can_unpack(const PixelFormat *f) { return f && f->comp != CT_NONE; }
BOOL pf_can_pack(const PixelFormat *f) { return f && f->comp != CT_NONE && f->comp != CT_RGBE; }
UINT pf_stride(const PixelFormat *f, UINT w) { return (UINT)(((UINT64)w * f->bpp + 7) / 8); }

/* the bits a channel occupies in one pixel, little-endian bytes */
HRESULT pf_channel_mask(const PixelFormat *f, UINT ch, UINT size, BYTE *mask, UINT *actual)
{
    if (ch >= f->channels) return E_INVALIDARG;
    UINT bytes = (f->bpp + 7) / 8;
    if (actual) *actual = bytes;
    if (!mask) return actual ? S_OK : E_INVALIDARG;
    if (size < bytes) return E_INVALIDARG;
    memset(mask, 0, bytes);
    UINT lo = 0, n = 0;                       /* bit range of the channel */
    switch (f->comp) {
    case CT_555:  lo = (2 - ch) * 5; n = 5; if (f->order[ch] == 'B') lo = 0; else if (f->order[ch] == 'G') lo = 5; else lo = 10; break;
    case CT_565:  if (f->order[ch] == 'B') { lo = 0; n = 5; } else if (f->order[ch] == 'G') { lo = 5; n = 6; } else { lo = 11; n = 5; } break;
    case CT_5551: if (ch == 3) { lo = 15; n = 1; } else { lo = ch * 5; n = 5; } break;
    case CT_101010: lo = ch * 10; n = 10; break;
    default:
        if (f->channels == 1) { lo = 0; n = f->bpp; }
        else { n = f->bpp / (UINT)strlen(f->order); lo = ch * n; }
        break;
    }
    for (UINT b = lo; b < lo + n && b / 8 < bytes; b++) mask[b / 8] |= (BYTE)(1u << (b % 8));
    return S_OK;
}

/* ---- scalar helpers ---- */
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float to_linear(float v)
{
    if (v <= 0.04045f) return v / 12.92f;
    return (float)pow((v + 0.055) / 1.055, 2.4);
}
static float to_srgb(float v)
{
    if (v <= 0) return 0;
    if (v <= 0.0031308f) return v * 12.92f;
    return (float)(1.055 * pow(v, 1 / 2.4) - 0.055);
}
static float half_to_float(WORD h)
{
    UINT s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (!e) v = (float)m / 16777216.0f;                        /* 2^-24 */
    else if (e == 31) v = m ? 0 : 65504.0f;
    else v = (float)ldexp(1.0 + m / 1024.0, (int)e - 15);
    return s ? -v : v;
}
static WORD float_to_half(float f)
{
    union { float f; UINT u; } c = { f };
    UINT s = (c.u >> 16) & 0x8000;
    int e = (int)((c.u >> 23) & 255) - 127 + 15;
    UINT m = c.u & 0x7FFFFF;
    if (((c.u >> 23) & 255) == 255) return (WORD)(s | 0x7C00 | (m ? 0x200 : 0));
    if (e >= 31) return (WORD)(s | 0x7BFF);
    if (e <= 0) {
        if (e < -10) return (WORD)s;
        m |= 0x800000;
        return (WORD)(s | ((m >> (14 - e)) + ((m >> (13 - e)) & 1)));
    }
    UINT r = s | ((UINT)e << 10) | (m >> 13);
    if (m & 0x1000) r++;                                       /* round */
    return (WORD)r;
}
static WORD rd16(const BYTE *p) { return (WORD)(p[0] | p[1] << 8); }
static void wr16(BYTE *p, UINT v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
static UINT rd32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT)p[3] << 24; }
static void wr32(BYTE *p, UINT v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); p[3] = (BYTE)(v >> 24); }
static BYTE q8(float v) { return (BYTE)(clamp01(v) * 255.0f + 0.5f); }
static WORD q16(float v) { return (WORD)(clamp01(v) * 65535.0f + 0.5f); }

static UINT getbits(const BYTE *row, UINT x, UINT bpp)
{
    UINT bit = x * bpp;
    return (row[bit / 8] >> (8 - bpp - bit % 8)) & ((1u << bpp) - 1);
}
static void setbits(BYTE *row, UINT x, UINT bpp, UINT v)
{
    UINT bit = x * bpp, sh = 8 - bpp - bit % 8, m = ((1u << bpp) - 1) << sh;
    row[bit / 8] = (BYTE)((row[bit / 8] & ~m) | ((v << sh) & m));
}

void pf_copy_pixels(UINT bpp, const BYTE *src, UINT sx, BYTE *dst, UINT dx, UINT n)
{
    if (bpp % 8 == 0) { memmove(dst + (SIZE_T)dx * (bpp / 8), src + (SIZE_T)sx * (bpp / 8), (SIZE_T)n * (bpp / 8)); return; }
    for (UINT i = 0; i < n; i++) setbits(dst, dx + i, bpp, getbits(src, sx + i, bpp));
}

static float luma(const Px *p) { return 0.299f * p->r + 0.587f * p->g + 0.114f * p->b; }

/* a channel value from memory, as a float in the format's own range */
static float rd_comp(BYTE ct, const BYTE *p)
{
    switch (ct) {
    case CT_U8:   return p[0] / 255.0f;
    case CT_U16:  return rd16(p) / 65535.0f;
    case CT_F16:  return half_to_float(rd16(p));
    case CT_F32:  { union { UINT u; float f; } c; c.u = rd32(p); return c.f; }
    case CT_FX16: return (SHORT)rd16(p) / 8192.0f;
    case CT_FX32: return (INT)rd32(p) / 16777216.0f;
    }
    return 0;
}
static void wr_comp(BYTE ct, BYTE *p, float v)
{
    switch (ct) {
    case CT_U8:   p[0] = q8(v); break;
    case CT_U16:  wr16(p, q16(v)); break;
    case CT_F16:  wr16(p, float_to_half(v)); break;
    case CT_F32:  { union { UINT u; float f; } c; c.f = v; wr32(p, c.u); break; }
    case CT_FX16: { float s = v * 8192.0f; if (s > 32767) s = 32767; if (s < -32768) s = -32768;
                    wr16(p, (WORD)(SHORT)(s < 0 ? s - 0.5f : s + 0.5f)); break; }
    case CT_FX32: { double s = v * 16777216.0; if (s > 2147483647.0) s = 2147483647.0; if (s < -2147483648.0) s = -2147483648.0;
                    wr32(p, (UINT)(INT)(s < 0 ? s - 0.5 : s + 0.5)); break; }
    }
}
static UINT comp_bytes(BYTE ct) { return ct == CT_U8 ? 1 : ct == CT_F32 || ct == CT_FX32 ? 4 : 2; }
static BOOL linear_rep(const PixelFormat *f) { return f->rep == FL || f->rep == FX; }

static Px pal_px(const WICColor *pal, UINT npal, UINT i)
{
    Px p = { 0, 0, 0, 1 };
    if (!pal || i >= npal) return p;
    WICColor c = pal[i];
    p.r = ((c >> 16) & 255) / 255.0f; p.g = ((c >> 8) & 255) / 255.0f;
    p.b = (c & 255) / 255.0f; p.a = (c >> 24) / 255.0f;
    return p;
}

void pf_unpack(const PixelFormat *f, const BYTE *row, UINT x0, UINT n, const WICColor *pal, UINT npal, Px *out)
{
    UINT bpp = f->bpp;
    if (f->kind == PK_INDEXED) {
        for (UINT i = 0; i < n; i++) out[i] = pal_px(pal, npal, bpp == 8 ? row[x0 + i] : getbits(row, x0 + i, bpp));
        return;
    }
    if (f->kind == PK_BW || (f->kind == PK_GRAY && bpp < 8)) {
        UINT max = (1u << bpp) - 1;
        for (UINT i = 0; i < n; i++) {
            float v = getbits(row, x0 + i, bpp) / (float)max;
            out[i].r = out[i].g = out[i].b = v; out[i].a = 1;
        }
        return;
    }
    UINT B = bpp / 8;
    for (UINT i = 0; i < n; i++) {
        const BYTE *p = row + (SIZE_T)(x0 + i) * B;
        Px o = { 0, 0, 0, 1 };
        switch (f->comp) {
        case CT_555:  { UINT v = rd16(p); o.b = (v & 31) / 31.0f; o.g = ((v >> 5) & 31) / 31.0f; o.r = ((v >> 10) & 31) / 31.0f; break; }
        case CT_565:  { UINT v = rd16(p); o.b = (v & 31) / 31.0f; o.g = ((v >> 5) & 63) / 63.0f; o.r = ((v >> 11) & 31) / 31.0f; break; }
        case CT_5551: { UINT v = rd16(p); o.b = (v & 31) / 31.0f; o.g = ((v >> 5) & 31) / 31.0f; o.r = ((v >> 10) & 31) / 31.0f;
                        o.a = (v >> 15) ? 1.0f : 0.0f; break; }
        case CT_101010: { UINT v = rd32(p); o.b = (v & 1023) / 1023.0f; o.g = ((v >> 10) & 1023) / 1023.0f;
                          o.r = ((v >> 20) & 1023) / 1023.0f; break; }
        case CT_CMYK8: case CT_CMYK16: {
            float c = rd_comp(f->comp == CT_CMYK8 ? CT_U8 : CT_U16, p), m = rd_comp(f->comp == CT_CMYK8 ? CT_U8 : CT_U16, p + B / 4);
            float y = rd_comp(f->comp == CT_CMYK8 ? CT_U8 : CT_U16, p + B / 2), k = rd_comp(f->comp == CT_CMYK8 ? CT_U8 : CT_U16, p + 3 * B / 4);
            o.r = (1 - c) * (1 - k); o.g = (1 - m) * (1 - k); o.b = (1 - y) * (1 - k);
            break;
        }
        case CT_RGBE: {
            float s = p[3] ? (float)ldexp(1.0, p[3] - 136) : 0;
            o.r = to_srgb(p[0] * s); o.g = to_srgb(p[1] * s); o.b = to_srgb(p[2] * s);
            break;
        }
        default: {
            UINT cb = comp_bytes(f->comp);
            for (UINT c = 0; f->order[c]; c++) {
                float v = rd_comp(f->comp, p + c * cb);
                switch (f->order[c]) {
                case 'R': o.r = v; break;
                case 'G': o.g = v; break;
                case 'B': o.b = v; break;
                case 'A': o.a = v; break;
                case 'L': o.r = o.g = o.b = v; break;
                }
            }
            if (f->order[0] == 'A' && !f->order[1]) o.r = o.g = o.b = 0;      /* 8bppAlpha */
            if (f->premul) {
                if (o.a > 0) { o.r /= o.a; o.g /= o.a; o.b /= o.a; }
                else o.r = o.g = o.b = 0;
            }
            if (linear_rep(f)) { o.r = to_srgb(o.r); o.g = to_srgb(o.g); o.b = to_srgb(o.b); }
            break;
        }
        }
        out[i] = o;
    }
}

static UINT nearest_index(const Px *p, const WICColor *pal, UINT npal)
{
    UINT best = 0;
    float bd = 1e30f;
    for (UINT i = 0; i < npal; i++) {
        Px q = pal_px(pal, npal, i);
        float dr = q.r - p->r, dg = q.g - p->g, db = q.b - p->b, da = q.a - p->a;
        float d = dr * dr + dg * dg + db * db + da * da;
        if (d < bd) { bd = d; best = i; if (d == 0) break; }
    }
    return best;
}

void pf_pack(const PixelFormat *f, const Px *in, UINT n, BYTE *row, UINT x0, const WICColor *pal, UINT npal)
{
    UINT bpp = f->bpp;
    if (f->kind == PK_INDEXED) {
        for (UINT i = 0; i < n; i++) {
            UINT idx = npal ? nearest_index(&in[i], pal, npal) : 0;
            if (bpp == 8) row[x0 + i] = (BYTE)idx; else setbits(row, x0 + i, bpp, idx);
        }
        return;
    }
    if (f->kind == PK_BW || (f->kind == PK_GRAY && bpp < 8)) {
        UINT max = (1u << bpp) - 1;
        for (UINT i = 0; i < n; i++) setbits(row, x0 + i, bpp, (UINT)(clamp01(luma(&in[i])) * max + 0.5f));
        return;
    }
    UINT B = bpp / 8;
    for (UINT i = 0; i < n; i++) {
        BYTE *p = row + (SIZE_T)(x0 + i) * B;
        Px o = in[i];
        switch (f->comp) {
        case CT_555:  wr16(p, (UINT)(clamp01(o.b) * 31 + .5f) | (UINT)(clamp01(o.g) * 31 + .5f) << 5 | (UINT)(clamp01(o.r) * 31 + .5f) << 10); break;
        case CT_565:  wr16(p, (UINT)(clamp01(o.b) * 31 + .5f) | (UINT)(clamp01(o.g) * 63 + .5f) << 5 | (UINT)(clamp01(o.r) * 31 + .5f) << 11); break;
        case CT_5551: wr16(p, (UINT)(clamp01(o.b) * 31 + .5f) | (UINT)(clamp01(o.g) * 31 + .5f) << 5 | (UINT)(clamp01(o.r) * 31 + .5f) << 10
                              | (o.a >= 0.5f ? 0x8000u : 0)); break;
        case CT_101010: wr32(p, (UINT)(clamp01(o.b) * 1023 + .5f) | (UINT)(clamp01(o.g) * 1023 + .5f) << 10
                                | (UINT)(clamp01(o.r) * 1023 + .5f) << 20); break;
        case CT_CMYK8: case CT_CMYK16: {
            float r = clamp01(o.r), g = clamp01(o.g), b = clamp01(o.b);
            float mx = r > g ? (r > b ? r : b) : (g > b ? g : b), k = 1 - mx;
            float c = mx > 0 ? (mx - r) / mx : 0, m = mx > 0 ? (mx - g) / mx : 0, y = mx > 0 ? (mx - b) / mx : 0;
            BYTE ct = f->comp == CT_CMYK8 ? CT_U8 : CT_U16;
            wr_comp(ct, p, c); wr_comp(ct, p + B / 4, m); wr_comp(ct, p + B / 2, y); wr_comp(ct, p + 3 * B / 4, k);
            break;
        }
        default: {
            UINT cb = comp_bytes(f->comp);
            float r = o.r, g = o.g, b = o.b, a = clamp01(o.a);
            if (linear_rep(f)) { r = to_linear(clamp01(r)); g = to_linear(clamp01(g)); b = to_linear(clamp01(b)); }
            if (f->premul) { r *= a; g *= a; b *= a; }
            float l = luma(&o);
            if (linear_rep(f)) l = to_linear(clamp01(l));
            for (UINT c = 0; f->order[c]; c++) {
                float v;
                switch (f->order[c]) {
                case 'R': v = r; break;
                case 'G': v = g; break;
                case 'B': v = b; break;
                case 'A': v = a; break;
                case 'L': v = l; break;
                default:  v = f->comp == CT_U8 || f->comp == CT_U16 ? 1.0f : 0.0f; break;    /* X: opaque */
                }
                wr_comp(f->comp, p + c * cb, v);
            }
            break;
        }
        }
    }
}

/* ---- bitmap source helpers ---- */
HRESULT src_check_rect(const WICRect *rc, UINT w, UINT h, WICRect *out)
{
    if (!rc) { out->X = 0; out->Y = 0; out->Width = (INT)w; out->Height = (INT)h; return S_OK; }
    if (rc->X < 0 || rc->Y < 0 || rc->Width < 0 || rc->Height < 0 ||
        (UINT)rc->X + (UINT)rc->Width > w || (UINT)rc->Y + (UINT)rc->Height > h) return E_INVALIDARG;
    *out = *rc;
    return S_OK;
}

HRESULT src_copy_from_buffer(const BYTE *buf, UINT stride, UINT w, UINT h, const PixelFormat *f,
                             const WICRect *rc, UINT cbStride, UINT cbSize, BYTE *out)
{
    WICRect r;
    if (!out) return E_INVALIDARG;
    HRESULT hr = src_check_rect(rc, w, h, &r);
    if (FAILED(hr)) return hr;
    if (!r.Width || !r.Height) return S_OK;
    UINT row = pf_stride(f, (UINT)r.Width);
    if (cbStride < row) return E_INVALIDARG;
    if ((UINT64)cbStride * (UINT)(r.Height - 1) + row > cbSize) return E_INVALIDARG;
    for (INT y = 0; y < r.Height; y++) {
        const BYTE *s = buf + (SIZE_T)(r.Y + y) * stride;
        BYTE *d = out + (SIZE_T)y * cbStride;
        if (f->bpp % 8 == 0) memcpy(d, s + (SIZE_T)r.X * (f->bpp / 8), row);
        else { memset(d, 0, row); pf_copy_pixels(f->bpp, s, (UINT)r.X, d, 0, (UINT)r.Width); }
    }
    return S_OK;
}

WICColor *src_palette(IWICBitmapSource *s, UINT *count)
{
    *count = 0;
    IWICPalette *p;
    if (FAILED(palette_create(&p))) return 0;
    WICColor *c = 0;
    if (SUCCEEDED(s->lpVtbl->CopyPalette(s, p))) {
        UINT n = 0;
        p->lpVtbl->GetColorCount(p, &n);
        if (n && (c = w_alloc(n * sizeof *c)) && SUCCEEDED(p->lpVtbl->GetColors(p, n, c, &n))) *count = n;
    }
    p->lpVtbl->Release(p);
    if (!*count) { w_free(c); c = 0; }
    return c;
}

/* reads rows [y, y+n) of @s's rect @x,@w into @buf (tight rows) */
static HRESULT read_rows(IWICBitmapSource *s, const PixelFormat *f, INT x, INT y, INT w, INT n, BYTE *buf)
{
    WICRect r = { x, y, w, n };
    UINT row = pf_stride(f, (UINT)w);
    return s->lpVtbl->CopyPixels(s, &r, row, row * (UINT)n, buf);
}

/* ---- common object code ---- */
typedef struct {
    const void *const *vtbl;
    LONG ref;
} ObjHead;

static HRESULT qi_source(void *This, REFIID riid, void **out, const IID *own)
{
    if (!out) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWICBitmapSource) || IsEqualIID(riid, own)) {
        *out = This;
        InterlockedIncrement(&((ObjHead *)This)->ref);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}

/* ================= format converter / colour transform ================= */
typedef struct {
    const void *const *vtbl;
    LONG ref;
    CRITICAL_SECTION lock;
    IWICBitmapSource *src;
    const PixelFormat *sf, *df;
    WICColor *spal, *dpal;
    UINT nspal, ndpal;
    BOOL transform;                   /* IWICColorTransform */
} Converter;

static HRESULT STDMETHODCALLTYPE cv_qi(Converter *c, REFIID riid, void **out)
{
    return qi_source(c, riid, out, c->transform ? &IID_IWICColorTransform : &IID_IWICFormatConverter);
}
static ULONG STDMETHODCALLTYPE cv_addref(Converter *c) { return (ULONG)InterlockedIncrement(&c->ref); }
static ULONG STDMETHODCALLTYPE cv_release(Converter *c)
{
    LONG r = InterlockedDecrement(&c->ref);
    if (!r) {
        COM_RELEASE(c->src);
        w_free(c->spal); w_free(c->dpal);
        DeleteCriticalSection(&c->lock);
        w_free(c);
        dll_release();
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE cv_size(Converter *c, UINT *w, UINT *h)
{
    if (!w || !h) return E_INVALIDARG;
    if (!c->src) return WINCODEC_ERR_NOTINITIALIZED;
    return c->src->lpVtbl->GetSize(c->src, w, h);
}
static HRESULT STDMETHODCALLTYPE cv_format(Converter *c, GUID *f)
{
    if (!f) return E_INVALIDARG;
    if (!c->src) { *f = GUID_WICPixelFormatDontCare; return WINCODEC_ERR_NOTINITIALIZED; }
    *f = *c->df->guid;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cv_res(Converter *c, double *x, double *y)
{
    if (!c->src) return WINCODEC_ERR_NOTINITIALIZED;
    return c->src->lpVtbl->GetResolution(c->src, x, y);
}
static HRESULT STDMETHODCALLTYPE cv_palette(Converter *c, IWICPalette *p)
{
    if (!p) return E_INVALIDARG;
    if (!c->src) return WINCODEC_ERR_NOTINITIALIZED;
    if (c->df->kind != PK_INDEXED || !c->ndpal) return WINCODEC_ERR_PALETTEUNAVAILABLE;
    return palette_set(p, c->dpal, c->ndpal);
}

/* the fast row conversions between the 8-bit RGB formats */
static BOOL cv_fast(const PixelFormat *s, const PixelFormat *d, const BYTE *in, BYTE *out, UINT n)
{
    if (s->comp != CT_U8 || d->comp != CT_U8 || s->kind != PK_RGB || d->kind != PK_RGB) return FALSE;
    if (s->bpp < 24 || d->bpp < 24) return FALSE;
    int si[4], di[4];                      /* byte offset of R, G, B, A (-1: none) */
    for (int k = 0; k < 4; k++) {
        const char *ch = "RGBA";
        const char *p = strchr(s->order, ch[k]); si[k] = p ? (int)(p - s->order) : -1;
        p = strchr(d->order, ch[k]); di[k] = p ? (int)(p - d->order) : -1;
    }
    int dx = -1;                           /* X byte */
    { const char *p = strchr(d->order, 'X'); if (p) dx = (int)(p - d->order); }
    UINT sb = s->bpp / 8, db = d->bpp / 8;
    BOOL sp = s->premul && si[3] >= 0, dp = d->premul && di[3] >= 0;
    for (UINT i = 0; i < n; i++, in += sb, out += db) {
        UINT a = si[3] >= 0 ? in[si[3]] : 255;
        UINT r = in[si[0]], g = in[si[1]], b = in[si[2]];
        if (sp && !dp && di[3] >= 0) {                 /* unpremultiply */
            if (a) { r = (r * 255 + a / 2) / a; g = (g * 255 + a / 2) / a; b = (b * 255 + a / 2) / a;
                     if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255; }
            else r = g = b = 0;
        } else if (sp && di[3] < 0) {
            /* premultiplied to opaque: keep the stored (composited on black) colour */
        } else if (!sp && dp) { r = (r * a + 127) / 255; g = (g * a + 127) / 255; b = (b * a + 127) / 255; }
        out[di[0]] = (BYTE)r; out[di[1]] = (BYTE)g; out[di[2]] = (BYTE)b;
        if (di[3] >= 0) out[di[3]] = (BYTE)a;
        if (dx >= 0) out[dx] = 255;
    }
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE cv_copy(Converter *c, const WICRect *rc, UINT stride, UINT size, BYTE *buf)
{
    if (!c->src) return WINCODEC_ERR_NOTINITIALIZED;
    UINT w, h;
    HRESULT hr = c->src->lpVtbl->GetSize(c->src, &w, &h);
    if (FAILED(hr)) return hr;
    WICRect r;
    if (!buf) return E_INVALIDARG;
    if (FAILED(hr = src_check_rect(rc, w, h, &r))) return hr;
    if (!r.Width || !r.Height) return S_OK;
    UINT drow = pf_stride(c->df, (UINT)r.Width);
    if (stride < drow || (UINT64)stride * (UINT)(r.Height - 1) + drow > size) return E_INVALIDARG;
    if (c->sf == c->df) return c->src->lpVtbl->CopyPixels(c->src, &r, stride, size, buf);
    UINT srow = pf_stride(c->sf, (UINT)r.Width);
    INT band = 64;
    if ((UINT64)srow * band > (8u << 20)) band = (INT)((8u << 20) / srow) + 1;
    BYTE *tmp = w_alloc((SIZE_T)srow * band);
    Px *px = w_alloc((SIZE_T)r.Width * sizeof(Px));
    if (!tmp || !px) { w_free(tmp); w_free(px); return E_OUTOFMEMORY; }
    EnterCriticalSection(&c->lock);
    for (INT y = 0; y < r.Height && SUCCEEDED(hr); y += band) {
        INT n = r.Height - y < band ? r.Height - y : band;
        hr = read_rows(c->src, c->sf, r.X, r.Y + y, r.Width, n, tmp);
        for (INT k = 0; k < n && SUCCEEDED(hr); k++) {
            BYTE *d = buf + (SIZE_T)(y + k) * stride;
            const BYTE *s = tmp + (SIZE_T)k * srow;
            if (cv_fast(c->sf, c->df, s, d, (UINT)r.Width)) continue;
            if (c->df->bpp % 8) memset(d, 0, drow);
            pf_unpack(c->sf, s, 0, (UINT)r.Width, c->spal, c->nspal, px);
            pf_pack(c->df, px, (UINT)r.Width, d, 0, c->dpal, c->ndpal);
        }
    }
    LeaveCriticalSection(&c->lock);
    w_free(tmp); w_free(px);
    return hr;
}

static HRESULT cv_init(Converter *c, IWICBitmapSource *src, REFGUID fmt, IWICPalette *pal, WICBitmapPaletteType translate)
{
    if (!src || !fmt) return E_INVALIDARG;
    if (c->src) return WINCODEC_ERR_WRONGSTATE;
    GUID sg;
    HRESULT hr = src->lpVtbl->GetPixelFormat(src, &sg);
    if (FAILED(hr)) return hr;
    const PixelFormat *sf = pf_find(&sg), *df = pf_find(fmt);
    if (!sf || !df) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (sf != df && (!pf_can_unpack(sf) || !pf_can_pack(df))) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    UINT nsp = 0, ndp = 0;
    WICColor *sp = 0, *dp = 0;
    if (sf->kind == PK_INDEXED) {
        sp = src_palette(src, &nsp);
        if (!sp) return WINCODEC_ERR_PALETTEUNAVAILABLE;
    }
    if (df->kind == PK_INDEXED && sf != df) {
        IWICPalette *tp = 0;
        if (!pal && translate != WICBitmapPaletteTypeCustom && SUCCEEDED(palette_create(&tp))) {
            if (FAILED(tp->lpVtbl->InitializePredefined(tp, translate, FALSE))) COM_RELEASE(tp);
            pal = tp;
        }
        if (pal) {
            UINT n = 0;
            pal->lpVtbl->GetColorCount(pal, &n);
            if (n && (dp = w_alloc(n * sizeof *dp))) pal->lpVtbl->GetColors(pal, n, dp, &ndp);
        } else if (sp) {
            dp = w_alloc(nsp * sizeof *dp);
            if (dp) { memcpy(dp, sp, nsp * sizeof *dp); ndp = nsp; }
        }
        COM_RELEASE(tp);
        if (!ndp) { w_free(sp); w_free(dp); return E_INVALIDARG; }
        UINT cap = 1u << df->bpp;
        if (ndp > cap) ndp = cap;
    }
    src->lpVtbl->AddRef(src);
    c->src = src; c->sf = sf; c->df = df;
    c->spal = sp; c->nspal = nsp; c->dpal = dp; c->ndpal = ndp;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cv_initialize(Converter *c, IWICBitmapSource *src, REFGUID fmt, WICBitmapDitherType dither,
                                               IWICPalette *pal, double alpha, WICBitmapPaletteType translate)
{
    (void)dither; (void)alpha;
    return cv_init(c, src, fmt, pal, translate);
}
static HRESULT STDMETHODCALLTYPE cv_can_convert(Converter *c, REFGUID from, REFGUID to, BOOL *can)
{
    (void)c;
    if (!from || !to || !can) return E_INVALIDARG;
    const PixelFormat *sf = pf_find(from), *df = pf_find(to);
    *can = sf && df && (sf == df || (pf_can_unpack(sf) && pf_can_pack(df)));
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ct_initialize(Converter *c, IWICBitmapSource *src, IWICColorContext *from,
                                               IWICColorContext *to, REFGUID fmt)
{
    (void)from; (void)to;                     /* no colour management: the colours pass unchanged */
    return cv_init(c, src, fmt, 0, WICBitmapPaletteTypeFixedHalftone256);
}

static const void *const cv_vtbl[] = {
    cv_qi, cv_addref, cv_release, cv_size, cv_format, cv_res, cv_palette, cv_copy, cv_initialize, cv_can_convert,
};
static const void *const ct_vtbl[] = {
    cv_qi, cv_addref, cv_release, cv_size, cv_format, cv_res, cv_palette, cv_copy, ct_initialize,
};

static HRESULT cv_new(BOOL transform, void **out)
{
    if (!out) return E_INVALIDARG;
    Converter *c = w_alloc(sizeof *c);
    if (!c) { *out = 0; return E_OUTOFMEMORY; }
    c->vtbl = transform ? ct_vtbl : cv_vtbl;
    c->ref = 1;
    c->transform = transform;
    InitializeCriticalSection(&c->lock);
    dll_addref();
    *out = c;
    return S_OK;
}
HRESULT converter_create(IWICFormatConverter **out) { return cv_new(FALSE, (void **)out); }
HRESULT color_transform_create(IWICColorTransform **out) { return cv_new(TRUE, (void **)out); }

__declspec(dllexport) HRESULT WINAPI WICConvertBitmapSource(REFGUID fmt, IWICBitmapSource *src, IWICBitmapSource **out)
{
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!fmt || !src) return E_INVALIDARG;
    GUID g;
    HRESULT hr = src->lpVtbl->GetPixelFormat(src, &g);
    if (FAILED(hr)) return hr;
    if (IsEqualGUID(&g, fmt)) return src->lpVtbl->QueryInterface(src, &IID_IWICBitmapSource, (void **)out);
    IWICFormatConverter *c;
    if (FAILED(hr = converter_create(&c))) return hr;
    hr = c->lpVtbl->Initialize(c, src, fmt, WICBitmapDitherTypeNone, 0, 0.0, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) { c->lpVtbl->Release(c); return hr; }
    *out = (IWICBitmapSource *)c;
    return S_OK;
}

/* ================= scaler ================= */
typedef struct { INT start, n; float *w; } Taps;
typedef struct {
    const void *const *vtbl;
    LONG ref;
    CRITICAL_SECTION lock;
    IWICBitmapSource *src;
    const PixelFormat *sf, *df;
    WICColor *pal; UINT npal;
    UINT sw, sh, dw, dh;
    WICBitmapInterpolationMode mode;
    Taps *tx, *ty;
    /* horizontally resampled source rows (premultiplied), keyed by source y */
    Px **rows; INT *rowy; UINT nrows, next;
    BYTE *srow;
    Px *sbuf;
} Scaler;

static void taps_free(Taps *t, UINT n) { if (t) { for (UINT i = 0; i < n; i++) w_free(t[i].w); w_free(t); } }
static Taps *taps_make(UINT src, UINT dst, WICBitmapInterpolationMode mode, UINT *maxn)
{
    Taps *t = w_alloc(dst * sizeof *t);
    if (!t) return 0;
    double scale = (double)src / dst;
    *maxn = 1;
    for (UINT i = 0; i < dst; i++) {
        if (mode == WICBitmapInterpolationModeNearestNeighbor) {
            UINT s = (UINT)((i + 0.5) * scale);
            if (s >= src) s = src - 1;
            t[i].start = (INT)s; t[i].n = 1;
            t[i].w = w_alloc(sizeof(float));
            if (!t[i].w) goto fail;
            t[i].w[0] = 1;
        } else if (scale > 1.0 && mode != WICBitmapInterpolationModeLinear) {
            /* area average (Fant, cubic, high-quality cubic when shrinking) */
            double a = i * scale, b = (i + 1) * scale;
            INT s0 = (INT)a, s1 = (INT)ceil(b);
            if (s1 > (INT)src) s1 = (INT)src;
            t[i].start = s0; t[i].n = s1 - s0;
            t[i].w = w_alloc((SIZE_T)t[i].n * sizeof(float));
            if (!t[i].w) goto fail;
            for (INT k = s0; k < s1; k++) {
                double lo = k > a ? k : a, hi = k + 1 < b ? k + 1 : b;
                t[i].w[k - s0] = (float)((hi - lo) / scale);
            }
        } else {
            double x = (i + 0.5) * scale - 0.5;
            if (x < 0) x = 0;
            INT s0 = (INT)x;
            if (s0 >= (INT)src - 1) { t[i].start = (INT)src - 1; t[i].n = 1; }
            else { t[i].start = s0; t[i].n = 2; }
            t[i].w = w_alloc(2 * sizeof(float));
            if (!t[i].w) goto fail;
            float f = (float)(x - s0);
            if (t[i].n == 1) t[i].w[0] = 1;
            else { t[i].w[0] = 1 - f; t[i].w[1] = f; }
        }
        if ((UINT)t[i].n > *maxn) *maxn = (UINT)t[i].n;
    }
    return t;
fail:
    taps_free(t, dst);
    return 0;
}

static HRESULT STDMETHODCALLTYPE sc_qi(Scaler *s, REFIID riid, void **out) { return qi_source(s, riid, out, &IID_IWICBitmapScaler); }
static ULONG STDMETHODCALLTYPE sc_addref(Scaler *s) { return (ULONG)InterlockedIncrement(&s->ref); }
static void sc_clear(Scaler *s)
{
    COM_RELEASE(s->src);
    taps_free(s->tx, s->dw); taps_free(s->ty, s->dh);
    s->tx = s->ty = 0;
    if (s->rows) for (UINT i = 0; i < s->nrows; i++) w_free(s->rows[i]);
    w_free(s->rows); w_free(s->rowy); w_free(s->srow); w_free(s->sbuf); w_free(s->pal);
    s->rows = 0; s->rowy = 0; s->srow = 0; s->sbuf = 0; s->pal = 0;
}
static ULONG STDMETHODCALLTYPE sc_release(Scaler *s)
{
    LONG r = InterlockedDecrement(&s->ref);
    if (!r) { sc_clear(s); DeleteCriticalSection(&s->lock); w_free(s); dll_release(); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE sc_size(Scaler *s, UINT *w, UINT *h)
{
    if (!w || !h) return E_INVALIDARG;
    if (!s->src) return WINCODEC_ERR_NOTINITIALIZED;
    *w = s->dw; *h = s->dh;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sc_format(Scaler *s, GUID *f)
{
    if (!f) return E_INVALIDARG;
    if (!s->src) { *f = GUID_WICPixelFormatDontCare; return WINCODEC_ERR_NOTINITIALIZED; }
    *f = *s->df->guid;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sc_res(Scaler *s, double *x, double *y)
{
    if (!s->src) return WINCODEC_ERR_NOTINITIALIZED;
    return s->src->lpVtbl->GetResolution(s->src, x, y);
}
static HRESULT STDMETHODCALLTYPE sc_palette(Scaler *s, IWICPalette *p)
{
    if (!p) return E_INVALIDARG;
    if (!s->src) return WINCODEC_ERR_NOTINITIALIZED;
    if (s->df != s->sf) return WINCODEC_ERR_PALETTEUNAVAILABLE;
    return s->src->lpVtbl->CopyPalette(s->src, p);
}

/* source row @y, resampled to the output width (premultiplied floats) */
static Px *sc_row(Scaler *s, INT y, HRESULT *hr)
{
    for (UINT i = 0; i < s->nrows; i++)
        if (s->rowy[i] == y) return s->rows[i];
    UINT slot = s->next++ % s->nrows;
    *hr = read_rows(s->src, s->sf, 0, y, (INT)s->sw, 1, s->srow);
    if (FAILED(*hr)) return 0;
    pf_unpack(s->sf, s->srow, 0, s->sw, s->pal, s->npal, s->sbuf);
    for (UINT x = 0; x < s->sw; x++) { Px *p = &s->sbuf[x]; p->r *= p->a; p->g *= p->a; p->b *= p->a; }
    Px *o = s->rows[slot];
    for (UINT x = 0; x < s->dw; x++) {
        Px acc = { 0, 0, 0, 0 };
        const Taps *t = &s->tx[x];
        for (INT k = 0; k < t->n; k++) {
            const Px *p = &s->sbuf[t->start + k];
            float wt = t->w[k];
            acc.r += p->r * wt; acc.g += p->g * wt; acc.b += p->b * wt; acc.a += p->a * wt;
        }
        o[x] = acc;
    }
    s->rowy[slot] = y;
    return o;
}

static HRESULT STDMETHODCALLTYPE sc_copy(Scaler *s, const WICRect *rc, UINT stride, UINT size, BYTE *buf)
{
    if (!s->src) return WINCODEC_ERR_NOTINITIALIZED;
    WICRect r;
    if (!buf) return E_INVALIDARG;
    HRESULT hr = src_check_rect(rc, s->dw, s->dh, &r);
    if (FAILED(hr)) return hr;
    if (!r.Width || !r.Height) return S_OK;
    UINT drow = pf_stride(s->df, (UINT)r.Width);
    if (stride < drow || (UINT64)stride * (UINT)(r.Height - 1) + drow > size) return E_INVALIDARG;
    EnterCriticalSection(&s->lock);
    if (s->mode == WICBitmapInterpolationModeNearestNeighbor) {
        /* pixels copied as they are, in the source's format */
        for (INT y = 0; y < r.Height && SUCCEEDED(hr); y++) {
            hr = read_rows(s->src, s->sf, 0, s->ty[r.Y + y].start, (INT)s->sw, 1, s->srow);
            BYTE *d = buf + (SIZE_T)y * stride;
            if (s->sf->bpp % 8) memset(d, 0, drow);
            for (INT x = 0; x < r.Width && SUCCEEDED(hr); x++)
                pf_copy_pixels(s->sf->bpp, s->srow, (UINT)s->tx[r.X + x].start, d, (UINT)x, 1);
        }
        LeaveCriticalSection(&s->lock);
        return hr;
    }
    Px *out = w_alloc(s->dw * sizeof(Px));
    if (!out) { LeaveCriticalSection(&s->lock); return E_OUTOFMEMORY; }
    for (INT y = 0; y < r.Height && SUCCEEDED(hr); y++) {
        const Taps *t = &s->ty[r.Y + y];
        memset(out, 0, s->dw * sizeof(Px));
        for (INT k = 0; k < t->n && SUCCEEDED(hr); k++) {
            Px *row = sc_row(s, t->start + k, &hr);
            if (!row) break;
            float wt = t->w[k];
            for (INT x = 0; x < r.Width; x++) {
                const Px *p = &row[r.X + x];
                out[x].r += p->r * wt; out[x].g += p->g * wt; out[x].b += p->b * wt; out[x].a += p->a * wt;
            }
        }
        if (FAILED(hr)) break;
        for (INT x = 0; x < r.Width; x++) {
            Px *p = &out[x];
            if (p->a > 0.0001f) { p->r /= p->a; p->g /= p->a; p->b /= p->a; }
            else p->r = p->g = p->b = p->a = 0;
        }
        BYTE *d = buf + (SIZE_T)y * stride;
        if (s->df->bpp % 8) memset(d, 0, drow);
        pf_pack(s->df, out, (UINT)r.Width, d, 0, s->pal, s->npal);
    }
    w_free(out);
    LeaveCriticalSection(&s->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE sc_initialize(Scaler *s, IWICBitmapSource *src, UINT w, UINT h, WICBitmapInterpolationMode mode)
{
    if (!src || !w || !h) return E_INVALIDARG;
    if (s->src) return WINCODEC_ERR_WRONGSTATE;
    UINT sw, sh;
    GUID g;
    HRESULT hr = src->lpVtbl->GetSize(src, &sw, &sh);
    if (SUCCEEDED(hr)) hr = src->lpVtbl->GetPixelFormat(src, &g);
    if (FAILED(hr)) return hr;
    const PixelFormat *sf = pf_find(&g);
    if (!sf || !sf->bpp) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (!sw || !sh) return E_INVALIDARG;
    if (mode > WICBitmapInterpolationModeHighQualityCubic) mode = WICBitmapInterpolationModeFant;
    const PixelFormat *df = sf;
    if (mode != WICBitmapInterpolationModeNearestNeighbor) {
        if (!pf_can_unpack(sf)) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        if (sf->kind == PK_INDEXED || sf->kind == PK_BW || !pf_can_pack(sf) || sf->bpp < 8)
            df = pf_find(&GUID_WICPixelFormat32bppBGRA);    /* filtered pixels need more than the palette */
    }
    UINT mx = 1, my = 1;
    s->sw = sw; s->sh = sh; s->dw = w; s->dh = h; s->mode = mode;
    s->tx = taps_make(sw, w, mode, &mx);
    s->ty = taps_make(sh, h, mode, &my);
    s->srow = w_alloc(pf_stride(sf, sw) + 16);
    s->sbuf = w_alloc(sw * sizeof(Px));
    s->nrows = my + 1;
    s->rows = w_alloc(s->nrows * sizeof *s->rows);
    s->rowy = w_alloc(s->nrows * sizeof *s->rowy);
    hr = s->tx && s->ty && s->srow && s->sbuf && s->rows && s->rowy ? S_OK : E_OUTOFMEMORY;
    for (UINT i = 0; SUCCEEDED(hr) && i < s->nrows; i++) {
        s->rowy[i] = -1;
        if (!(s->rows[i] = w_alloc(w * sizeof(Px)))) hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr) && sf->kind == PK_INDEXED && df != sf) {
        s->pal = src_palette(src, &s->npal);
        if (!s->pal) hr = WINCODEC_ERR_PALETTEUNAVAILABLE;
    }
    if (FAILED(hr)) { sc_clear(s); return hr; }
    src->lpVtbl->AddRef(src);
    s->src = src; s->sf = sf; s->df = df;
    return S_OK;
}

static const void *const sc_vtbl[] = {
    sc_qi, sc_addref, sc_release, sc_size, sc_format, sc_res, sc_palette, sc_copy, sc_initialize,
};
HRESULT scaler_create(IWICBitmapScaler **out)
{
    if (!out) return E_INVALIDARG;
    Scaler *s = w_alloc(sizeof *s);
    if (!s) { *out = 0; return E_OUTOFMEMORY; }
    s->vtbl = sc_vtbl;
    s->ref = 1;
    InitializeCriticalSection(&s->lock);
    dll_addref();
    *out = (IWICBitmapScaler *)s;
    return S_OK;
}

/* ================= clipper ================= */
typedef struct {
    const void *const *vtbl;
    LONG ref;
    IWICBitmapSource *src;
    WICRect rc;
} Clipper;

static HRESULT STDMETHODCALLTYPE cl_qi(Clipper *c, REFIID riid, void **out) { return qi_source(c, riid, out, &IID_IWICBitmapClipper); }
static ULONG STDMETHODCALLTYPE cl_addref(Clipper *c) { return (ULONG)InterlockedIncrement(&c->ref); }
static ULONG STDMETHODCALLTYPE cl_release(Clipper *c)
{
    LONG r = InterlockedDecrement(&c->ref);
    if (!r) { COM_RELEASE(c->src); w_free(c); dll_release(); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE cl_size(Clipper *c, UINT *w, UINT *h)
{
    if (!w || !h) return E_INVALIDARG;
    if (!c->src) return WINCODEC_ERR_NOTINITIALIZED;
    *w = (UINT)c->rc.Width; *h = (UINT)c->rc.Height;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cl_format(Clipper *c, GUID *f)
{
    if (!f) return E_INVALIDARG;
    if (!c->src) return WINCODEC_ERR_NOTINITIALIZED;
    return c->src->lpVtbl->GetPixelFormat(c->src, f);
}
static HRESULT STDMETHODCALLTYPE cl_res(Clipper *c, double *x, double *y)
{
    if (!c->src) return WINCODEC_ERR_NOTINITIALIZED;
    return c->src->lpVtbl->GetResolution(c->src, x, y);
}
static HRESULT STDMETHODCALLTYPE cl_palette(Clipper *c, IWICPalette *p)
{
    if (!c->src) return WINCODEC_ERR_NOTINITIALIZED;
    return c->src->lpVtbl->CopyPalette(c->src, p);
}
static HRESULT STDMETHODCALLTYPE cl_copy(Clipper *c, const WICRect *rc, UINT stride, UINT size, BYTE *buf)
{
    if (!c->src) return WINCODEC_ERR_NOTINITIALIZED;
    WICRect r;
    HRESULT hr = src_check_rect(rc, (UINT)c->rc.Width, (UINT)c->rc.Height, &r);
    if (FAILED(hr)) return hr;
    r.X += c->rc.X; r.Y += c->rc.Y;
    return c->src->lpVtbl->CopyPixels(c->src, &r, stride, size, buf);
}
static HRESULT STDMETHODCALLTYPE cl_initialize(Clipper *c, IWICBitmapSource *src, const WICRect *rc)
{
    if (!src || !rc) return E_INVALIDARG;
    if (c->src) return WINCODEC_ERR_WRONGSTATE;
    UINT w, h;
    WICRect r;
    HRESULT hr = src->lpVtbl->GetSize(src, &w, &h);
    if (FAILED(hr)) return hr;
    if (FAILED(src_check_rect(rc, w, h, &r))) return E_INVALIDARG;
    src->lpVtbl->AddRef(src);
    c->src = src;
    c->rc = r;
    return S_OK;
}
static const void *const cl_vtbl[] = {
    cl_qi, cl_addref, cl_release, cl_size, cl_format, cl_res, cl_palette, cl_copy, cl_initialize,
};
HRESULT clipper_create(IWICBitmapClipper **out)
{
    if (!out) return E_INVALIDARG;
    Clipper *c = w_alloc(sizeof *c);
    if (!c) { *out = 0; return E_OUTOFMEMORY; }
    c->vtbl = cl_vtbl;
    c->ref = 1;
    dll_addref();
    *out = (IWICBitmapClipper *)c;
    return S_OK;
}

/* ================= flip-rotator ================= */
typedef struct {
    const void *const *vtbl;
    LONG ref;
    CRITICAL_SECTION lock;
    IWICBitmapSource *src;
    const PixelFormat *f;
    UINT sw, sh, rot;
    BOOL fliph, flipv;
    BYTE *pixels;                       /* the whole source, read on first use */
    UINT sstride;
} FlipRot;

static HRESULT STDMETHODCALLTYPE fr_qi(FlipRot *f, REFIID riid, void **out) { return qi_source(f, riid, out, &IID_IWICBitmapFlipRotator); }
static ULONG STDMETHODCALLTYPE fr_addref(FlipRot *f) { return (ULONG)InterlockedIncrement(&f->ref); }
static ULONG STDMETHODCALLTYPE fr_release(FlipRot *f)
{
    LONG r = InterlockedDecrement(&f->ref);
    if (!r) { COM_RELEASE(f->src); w_free(f->pixels); DeleteCriticalSection(&f->lock); w_free(f); dll_release(); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE fr_size(FlipRot *f, UINT *w, UINT *h)
{
    if (!w || !h) return E_INVALIDARG;
    if (!f->src) return WINCODEC_ERR_NOTINITIALIZED;
    if (f->rot & 1) { *w = f->sh; *h = f->sw; } else { *w = f->sw; *h = f->sh; }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fr_format(FlipRot *f, GUID *g)
{
    if (!g) return E_INVALIDARG;
    if (!f->src) return WINCODEC_ERR_NOTINITIALIZED;
    *g = *f->f->guid;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fr_res(FlipRot *f, double *x, double *y)
{
    if (!f->src) return WINCODEC_ERR_NOTINITIALIZED;
    HRESULT hr = f->src->lpVtbl->GetResolution(f->src, x, y);
    if (SUCCEEDED(hr) && (f->rot & 1) && x && y) { double t = *x; *x = *y; *y = t; }
    return hr;
}
static HRESULT STDMETHODCALLTYPE fr_palette(FlipRot *f, IWICPalette *p)
{
    if (!f->src) return WINCODEC_ERR_NOTINITIALIZED;
    return f->src->lpVtbl->CopyPalette(f->src, p);
}
static HRESULT STDMETHODCALLTYPE fr_copy(FlipRot *f, const WICRect *rc, UINT stride, UINT size, BYTE *buf)
{
    if (!f->src) return WINCODEC_ERR_NOTINITIALIZED;
    UINT w, h;
    fr_size(f, &w, &h);
    WICRect r;
    if (!buf) return E_INVALIDARG;
    HRESULT hr = src_check_rect(rc, w, h, &r);
    if (FAILED(hr)) return hr;
    if (!r.Width || !r.Height) return S_OK;
    UINT drow = pf_stride(f->f, (UINT)r.Width);
    if (stride < drow || (UINT64)stride * (UINT)(r.Height - 1) + drow > size) return E_INVALIDARG;
    if (!f->rot && !f->fliph && !f->flipv) return f->src->lpVtbl->CopyPixels(f->src, &r, stride, size, buf);
    EnterCriticalSection(&f->lock);
    if (!f->pixels) {
        f->sstride = pf_stride(f->f, f->sw);
        f->pixels = w_alloc((SIZE_T)f->sstride * f->sh);
        if (!f->pixels) hr = E_OUTOFMEMORY;
        else {
            WICRect all = { 0, 0, (INT)f->sw, (INT)f->sh };
            hr = f->src->lpVtbl->CopyPixels(f->src, &all, f->sstride, f->sstride * f->sh, f->pixels);
            if (FAILED(hr)) { w_free(f->pixels); f->pixels = 0; }
        }
    }
    for (INT y = 0; y < r.Height && SUCCEEDED(hr); y++) {
        BYTE *d = buf + (SIZE_T)y * stride;
        if (f->f->bpp % 8) memset(d, 0, drow);
        for (INT x = 0; x < r.Width; x++) {
            UINT ox = (UINT)(r.X + x), oy = (UINT)(r.Y + y), sx, sy;
            if (f->fliph) ox = w - 1 - ox;
            if (f->flipv) oy = h - 1 - oy;
            switch (f->rot) {
            case 1:  sx = oy; sy = f->sh - 1 - ox; break;
            case 2:  sx = f->sw - 1 - ox; sy = f->sh - 1 - oy; break;
            case 3:  sx = f->sw - 1 - oy; sy = ox; break;
            default: sx = ox; sy = oy; break;
            }
            pf_copy_pixels(f->f->bpp, f->pixels + (SIZE_T)sy * f->sstride, sx, d, (UINT)x, 1);
        }
    }
    LeaveCriticalSection(&f->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE fr_initialize(FlipRot *f, IWICBitmapSource *src, WICBitmapTransformOptions opt)
{
    if (!src) return E_INVALIDARG;
    if (f->src) return WINCODEC_ERR_WRONGSTATE;
    GUID g;
    HRESULT hr = src->lpVtbl->GetSize(src, &f->sw, &f->sh);
    if (SUCCEEDED(hr)) hr = src->lpVtbl->GetPixelFormat(src, &g);
    if (FAILED(hr)) return hr;
    if (!(f->f = pf_find(&g)) || !f->f->bpp) return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    f->rot = (UINT)opt & 3;
    f->fliph = (opt & WICBitmapTransformFlipHorizontal) != 0;
    f->flipv = (opt & WICBitmapTransformFlipVertical) != 0;
    src->lpVtbl->AddRef(src);
    f->src = src;
    return S_OK;
}
static const void *const fr_vtbl[] = {
    fr_qi, fr_addref, fr_release, fr_size, fr_format, fr_res, fr_palette, fr_copy, fr_initialize,
};
HRESULT fliprotator_create(IWICBitmapFlipRotator **out)
{
    if (!out) return E_INVALIDARG;
    FlipRot *f = w_alloc(sizeof *f);
    if (!f) { *out = 0; return E_OUTOFMEMORY; }
    f->vtbl = fr_vtbl;
    f->ref = 1;
    InitializeCriticalSection(&f->lock);
    dll_addref();
    *out = (IWICBitmapFlipRotator *)f;
    return S_OK;
}
