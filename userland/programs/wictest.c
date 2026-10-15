/*
 * wictest — the Windows Imaging Component (windowscodecs.dll): the imaging
 * factory from CoCreateInstance and from WICCreateImagingFactory_Proxy;
 * PNG, BMP, JPEG, GIF and ICO decoded through the factory and the format
 * converter; resolution, decoder information, pixel format information;
 * the scaler, clipper and flip-rotator; bitmaps, locks, palettes, streams,
 * bitmaps over a section and from an HICON; the IWIC*_Proxy exports WPF
 * calls, and the documented errors of what is not supported.
 */
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

static const BYTE png_3x2[104] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x9D, 0x74, 0x66,
    0x1A, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x16, 0x25, 0x00, 0x00, 0x16,
    0x25, 0x01, 0x49, 0x52, 0x24, 0xF0, 0x00, 0x00, 0x00, 0x1A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C,
    0x05, 0xC1, 0x81, 0x01, 0x00, 0x00, 0x04, 0xC0, 0xA0, 0xF8, 0xDC, 0xE5, 0x53, 0x22, 0x89, 0x5A,
    0xC5, 0x90, 0xB9, 0x07, 0x8B, 0x33, 0x09, 0x7D, 0x24, 0x66, 0x90, 0x3A, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};
static const BYTE bmp_3x2[78] = {
    0x42, 0x4D, 0x4E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xC4, 0x0E, 0x00, 0x00, 0xC4, 0x0E, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x1E, 0x14, 0x0A, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const BYTE jpg_8x8[634] = {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02,
    0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04,
    0x04, 0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05, 0x06, 0x06, 0x06, 0x07, 0x09, 0x08, 0x06,
    0x07, 0x09, 0x07, 0x06, 0x06, 0x08, 0x0B, 0x08, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x06, 0x08,
    0x0B, 0x0C, 0x0B, 0x0A, 0x0C, 0x09, 0x0A, 0x0A, 0x0A, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x05, 0x03, 0x03, 0x05, 0x0A, 0x07, 0x06, 0x07, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0xFF, 0xC0,
    0x00, 0x11, 0x08, 0x00, 0x08, 0x00, 0x08, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
    0x01, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05,
    0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21,
    0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23,
    0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
    0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
    0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5,
    0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1,
    0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xC4, 0x00, 0x1F, 0x01, 0x00, 0x03,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x11, 0x00,
    0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00,
    0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
    0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15,
    0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4,
    0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9,
    0xFA, 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00, 0xF9,
    0x9E, 0x8A, 0x28, 0xAF, 0xCE, 0xCF, 0xF4, 0xE0, 0xFF, 0xD9,
};
static const BYTE gif_2f[111] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x02, 0x00, 0x81, 0x00, 0x00, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0xFF, 0x0B, 0x4E, 0x45, 0x54, 0x53,
    0x43, 0x41, 0x50, 0x45, 0x32, 0x2E, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00, 0x21, 0xF9, 0x04, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x08, 0x06,
    0x00, 0x01, 0x08, 0x04, 0x10, 0x10, 0x00, 0x21, 0xF9, 0x04, 0x01, 0x0A, 0x00, 0x01, 0x00, 0x2C,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x81, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x06, 0x00, 0x01, 0x08, 0x04, 0x10, 0x10, 0x00, 0x3B,
};
static const BYTE ico_2[185] = {
    0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x02, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x40, 0x00,
    0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x53, 0x00,
    0x00, 0x00, 0x66, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00,
    0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06,
    0x00, 0x00, 0x00, 0x9D, 0x74, 0x66, 0x1A, 0x00, 0x00, 0x00, 0x1A, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9C, 0x05, 0xC1, 0x81, 0x01, 0x00, 0x00, 0x04, 0xC0, 0xA0, 0xF8, 0xDC, 0xE5, 0x53, 0x22, 0x89,
    0x5A, 0xC5, 0x90, 0xB9, 0x07, 0x8B, 0x33, 0x09, 0x7D, 0x24, 0x66, 0x90, 0x3A, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

static IWICImagingFactory *factory;

static IWICBitmapDecoder *decode(const BYTE *data, DWORD size)
{
    IWICStream *s = 0;
    IWICBitmapDecoder *d = 0;
    if (FAILED(factory->lpVtbl->CreateStream(factory, &s))) return 0;
    if (SUCCEEDED(s->lpVtbl->InitializeFromMemory(s, (BYTE *)data, size)))
        factory->lpVtbl->CreateDecoderFromStream(factory, (IStream *)s, 0, WICDecodeMetadataCacheOnDemand, &d);
    s->lpVtbl->Release(s);
    return d;
}

/* frame @index of @d converted to 32bppPBGRA into @px (w*h*4 bytes) */
static BOOL to_pbgra(IWICBitmapSource *src, UINT w, UINT h, BYTE *px)
{
    IWICFormatConverter *c = 0;
    BOOL ok = FALSE;
    if (SUCCEEDED(factory->lpVtbl->CreateFormatConverter(factory, &c)) &&
        SUCCEEDED(c->lpVtbl->Initialize(c, src, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, 0, 0.0,
                                        WICBitmapPaletteTypeCustom)))
        ok = SUCCEEDED(c->lpVtbl->CopyPixels(c, 0, w * 4, w * h * 4, px));
    if (c) c->lpVtbl->Release(c);
    return ok;
}

static BOOL near_(int a, int b, int tol) { return a - b <= tol && b - a <= tol; }
static BOOL px_is(const BYTE *p, int b, int g, int r, int a, int tol)
{
    return near_(p[0], b, tol) && near_(p[1], g, tol) && near_(p[2], r, tol) && near_(p[3], a, tol);
}

static IWICBitmapFrameDecode *frame0(IWICBitmapDecoder *d, UINT i)
{
    IWICBitmapFrameDecode *f = 0;
    if (d) d->lpVtbl->GetFrame(d, i, &f);
    return f;
}

int main(void)
{
    CHECK("CoInitializeEx", SUCCEEDED(CoInitializeEx(0, COINIT_APARTMENTTHREADED)));
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, 0, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
    CHECK("CoCreateInstance(CLSID_WICImagingFactory)", SUCCEEDED(hr) && factory);
    IWICImagingFactory *f2 = 0;
    CHECK("WICCreateImagingFactory_Proxy", SUCCEEDED(WICCreateImagingFactory_Proxy(WINCODEC_SDK_VERSION1, &f2)) && f2);
    if (f2) f2->lpVtbl->Release(f2);
    if (!factory) { printf("wictest: %d passed, %d failed\n", pass, fail + 1); return 1; }
    IWICImagingFactory2 *fac2 = 0;
    CHECK("QueryInterface(IWICImagingFactory2)",
          SUCCEEDED(factory->lpVtbl->QueryInterface(factory, &IID_IWICImagingFactory2, (void **)&fac2)) && fac2);
    if (fac2) fac2->lpVtbl->Release(fac2);

    /* ---- PNG: format, size, resolution, pixels ---- */
    BYTE px[8 * 8 * 4];
    IWICBitmapDecoder *d = decode(png_3x2, sizeof png_3x2);
    CHECK("PNG decoder", d != 0);
    GUID g;
    UINT w = 0, h = 0, n = 0;
    CHECK("PNG container", d && SUCCEEDED(d->lpVtbl->GetContainerFormat(d, &g)) && IsEqualGUID(&g, &GUID_ContainerFormatPng));
    CHECK("PNG frame count", d && SUCCEEDED(d->lpVtbl->GetFrameCount(d, &n)) && n == 1);
    IWICBitmapFrameDecode *fr = frame0(d, 0);
    CHECK("PNG size", fr && SUCCEEDED(fr->lpVtbl->GetSize(fr, &w, &h)) && w == 3 && h == 2);
    CHECK("PNG pixel format", fr && SUCCEEDED(fr->lpVtbl->GetPixelFormat(fr, &g)) && IsEqualGUID(&g, &GUID_WICPixelFormat32bppBGRA));
    double dx = 0, dy = 0;
    CHECK("PNG resolution (pHYs)", fr && SUCCEEDED(fr->lpVtbl->GetResolution(fr, &dx, &dy)) && dx > 143.9 && dx < 144.1 && dy > 143.9);
    CHECK("PNG to PBGRA", fr && to_pbgra((IWICBitmapSource *)fr, 3, 2, px));
    CHECK("PNG pixels", px_is(px, 0, 0, 255, 255, 0) && px_is(px + 4, 0, 255, 0, 255, 0) && px_is(px + 8, 255, 0, 0, 255, 0) &&
                        px_is(px + 12, 255, 255, 255, 255, 0) && px_is(px + 16, 0, 0, 0, 255, 0) && px_is(px + 20, 0, 0, 128, 128, 1));
    IWICMetadataQueryReader *mq = (void *)1;
    CHECK("frame metadata unsupported", fr && fr->lpVtbl->GetMetadataQueryReader(fr, &mq) == WINCODEC_ERR_UNSUPPORTEDOPERATION && !mq);
    IWICBitmapSource *th = (void *)1;
    CHECK("frame has no thumbnail", fr && fr->lpVtbl->GetThumbnail(fr, &th) == WINCODEC_ERR_CODECNOTHUMBNAIL && !th);
    n = 99;
    CHECK("frame has no colour contexts", fr && SUCCEEDED(fr->lpVtbl->GetColorContexts(fr, 0, 0, &n)) && n == 0);
    /* rect copy, straight from the frame */
    WICRect rc = { 1, 1, 2, 1 };
    BYTE two[8];
    CHECK("CopyPixels(rect)", fr && SUCCEEDED(fr->lpVtbl->CopyPixels(fr, &rc, 8, 8, two)) && px_is(two, 0, 0, 0, 255, 0) &&
                              px_is(two + 4, 0, 0, 255, 128, 0));
    WICRect bad = { 2, 0, 2, 1 };
    CHECK("CopyPixels(rect outside)", fr && fr->lpVtbl->CopyPixels(fr, &bad, 8, 8, two) == E_INVALIDARG);

    /* decoder information, as WPF reads it */
    IWICBitmapDecoderInfo *di = 0;
    CHECK("GetDecoderInfo", d && SUCCEEDED(d->lpVtbl->GetDecoderInfo(d, &di)) && di);
    WCHAR mime[64];
    UINT len = 0;
    CHECK("GetMimeTypes length", di && SUCCEEDED(di->lpVtbl->GetMimeTypes(di, 0, 0, &len)) && len == 10);
    CHECK("GetMimeTypes", di && SUCCEEDED(di->lpVtbl->GetMimeTypes(di, 64, mime, &len)) && !wcscmp(mime, L"image/png"));
    CLSID cls;
    CHECK("decoder CLSID", di && SUCCEEDED(di->lpVtbl->GetCLSID(di, &cls)) && IsEqualGUID(&cls, &CLSID_WICPngDecoder));
    if (di) di->lpVtbl->Release(di);
    typedef HRESULT (WINAPI *GetSizeProxy)(IWICBitmapSource *, UINT *, UINT *);
    HMODULE wc = GetModuleHandleA("windowscodecs.dll");
    GetSizeProxy gsp = wc ? (GetSizeProxy)GetProcAddress(wc, "IWICBitmapSource_GetSize_Proxy") : 0;
    w = h = 0;
    CHECK("IWICBitmapSource_GetSize_Proxy", gsp && fr && SUCCEEDED(gsp((IWICBitmapSource *)fr, &w, &h)) && w == 3 && h == 2);
    CHECK("IWICImagingFactory_CreateDecoderFromStream_Proxy", wc && GetProcAddress(wc, "IWICImagingFactory_CreateDecoderFromStream_Proxy"));
    CHECK("IWICBitmapLock_GetDataPointer_STA_Proxy", wc && GetProcAddress(wc, "IWICBitmapLock_GetDataPointer_STA_Proxy"));

    /* ---- the scaler, clipper and flip-rotator on the PNG ---- */
    IWICBitmapScaler *sc = 0;
    CHECK("scaler nearest 6x4", fr && SUCCEEDED(factory->lpVtbl->CreateBitmapScaler(factory, &sc)) &&
          SUCCEEDED(sc->lpVtbl->Initialize(sc, (IWICBitmapSource *)fr, 6, 4, WICBitmapInterpolationModeNearestNeighbor)) &&
          to_pbgra((IWICBitmapSource *)sc, 6, 4, px) && px_is(px, 0, 0, 255, 255, 0) && px_is(px + 4, 0, 0, 255, 255, 0) &&
          px_is(px + 8, 0, 255, 0, 255, 0) && px_is(px + 6 * 4 * 3 + 20, 0, 0, 128, 128, 1));
    if (sc) { sc->lpVtbl->Release(sc); sc = 0; }
    IWICBitmapClipper *cl = 0;
    WICRect crc = { 1, 0, 2, 2 };
    CHECK("clipper", fr && SUCCEEDED(factory->lpVtbl->CreateBitmapClipper(factory, &cl)) &&
          SUCCEEDED(cl->lpVtbl->Initialize(cl, (IWICBitmapSource *)fr, &crc)) && SUCCEEDED(cl->lpVtbl->GetSize(cl, &w, &h)) &&
          w == 2 && h == 2 && to_pbgra((IWICBitmapSource *)cl, 2, 2, px) && px_is(px, 0, 255, 0, 255, 0) && px_is(px + 12, 0, 0, 128, 128, 1));
    if (cl) cl->lpVtbl->Release(cl);
    IWICBitmapFlipRotator *rot = 0;
    CHECK("flip-rotator 90", fr && SUCCEEDED(factory->lpVtbl->CreateBitmapFlipRotator(factory, &rot)) &&
          SUCCEEDED(rot->lpVtbl->Initialize(rot, (IWICBitmapSource *)fr, WICBitmapTransformRotate90)) &&
          SUCCEEDED(rot->lpVtbl->GetSize(rot, &w, &h)) && w == 2 && h == 3 && to_pbgra((IWICBitmapSource *)rot, 2, 3, px) &&
          /* clockwise: the bottom-left source pixel (white) is now top-left */
          px_is(px, 255, 255, 255, 255, 0) && px_is(px + 4, 0, 0, 255, 255, 0) && px_is(px + 20, 255, 0, 0, 255, 0));
    if (rot) rot->lpVtbl->Release(rot);
    if (fr) fr->lpVtbl->Release(fr);
    if (d) d->lpVtbl->Release(d);

    /* ---- BMP ---- */
    d = decode(bmp_3x2, sizeof bmp_3x2);
    fr = frame0(d, 0);
    CHECK("BMP container", d && SUCCEEDED(d->lpVtbl->GetContainerFormat(d, &g)) && IsEqualGUID(&g, &GUID_ContainerFormatBmp));
    CHECK("BMP 24bppBGR", fr && SUCCEEDED(fr->lpVtbl->GetPixelFormat(fr, &g)) && IsEqualGUID(&g, &GUID_WICPixelFormat24bppBGR));
    CHECK("BMP pixels", fr && to_pbgra((IWICBitmapSource *)fr, 3, 2, px) && px_is(px, 0, 0, 255, 255, 0) &&
                        px_is(px + 8, 255, 0, 0, 255, 0) && px_is(px + 20, 30, 20, 10, 255, 0));
    /* Fant shrinks to the average of the pixels */
    CHECK("scaler Fant 1x1", fr && SUCCEEDED(factory->lpVtbl->CreateBitmapScaler(factory, &sc)) &&
          SUCCEEDED(sc->lpVtbl->Initialize(sc, (IWICBitmapSource *)fr, 1, 1, WICBitmapInterpolationModeFant)) &&
          to_pbgra((IWICBitmapSource *)sc, 1, 1, px) && px_is(px, (255 + 255 + 30) / 6, (255 + 255 + 20) / 6, (255 + 255 + 10) / 6, 255, 2));
    if (sc) { sc->lpVtbl->Release(sc); sc = 0; }
    if (fr) fr->lpVtbl->Release(fr);
    if (d) d->lpVtbl->Release(d);

    /* ---- JPEG ---- */
    d = decode(jpg_8x8, sizeof jpg_8x8);
    fr = frame0(d, 0);
    CHECK("JPEG container", d && SUCCEEDED(d->lpVtbl->GetContainerFormat(d, &g)) && IsEqualGUID(&g, &GUID_ContainerFormatJpeg));
    CHECK("JPEG size", fr && SUCCEEDED(fr->lpVtbl->GetSize(fr, &w, &h)) && w == 8 && h == 8);
    CHECK("JPEG pixels", fr && to_pbgra((IWICBitmapSource *)fr, 8, 8, px) && px_is(px, 60, 40, 200, 255, 8) && px_is(px + 4 * 63, 60, 40, 200, 255, 8));
    if (fr) fr->lpVtbl->Release(fr);
    if (d) d->lpVtbl->Release(d);

    /* ---- GIF: two frames ---- */
    d = decode(gif_2f, sizeof gif_2f);
    CHECK("GIF frames", d && SUCCEEDED(d->lpVtbl->GetFrameCount(d, &n)) && n == 2);
    fr = frame0(d, 1);
    CHECK("GIF second frame", fr && to_pbgra((IWICBitmapSource *)fr, 2, 2, px) && px_is(px, 255, 0, 0, 255, 0));
    if (fr) fr->lpVtbl->Release(fr);
    if (d) d->lpVtbl->Release(d);

    /* ---- ICO: a DIB entry with an AND mask, and a PNG entry ---- */
    d = decode(ico_2, sizeof ico_2);
    CHECK("ICO container", d && SUCCEEDED(d->lpVtbl->GetContainerFormat(d, &g)) && IsEqualGUID(&g, &GUID_ContainerFormatIco));
    CHECK("ICO frames", d && SUCCEEDED(d->lpVtbl->GetFrameCount(d, &n)) && n == 2);
    fr = frame0(d, 0);
    CHECK("ICO DIB entry", fr && SUCCEEDED(fr->lpVtbl->GetSize(fr, &w, &h)) && w == 2 && h == 2 &&
          to_pbgra((IWICBitmapSource *)fr, 2, 2, px) && px_is(px, 0, 0, 0, 0, 0) && px_is(px + 4, 0, 255, 0, 255, 0) &&
          px_is(px + 8, 255, 0, 0, 255, 0) && px_is(px + 12, 255, 255, 255, 255, 0));
    if (fr) fr->lpVtbl->Release(fr);
    fr = frame0(d, 1);
    CHECK("ICO PNG entry", fr && SUCCEEDED(fr->lpVtbl->GetSize(fr, &w, &h)) && w == 3 && h == 2 &&
          to_pbgra((IWICBitmapSource *)fr, 3, 2, px) && px_is(px + 8, 255, 0, 0, 255, 0));
    if (fr) fr->lpVtbl->Release(fr);
    if (d) d->lpVtbl->Release(d);

    /* unknown data */
    static const BYTE junk[16] = "not an image";
    CHECK("unknown format", !decode(junk, sizeof junk));

    /* ---- from a file name ---- */
    HANDLE fh = CreateFileA("C:\\Temp\\wictest.png", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    DWORD wr = 0;
    BOOL wrote = fh != INVALID_HANDLE_VALUE && WriteFile(fh, png_3x2, sizeof png_3x2, &wr, 0);
    if (fh != INVALID_HANDLE_VALUE) CloseHandle(fh);
    d = 0;
    CHECK("CreateDecoderFromFilename", wrote && SUCCEEDED(factory->lpVtbl->CreateDecoderFromFilename(factory, L"C:\\Temp\\wictest.png", 0,
          GENERIC_READ, WICDecodeMetadataCacheOnDemand, &d)) && d && SUCCEEDED(d->lpVtbl->GetFrameCount(d, &n)) && n == 1);
    if (d) d->lpVtbl->Release(d);
    fh = CreateFileA("C:\\Temp\\wictest.png", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    d = 0;
    CHECK("CreateDecoderFromFileHandle", fh != INVALID_HANDLE_VALUE &&
          SUCCEEDED(factory->lpVtbl->CreateDecoderFromFileHandle(factory, (ULONG_PTR)fh, 0, WICDecodeMetadataCacheOnDemand, &d)) && d);
    if (d) d->lpVtbl->Release(d);
    if (fh != INVALID_HANDLE_VALUE) CloseHandle(fh);
    DeleteFileA("C:\\Temp\\wictest.png");

    /* ---- pixel format information ---- */
    IWICComponentInfo *ci = 0;
    IWICPixelFormatInfo *pfi = 0;
    UINT bpp = 0, ch = 0;
    BYTE mask[4];
    CHECK("pixel format info", SUCCEEDED(factory->lpVtbl->CreateComponentInfo(factory, &GUID_WICPixelFormat32bppPBGRA, &ci)) &&
          SUCCEEDED(ci->lpVtbl->QueryInterface(ci, &IID_IWICPixelFormatInfo, (void **)&pfi)) &&
          SUCCEEDED(pfi->lpVtbl->GetBitsPerPixel(pfi, &bpp)) && bpp == 32 && SUCCEEDED(pfi->lpVtbl->GetChannelCount(pfi, &ch)) && ch == 4 &&
          SUCCEEDED(pfi->lpVtbl->GetChannelMask(pfi, 3, 4, mask, &n)) && n == 4 && !mask[0] && mask[3] == 0xFF);
    if (pfi) pfi->lpVtbl->Release(pfi);
    if (ci) ci->lpVtbl->Release(ci);
    ci = 0;
    CHECK("1bppIndexed info", SUCCEEDED(factory->lpVtbl->CreateComponentInfo(factory, &GUID_WICPixelFormat1bppIndexed, &ci)) &&
          SUCCEEDED(ci->lpVtbl->QueryInterface(ci, &IID_IWICPixelFormatInfo, (void **)&pfi)) &&
          SUCCEEDED(pfi->lpVtbl->GetBitsPerPixel(pfi, &bpp)) && bpp == 1);
    if (pfi) pfi->lpVtbl->Release(pfi);
    if (ci) ci->lpVtbl->Release(ci);
    ci = (void *)1;
    CHECK("unknown component", factory->lpVtbl->CreateComponentInfo(factory, &GUID_ContainerFormatTiff, &ci) == WINCODEC_ERR_COMPONENTNOTFOUND);

    /* ---- bitmap, lock, palette, conversion to indexed ---- */
    IWICBitmap *bm = 0;
    IWICBitmapLock *lk = 0;
    BYTE *data = 0;
    UINT size = 0, stride = 0;
    CHECK("CreateBitmap", SUCCEEDED(factory->lpVtbl->CreateBitmap(factory, 4, 4, &GUID_WICPixelFormat32bppBGRA, WICBitmapCacheOnLoad, &bm)));
    WICRect lr = { 1, 1, 2, 2 };
    CHECK("Lock(write)", bm && SUCCEEDED(bm->lpVtbl->Lock(bm, &lr, WICBitmapLockWrite, &lk)) &&
          SUCCEEDED(lk->lpVtbl->GetStride(lk, &stride)) && stride >= 16 && SUCCEEDED(lk->lpVtbl->GetDataPointer(lk, &size, &data)) &&
          size == stride + 8);
    IWICBitmapLock *lk2 = (void *)1;
    CHECK("second lock refused", bm && bm->lpVtbl->Lock(bm, &lr, WICBitmapLockRead, &lk2) == WINCODEC_ERR_ALREADYLOCKED);
    if (data) { data[0] = 10; data[1] = 20; data[2] = 30; data[3] = 255; memset(data + stride + 4, 255, 4); }
    if (lk) lk->lpVtbl->Release(lk);
    BYTE all[64];
    CHECK("bitmap pixels", bm && SUCCEEDED(bm->lpVtbl->CopyPixels(bm, 0, 16, 64, all)) && px_is(all + 20, 10, 20, 30, 255, 0) &&
          px_is(all + 40, 255, 255, 255, 255, 0) && px_is(all, 0, 0, 0, 0, 0));
    IWICPalette *pal = 0;
    CHECK("palette FixedBW", SUCCEEDED(factory->lpVtbl->CreatePalette(factory, &pal)) &&
          SUCCEEDED(pal->lpVtbl->InitializePredefined(pal, WICBitmapPaletteTypeFixedBW, FALSE)) &&
          SUCCEEDED(pal->lpVtbl->GetColorCount(pal, &n)) && n == 2);
    n = 0;
    IWICPalette *web = 0;
    CHECK("palette Halftone216", SUCCEEDED(factory->lpVtbl->CreatePalette(factory, &web)) &&
          SUCCEEDED(web->lpVtbl->InitializePredefined(web, WICBitmapPaletteTypeFixedHalftone216, TRUE)) &&
          SUCCEEDED(web->lpVtbl->GetColorCount(web, &n)) && n == 225);
    if (web) web->lpVtbl->Release(web);
    IWICFormatConverter *cv = 0;
    BOOL can = FALSE;
    BYTE idx[8];
    CHECK("convert to 1bppIndexed", bm && pal && SUCCEEDED(factory->lpVtbl->CreateFormatConverter(factory, &cv)) &&
          SUCCEEDED(cv->lpVtbl->CanConvert(cv, &GUID_WICPixelFormat32bppBGRA, &GUID_WICPixelFormat1bppIndexed, &can)) && can &&
          SUCCEEDED(cv->lpVtbl->Initialize(cv, (IWICBitmapSource *)bm, &GUID_WICPixelFormat1bppIndexed, WICBitmapDitherTypeNone, pal, 0.0,
                                           WICBitmapPaletteTypeCustom)) &&
          SUCCEEDED(cv->lpVtbl->CopyPixels(cv, 0, 2, 8, idx)) && idx[0] == 0 && idx[2] == 0 && idx[4] == 0x20);
    if (cv) cv->lpVtbl->Release(cv);
    if (pal) pal->lpVtbl->Release(pal);
    IWICBitmapSource *same = 0;
    CHECK("WICConvertBitmapSource (same format)", bm && SUCCEEDED(WICConvertBitmapSource(&GUID_WICPixelFormat32bppBGRA,
          (IWICBitmapSource *)bm, &same)) && same == (IWICBitmapSource *)bm);
    if (same) same->lpVtbl->Release(same);
    same = 0;
    CHECK("WICConvertBitmapSource (to 24bppRGB)", bm && SUCCEEDED(WICConvertBitmapSource(&GUID_WICPixelFormat24bppRGB,
          (IWICBitmapSource *)bm, &same)) && SUCCEEDED(same->lpVtbl->CopyPixels(same, &lr, 6, 12, all)) &&
          all[0] == 30 && all[1] == 20 && all[2] == 10);
    if (same) same->lpVtbl->Release(same);
    if (bm) bm->lpVtbl->Release(bm);

    /* ---- a bitmap over a section: later writes show ---- */
    HANDLE sec = CreateFileMappingA(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, 4096, 0);
    BYTE *view = sec ? MapViewOfFile(sec, FILE_MAP_WRITE, 0, 0, 4096) : 0;
    bm = 0;
    CHECK("WICCreateBitmapFromSection", view && SUCCEEDED(WICCreateBitmapFromSection(2, 2, &GUID_WICPixelFormat32bppPBGRA, sec, 8, 0, &bm)));
    if (view) { view[0] = 1; view[1] = 2; view[2] = 3; view[3] = 4; }
    CHECK("section pixels shared", bm && SUCCEEDED(bm->lpVtbl->CopyPixels(bm, 0, 8, 16, all)) && all[0] == 1 && all[3] == 4);
    if (bm) bm->lpVtbl->Release(bm);
    if (view) UnmapViewOfFile(view);
    if (sec) CloseHandle(sec);

    /* ---- an HICON ---- */
    DWORD ibits[4] = { 0xFF0000FF, 0x8000FF00, 0xFFFF0000, 0xFFFFFFFF };
    BYTE mbits[8] = { 0 };
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), 2, -2, 1, 32, BI_RGB } };
    void *dib = 0;
    ICONINFO ii = { TRUE, 0, 0, CreateBitmap(2, 2, 1, 1, mbits), CreateDIBSection(0, &bi, DIB_RGB_COLORS, &dib, 0, 0) };
    if (dib) memcpy(dib, ibits, sizeof ibits);
    HICON icon = CreateIconIndirect(&ii);
    bm = 0;
    CHECK("CreateBitmapFromHICON", icon && SUCCEEDED(factory->lpVtbl->CreateBitmapFromHICON(factory, icon, &bm)) &&
          SUCCEEDED(bm->lpVtbl->GetSize(bm, &w, &h)) && w == 2 && h == 2 && SUCCEEDED(bm->lpVtbl->CopyPixels(bm, 0, 8, 16, all)) &&
          px_is(all, 0xFF, 0, 0, 0xFF, 0) && all[7] == 0x80);
    if (bm) bm->lpVtbl->Release(bm);
    if (icon) DestroyIcon(icon);
    DeleteObject(ii.hbmMask);
    DeleteObject(ii.hbmColor);

    /* ---- metadata names; no encoders yet ---- */
    WCHAR sn[16];
    CHECK("WICMapGuidToShortName", SUCCEEDED(WICMapGuidToShortName(&GUID_MetadataFormatApp1, 16, sn, &n)) && !wcscmp(sn, L"app1") && n == 5);
    CHECK("WICMapShortNameToGuid", SUCCEEDED(WICMapShortNameToGuid(L"xmp", &g)) && IsEqualGUID(&g, &GUID_MetadataFormatXMP));
    IWICBitmapEncoder *enc = (void *)1;
    CHECK("no encoders", factory->lpVtbl->CreateEncoder(factory, &GUID_ContainerFormatPng, 0, &enc) == WINCODEC_ERR_COMPONENTNOTFOUND && !enc);

    factory->lpVtbl->Release(factory);
    CoUninitialize();
    printf("wictest: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}
