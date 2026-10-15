/*
 * windowscodecs.dll — the flat IWIC*_Proxy functions.  Windows exports one
 * per interface method (the MIDL-generated client side); each calls the
 * method through the object's vtable.  WPF's PresentationCore P/Invokes
 * these rather than calling the vtables itself.
 */
#include "wic.h"

#define PROXY __declspec(dllexport)

PROXY HRESULT WINAPI IWICBitmapClipper_Initialize_Proxy(IWICBitmapClipper *This, IWICBitmapSource *source, const WICRect *rect)
{
    return This->lpVtbl->Initialize(This, source, rect);
}
PROXY HRESULT WINAPI IWICBitmapCodecInfo_DoesSupportAnimation_Proxy(IWICBitmapCodecInfo *This, BOOL *support)
{
    return This->lpVtbl->DoesSupportAnimation(This, support);
}
PROXY HRESULT WINAPI IWICBitmapCodecInfo_DoesSupportLossless_Proxy(IWICBitmapCodecInfo *This, BOOL *support)
{
    return This->lpVtbl->DoesSupportLossless(This, support);
}
PROXY HRESULT WINAPI IWICBitmapCodecInfo_DoesSupportMultiframe_Proxy(IWICBitmapCodecInfo *This, BOOL *support)
{
    return This->lpVtbl->DoesSupportMultiframe(This, support);
}
PROXY HRESULT WINAPI IWICBitmapCodecInfo_GetContainerFormat_Proxy(IWICBitmapCodecInfo *This, GUID *format)
{
    return This->lpVtbl->GetContainerFormat(This, format);
}
PROXY HRESULT WINAPI IWICBitmapCodecInfo_GetDeviceManufacturer_Proxy(IWICBitmapCodecInfo *This, UINT cch, WCHAR *manufacturer, UINT *actual)
{
    return This->lpVtbl->GetDeviceManufacturer(This, cch, manufacturer, actual);
}
PROXY HRESULT WINAPI IWICBitmapCodecInfo_GetDeviceModels_Proxy(IWICBitmapCodecInfo *This, UINT cch, WCHAR *models, UINT *actual)
{
    return This->lpVtbl->GetDeviceModels(This, cch, models, actual);
}
PROXY HRESULT WINAPI IWICBitmapCodecInfo_GetFileExtensions_Proxy(IWICBitmapCodecInfo *This, UINT cch, WCHAR *extensions, UINT *actual)
{
    return This->lpVtbl->GetFileExtensions(This, cch, extensions, actual);
}
PROXY HRESULT WINAPI IWICBitmapCodecInfo_GetMimeTypes_Proxy(IWICBitmapCodecInfo *This, UINT cch, WCHAR *types, UINT *actual)
{
    return This->lpVtbl->GetMimeTypes(This, cch, types, actual);
}
PROXY HRESULT WINAPI IWICBitmapDecoder_CopyPalette_Proxy(IWICBitmapDecoder *This, IWICPalette *palette)
{
    return This->lpVtbl->CopyPalette(This, palette);
}
PROXY HRESULT WINAPI IWICBitmapDecoder_GetColorContexts_Proxy(IWICBitmapDecoder *This, UINT count, IWICColorContext **contexts, UINT *actual)
{
    return This->lpVtbl->GetColorContexts(This, count, contexts, actual);
}
PROXY HRESULT WINAPI IWICBitmapDecoder_GetDecoderInfo_Proxy(IWICBitmapDecoder *This, IWICBitmapDecoderInfo **out)
{
    return This->lpVtbl->GetDecoderInfo(This, out);
}
PROXY HRESULT WINAPI IWICBitmapDecoder_GetFrameCount_Proxy(IWICBitmapDecoder *This, UINT *count)
{
    return This->lpVtbl->GetFrameCount(This, count);
}
PROXY HRESULT WINAPI IWICBitmapDecoder_GetFrame_Proxy(IWICBitmapDecoder *This, UINT index, IWICBitmapFrameDecode **out)
{
    return This->lpVtbl->GetFrame(This, index, out);
}
PROXY HRESULT WINAPI IWICBitmapDecoder_GetMetadataQueryReader_Proxy(IWICBitmapDecoder *This, IWICMetadataQueryReader **out)
{
    return This->lpVtbl->GetMetadataQueryReader(This, out);
}
PROXY HRESULT WINAPI IWICBitmapDecoder_GetPreview_Proxy(IWICBitmapDecoder *This, IWICBitmapSource **out)
{
    return This->lpVtbl->GetPreview(This, out);
}
PROXY HRESULT WINAPI IWICBitmapDecoder_GetThumbnail_Proxy(IWICBitmapDecoder *This, IWICBitmapSource **out)
{
    return This->lpVtbl->GetThumbnail(This, out);
}
PROXY HRESULT WINAPI IWICBitmapEncoder_Commit_Proxy(IWICBitmapEncoder *This)
{
    return This->lpVtbl->Commit(This);
}
PROXY HRESULT WINAPI IWICBitmapEncoder_CreateNewFrame_Proxy(IWICBitmapEncoder *This, IWICBitmapFrameEncode **frame, IPropertyBag2 **options)
{
    return This->lpVtbl->CreateNewFrame(This, frame, options);
}
PROXY HRESULT WINAPI IWICBitmapEncoder_GetEncoderInfo_Proxy(IWICBitmapEncoder *This, IWICBitmapEncoderInfo **out)
{
    return This->lpVtbl->GetEncoderInfo(This, out);
}
PROXY HRESULT WINAPI IWICBitmapEncoder_GetMetadataQueryWriter_Proxy(IWICBitmapEncoder *This, IWICMetadataQueryWriter **out)
{
    return This->lpVtbl->GetMetadataQueryWriter(This, out);
}
PROXY HRESULT WINAPI IWICBitmapEncoder_Initialize_Proxy(IWICBitmapEncoder *This, IStream *stream, WICBitmapEncoderCacheOption option)
{
    return This->lpVtbl->Initialize(This, stream, option);
}
PROXY HRESULT WINAPI IWICBitmapEncoder_SetPalette_Proxy(IWICBitmapEncoder *This, IWICPalette *palette)
{
    return This->lpVtbl->SetPalette(This, palette);
}
PROXY HRESULT WINAPI IWICBitmapEncoder_SetThumbnail_Proxy(IWICBitmapEncoder *This, IWICBitmapSource *thumbnail)
{
    return This->lpVtbl->SetThumbnail(This, thumbnail);
}
PROXY HRESULT WINAPI IWICBitmapFlipRotator_Initialize_Proxy(IWICBitmapFlipRotator *This, IWICBitmapSource *source, WICBitmapTransformOptions options)
{
    return This->lpVtbl->Initialize(This, source, options);
}
PROXY HRESULT WINAPI IWICBitmapFrameDecode_GetColorContexts_Proxy(IWICBitmapFrameDecode *This, UINT count, IWICColorContext **contexts, UINT *actual)
{
    return This->lpVtbl->GetColorContexts(This, count, contexts, actual);
}
PROXY HRESULT WINAPI IWICBitmapFrameDecode_GetMetadataQueryReader_Proxy(IWICBitmapFrameDecode *This, IWICMetadataQueryReader **out)
{
    return This->lpVtbl->GetMetadataQueryReader(This, out);
}
PROXY HRESULT WINAPI IWICBitmapFrameDecode_GetThumbnail_Proxy(IWICBitmapFrameDecode *This, IWICBitmapSource **out)
{
    return This->lpVtbl->GetThumbnail(This, out);
}
PROXY HRESULT WINAPI IWICBitmapFrameEncode_Commit_Proxy(IWICBitmapFrameEncode *This)
{
    return This->lpVtbl->Commit(This);
}
PROXY HRESULT WINAPI IWICBitmapFrameEncode_GetMetadataQueryWriter_Proxy(IWICBitmapFrameEncode *This, IWICMetadataQueryWriter **out)
{
    return This->lpVtbl->GetMetadataQueryWriter(This, out);
}
PROXY HRESULT WINAPI IWICBitmapFrameEncode_Initialize_Proxy(IWICBitmapFrameEncode *This, IPropertyBag2 *options)
{
    return This->lpVtbl->Initialize(This, options);
}
PROXY HRESULT WINAPI IWICBitmapFrameEncode_SetColorContexts_Proxy(IWICBitmapFrameEncode *This, UINT count, IWICColorContext **contexts)
{
    return This->lpVtbl->SetColorContexts(This, count, contexts);
}
PROXY HRESULT WINAPI IWICBitmapFrameEncode_SetResolution_Proxy(IWICBitmapFrameEncode *This, double dpix, double dpiy)
{
    return This->lpVtbl->SetResolution(This, dpix, dpiy);
}
PROXY HRESULT WINAPI IWICBitmapFrameEncode_SetSize_Proxy(IWICBitmapFrameEncode *This, UINT width, UINT height)
{
    return This->lpVtbl->SetSize(This, width, height);
}
PROXY HRESULT WINAPI IWICBitmapFrameEncode_SetThumbnail_Proxy(IWICBitmapFrameEncode *This, IWICBitmapSource *thumbnail)
{
    return This->lpVtbl->SetThumbnail(This, thumbnail);
}
PROXY HRESULT WINAPI IWICBitmapFrameEncode_WriteSource_Proxy(IWICBitmapFrameEncode *This, IWICBitmapSource *source, WICRect *rect)
{
    return This->lpVtbl->WriteSource(This, source, rect);
}
PROXY HRESULT WINAPI IWICBitmapLock_GetStride_Proxy(IWICBitmapLock *This, UINT *stride)
{
    return This->lpVtbl->GetStride(This, stride);
}
PROXY HRESULT WINAPI IWICBitmapScaler_Initialize_Proxy(IWICBitmapScaler *This, IWICBitmapSource *source, UINT width, UINT height, WICBitmapInterpolationMode mode)
{
    return This->lpVtbl->Initialize(This, source, width, height, mode);
}
PROXY HRESULT WINAPI IWICBitmapSource_CopyPalette_Proxy(IWICBitmapSource *This, IWICPalette *palette)
{
    return This->lpVtbl->CopyPalette(This, palette);
}
PROXY HRESULT WINAPI IWICBitmapSource_CopyPixels_Proxy(IWICBitmapSource *This, const WICRect *rect, UINT stride, UINT size, BYTE *buffer)
{
    return This->lpVtbl->CopyPixels(This, rect, stride, size, buffer);
}
PROXY HRESULT WINAPI IWICBitmapSource_GetPixelFormat_Proxy(IWICBitmapSource *This, WICPixelFormatGUID *format)
{
    return This->lpVtbl->GetPixelFormat(This, format);
}
PROXY HRESULT WINAPI IWICBitmapSource_GetResolution_Proxy(IWICBitmapSource *This, double *dpix, double *dpiy)
{
    return This->lpVtbl->GetResolution(This, dpix, dpiy);
}
PROXY HRESULT WINAPI IWICBitmapSource_GetSize_Proxy(IWICBitmapSource *This, UINT *width, UINT *height)
{
    return This->lpVtbl->GetSize(This, width, height);
}
PROXY HRESULT WINAPI IWICBitmap_Lock_Proxy(IWICBitmap *This, const WICRect *rect, DWORD flags, IWICBitmapLock **out)
{
    return This->lpVtbl->Lock(This, rect, flags, out);
}
PROXY HRESULT WINAPI IWICBitmap_SetPalette_Proxy(IWICBitmap *This, IWICPalette *palette)
{
    return This->lpVtbl->SetPalette(This, palette);
}
PROXY HRESULT WINAPI IWICBitmap_SetResolution_Proxy(IWICBitmap *This, double dpix, double dpiy)
{
    return This->lpVtbl->SetResolution(This, dpix, dpiy);
}
PROXY HRESULT WINAPI IWICColorContext_InitializeFromMemory_Proxy(IWICColorContext *This, const BYTE *buffer, UINT size)
{
    return This->lpVtbl->InitializeFromMemory(This, buffer, size);
}
PROXY HRESULT WINAPI IWICComponentFactory_CreateMetadataWriterFromReader_Proxy(IWICComponentFactory *This, IWICMetadataReader *reader, const GUID *vendor, IWICMetadataWriter **out)
{
    return This->lpVtbl->CreateMetadataWriterFromReader(This, reader, vendor, out);
}
PROXY HRESULT WINAPI IWICComponentFactory_CreateQueryWriterFromBlockWriter_Proxy(IWICComponentFactory *This, IWICMetadataBlockWriter *writer, IWICMetadataQueryWriter **out)
{
    return This->lpVtbl->CreateQueryWriterFromBlockWriter(This, writer, out);
}
PROXY HRESULT WINAPI IWICComponentInfo_GetAuthor_Proxy(IWICComponentInfo *This, UINT cch, WCHAR *author, UINT *actual)
{
    return This->lpVtbl->GetAuthor(This, cch, author, actual);
}
PROXY HRESULT WINAPI IWICComponentInfo_GetCLSID_Proxy(IWICComponentInfo *This, CLSID *clsid)
{
    return This->lpVtbl->GetCLSID(This, clsid);
}
PROXY HRESULT WINAPI IWICComponentInfo_GetFriendlyName_Proxy(IWICComponentInfo *This, UINT cch, WCHAR *name, UINT *actual)
{
    return This->lpVtbl->GetFriendlyName(This, cch, name, actual);
}
PROXY HRESULT WINAPI IWICComponentInfo_GetSpecVersion_Proxy(IWICComponentInfo *This, UINT cch, WCHAR *version, UINT *actual)
{
    return This->lpVtbl->GetSpecVersion(This, cch, version, actual);
}
PROXY HRESULT WINAPI IWICComponentInfo_GetVersion_Proxy(IWICComponentInfo *This, UINT cch, WCHAR *version, UINT *actual)
{
    return This->lpVtbl->GetVersion(This, cch, version, actual);
}
PROXY HRESULT WINAPI IWICFastMetadataEncoder_Commit_Proxy(IWICFastMetadataEncoder *This)
{
    return This->lpVtbl->Commit(This);
}
PROXY HRESULT WINAPI IWICFastMetadataEncoder_GetMetadataQueryWriter_Proxy(IWICFastMetadataEncoder *This, IWICMetadataQueryWriter **out)
{
    return This->lpVtbl->GetMetadataQueryWriter(This, out);
}
PROXY HRESULT WINAPI IWICFormatConverter_Initialize_Proxy(IWICFormatConverter *This, IWICBitmapSource *source, REFWICPixelFormatGUID format, WICBitmapDitherType dither, IWICPalette *palette, double alpha_threshold, WICBitmapPaletteType translate)
{
    return This->lpVtbl->Initialize(This, source, format, dither, palette, alpha_threshold, translate);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateBitmapClipper_Proxy(IWICImagingFactory *This, IWICBitmapClipper **out)
{
    return This->lpVtbl->CreateBitmapClipper(This, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateBitmapFlipRotator_Proxy(IWICImagingFactory *This, IWICBitmapFlipRotator **out)
{
    return This->lpVtbl->CreateBitmapFlipRotator(This, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateBitmapFromHBITMAP_Proxy(IWICImagingFactory *This, HBITMAP bitmap, HPALETTE palette, WICBitmapAlphaChannelOption option, IWICBitmap **out)
{
    return This->lpVtbl->CreateBitmapFromHBITMAP(This, bitmap, palette, option, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateBitmapFromHICON_Proxy(IWICImagingFactory *This, HICON icon, IWICBitmap **out)
{
    return This->lpVtbl->CreateBitmapFromHICON(This, icon, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateBitmapFromMemory_Proxy(IWICImagingFactory *This, UINT width, UINT height, REFWICPixelFormatGUID format, UINT stride, UINT size, BYTE *buffer, IWICBitmap **out)
{
    return This->lpVtbl->CreateBitmapFromMemory(This, width, height, format, stride, size, buffer, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateBitmapFromSource_Proxy(IWICImagingFactory *This, IWICBitmapSource *source, WICBitmapCreateCacheOption option, IWICBitmap **out)
{
    return This->lpVtbl->CreateBitmapFromSource(This, source, option, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateBitmapScaler_Proxy(IWICImagingFactory *This, IWICBitmapScaler **out)
{
    return This->lpVtbl->CreateBitmapScaler(This, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateBitmap_Proxy(IWICImagingFactory *This, UINT width, UINT height, REFWICPixelFormatGUID format, WICBitmapCreateCacheOption option, IWICBitmap **out)
{
    return This->lpVtbl->CreateBitmap(This, width, height, format, option, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateComponentInfo_Proxy(IWICImagingFactory *This, REFCLSID clsid, IWICComponentInfo **out)
{
    return This->lpVtbl->CreateComponentInfo(This, clsid, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateDecoderFromFileHandle_Proxy(IWICImagingFactory *This, ULONG_PTR file, const GUID *vendor, WICDecodeOptions options, IWICBitmapDecoder **out)
{
    return This->lpVtbl->CreateDecoderFromFileHandle(This, file, vendor, options, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateDecoderFromStream_Proxy(IWICImagingFactory *This, IStream *stream, const GUID *vendor, WICDecodeOptions options, IWICBitmapDecoder **out)
{
    return This->lpVtbl->CreateDecoderFromStream(This, stream, vendor, options, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateEncoder_Proxy(IWICImagingFactory *This, REFGUID format, const GUID *vendor, IWICBitmapEncoder **out)
{
    return This->lpVtbl->CreateEncoder(This, format, vendor, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateFastMetadataEncoderFromDecoder_Proxy(IWICImagingFactory *This, IWICBitmapDecoder *decoder, IWICFastMetadataEncoder **out)
{
    return This->lpVtbl->CreateFastMetadataEncoderFromDecoder(This, decoder, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateFastMetadataEncoderFromFrameDecode_Proxy(IWICImagingFactory *This, IWICBitmapFrameDecode *frame, IWICFastMetadataEncoder **out)
{
    return This->lpVtbl->CreateFastMetadataEncoderFromFrameDecode(This, frame, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateFormatConverter_Proxy(IWICImagingFactory *This, IWICFormatConverter **out)
{
    return This->lpVtbl->CreateFormatConverter(This, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreatePalette_Proxy(IWICImagingFactory *This, IWICPalette **out)
{
    return This->lpVtbl->CreatePalette(This, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateQueryWriterFromReader_Proxy(IWICImagingFactory *This, IWICMetadataQueryReader *reader, const GUID *vendor, IWICMetadataQueryWriter **out)
{
    return This->lpVtbl->CreateQueryWriterFromReader(This, reader, vendor, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateQueryWriter_Proxy(IWICImagingFactory *This, REFGUID format, const GUID *vendor, IWICMetadataQueryWriter **out)
{
    return This->lpVtbl->CreateQueryWriter(This, format, vendor, out);
}
PROXY HRESULT WINAPI IWICImagingFactory_CreateStream_Proxy(IWICImagingFactory *This, IWICStream **out)
{
    return This->lpVtbl->CreateStream(This, out);
}
PROXY HRESULT WINAPI IWICMetadataBlockReader_GetCount_Proxy(IWICMetadataBlockReader *This, UINT *count)
{
    return This->lpVtbl->GetCount(This, count);
}
PROXY HRESULT WINAPI IWICMetadataBlockReader_GetReaderByIndex_Proxy(IWICMetadataBlockReader *This, UINT index, IWICMetadataReader **out)
{
    return This->lpVtbl->GetReaderByIndex(This, index, out);
}
PROXY HRESULT WINAPI IWICMetadataQueryReader_GetContainerFormat_Proxy(IWICMetadataQueryReader *This, GUID *format)
{
    return This->lpVtbl->GetContainerFormat(This, format);
}
PROXY HRESULT WINAPI IWICMetadataQueryReader_GetEnumerator_Proxy(IWICMetadataQueryReader *This, IEnumString **out)
{
    return This->lpVtbl->GetEnumerator(This, out);
}
PROXY HRESULT WINAPI IWICMetadataQueryReader_GetLocation_Proxy(IWICMetadataQueryReader *This, UINT cch, WCHAR *ns, UINT *actual)
{
    return This->lpVtbl->GetLocation(This, cch, ns, actual);
}
PROXY HRESULT WINAPI IWICMetadataQueryReader_GetMetadataByName_Proxy(IWICMetadataQueryReader *This, LPCWSTR name, PROPVARIANT *value)
{
    return This->lpVtbl->GetMetadataByName(This, name, value);
}
PROXY HRESULT WINAPI IWICMetadataQueryWriter_RemoveMetadataByName_Proxy(IWICMetadataQueryWriter *This, LPCWSTR name)
{
    return This->lpVtbl->RemoveMetadataByName(This, name);
}
PROXY HRESULT WINAPI IWICMetadataQueryWriter_SetMetadataByName_Proxy(IWICMetadataQueryWriter *This, LPCWSTR name, const PROPVARIANT *value)
{
    return This->lpVtbl->SetMetadataByName(This, name, value);
}
PROXY HRESULT WINAPI IWICPalette_GetColorCount_Proxy(IWICPalette *This, UINT *count)
{
    return This->lpVtbl->GetColorCount(This, count);
}
PROXY HRESULT WINAPI IWICPalette_GetColors_Proxy(IWICPalette *This, UINT count, WICColor *colors, UINT *actual)
{
    return This->lpVtbl->GetColors(This, count, colors, actual);
}
PROXY HRESULT WINAPI IWICPalette_GetType_Proxy(IWICPalette *This, WICBitmapPaletteType *type)
{
    return This->lpVtbl->GetType(This, type);
}
PROXY HRESULT WINAPI IWICPalette_HasAlpha_Proxy(IWICPalette *This, BOOL *result)
{
    return This->lpVtbl->HasAlpha(This, result);
}
PROXY HRESULT WINAPI IWICPalette_InitializeCustom_Proxy(IWICPalette *This, WICColor *colors, UINT count)
{
    return This->lpVtbl->InitializeCustom(This, colors, count);
}
PROXY HRESULT WINAPI IWICPalette_InitializeFromBitmap_Proxy(IWICPalette *This, IWICBitmapSource *source, UINT count, BOOL transparent)
{
    return This->lpVtbl->InitializeFromBitmap(This, source, count, transparent);
}
PROXY HRESULT WINAPI IWICPalette_InitializeFromPalette_Proxy(IWICPalette *This, IWICPalette *palette)
{
    return This->lpVtbl->InitializeFromPalette(This, palette);
}
PROXY HRESULT WINAPI IWICPalette_InitializePredefined_Proxy(IWICPalette *This, WICBitmapPaletteType type, BOOL transparent)
{
    return This->lpVtbl->InitializePredefined(This, type, transparent);
}
PROXY HRESULT WINAPI IWICPixelFormatInfo_GetBitsPerPixel_Proxy(IWICPixelFormatInfo *This, UINT *bpp)
{
    return This->lpVtbl->GetBitsPerPixel(This, bpp);
}
PROXY HRESULT WINAPI IWICPixelFormatInfo_GetChannelCount_Proxy(IWICPixelFormatInfo *This, UINT *count)
{
    return This->lpVtbl->GetChannelCount(This, count);
}
PROXY HRESULT WINAPI IWICPixelFormatInfo_GetChannelMask_Proxy(IWICPixelFormatInfo *This, UINT channel, UINT size, BYTE *mask, UINT *actual)
{
    return This->lpVtbl->GetChannelMask(This, channel, size, mask, actual);
}
PROXY HRESULT WINAPI IWICStream_InitializeFromIStream_Proxy(IWICStream *This, IStream *stream)
{
    return This->lpVtbl->InitializeFromIStream(This, stream);
}
PROXY HRESULT WINAPI IWICStream_InitializeFromMemory_Proxy(IWICStream *This, BYTE *buffer, DWORD size)
{
    return This->lpVtbl->InitializeFromMemory(This, buffer, size);
}

PROXY HRESULT WINAPI IWICBitmapLock_GetDataPointer_STA_Proxy(IWICBitmapLock *This, UINT *size, BYTE **data)
{
    return This->lpVtbl->GetDataPointer(This, size, data);
}
PROXY HRESULT WINAPI IEnumString_Next_WIC_Proxy(IEnumString *This, ULONG count, LPOLESTR *items, ULONG *fetched)
{
    return This->lpVtbl->Next(This, count, items, fetched);
}
PROXY HRESULT WINAPI IEnumString_Reset_WIC_Proxy(IEnumString *This)
{
    return This->lpVtbl->Reset(This);
}
/* IPropertyBag2::Write (after QueryInterface, AddRef, Release and Read) */
PROXY HRESULT WINAPI IPropertyBag2_Write_Proxy(IPropertyBag2 *This, ULONG count, PROPBAG2 *bag, VARIANT *values)
{
    typedef HRESULT (STDMETHODCALLTYPE *WriteFn)(IPropertyBag2 *, ULONG, PROPBAG2 *, VARIANT *);
    return ((WriteFn)(*(void ***)This)[4])(This, count, bag, values);
}
PROXY HRESULT WINAPI WICCreateColorContext_Proxy(IWICImagingFactory *factory, IWICColorContext **out)
{
    return factory->lpVtbl->CreateColorContext(factory, out);
}
/* the source in the pixel format the frame encoder takes (the encoder
 * picks the closest format it supports when given the source's) */
PROXY HRESULT WINAPI WICSetEncoderFormat_Proxy(IWICBitmapSource *src, IWICPalette *pal, IWICBitmapFrameEncode *frame,
                                               IWICBitmapSource **out)
{
    if (!src || !frame || !out) return E_INVALIDARG;
    *out = 0;
    GUID have, want;
    HRESULT hr = src->lpVtbl->GetPixelFormat(src, &have);
    if (FAILED(hr)) return hr;
    want = have;
    if (FAILED(hr = frame->lpVtbl->SetPixelFormat(frame, &want))) return hr;
    if (pal) frame->lpVtbl->SetPalette(frame, pal);
    if (IsEqualGUID(&have, &want)) return src->lpVtbl->QueryInterface(src, &IID_IWICBitmapSource, (void **)out);
    IWICFormatConverter *c;
    if (FAILED(hr = converter_create(&c))) return hr;
    hr = c->lpVtbl->Initialize(c, src, &want, WICBitmapDitherTypeNone, pal, 0.0,
                               pal ? WICBitmapPaletteTypeCustom : WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) { c->lpVtbl->Release(c); return hr; }
    *out = (IWICBitmapSource *)c;
    return S_OK;
}
