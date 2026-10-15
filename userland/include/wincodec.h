/*
 * wincodec.h — the Windows Imaging Component (userland/windowscodecs):
 * the imaging factory, decoders, bitmaps, format converters, scalers,
 * palettes and streams.  The GUIDs and the method order of each
 * interface are Windows' (the vtables are the ABI programs call).
 */
#pragma once
#include <objbase.h>
#include <oleauto.h>

_NOVA_BEGIN

#ifndef NOVA_PROPVARIANT_DEFINED
#define NOVA_PROPVARIANT_DEFINED
typedef struct tagPROPVARIANT {
    VARTYPE vt;
    WORD    wReserved1, wReserved2, wReserved3;
    union {
        CHAR cVal; UCHAR bVal; SHORT iVal; USHORT uiVal; LONG lVal; ULONG ulVal; INT intVal; UINT uintVal;
        LARGE_INTEGER hVal; ULARGE_INTEGER uhVal; float fltVal; double dblVal;
        CLSID *puuid; LPSTR pszVal; LPWSTR pwszVal; IUnknown *punkVal;
        struct { ULONG cElems; UCHAR *pElems; } caub;
        struct { ULONG cbSize; BYTE *pBlobData; } blob;
    };
} PROPVARIANT, *LPPROPVARIANT;
#ifdef __cplusplus
typedef const PROPVARIANT &REFPROPVARIANT;
#else
typedef const PROPVARIANT *REFPROPVARIANT;
#endif
#endif

typedef UINT32 WICColor;
typedef GUID WICPixelFormatGUID;
typedef REFGUID REFWICPixelFormatGUID;
typedef struct WICRect { INT X, Y, Width, Height; } WICRect;
typedef struct WICBitmapPattern {
    ULARGE_INTEGER Position;
    ULONG Length;
    BYTE *Pattern, *Mask;
    BOOL EndOfStream;
} WICBitmapPattern;

typedef enum WICDecodeOptions { WICDecodeMetadataCacheOnDemand = 0, WICDecodeMetadataCacheOnLoad = 1 } WICDecodeOptions;
typedef enum WICBitmapCreateCacheOption { WICBitmapNoCache = 0, WICBitmapCacheOnDemand = 1, WICBitmapCacheOnLoad = 2 } WICBitmapCreateCacheOption;
typedef enum WICBitmapAlphaChannelOption { WICBitmapUseAlpha = 0, WICBitmapUsePremultipliedAlpha = 1, WICBitmapIgnoreAlpha = 2 } WICBitmapAlphaChannelOption;
typedef enum WICBitmapDecoderCapabilities {
    WICBitmapDecoderCapabilitySameEncoder = 0x1, WICBitmapDecoderCapabilityCanDecodeAllImages = 0x2,
    WICBitmapDecoderCapabilityCanDecodeSomeImages = 0x4, WICBitmapDecoderCapabilityCanEnumerateMetadata = 0x8,
    WICBitmapDecoderCapabilityCanDecodeThumbnail = 0x10
} WICBitmapDecoderCapabilities;
typedef enum WICBitmapDitherType {
    WICBitmapDitherTypeNone = 0, WICBitmapDitherTypeSolid = 0, WICBitmapDitherTypeOrdered4x4 = 1,
    WICBitmapDitherTypeOrdered8x8 = 2, WICBitmapDitherTypeOrdered16x16 = 3, WICBitmapDitherTypeSpiral4x4 = 4,
    WICBitmapDitherTypeSpiral8x8 = 5, WICBitmapDitherTypeDualSpiral4x4 = 6, WICBitmapDitherTypeDualSpiral8x8 = 7,
    WICBitmapDitherTypeErrorDiffusion = 8
} WICBitmapDitherType;
typedef enum WICBitmapEncoderCacheOption { WICBitmapEncoderCacheInMemory = 0, WICBitmapEncoderCacheTempFile = 1, WICBitmapEncoderNoCache = 2 } WICBitmapEncoderCacheOption;
typedef enum WICBitmapInterpolationMode {
    WICBitmapInterpolationModeNearestNeighbor = 0, WICBitmapInterpolationModeLinear = 1,
    WICBitmapInterpolationModeCubic = 2, WICBitmapInterpolationModeFant = 3,
    WICBitmapInterpolationModeHighQualityCubic = 4
} WICBitmapInterpolationMode;
typedef enum WICBitmapLockFlags { WICBitmapLockRead = 1, WICBitmapLockWrite = 2 } WICBitmapLockFlags;
typedef enum WICBitmapPaletteType {
    WICBitmapPaletteTypeCustom = 0, WICBitmapPaletteTypeMedianCut = 1, WICBitmapPaletteTypeFixedBW = 2,
    WICBitmapPaletteTypeFixedHalftone8 = 3, WICBitmapPaletteTypeFixedHalftone27 = 4,
    WICBitmapPaletteTypeFixedHalftone64 = 5, WICBitmapPaletteTypeFixedHalftone125 = 6,
    WICBitmapPaletteTypeFixedHalftone216 = 7, WICBitmapPaletteTypeFixedWebPalette = 7,
    WICBitmapPaletteTypeFixedHalftone252 = 8, WICBitmapPaletteTypeFixedHalftone256 = 9,
    WICBitmapPaletteTypeFixedGray4 = 10, WICBitmapPaletteTypeFixedGray16 = 11, WICBitmapPaletteTypeFixedGray256 = 12
} WICBitmapPaletteType;
typedef enum WICBitmapTransformOptions {
    WICBitmapTransformRotate0 = 0, WICBitmapTransformRotate90 = 1, WICBitmapTransformRotate180 = 2,
    WICBitmapTransformRotate270 = 3, WICBitmapTransformFlipHorizontal = 8, WICBitmapTransformFlipVertical = 0x10
} WICBitmapTransformOptions;
typedef enum WICColorContextType { WICColorContextUninitialized = 0, WICColorContextProfile = 1, WICColorContextExifColorSpace = 2 } WICColorContextType;
typedef enum WICComponentType {
    WICDecoder = 0x1, WICEncoder = 0x2, WICPixelFormatConverter = 0x4, WICMetadataReader = 0x8,
    WICMetadataWriter = 0x10, WICPixelFormat = 0x20, WICAllComponents = 0x3F
} WICComponentType;
typedef enum WICComponentSigning { WICComponentSigned = 0x1, WICComponentUnsigned = 0x2, WICComponentSafe = 0x4, WICComponentDisabled = (int)0x80000000 } WICComponentSigning;
typedef enum WICComponentEnumerateOptions { WICComponentEnumerateDefault = 0, WICComponentEnumerateRefresh = 1, WICComponentEnumerateDisabled = (int)0x80000000 } WICComponentEnumerateOptions;
typedef enum WICPixelFormatNumericRepresentation {
    WICPixelFormatNumericRepresentationUnspecified = 0, WICPixelFormatNumericRepresentationIndexed = 1,
    WICPixelFormatNumericRepresentationUnsignedInteger = 2, WICPixelFormatNumericRepresentationSignedInteger = 3,
    WICPixelFormatNumericRepresentationFixed = 4, WICPixelFormatNumericRepresentationFloat = 5
} WICPixelFormatNumericRepresentation;

#define WINCODEC_SDK_VERSION1 0x0236
#define WINCODEC_SDK_VERSION2 0x0237
#define WINCODEC_SDK_VERSION  WINCODEC_SDK_VERSION2

/* errors (FACILITY_WINCODEC_ERR) */
#define WINCODEC_ERR_GENERIC_ERROR      E_FAIL
#define WINCODEC_ERR_INVALIDPARAMETER   E_INVALIDARG
#define WINCODEC_ERR_OUTOFMEMORY        E_OUTOFMEMORY
#define WINCODEC_ERR_NOTIMPLEMENTED     E_NOTIMPL
#define WINCODEC_ERR_ABORTED            E_ABORT
#define WINCODEC_ERR_ACCESSDENIED       E_ACCESSDENIED
#define WINCODEC_ERR_VALUEOVERFLOW      ((HRESULT)0x80070216L)
#define WINCODEC_ERR_WRONGSTATE                      ((HRESULT)0x88982F04L)
#define WINCODEC_ERR_VALUEOUTOFRANGE                 ((HRESULT)0x88982F05L)
#define WINCODEC_ERR_UNKNOWNIMAGEFORMAT              ((HRESULT)0x88982F07L)
#define WINCODEC_ERR_UNSUPPORTEDVERSION              ((HRESULT)0x88982F0BL)
#define WINCODEC_ERR_NOTINITIALIZED                  ((HRESULT)0x88982F0CL)
#define WINCODEC_ERR_ALREADYLOCKED                   ((HRESULT)0x88982F0DL)
#define WINCODEC_ERR_PROPERTYNOTFOUND                ((HRESULT)0x88982F40L)
#define WINCODEC_ERR_PROPERTYNOTSUPPORTED            ((HRESULT)0x88982F41L)
#define WINCODEC_ERR_PROPERTYSIZE                    ((HRESULT)0x88982F42L)
#define WINCODEC_ERR_CODECPRESENT                    ((HRESULT)0x88982F43L)
#define WINCODEC_ERR_CODECNOTHUMBNAIL                ((HRESULT)0x88982F44L)
#define WINCODEC_ERR_PALETTEUNAVAILABLE              ((HRESULT)0x88982F45L)
#define WINCODEC_ERR_CODECTOOMANYSCANLINES           ((HRESULT)0x88982F46L)
#define WINCODEC_ERR_INTERNALERROR                   ((HRESULT)0x88982F48L)
#define WINCODEC_ERR_SOURCERECTDOESNOTMATCHDIMENSIONS ((HRESULT)0x88982F49L)
#define WINCODEC_ERR_COMPONENTNOTFOUND               ((HRESULT)0x88982F50L)
#define WINCODEC_ERR_IMAGESIZEOUTOFRANGE             ((HRESULT)0x88982F51L)
#define WINCODEC_ERR_TOOMUCHMETADATA                 ((HRESULT)0x88982F52L)
#define WINCODEC_ERR_BADIMAGE                        ((HRESULT)0x88982F60L)
#define WINCODEC_ERR_BADHEADER                       ((HRESULT)0x88982F61L)
#define WINCODEC_ERR_FRAMEMISSING                    ((HRESULT)0x88982F62L)
#define WINCODEC_ERR_BADMETADATAHEADER               ((HRESULT)0x88982F63L)
#define WINCODEC_ERR_BADSTREAMDATA                   ((HRESULT)0x88982F70L)
#define WINCODEC_ERR_STREAMWRITE                     ((HRESULT)0x88982F71L)
#define WINCODEC_ERR_STREAMREAD                      ((HRESULT)0x88982F72L)
#define WINCODEC_ERR_STREAMNOTAVAILABLE              ((HRESULT)0x88982F73L)
#define WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT          ((HRESULT)0x88982F80L)
#define WINCODEC_ERR_UNSUPPORTEDOPERATION            ((HRESULT)0x88982F81L)
#define WINCODEC_ERR_INVALIDREGISTRATION             ((HRESULT)0x88982F8AL)
#define WINCODEC_ERR_COMPONENTINITIALIZEFAILURE      ((HRESULT)0x88982F8BL)
#define WINCODEC_ERR_INSUFFICIENTBUFFER              ((HRESULT)0x88982F8CL)
#define WINCODEC_ERR_DUPLICATEMETADATAPRESENT        ((HRESULT)0x88982F8DL)
#define WINCODEC_ERR_PROPERTYUNEXPECTEDTYPE          ((HRESULT)0x88982F8EL)
#define WINCODEC_ERR_UNEXPECTEDSIZE                  ((HRESULT)0x88982F8FL)
#define WINCODEC_ERR_INVALIDQUERYREQUEST             ((HRESULT)0x88982F90L)
#define WINCODEC_ERR_UNEXPECTEDMETADATATYPE          ((HRESULT)0x88982F91L)
#define WINCODEC_ERR_REQUESTONLYVALIDATMETADATAROOT  ((HRESULT)0x88982F92L)
#define WINCODEC_ERR_INVALIDQUERYCHARACTER           ((HRESULT)0x88982F93L)
#define WINCODEC_ERR_WIN32ERROR                      ((HRESULT)0x88982F94L)
#define WINCODEC_ERR_INVALIDPROGRESSIVELEVEL         ((HRESULT)0x88982F95L)
#define WINCODEC_ERR_INVALIDJPEGSCANINDEX            ((HRESULT)0x88982F96L)

/* pixel formats */
DEFINE_GUID(GUID_WICPixelFormatDontCare, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x00);
DEFINE_GUID(GUID_WICPixelFormat1bppIndexed, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x01);
DEFINE_GUID(GUID_WICPixelFormat2bppIndexed, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x02);
DEFINE_GUID(GUID_WICPixelFormat4bppIndexed, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x03);
DEFINE_GUID(GUID_WICPixelFormat8bppIndexed, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x04);
DEFINE_GUID(GUID_WICPixelFormatBlackWhite, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x05);
DEFINE_GUID(GUID_WICPixelFormat2bppGray, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x06);
DEFINE_GUID(GUID_WICPixelFormat4bppGray, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x07);
DEFINE_GUID(GUID_WICPixelFormat8bppGray, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x08);
DEFINE_GUID(GUID_WICPixelFormat16bppGray, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0b);
DEFINE_GUID(GUID_WICPixelFormat8bppAlpha, 0xe6cd0116, 0xeeba, 0x4161, 0xaa, 0x85, 0x27, 0xdd, 0x9f, 0xb3, 0xa8, 0x95);
DEFINE_GUID(GUID_WICPixelFormat16bppBGR555, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x09);
DEFINE_GUID(GUID_WICPixelFormat16bppBGR565, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0a);
DEFINE_GUID(GUID_WICPixelFormat16bppBGRA5551, 0x05ec7c2b, 0xf1e6, 0x4961, 0xad, 0x46, 0xe1, 0xcc, 0x81, 0x0a, 0x87, 0xd2);
DEFINE_GUID(GUID_WICPixelFormat24bppBGR, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c);
DEFINE_GUID(GUID_WICPixelFormat24bppRGB, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0d);
DEFINE_GUID(GUID_WICPixelFormat32bppBGR, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0e);
DEFINE_GUID(GUID_WICPixelFormat32bppBGRA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f);
DEFINE_GUID(GUID_WICPixelFormat32bppPBGRA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10);
DEFINE_GUID(GUID_WICPixelFormat32bppRGB, 0xd98c6b95, 0x3efe, 0x47d6, 0xbb, 0x25, 0xeb, 0x17, 0x48, 0xab, 0x0c, 0xf1);
DEFINE_GUID(GUID_WICPixelFormat32bppRGBA, 0xf5c7ad2d, 0x6a8d, 0x43dd, 0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9);
DEFINE_GUID(GUID_WICPixelFormat32bppPRGBA, 0x3cc4a650, 0xa527, 0x4d37, 0xa9, 0x16, 0x31, 0x42, 0xc7, 0xeb, 0xed, 0xba);
DEFINE_GUID(GUID_WICPixelFormat32bppGrayFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x11);
DEFINE_GUID(GUID_WICPixelFormat48bppRGB, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x15);
DEFINE_GUID(GUID_WICPixelFormat48bppBGR, 0xe605a384, 0xb468, 0x46ce, 0xbb, 0x2e, 0x36, 0xf1, 0x80, 0xe6, 0x43, 0x13);
DEFINE_GUID(GUID_WICPixelFormat64bppRGB, 0xa1182111, 0x186d, 0x4d42, 0xbc, 0x6a, 0x9c, 0x83, 0x03, 0xa8, 0xdf, 0xf9);
DEFINE_GUID(GUID_WICPixelFormat64bppRGBA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x16);
DEFINE_GUID(GUID_WICPixelFormat64bppBGRA, 0x1562ff7c, 0xd352, 0x46f9, 0x97, 0x9e, 0x42, 0x97, 0x6b, 0x79, 0x22, 0x46);
DEFINE_GUID(GUID_WICPixelFormat64bppPRGBA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x17);
DEFINE_GUID(GUID_WICPixelFormat64bppPBGRA, 0x8c518e8e, 0xa4ec, 0x468b, 0xae, 0x70, 0xc9, 0xa3, 0x5a, 0x9c, 0x55, 0x30);
DEFINE_GUID(GUID_WICPixelFormat16bppGrayFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x13);
DEFINE_GUID(GUID_WICPixelFormat32bppBGR101010, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x14);
DEFINE_GUID(GUID_WICPixelFormat48bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x12);
DEFINE_GUID(GUID_WICPixelFormat48bppBGRFixedPoint, 0x49ca140e, 0xcab6, 0x493b, 0x9d, 0xdf, 0x60, 0x18, 0x7c, 0x37, 0x53, 0x2a);
DEFINE_GUID(GUID_WICPixelFormat96bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x18);
DEFINE_GUID(GUID_WICPixelFormat96bppRGBFloat, 0xe3fed78f, 0xe8db, 0x4acf, 0x84, 0xc1, 0xe9, 0x7f, 0x61, 0x36, 0xb3, 0x27);
DEFINE_GUID(GUID_WICPixelFormat128bppRGBAFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x19);
DEFINE_GUID(GUID_WICPixelFormat128bppPRGBAFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1a);
DEFINE_GUID(GUID_WICPixelFormat128bppRGBFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1b);
DEFINE_GUID(GUID_WICPixelFormat32bppCMYK, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1c);
DEFINE_GUID(GUID_WICPixelFormat64bppRGBAFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1d);
DEFINE_GUID(GUID_WICPixelFormat64bppBGRAFixedPoint, 0x356de33c, 0x54d2, 0x4a23, 0xbb, 0x4, 0x9b, 0x7b, 0xf9, 0xb1, 0xd4, 0x2d);
DEFINE_GUID(GUID_WICPixelFormat64bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x40);
DEFINE_GUID(GUID_WICPixelFormat128bppRGBAFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1e);
DEFINE_GUID(GUID_WICPixelFormat128bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x41);
DEFINE_GUID(GUID_WICPixelFormat64bppRGBAHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x3a);
DEFINE_GUID(GUID_WICPixelFormat64bppPRGBAHalf, 0x58ad26c2, 0xc623, 0x4d9d, 0xb3, 0x20, 0x38, 0x7e, 0x49, 0xf8, 0xc4, 0x42);
DEFINE_GUID(GUID_WICPixelFormat64bppRGBHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x42);
DEFINE_GUID(GUID_WICPixelFormat48bppRGBHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x3b);
DEFINE_GUID(GUID_WICPixelFormat32bppRGBE, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x3d);
DEFINE_GUID(GUID_WICPixelFormat16bppGrayHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x3e);
DEFINE_GUID(GUID_WICPixelFormat32bppGrayFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x3f);
DEFINE_GUID(GUID_WICPixelFormat32bppRGBA1010102, 0x25238d72, 0xfcf9, 0x4522, 0xb5, 0x14, 0x55, 0x78, 0xe5, 0xad, 0x55, 0xe0);
DEFINE_GUID(GUID_WICPixelFormat32bppRGBA1010102XR, 0x00de6b9a, 0xc101, 0x434b, 0xb5, 0x02, 0xd0, 0x16, 0x5e, 0xe1, 0x12, 0x2c);
DEFINE_GUID(GUID_WICPixelFormat32bppR10G10B10A2, 0x604e1bb5, 0x8a3c, 0x4b65, 0xb1, 0x1c, 0xbc, 0x0b, 0x8d, 0xd7, 0x5b, 0x7f);
DEFINE_GUID(GUID_WICPixelFormat32bppR10G10B10A2HDR10, 0x9c215c5d, 0x1acc, 0x4f0e, 0xa4, 0xbc, 0x70, 0xfb, 0x3a, 0xe8, 0xfd, 0x28);
DEFINE_GUID(GUID_WICPixelFormat64bppCMYK, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1f);
DEFINE_GUID(GUID_WICPixelFormat24bpp3Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x20);
DEFINE_GUID(GUID_WICPixelFormat32bpp4Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x21);
DEFINE_GUID(GUID_WICPixelFormat40bpp5Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x22);
DEFINE_GUID(GUID_WICPixelFormat48bpp6Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x23);
DEFINE_GUID(GUID_WICPixelFormat56bpp7Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x24);
DEFINE_GUID(GUID_WICPixelFormat64bpp8Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x25);
DEFINE_GUID(GUID_WICPixelFormat48bpp3Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x26);
DEFINE_GUID(GUID_WICPixelFormat64bpp4Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x27);
DEFINE_GUID(GUID_WICPixelFormat80bpp5Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x28);
DEFINE_GUID(GUID_WICPixelFormat96bpp6Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x29);
DEFINE_GUID(GUID_WICPixelFormat112bpp7Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x2a);
DEFINE_GUID(GUID_WICPixelFormat128bpp8Channels, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x2b);
DEFINE_GUID(GUID_WICPixelFormat40bppCMYKAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x2c);
DEFINE_GUID(GUID_WICPixelFormat80bppCMYKAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x2d);
DEFINE_GUID(GUID_WICPixelFormat32bpp3ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x2e);
DEFINE_GUID(GUID_WICPixelFormat40bpp4ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x2f);
DEFINE_GUID(GUID_WICPixelFormat48bpp5ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x30);
DEFINE_GUID(GUID_WICPixelFormat56bpp6ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x31);
DEFINE_GUID(GUID_WICPixelFormat64bpp7ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x32);
DEFINE_GUID(GUID_WICPixelFormat72bpp8ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x33);
DEFINE_GUID(GUID_WICPixelFormat64bpp3ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x34);
DEFINE_GUID(GUID_WICPixelFormat80bpp4ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x35);
DEFINE_GUID(GUID_WICPixelFormat96bpp5ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x36);
DEFINE_GUID(GUID_WICPixelFormat112bpp6ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x37);
DEFINE_GUID(GUID_WICPixelFormat128bpp7ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x38);
DEFINE_GUID(GUID_WICPixelFormat144bpp8ChannelsAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x39);
DEFINE_GUID(GUID_WICPixelFormat8bppY, 0x91b4db54, 0x2df9, 0x42f0, 0xb4, 0x49, 0x29, 0x09, 0xbb, 0x3d, 0xf8, 0x8e);
DEFINE_GUID(GUID_WICPixelFormat8bppCb, 0x1339f224, 0x6bfe, 0x4c3e, 0x93, 0x02, 0xe4, 0xf3, 0xa6, 0xd0, 0xca, 0x2a);
DEFINE_GUID(GUID_WICPixelFormat8bppCr, 0xb8145053, 0x2116, 0x49f0, 0x88, 0x35, 0xed, 0x84, 0x4b, 0x20, 0x5c, 0x51);
DEFINE_GUID(GUID_WICPixelFormat16bppCbCr, 0xff95ba6e, 0x11e0, 0x4263, 0xbb, 0x45, 0x01, 0x72, 0x1f, 0x34, 0x60, 0xa4);
DEFINE_GUID(GUID_WICPixelFormat16bppYQuantizedDctCoefficients, 0xa355f433, 0x48e8, 0x4a42, 0x84, 0xd8, 0xe2, 0xaa, 0x26, 0xca, 0x80, 0xa4);
DEFINE_GUID(GUID_WICPixelFormat16bppCbQuantizedDctCoefficients, 0xd2c4ff61, 0x56a5, 0x49c2, 0x8b, 0x5c, 0x4c, 0x19, 0x25, 0x96, 0x48, 0x37);
DEFINE_GUID(GUID_WICPixelFormat16bppCrQuantizedDctCoefficients, 0x2fe354f0, 0x1680, 0x42d8, 0x92, 0x31, 0xe7, 0x3c, 0x05, 0x65, 0xbf, 0xc1);

/* classes */
DEFINE_GUID(CLSID_WICImagingFactory, 0xcacaf262, 0x9370, 0x4615, 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a);
DEFINE_GUID(CLSID_WICImagingFactory1, 0xcacaf262, 0x9370, 0x4615, 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a);
DEFINE_GUID(CLSID_WICImagingFactory2, 0x317d06e8, 0x5f24, 0x433d, 0xbd, 0xf7, 0x79, 0xce, 0x68, 0xd8, 0xab, 0xc2);
DEFINE_GUID(CLSID_WICBmpDecoder, 0x6b462062, 0x7cbf, 0x400d, 0x9f, 0xdb, 0x81, 0x3d, 0xd1, 0x0f, 0x27, 0x78);
DEFINE_GUID(CLSID_WICPngDecoder, 0x389ea17b, 0x5078, 0x4cde, 0xb6, 0xef, 0x25, 0xc1, 0x51, 0x75, 0xc7, 0x51);
DEFINE_GUID(CLSID_WICPngDecoder1, 0x389ea17b, 0x5078, 0x4cde, 0xb6, 0xef, 0x25, 0xc1, 0x51, 0x75, 0xc7, 0x51);
DEFINE_GUID(CLSID_WICPngDecoder2, 0xe018945b, 0xaa86, 0x4008, 0x9b, 0xd4, 0x67, 0x77, 0xa1, 0xe4, 0x0c, 0x11);
DEFINE_GUID(CLSID_WICIcoDecoder, 0xc61bfcdf, 0x2e0f, 0x4aad, 0xa8, 0xd7, 0xe0, 0x6b, 0xaf, 0xeb, 0xcd, 0xfe);
DEFINE_GUID(CLSID_WICJpegDecoder, 0x9456a480, 0xe88b, 0x43ea, 0x9e, 0x73, 0x0b, 0x2d, 0x9b, 0x71, 0xb1, 0xca);
DEFINE_GUID(CLSID_WICGifDecoder, 0x381dda3c, 0x9ce9, 0x4834, 0xa2, 0x3e, 0x1f, 0x98, 0xf8, 0xfc, 0x52, 0xbe);
DEFINE_GUID(CLSID_WICTiffDecoder, 0xb54e85d9, 0xfe23, 0x499f, 0x8b, 0x88, 0x6a, 0xce, 0xa7, 0x13, 0x75, 0x2b);
DEFINE_GUID(CLSID_WICWmpDecoder, 0xa26cec36, 0x234c, 0x4950, 0xae, 0x16, 0xe3, 0x4a, 0xac, 0xe7, 0x1d, 0x0d);
DEFINE_GUID(CLSID_WICDdsDecoder, 0x9053699f, 0xa341, 0x429d, 0x9e, 0x90, 0xee, 0x43, 0x7c, 0xf8, 0x0c, 0x73);
DEFINE_GUID(CLSID_WICBmpEncoder, 0x69be8bb4, 0xd66d, 0x47c8, 0x86, 0x5a, 0xed, 0x15, 0x89, 0x43, 0x37, 0x82);
DEFINE_GUID(CLSID_WICPngEncoder, 0x27949969, 0x876a, 0x41d7, 0x94, 0x47, 0x56, 0x8f, 0x6a, 0x35, 0xa4, 0xdc);
DEFINE_GUID(CLSID_WICJpegEncoder, 0x1a34f5c1, 0x4a5a, 0x46dc, 0xb6, 0x44, 0x1f, 0x45, 0x67, 0xe7, 0xa6, 0x76);
DEFINE_GUID(CLSID_WICGifEncoder, 0x114f5598, 0x0b22, 0x40a0, 0x86, 0xa1, 0xc8, 0x3e, 0xa4, 0x95, 0xad, 0xbd);
DEFINE_GUID(CLSID_WICTiffEncoder, 0x0131be10, 0x2001, 0x4c5f, 0xa9, 0xb0, 0xcc, 0x88, 0xfa, 0xb6, 0x4c, 0xe8);
DEFINE_GUID(CLSID_WICWmpEncoder, 0xac4ce3cb, 0xe1c1, 0x44cd, 0x82, 0x15, 0x5a, 0x16, 0x65, 0x50, 0x9e, 0xc2);
DEFINE_GUID(CLSID_WICDdsEncoder, 0xa61dde94, 0x66ce, 0x4ac1, 0x88, 0x1b, 0x71, 0x68, 0x05, 0x88, 0x89, 0x5e);
DEFINE_GUID(CLSID_WICAdngDecoder, 0x981d9411, 0x909e, 0x42a7, 0x8f, 0x5d, 0xa7, 0x47, 0xff, 0x05, 0x2e, 0xdb);
DEFINE_GUID(CLSID_WICJpegQualcommPhoneEncoder, 0x68ed5c62, 0xf534, 0x4979, 0xb2, 0xb3, 0x68, 0x6a, 0x12, 0xb2, 0xb3, 0x4c);
DEFINE_GUID(CLSID_WICHeifDecoder, 0xe9a4a80a, 0x44fe, 0x4de4, 0x89, 0x71, 0x71, 0x50, 0xb1, 0x0a, 0x51, 0x99);
DEFINE_GUID(CLSID_WICHeifEncoder, 0x0dbecec1, 0x9eb3, 0x4860, 0x9c, 0x6f, 0xdd, 0xbe, 0x86, 0x63, 0x45, 0x75);
DEFINE_GUID(CLSID_WICWebpDecoder, 0x7693e886, 0x51c9, 0x4070, 0x84, 0x19, 0x9f, 0x70, 0x73, 0x8e, 0xc8, 0xfa);
DEFINE_GUID(CLSID_WICRAWDecoder, 0x41945702, 0x8302, 0x44a6, 0x94, 0x45, 0xac, 0x98, 0xe8, 0xaf, 0xa0, 0x86);
DEFINE_GUID(CLSID_WICDefaultFormatConverter, 0x1a3f11dc, 0xb514, 0x4b17, 0x8c, 0x5f, 0x21, 0x54, 0x51, 0x38, 0x52, 0xf1);
DEFINE_GUID(CLSID_WICFormatConverterHighColor, 0xac75d454, 0x9f37, 0x48f8, 0xb9, 0x72, 0x4e, 0x19, 0xbc, 0x85, 0x60, 0x11);
DEFINE_GUID(CLSID_WICFormatConverterNChannel, 0xc17cabb2, 0xd4a3, 0x47d7, 0xa5, 0x57, 0x33, 0x9b, 0x2e, 0xfb, 0xd4, 0xf1);
DEFINE_GUID(CLSID_WICFormatConverterWMPhoto, 0x9cb5172b, 0xd600, 0x46ba, 0xab, 0x77, 0x77, 0xbb, 0x7e, 0x3a, 0x00, 0xd9);
DEFINE_GUID(CLSID_WICPlanarFormatConverter, 0x184132b8, 0x32f8, 0x4784, 0x91, 0x31, 0xdd, 0x72, 0x24, 0xb2, 0x34, 0x38);
DEFINE_GUID(CLSID_WICImagingCategories, 0xfae3d380, 0xfea4, 0x4623, 0x8c, 0x75, 0xc6, 0xb6, 0x11, 0x10, 0xb6, 0x81);

/* container formats and vendors */
DEFINE_GUID(GUID_ContainerFormatBmp, 0x0af1d87e, 0xfcfe, 0x4188, 0xbd, 0xeb, 0xa7, 0x90, 0x64, 0x71, 0xcb, 0xe3);
DEFINE_GUID(GUID_ContainerFormatPng, 0x1b7cfaf4, 0x713f, 0x473c, 0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf);
DEFINE_GUID(GUID_ContainerFormatIco, 0xa3a860c4, 0x338f, 0x4c17, 0x91, 0x9a, 0xfb, 0xa4, 0xb5, 0x62, 0x8f, 0x21);
DEFINE_GUID(GUID_ContainerFormatJpeg, 0x19e4a5aa, 0x5662, 0x4fc5, 0xa0, 0xc0, 0x17, 0x58, 0x02, 0x8e, 0x10, 0x57);
DEFINE_GUID(GUID_ContainerFormatTiff, 0x163bcc30, 0xe2e9, 0x4f0b, 0x96, 0x1d, 0xa3, 0xe9, 0xfd, 0xb7, 0x88, 0xa3);
DEFINE_GUID(GUID_ContainerFormatGif, 0x1f8a5601, 0x7d4d, 0x4cbd, 0x9c, 0x82, 0x1b, 0xc8, 0xd4, 0xee, 0xb9, 0xa5);
DEFINE_GUID(GUID_ContainerFormatWmp, 0x57a37caa, 0x367a, 0x4540, 0x91, 0x6b, 0xf1, 0x83, 0xc5, 0x09, 0x3a, 0x4b);
DEFINE_GUID(GUID_ContainerFormatDds, 0x9967cb95, 0x2e85, 0x4ac8, 0x8c, 0xa2, 0x83, 0xd7, 0xcc, 0xd4, 0x25, 0xc9);
DEFINE_GUID(GUID_ContainerFormatAdng, 0xf3ff6d0d, 0x38c0, 0x41c4, 0xb1, 0xfe, 0x1f, 0x38, 0x24, 0xf1, 0x7b, 0x84);
DEFINE_GUID(GUID_ContainerFormatHeif, 0xe1e62521, 0x6787, 0x405b, 0xa3, 0x39, 0x50, 0x07, 0x15, 0xb5, 0x76, 0x3f);
DEFINE_GUID(GUID_ContainerFormatWebp, 0xe094b0e2, 0x67f2, 0x45b3, 0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3);
DEFINE_GUID(GUID_ContainerFormatRaw, 0xfe99ce60, 0xf19c, 0x433c, 0xa3, 0xae, 0x00, 0xac, 0xef, 0xa9, 0xca, 0x21);
DEFINE_GUID(GUID_VendorMicrosoft, 0xf0e749ca, 0xedef, 0x4589, 0xa7, 0x3a, 0xee, 0x0e, 0x62, 0x6a, 0x2a, 0x2b);
DEFINE_GUID(GUID_VendorMicrosoftBuiltIn, 0x257a30fd, 0x6b6, 0x462b, 0xae, 0xa4, 0x63, 0xf7, 0xb, 0x86, 0xe5, 0x33);

/* metadata formats */
DEFINE_GUID(GUID_MetadataFormatUnknown, 0xa45e592f, 0x9078, 0x4a7c, 0xad, 0xb5, 0x4e, 0xdc, 0x4f, 0xd6, 0x1b, 0x1f);
DEFINE_GUID(GUID_MetadataFormatChunkcHRM, 0x9db3655b, 0x2842, 0x44b3, 0x80, 0x67, 0x12, 0xe9, 0xb3, 0x75, 0x55, 0x6a);
DEFINE_GUID(GUID_MetadataFormatChunkgAMA, 0xf00935a5, 0x1d5d, 0x4cd1, 0x81, 0xb2, 0x93, 0x24, 0xd7, 0xec, 0xa7, 0x81);
DEFINE_GUID(GUID_MetadataFormatChunktEXt, 0x568d8936, 0xc0a9, 0x4923, 0x90, 0x5d, 0xdf, 0x2b, 0x38, 0x23, 0x8f, 0xbc);
DEFINE_GUID(GUID_MetadataFormatChunktIME, 0x6b00ae2d, 0xe24b, 0x460a, 0x98, 0xb6, 0x87, 0x8b, 0xd0, 0x30, 0x72, 0xfd);
DEFINE_GUID(GUID_MetadataFormatIfd, 0x537396c6, 0x2d8a, 0x4bb6, 0x9b, 0xf8, 0x2f, 0x0a, 0x8e, 0x2a, 0x3a, 0xdf);
DEFINE_GUID(GUID_MetadataFormatExif, 0x1c3c4f9d, 0xb84a, 0x467d, 0x94, 0x93, 0x36, 0xcf, 0xbd, 0x59, 0xea, 0x57);
DEFINE_GUID(GUID_MetadataFormatXMP, 0xbb5acc38, 0xf216, 0x4cec, 0xa6, 0xc5, 0x5f, 0x6e, 0x73, 0x97, 0x63, 0xa9);
DEFINE_GUID(GUID_MetadataFormatXMPStruct, 0x22383cf1, 0xed17, 0x4e2e, 0xaf, 0x17, 0xd8, 0x5b, 0x8f, 0x6b, 0x30, 0xd0);
DEFINE_GUID(GUID_MetadataFormatIMD, 0xbd2bb086, 0x4d52, 0x48dd, 0x96, 0x77, 0xdb, 0x48, 0x3e, 0x85, 0xae, 0x8f);
DEFINE_GUID(GUID_MetadataFormatLSD, 0xe256031e, 0x6299, 0x4929, 0xb9, 0x8d, 0x5a, 0xc8, 0x84, 0xaf, 0xba, 0x92);
DEFINE_GUID(GUID_MetadataFormatGCE, 0x2a25cad8, 0xdeeb, 0x4c69, 0xa7, 0x88, 0x0e, 0xc2, 0x26, 0x6d, 0xca, 0xfd);
DEFINE_GUID(GUID_MetadataFormatAPE, 0x2e043dc2, 0xc967, 0x4e05, 0x87, 0x5e, 0x61, 0x8b, 0xf6, 0x7e, 0x85, 0xc3);
DEFINE_GUID(GUID_MetadataFormatGifComment, 0xc4b6e0e0, 0xcfb4, 0x4ad3, 0xab, 0x33, 0x9a, 0xad, 0x23, 0x55, 0xa3, 0x4a);
DEFINE_GUID(GUID_MetadataFormatSubIfd, 0x58a2e128, 0x2db9, 0x4e57, 0xbb, 0x14, 0x51, 0x77, 0x89, 0x1e, 0xd3, 0x31);
DEFINE_GUID(GUID_MetadataFormatGps, 0x7134ab8a, 0x9351, 0x44ad, 0xaf, 0x62, 0x44, 0x8d, 0xb6, 0xb5, 0x02, 0xec);
DEFINE_GUID(GUID_MetadataFormatInterop, 0xed686f8e, 0x681f, 0x4c8b, 0xbd, 0x41, 0xa8, 0xad, 0xdb, 0xf6, 0xb3, 0xfc);
DEFINE_GUID(GUID_MetadataFormatApp0, 0x79007028, 0x268d, 0x45d6, 0xa3, 0xc2, 0x35, 0x4e, 0x6a, 0x50, 0x4b, 0xc9);
DEFINE_GUID(GUID_MetadataFormatApp1, 0x8fd3dfc3, 0xf951, 0x492b, 0x81, 0x7f, 0x69, 0xc2, 0xe6, 0xd9, 0xa5, 0xb0);
DEFINE_GUID(GUID_MetadataFormatApp13, 0x326556a2, 0xf502, 0x4354, 0x9c, 0xc0, 0x8e, 0x3f, 0x48, 0xea, 0xf6, 0xb5);
DEFINE_GUID(GUID_MetadataFormatIPTC, 0x4fab0914, 0xe129, 0x4087, 0xa1, 0xd1, 0xbc, 0x81, 0x2d, 0x45, 0xa7, 0xb5);
DEFINE_GUID(GUID_MetadataFormatIRB, 0x16100d66, 0x8570, 0x4bb9, 0xb9, 0x2d, 0xfd, 0xa4, 0xb2, 0x3e, 0xce, 0x67);
DEFINE_GUID(GUID_MetadataFormat8BIMIPTC, 0x0010568c, 0x0852, 0x4e6a, 0xb1, 0x91, 0x5c, 0x33, 0xac, 0x5b, 0x04, 0x30);
DEFINE_GUID(GUID_MetadataFormat8BIMResolutionInfo, 0x739f305d, 0x81db, 0x43cb, 0xac, 0x5e, 0x55, 0x01, 0x3e, 0xf9, 0xf0, 0x03);
DEFINE_GUID(GUID_MetadataFormat8BIMIPTCDigest, 0x1ca32285, 0x9ccd, 0x4786, 0x8b, 0xd8, 0x79, 0x53, 0x9d, 0xb6, 0xa0, 0x06);
DEFINE_GUID(GUID_MetadataFormatThumbnail, 0x243dcee9, 0x8703, 0x40ee, 0x8e, 0xf0, 0x22, 0xa6, 0x0, 0xb8, 0x5, 0x8c);
DEFINE_GUID(GUID_MetadataFormatXMPBag, 0x833cca5f, 0xdcb7, 0x4516, 0x80, 0x6f, 0x65, 0x96, 0xab, 0x26, 0xdc, 0xe4);
DEFINE_GUID(GUID_MetadataFormatXMPSeq, 0x63e8df02, 0xeb6c, 0x456c, 0xa2, 0x24, 0xb2, 0x5e, 0x79, 0x4f, 0xd6, 0x48);
DEFINE_GUID(GUID_MetadataFormatXMPAlt, 0x7b08a675, 0x91aa, 0x481b, 0xa7, 0x98, 0x4d, 0xa9, 0x49, 0x08, 0x61, 0x3b);
DEFINE_GUID(GUID_MetadataFormatJpegChrominance, 0xf73d0dcf, 0xcec6, 0x4f85, 0x9b, 0x0e, 0x1c, 0x39, 0x56, 0xb1, 0xbe, 0xf7);
DEFINE_GUID(GUID_MetadataFormatJpegLuminance, 0x86908007, 0xedfc, 0x4860, 0x8d, 0x4b, 0x4e, 0xe6, 0xe8, 0x3e, 0x60, 0x58);
DEFINE_GUID(GUID_MetadataFormatJpegComment, 0x220e5f33, 0xafd3, 0x474e, 0x9d, 0x31, 0x7d, 0x4f, 0xe7, 0x30, 0xf5, 0x57);
DEFINE_GUID(GUID_MetadataFormatChunkbKGD, 0xe14d3571, 0x6b47, 0x4dea, 0xb6, 0xa, 0x87, 0xce, 0xa, 0x78, 0xdf, 0xb7);
DEFINE_GUID(GUID_MetadataFormatChunkiTXt, 0xc2bec729, 0xb68, 0x4b77, 0xaa, 0xe, 0x62, 0x95, 0xa6, 0xac, 0x18, 0x14);
DEFINE_GUID(GUID_MetadataFormatChunkhIST, 0xc59a82da, 0xdb74, 0x48a4, 0xbd, 0x6a, 0xb6, 0x9c, 0x49, 0x31, 0xef, 0x95);
DEFINE_GUID(GUID_MetadataFormatChunkiCCP, 0xeb4349ab, 0xb685, 0x450f, 0x91, 0xb5, 0xe8, 0x2, 0xe8, 0x92, 0x53, 0x6c);
DEFINE_GUID(GUID_MetadataFormatChunksRGB, 0xc115fd36, 0xcc6f, 0x4e3f, 0x83, 0x63, 0x52, 0x4b, 0x87, 0xc6, 0xb0, 0xd9);
DEFINE_GUID(GUID_MetadataFormatDds, 0x4a064603, 0x8c33, 0x4e60, 0x9c, 0x29, 0x13, 0x62, 0x31, 0x70, 0x2d, 0x08);
DEFINE_GUID(GUID_MetadataFormatHeif, 0x817ef3e1, 0x1288, 0x45f4, 0xa8, 0x52, 0x26, 0x0d, 0x9e, 0x7c, 0xce, 0x83);
DEFINE_GUID(GUID_MetadataFormatHeifHDR, 0x568b8d8a, 0x1e65, 0x438c, 0x89, 0x68, 0xd6, 0x0e, 0x10, 0x12, 0xbe, 0xb9);
DEFINE_GUID(GUID_MetadataFormatWebpANIM, 0x6dc4fda6, 0x78e6, 0x4102, 0xae, 0x35, 0xbc, 0xfa, 0x1e, 0xdc, 0xc7, 0x8b);
DEFINE_GUID(GUID_MetadataFormatWebpANMF, 0x43c105ee, 0xb93b, 0x4abb, 0xb0, 0x03, 0xa0, 0x8c, 0x0d, 0x87, 0x04, 0x71);

/* interfaces */
DEFINE_GUID(IID_IWICColorContext, 0x3c613a02, 0x34b2, 0x44ea, 0x9a, 0x7c, 0x45, 0xae, 0xa9, 0xc6, 0xfd, 0x6d);
DEFINE_GUID(IID_IWICBitmapSource, 0x00000120, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
DEFINE_GUID(IID_IWICBitmapLock, 0x00000123, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
DEFINE_GUID(IID_IWICBitmapFlipRotator, 0x5009834f, 0x2d6a, 0x41ce, 0x9e, 0x1b, 0x17, 0xc5, 0xaf, 0xf7, 0xa7, 0x82);
DEFINE_GUID(IID_IWICBitmap, 0x00000121, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
DEFINE_GUID(IID_IWICPalette, 0x00000040, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
DEFINE_GUID(IID_IWICComponentInfo, 0x23bc3f0a, 0x698b, 0x4357, 0x88, 0x6b, 0xf2, 0x4d, 0x50, 0x67, 0x13, 0x34);
DEFINE_GUID(IID_IWICMetadataQueryReader, 0x30989668, 0xe1c9, 0x4597, 0xb3, 0x95, 0x45, 0x8e, 0xed, 0xb8, 0x08, 0xdf);
DEFINE_GUID(IID_IWICMetadataQueryWriter, 0xa721791a, 0x0def, 0x4d06, 0xbd, 0x91, 0x21, 0x18, 0xbf, 0x1d, 0xb1, 0x0b);
DEFINE_GUID(IID_IWICBitmapFrameDecode, 0x3b16811b, 0x6a43, 0x4ec9, 0xa8, 0x13, 0x3d, 0x93, 0x0c, 0x13, 0xb9, 0x40);
DEFINE_GUID(IID_IWICPixelFormatInfo, 0xe8eda601, 0x3d48, 0x431a, 0xab, 0x44, 0x69, 0x05, 0x9b, 0xe8, 0x8b, 0xbe);
DEFINE_GUID(IID_IWICPixelFormatInfo2, 0xa9db33a2, 0xaf5f, 0x43c7, 0xb6, 0x79, 0x74, 0xf5, 0x98, 0x4b, 0x5a, 0xa4);
DEFINE_GUID(IID_IWICBitmapCodecInfo, 0xe87a44c4, 0xb76e, 0x4c47, 0x8b, 0x09, 0x29, 0x8e, 0xb1, 0x2a, 0x27, 0x14);
DEFINE_GUID(IID_IWICBitmapDecoderInfo, 0xd8cd007f, 0xd08f, 0x4191, 0x9b, 0xfc, 0x23, 0x6e, 0xa7, 0xf0, 0xe4, 0xb5);
DEFINE_GUID(IID_IWICBitmapDecoder, 0x9edde9e7, 0x8dee, 0x47ea, 0x99, 0xdf, 0xe6, 0xfa, 0xf2, 0xed, 0x44, 0xbf);
DEFINE_GUID(IID_IWICBitmapFrameEncode, 0x00000105, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
DEFINE_GUID(IID_IWICBitmapEncoderInfo, 0x94c9b4ee, 0xa09f, 0x4f92, 0x8a, 0x1e, 0x4a, 0x9b, 0xce, 0x7e, 0x76, 0xfb);
DEFINE_GUID(IID_IWICBitmapEncoder, 0x00000103, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
DEFINE_GUID(IID_IWICFormatConverter, 0x00000301, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
DEFINE_GUID(IID_IWICFormatConverterInfo, 0x9f34fb65, 0x13f4, 0x4f15, 0xbc, 0x57, 0x37, 0x26, 0xb5, 0xe5, 0x3d, 0x9f);
DEFINE_GUID(IID_IWICStream, 0x135ff860, 0x22b7, 0x4ddf, 0xb0, 0xf6, 0x21, 0x8f, 0x4f, 0x29, 0x9a, 0x43);
DEFINE_GUID(IID_IWICBitmapScaler, 0x00000302, 0xa8f2, 0x4877, 0xba, 0x0a, 0xfd, 0x2b, 0x66, 0x45, 0xfb, 0x94);
DEFINE_GUID(IID_IWICBitmapClipper, 0xe4fbcf03, 0x223d, 0x4e81, 0x93, 0x33, 0xd6, 0x35, 0x55, 0x6d, 0xd1, 0xb5);
DEFINE_GUID(IID_IWICColorTransform, 0xb66f034f, 0xd0e2, 0x40ab, 0xb4, 0x36, 0x6d, 0xe3, 0x9e, 0x32, 0x1a, 0x94);
DEFINE_GUID(IID_IWICFastMetadataEncoder, 0xb84e2c09, 0x78c9, 0x4ac4, 0x8b, 0xd3, 0x52, 0x4a, 0xe1, 0x66, 0x3a, 0x2f);
DEFINE_GUID(IID_IWICImageEncoder, 0x04c75bf8, 0x3ce1, 0x473b, 0xac, 0xc5, 0x3c, 0xc4, 0xf5, 0xe9, 0x49, 0x99);
DEFINE_GUID(IID_IWICImagingFactory, 0xec5ec8a9, 0xc395, 0x4314, 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70);
DEFINE_GUID(IID_IWICImagingFactory2, 0x7b816b45, 0x1996, 0x4476, 0xb1, 0x32, 0xde, 0x9e, 0x24, 0x7c, 0x8a, 0xf0);
DEFINE_GUID(IID_IWICEnumMetadataItem, 0xdc2bb46d, 0x3f07, 0x481e, 0x86, 0x25, 0x22, 0x0c, 0x4a, 0xed, 0xbb, 0x33);
DEFINE_GUID(IID_IWICDdsDecoder, 0x409cd537, 0x8532, 0x40cb, 0x97, 0x74, 0xe2, 0xfe, 0xb2, 0xdf, 0x4e, 0x9c);
DEFINE_GUID(IID_IWICDdsEncoder, 0x5cacdb4c, 0x407e, 0x41b3, 0xb9, 0x36, 0xd0, 0xf0, 0x10, 0xcd, 0x67, 0x32);
DEFINE_GUID(IID_IWICDdsFrameDecode, 0x3d4c0c61, 0x18a4, 0x41e4, 0xbd, 0x80, 0x48, 0x1a, 0x4f, 0xc9, 0xf4, 0x64);
DEFINE_GUID(IID_IWICWineDecoder, 0xb9bd430d, 0x28a8, 0x41d3, 0xa1, 0xf5, 0xf3, 0x6e, 0xe0, 0x28, 0x40, 0xbf);
DEFINE_GUID(IID_IWICMetadataHandlerInfo, 0xaba958bf, 0xc672, 0x44d1, 0x8d, 0x61, 0xce, 0x6d, 0xf2, 0xe6, 0x82, 0xc2);
DEFINE_GUID(IID_IWICMetadataReader, 0x9204fe99, 0xd8fc, 0x4fd5, 0xa0, 0x01, 0x95, 0x36, 0xb0, 0x67, 0xa8, 0x99);
DEFINE_GUID(IID_IWICMetadataReaderInfo, 0xeebf1f5b, 0x07c1, 0x4447, 0xa3, 0xab, 0x22, 0xac, 0xaf, 0x78, 0xa8, 0x04);
DEFINE_GUID(IID_IWICMetadataWriter, 0xf7836e16, 0x3be0, 0x470b, 0x86, 0xbb, 0x16, 0x0d, 0x0a, 0xec, 0xd7, 0xde);
DEFINE_GUID(IID_IWICMetadataWriterInfo, 0xb22e3fba, 0x3925, 0x4323, 0xb5, 0xc1, 0x9e, 0xbf, 0xc4, 0x30, 0xf2, 0x36);
DEFINE_GUID(IID_IWICMetadataBlockReader, 0xfeaa2a8d, 0xb3f3, 0x43e4, 0xb2, 0x5c, 0xd1, 0xde, 0x99, 0x0a, 0x1a, 0xe1);
DEFINE_GUID(IID_IWICMetadataBlockWriter, 0x08fb9676, 0xb444, 0x41e8, 0x8d, 0xbe, 0x6a, 0x53, 0xa5, 0x42, 0xbf, 0xf1);
DEFINE_GUID(IID_IWICPersistStream, 0x00675040, 0x6908, 0x45f8, 0x86, 0xa3, 0x49, 0xc7, 0xdf, 0xd6, 0xd9, 0xad);
DEFINE_GUID(IID_IWICComponentFactory, 0x412d0c3a, 0x9650, 0x44fa, 0xaf, 0x5b, 0xdd, 0x2a, 0x06, 0xc8, 0xe8, 0xfb);
DEFINE_OLEGUID(IID_IEnumString, 0x00000101, 0, 0);
DEFINE_GUID(IID_IPropertyBag2, 0x22F55882, 0x280B, 0x11D0, 0xA8, 0xA9, 0x00, 0xA0, 0xC9, 0x0C, 0x20, 0x04);

typedef interface IWICPalette IWICPalette;
typedef interface IWICBitmapDecoder IWICBitmapDecoder;
typedef interface IWICBitmapDecoderInfo IWICBitmapDecoderInfo;
typedef interface IWICBitmapEncoder IWICBitmapEncoder;
typedef interface IWICBitmapEncoderInfo IWICBitmapEncoderInfo;
typedef interface IWICFormatConverter IWICFormatConverter;
typedef interface IWICBitmapScaler IWICBitmapScaler;
typedef interface IWICBitmapClipper IWICBitmapClipper;
typedef interface IWICBitmapFlipRotator IWICBitmapFlipRotator;
typedef interface IWICStream IWICStream;
typedef interface IWICColorTransform IWICColorTransform;
typedef interface IWICBitmap IWICBitmap;
typedef interface IWICBitmapLock IWICBitmapLock;
typedef interface IWICFastMetadataEncoder IWICFastMetadataEncoder;
typedef interface IWICMetadataQueryReader IWICMetadataQueryReader;
typedef interface IWICMetadataQueryWriter IWICMetadataQueryWriter;
typedef interface IWICBitmapFrameDecode IWICBitmapFrameDecode;
typedef interface IWICBitmapFrameEncode IWICBitmapFrameEncode;
typedef interface IWICComponentInfo IWICComponentInfo;
typedef interface IWICMetadataReader IWICMetadataReader;
typedef interface IWICMetadataWriter IWICMetadataWriter;
typedef interface IWICMetadataBlockReader IWICMetadataBlockReader;
typedef interface IWICMetadataBlockWriter IWICMetadataBlockWriter;
typedef interface IWICImageEncoder IWICImageEncoder;
typedef interface IPropertyBag2 IPropertyBag2;
typedef interface ID2D1Device ID2D1Device;
typedef struct tagPROPBAG2 PROPBAG2;

#ifndef __cplusplus
#define NOVA_IUNKNOWN_METHODS \
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE; \
    STDMETHOD_(ULONG, AddRef)(THIS) PURE; \
    STDMETHOD_(ULONG, Release)(THIS) PURE;
#endif

#ifndef NOVA_IENUMUNKNOWN_DEFINED
#define NOVA_IENUMUNKNOWN_DEFINED
#undef INTERFACE
#define INTERFACE IEnumUnknown
DECLARE_INTERFACE_(IEnumUnknown, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(Next)(THIS_ ULONG count, IUnknown **items, ULONG *fetched) PURE;
    STDMETHOD(Skip)(THIS_ ULONG count) PURE;
    STDMETHOD(Reset)(THIS) PURE;
    STDMETHOD(Clone)(THIS_ IEnumUnknown **out) PURE;
    END_INTERFACE
};
#endif

#ifndef NOVA_IENUMSTRING_DEFINED
#define NOVA_IENUMSTRING_DEFINED
#undef INTERFACE
#define INTERFACE IEnumString
DECLARE_INTERFACE_(IEnumString, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(Next)(THIS_ ULONG count, LPOLESTR *items, ULONG *fetched) PURE;
    STDMETHOD(Skip)(THIS_ ULONG count) PURE;
    STDMETHOD(Reset)(THIS) PURE;
    STDMETHOD(Clone)(THIS_ IEnumString **out) PURE;
    END_INTERFACE
};
#endif

#undef INTERFACE
#define INTERFACE IWICColorContext
DECLARE_INTERFACE_(IWICColorContext, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(InitializeFromFilename)(THIS_ LPCWSTR name) PURE;
    STDMETHOD(InitializeFromMemory)(THIS_ const BYTE *buffer, UINT size) PURE;
    STDMETHOD(InitializeFromExifColorSpace)(THIS_ UINT value) PURE;
    STDMETHOD(GetType)(THIS_ WICColorContextType *type) PURE;
    STDMETHOD(GetProfileBytes)(THIS_ UINT size, BYTE *buffer, UINT *actual) PURE;
    STDMETHOD(GetExifColorSpace)(THIS_ UINT *value) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapSource
DECLARE_INTERFACE_(IWICBitmapSource, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(GetSize)(THIS_ UINT *width, UINT *height) PURE;
    STDMETHOD(GetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    STDMETHOD(GetResolution)(THIS_ double *dpix, double *dpiy) PURE;
    STDMETHOD(CopyPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(CopyPixels)(THIS_ const WICRect *rect, UINT stride, UINT size, BYTE *buffer) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapLock
DECLARE_INTERFACE_(IWICBitmapLock, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(GetSize)(THIS_ UINT *width, UINT *height) PURE;
    STDMETHOD(GetStride)(THIS_ UINT *stride) PURE;
    STDMETHOD(GetDataPointer)(THIS_ UINT *size, BYTE **data) PURE;
    STDMETHOD(GetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapFlipRotator
DECLARE_INTERFACE_(IWICBitmapFlipRotator, IWICBitmapSource)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetSize)(THIS_ UINT *width, UINT *height) PURE;
    STDMETHOD(GetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    STDMETHOD(GetResolution)(THIS_ double *dpix, double *dpiy) PURE;
    STDMETHOD(CopyPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(CopyPixels)(THIS_ const WICRect *rect, UINT stride, UINT size, BYTE *buffer) PURE;
#endif
    STDMETHOD(Initialize)(THIS_ IWICBitmapSource *source, WICBitmapTransformOptions options) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmap
DECLARE_INTERFACE_(IWICBitmap, IWICBitmapSource)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetSize)(THIS_ UINT *width, UINT *height) PURE;
    STDMETHOD(GetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    STDMETHOD(GetResolution)(THIS_ double *dpix, double *dpiy) PURE;
    STDMETHOD(CopyPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(CopyPixels)(THIS_ const WICRect *rect, UINT stride, UINT size, BYTE *buffer) PURE;
#endif
    STDMETHOD(Lock)(THIS_ const WICRect *rect, DWORD flags, IWICBitmapLock **out) PURE;
    STDMETHOD(SetPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(SetResolution)(THIS_ double dpix, double dpiy) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICPalette
DECLARE_INTERFACE_(IWICPalette, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(InitializePredefined)(THIS_ WICBitmapPaletteType type, BOOL transparent) PURE;
    STDMETHOD(InitializeCustom)(THIS_ WICColor *colors, UINT count) PURE;
    STDMETHOD(InitializeFromBitmap)(THIS_ IWICBitmapSource *source, UINT count, BOOL transparent) PURE;
    STDMETHOD(InitializeFromPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(GetType)(THIS_ WICBitmapPaletteType *type) PURE;
    STDMETHOD(GetColorCount)(THIS_ UINT *count) PURE;
    STDMETHOD(GetColors)(THIS_ UINT count, WICColor *colors, UINT *actual) PURE;
    STDMETHOD(IsBlackWhite)(THIS_ BOOL *result) PURE;
    STDMETHOD(IsGrayscale)(THIS_ BOOL *result) PURE;
    STDMETHOD(HasAlpha)(THIS_ BOOL *result) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICComponentInfo
DECLARE_INTERFACE_(IWICComponentInfo, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(GetComponentType)(THIS_ WICComponentType *type) PURE;
    STDMETHOD(GetCLSID)(THIS_ CLSID *clsid) PURE;
    STDMETHOD(GetSigningStatus)(THIS_ DWORD *status) PURE;
    STDMETHOD(GetAuthor)(THIS_ UINT cch, WCHAR *author, UINT *actual) PURE;
    STDMETHOD(GetVendorGUID)(THIS_ GUID *vendor) PURE;
    STDMETHOD(GetVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetSpecVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetFriendlyName)(THIS_ UINT cch, WCHAR *name, UINT *actual) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICMetadataQueryReader
DECLARE_INTERFACE_(IWICMetadataQueryReader, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(GetContainerFormat)(THIS_ GUID *format) PURE;
    STDMETHOD(GetLocation)(THIS_ UINT cch, WCHAR *ns, UINT *actual) PURE;
    STDMETHOD(GetMetadataByName)(THIS_ LPCWSTR name, PROPVARIANT *value) PURE;
    STDMETHOD(GetEnumerator)(THIS_ IEnumString **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICMetadataQueryWriter
DECLARE_INTERFACE_(IWICMetadataQueryWriter, IWICMetadataQueryReader)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetContainerFormat)(THIS_ GUID *format) PURE;
    STDMETHOD(GetLocation)(THIS_ UINT cch, WCHAR *ns, UINT *actual) PURE;
    STDMETHOD(GetMetadataByName)(THIS_ LPCWSTR name, PROPVARIANT *value) PURE;
    STDMETHOD(GetEnumerator)(THIS_ IEnumString **out) PURE;
#endif
    STDMETHOD(SetMetadataByName)(THIS_ LPCWSTR name, const PROPVARIANT *value) PURE;
    STDMETHOD(RemoveMetadataByName)(THIS_ LPCWSTR name) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapFrameDecode
DECLARE_INTERFACE_(IWICBitmapFrameDecode, IWICBitmapSource)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetSize)(THIS_ UINT *width, UINT *height) PURE;
    STDMETHOD(GetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    STDMETHOD(GetResolution)(THIS_ double *dpix, double *dpiy) PURE;
    STDMETHOD(CopyPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(CopyPixels)(THIS_ const WICRect *rect, UINT stride, UINT size, BYTE *buffer) PURE;
#endif
    STDMETHOD(GetMetadataQueryReader)(THIS_ IWICMetadataQueryReader **out) PURE;
    STDMETHOD(GetColorContexts)(THIS_ UINT count, IWICColorContext **contexts, UINT *actual) PURE;
    STDMETHOD(GetThumbnail)(THIS_ IWICBitmapSource **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICPixelFormatInfo
DECLARE_INTERFACE_(IWICPixelFormatInfo, IWICComponentInfo)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetComponentType)(THIS_ WICComponentType *type) PURE;
    STDMETHOD(GetCLSID)(THIS_ CLSID *clsid) PURE;
    STDMETHOD(GetSigningStatus)(THIS_ DWORD *status) PURE;
    STDMETHOD(GetAuthor)(THIS_ UINT cch, WCHAR *author, UINT *actual) PURE;
    STDMETHOD(GetVendorGUID)(THIS_ GUID *vendor) PURE;
    STDMETHOD(GetVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetSpecVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetFriendlyName)(THIS_ UINT cch, WCHAR *name, UINT *actual) PURE;
#endif
    STDMETHOD(GetFormatGUID)(THIS_ GUID *format) PURE;
    STDMETHOD(GetColorContext)(THIS_ IWICColorContext **out) PURE;
    STDMETHOD(GetBitsPerPixel)(THIS_ UINT *bpp) PURE;
    STDMETHOD(GetChannelCount)(THIS_ UINT *count) PURE;
    STDMETHOD(GetChannelMask)(THIS_ UINT channel, UINT size, BYTE *mask, UINT *actual) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICPixelFormatInfo2
DECLARE_INTERFACE_(IWICPixelFormatInfo2, IWICPixelFormatInfo)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetComponentType)(THIS_ WICComponentType *type) PURE;
    STDMETHOD(GetCLSID)(THIS_ CLSID *clsid) PURE;
    STDMETHOD(GetSigningStatus)(THIS_ DWORD *status) PURE;
    STDMETHOD(GetAuthor)(THIS_ UINT cch, WCHAR *author, UINT *actual) PURE;
    STDMETHOD(GetVendorGUID)(THIS_ GUID *vendor) PURE;
    STDMETHOD(GetVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetSpecVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetFriendlyName)(THIS_ UINT cch, WCHAR *name, UINT *actual) PURE;
    STDMETHOD(GetFormatGUID)(THIS_ GUID *format) PURE;
    STDMETHOD(GetColorContext)(THIS_ IWICColorContext **out) PURE;
    STDMETHOD(GetBitsPerPixel)(THIS_ UINT *bpp) PURE;
    STDMETHOD(GetChannelCount)(THIS_ UINT *count) PURE;
    STDMETHOD(GetChannelMask)(THIS_ UINT channel, UINT size, BYTE *mask, UINT *actual) PURE;
#endif
    STDMETHOD(SupportsTransparency)(THIS_ BOOL *support) PURE;
    STDMETHOD(GetNumericRepresentation)(THIS_ WICPixelFormatNumericRepresentation *rep) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapCodecInfo
DECLARE_INTERFACE_(IWICBitmapCodecInfo, IWICComponentInfo)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetComponentType)(THIS_ WICComponentType *type) PURE;
    STDMETHOD(GetCLSID)(THIS_ CLSID *clsid) PURE;
    STDMETHOD(GetSigningStatus)(THIS_ DWORD *status) PURE;
    STDMETHOD(GetAuthor)(THIS_ UINT cch, WCHAR *author, UINT *actual) PURE;
    STDMETHOD(GetVendorGUID)(THIS_ GUID *vendor) PURE;
    STDMETHOD(GetVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetSpecVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetFriendlyName)(THIS_ UINT cch, WCHAR *name, UINT *actual) PURE;
#endif
    STDMETHOD(GetContainerFormat)(THIS_ GUID *format) PURE;
    STDMETHOD(GetPixelFormats)(THIS_ UINT count, GUID *formats, UINT *actual) PURE;
    STDMETHOD(GetColorManagementVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetDeviceManufacturer)(THIS_ UINT cch, WCHAR *manufacturer, UINT *actual) PURE;
    STDMETHOD(GetDeviceModels)(THIS_ UINT cch, WCHAR *models, UINT *actual) PURE;
    STDMETHOD(GetMimeTypes)(THIS_ UINT cch, WCHAR *types, UINT *actual) PURE;
    STDMETHOD(GetFileExtensions)(THIS_ UINT cch, WCHAR *extensions, UINT *actual) PURE;
    STDMETHOD(DoesSupportAnimation)(THIS_ BOOL *support) PURE;
    STDMETHOD(DoesSupportChromaKey)(THIS_ BOOL *support) PURE;
    STDMETHOD(DoesSupportLossless)(THIS_ BOOL *support) PURE;
    STDMETHOD(DoesSupportMultiframe)(THIS_ BOOL *support) PURE;
    STDMETHOD(MatchesMimeType)(THIS_ LPCWSTR type, BOOL *matches) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapDecoderInfo
DECLARE_INTERFACE_(IWICBitmapDecoderInfo, IWICBitmapCodecInfo)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetComponentType)(THIS_ WICComponentType *type) PURE;
    STDMETHOD(GetCLSID)(THIS_ CLSID *clsid) PURE;
    STDMETHOD(GetSigningStatus)(THIS_ DWORD *status) PURE;
    STDMETHOD(GetAuthor)(THIS_ UINT cch, WCHAR *author, UINT *actual) PURE;
    STDMETHOD(GetVendorGUID)(THIS_ GUID *vendor) PURE;
    STDMETHOD(GetVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetSpecVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetFriendlyName)(THIS_ UINT cch, WCHAR *name, UINT *actual) PURE;
    STDMETHOD(GetContainerFormat)(THIS_ GUID *format) PURE;
    STDMETHOD(GetPixelFormats)(THIS_ UINT count, GUID *formats, UINT *actual) PURE;
    STDMETHOD(GetColorManagementVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetDeviceManufacturer)(THIS_ UINT cch, WCHAR *manufacturer, UINT *actual) PURE;
    STDMETHOD(GetDeviceModels)(THIS_ UINT cch, WCHAR *models, UINT *actual) PURE;
    STDMETHOD(GetMimeTypes)(THIS_ UINT cch, WCHAR *types, UINT *actual) PURE;
    STDMETHOD(GetFileExtensions)(THIS_ UINT cch, WCHAR *extensions, UINT *actual) PURE;
    STDMETHOD(DoesSupportAnimation)(THIS_ BOOL *support) PURE;
    STDMETHOD(DoesSupportChromaKey)(THIS_ BOOL *support) PURE;
    STDMETHOD(DoesSupportLossless)(THIS_ BOOL *support) PURE;
    STDMETHOD(DoesSupportMultiframe)(THIS_ BOOL *support) PURE;
    STDMETHOD(MatchesMimeType)(THIS_ LPCWSTR type, BOOL *matches) PURE;
#endif
    STDMETHOD(GetPatterns)(THIS_ UINT size, WICBitmapPattern *patterns, UINT *count, UINT *actual) PURE;
    STDMETHOD(MatchesPattern)(THIS_ IStream *stream, BOOL *matches) PURE;
    STDMETHOD(CreateInstance)(THIS_ IWICBitmapDecoder **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapDecoder
DECLARE_INTERFACE_(IWICBitmapDecoder, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(QueryCapability)(THIS_ IStream *stream, DWORD *capability) PURE;
    STDMETHOD(Initialize)(THIS_ IStream *stream, WICDecodeOptions options) PURE;
    STDMETHOD(GetContainerFormat)(THIS_ GUID *format) PURE;
    STDMETHOD(GetDecoderInfo)(THIS_ IWICBitmapDecoderInfo **out) PURE;
    STDMETHOD(CopyPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(GetMetadataQueryReader)(THIS_ IWICMetadataQueryReader **out) PURE;
    STDMETHOD(GetPreview)(THIS_ IWICBitmapSource **out) PURE;
    STDMETHOD(GetColorContexts)(THIS_ UINT count, IWICColorContext **contexts, UINT *actual) PURE;
    STDMETHOD(GetThumbnail)(THIS_ IWICBitmapSource **out) PURE;
    STDMETHOD(GetFrameCount)(THIS_ UINT *count) PURE;
    STDMETHOD(GetFrame)(THIS_ UINT index, IWICBitmapFrameDecode **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapFrameEncode
DECLARE_INTERFACE_(IWICBitmapFrameEncode, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(Initialize)(THIS_ IPropertyBag2 *options) PURE;
    STDMETHOD(SetSize)(THIS_ UINT width, UINT height) PURE;
    STDMETHOD(SetResolution)(THIS_ double dpix, double dpiy) PURE;
    STDMETHOD(SetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    STDMETHOD(SetColorContexts)(THIS_ UINT count, IWICColorContext **contexts) PURE;
    STDMETHOD(SetPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(SetThumbnail)(THIS_ IWICBitmapSource *thumbnail) PURE;
    STDMETHOD(WritePixels)(THIS_ UINT lines, UINT stride, UINT size, BYTE *pixels) PURE;
    STDMETHOD(WriteSource)(THIS_ IWICBitmapSource *source, WICRect *rect) PURE;
    STDMETHOD(Commit)(THIS) PURE;
    STDMETHOD(GetMetadataQueryWriter)(THIS_ IWICMetadataQueryWriter **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapEncoderInfo
DECLARE_INTERFACE_(IWICBitmapEncoderInfo, IWICBitmapCodecInfo)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetComponentType)(THIS_ WICComponentType *type) PURE;
    STDMETHOD(GetCLSID)(THIS_ CLSID *clsid) PURE;
    STDMETHOD(GetSigningStatus)(THIS_ DWORD *status) PURE;
    STDMETHOD(GetAuthor)(THIS_ UINT cch, WCHAR *author, UINT *actual) PURE;
    STDMETHOD(GetVendorGUID)(THIS_ GUID *vendor) PURE;
    STDMETHOD(GetVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetSpecVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetFriendlyName)(THIS_ UINT cch, WCHAR *name, UINT *actual) PURE;
    STDMETHOD(GetContainerFormat)(THIS_ GUID *format) PURE;
    STDMETHOD(GetPixelFormats)(THIS_ UINT count, GUID *formats, UINT *actual) PURE;
    STDMETHOD(GetColorManagementVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetDeviceManufacturer)(THIS_ UINT cch, WCHAR *manufacturer, UINT *actual) PURE;
    STDMETHOD(GetDeviceModels)(THIS_ UINT cch, WCHAR *models, UINT *actual) PURE;
    STDMETHOD(GetMimeTypes)(THIS_ UINT cch, WCHAR *types, UINT *actual) PURE;
    STDMETHOD(GetFileExtensions)(THIS_ UINT cch, WCHAR *extensions, UINT *actual) PURE;
    STDMETHOD(DoesSupportAnimation)(THIS_ BOOL *support) PURE;
    STDMETHOD(DoesSupportChromaKey)(THIS_ BOOL *support) PURE;
    STDMETHOD(DoesSupportLossless)(THIS_ BOOL *support) PURE;
    STDMETHOD(DoesSupportMultiframe)(THIS_ BOOL *support) PURE;
    STDMETHOD(MatchesMimeType)(THIS_ LPCWSTR type, BOOL *matches) PURE;
#endif
    STDMETHOD(CreateInstance)(THIS_ IWICBitmapEncoder **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapEncoder
DECLARE_INTERFACE_(IWICBitmapEncoder, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(Initialize)(THIS_ IStream *stream, WICBitmapEncoderCacheOption option) PURE;
    STDMETHOD(GetContainerFormat)(THIS_ GUID *format) PURE;
    STDMETHOD(GetEncoderInfo)(THIS_ IWICBitmapEncoderInfo **out) PURE;
    STDMETHOD(SetColorContexts)(THIS_ UINT count, IWICColorContext **contexts) PURE;
    STDMETHOD(SetPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(SetThumbnail)(THIS_ IWICBitmapSource *thumbnail) PURE;
    STDMETHOD(SetPreview)(THIS_ IWICBitmapSource *preview) PURE;
    STDMETHOD(CreateNewFrame)(THIS_ IWICBitmapFrameEncode **frame, IPropertyBag2 **options) PURE;
    STDMETHOD(Commit)(THIS) PURE;
    STDMETHOD(GetMetadataQueryWriter)(THIS_ IWICMetadataQueryWriter **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICFormatConverter
DECLARE_INTERFACE_(IWICFormatConverter, IWICBitmapSource)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetSize)(THIS_ UINT *width, UINT *height) PURE;
    STDMETHOD(GetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    STDMETHOD(GetResolution)(THIS_ double *dpix, double *dpiy) PURE;
    STDMETHOD(CopyPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(CopyPixels)(THIS_ const WICRect *rect, UINT stride, UINT size, BYTE *buffer) PURE;
#endif
    STDMETHOD(Initialize)(THIS_ IWICBitmapSource *source, REFWICPixelFormatGUID format, WICBitmapDitherType dither, IWICPalette *palette, double alpha_threshold, WICBitmapPaletteType translate) PURE;
    STDMETHOD(CanConvert)(THIS_ REFWICPixelFormatGUID from, REFWICPixelFormatGUID to, BOOL *can) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICFormatConverterInfo
DECLARE_INTERFACE_(IWICFormatConverterInfo, IWICComponentInfo)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetComponentType)(THIS_ WICComponentType *type) PURE;
    STDMETHOD(GetCLSID)(THIS_ CLSID *clsid) PURE;
    STDMETHOD(GetSigningStatus)(THIS_ DWORD *status) PURE;
    STDMETHOD(GetAuthor)(THIS_ UINT cch, WCHAR *author, UINT *actual) PURE;
    STDMETHOD(GetVendorGUID)(THIS_ GUID *vendor) PURE;
    STDMETHOD(GetVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetSpecVersion)(THIS_ UINT cch, WCHAR *version, UINT *actual) PURE;
    STDMETHOD(GetFriendlyName)(THIS_ UINT cch, WCHAR *name, UINT *actual) PURE;
#endif
    STDMETHOD(GetPixelFormats)(THIS_ UINT count, WICPixelFormatGUID *formats, UINT *actual) PURE;
    STDMETHOD(CreateInstance)(THIS_ IWICFormatConverter **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICStream
DECLARE_INTERFACE_(IWICStream, IStream)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(Read)(THIS_ void *pv, ULONG cb, ULONG *read) PURE;
    STDMETHOD(Write)(THIS_ const void *pv, ULONG cb, ULONG *written) PURE;
    STDMETHOD(Seek)(THIS_ LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *pos) PURE;
    STDMETHOD(SetSize)(THIS_ ULARGE_INTEGER size) PURE;
    STDMETHOD(CopyTo)(THIS_ IStream *to, ULARGE_INTEGER cb, ULARGE_INTEGER *read, ULARGE_INTEGER *written) PURE;
    STDMETHOD(Commit)(THIS_ DWORD flags) PURE;
    STDMETHOD(Revert)(THIS) PURE;
    STDMETHOD(LockRegion)(THIS_ ULARGE_INTEGER off, ULARGE_INTEGER cb, DWORD type) PURE;
    STDMETHOD(UnlockRegion)(THIS_ ULARGE_INTEGER off, ULARGE_INTEGER cb, DWORD type) PURE;
    STDMETHOD(Stat)(THIS_ STATSTG *st, DWORD flags) PURE;
    STDMETHOD(Clone)(THIS_ IStream **out) PURE;
#endif
    STDMETHOD(InitializeFromIStream)(THIS_ IStream *stream) PURE;
    STDMETHOD(InitializeFromFilename)(THIS_ LPCWSTR name, DWORD access) PURE;
    STDMETHOD(InitializeFromMemory)(THIS_ BYTE *buffer, DWORD size) PURE;
    STDMETHOD(InitializeFromIStreamRegion)(THIS_ IStream *stream, ULARGE_INTEGER offset, ULARGE_INTEGER max_size) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapScaler
DECLARE_INTERFACE_(IWICBitmapScaler, IWICBitmapSource)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetSize)(THIS_ UINT *width, UINT *height) PURE;
    STDMETHOD(GetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    STDMETHOD(GetResolution)(THIS_ double *dpix, double *dpiy) PURE;
    STDMETHOD(CopyPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(CopyPixels)(THIS_ const WICRect *rect, UINT stride, UINT size, BYTE *buffer) PURE;
#endif
    STDMETHOD(Initialize)(THIS_ IWICBitmapSource *source, UINT width, UINT height, WICBitmapInterpolationMode mode) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICBitmapClipper
DECLARE_INTERFACE_(IWICBitmapClipper, IWICBitmapSource)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetSize)(THIS_ UINT *width, UINT *height) PURE;
    STDMETHOD(GetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    STDMETHOD(GetResolution)(THIS_ double *dpix, double *dpiy) PURE;
    STDMETHOD(CopyPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(CopyPixels)(THIS_ const WICRect *rect, UINT stride, UINT size, BYTE *buffer) PURE;
#endif
    STDMETHOD(Initialize)(THIS_ IWICBitmapSource *source, const WICRect *rect) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICColorTransform
DECLARE_INTERFACE_(IWICColorTransform, IWICBitmapSource)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(GetSize)(THIS_ UINT *width, UINT *height) PURE;
    STDMETHOD(GetPixelFormat)(THIS_ WICPixelFormatGUID *format) PURE;
    STDMETHOD(GetResolution)(THIS_ double *dpix, double *dpiy) PURE;
    STDMETHOD(CopyPalette)(THIS_ IWICPalette *palette) PURE;
    STDMETHOD(CopyPixels)(THIS_ const WICRect *rect, UINT stride, UINT size, BYTE *buffer) PURE;
#endif
    STDMETHOD(Initialize)(THIS_ IWICBitmapSource *source, IWICColorContext *from, IWICColorContext *to, REFWICPixelFormatGUID format) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICFastMetadataEncoder
DECLARE_INTERFACE_(IWICFastMetadataEncoder, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(Commit)(THIS) PURE;
    STDMETHOD(GetMetadataQueryWriter)(THIS_ IWICMetadataQueryWriter **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICImagingFactory
DECLARE_INTERFACE_(IWICImagingFactory, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(CreateDecoderFromFilename)(THIS_ LPCWSTR name, const GUID *vendor, DWORD access, WICDecodeOptions options, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateDecoderFromStream)(THIS_ IStream *stream, const GUID *vendor, WICDecodeOptions options, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateDecoderFromFileHandle)(THIS_ ULONG_PTR file, const GUID *vendor, WICDecodeOptions options, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateComponentInfo)(THIS_ REFCLSID clsid, IWICComponentInfo **out) PURE;
    STDMETHOD(CreateDecoder)(THIS_ REFGUID format, const GUID *vendor, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateEncoder)(THIS_ REFGUID format, const GUID *vendor, IWICBitmapEncoder **out) PURE;
    STDMETHOD(CreatePalette)(THIS_ IWICPalette **out) PURE;
    STDMETHOD(CreateFormatConverter)(THIS_ IWICFormatConverter **out) PURE;
    STDMETHOD(CreateBitmapScaler)(THIS_ IWICBitmapScaler **out) PURE;
    STDMETHOD(CreateBitmapClipper)(THIS_ IWICBitmapClipper **out) PURE;
    STDMETHOD(CreateBitmapFlipRotator)(THIS_ IWICBitmapFlipRotator **out) PURE;
    STDMETHOD(CreateStream)(THIS_ IWICStream **out) PURE;
    STDMETHOD(CreateColorContext)(THIS_ IWICColorContext **out) PURE;
    STDMETHOD(CreateColorTransformer)(THIS_ IWICColorTransform **out) PURE;
    STDMETHOD(CreateBitmap)(THIS_ UINT width, UINT height, REFWICPixelFormatGUID format, WICBitmapCreateCacheOption option, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromSource)(THIS_ IWICBitmapSource *source, WICBitmapCreateCacheOption option, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromSourceRect)(THIS_ IWICBitmapSource *source, UINT x, UINT y, UINT width, UINT height, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromMemory)(THIS_ UINT width, UINT height, REFWICPixelFormatGUID format, UINT stride, UINT size, BYTE *buffer, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromHBITMAP)(THIS_ HBITMAP bitmap, HPALETTE palette, WICBitmapAlphaChannelOption option, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromHICON)(THIS_ HICON icon, IWICBitmap **out) PURE;
    STDMETHOD(CreateComponentEnumerator)(THIS_ DWORD types, DWORD options, IEnumUnknown **out) PURE;
    STDMETHOD(CreateFastMetadataEncoderFromDecoder)(THIS_ IWICBitmapDecoder *decoder, IWICFastMetadataEncoder **out) PURE;
    STDMETHOD(CreateFastMetadataEncoderFromFrameDecode)(THIS_ IWICBitmapFrameDecode *frame, IWICFastMetadataEncoder **out) PURE;
    STDMETHOD(CreateQueryWriter)(THIS_ REFGUID format, const GUID *vendor, IWICMetadataQueryWriter **out) PURE;
    STDMETHOD(CreateQueryWriterFromReader)(THIS_ IWICMetadataQueryReader *reader, const GUID *vendor, IWICMetadataQueryWriter **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICImagingFactory2
DECLARE_INTERFACE_(IWICImagingFactory2, IWICImagingFactory)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(CreateDecoderFromFilename)(THIS_ LPCWSTR name, const GUID *vendor, DWORD access, WICDecodeOptions options, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateDecoderFromStream)(THIS_ IStream *stream, const GUID *vendor, WICDecodeOptions options, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateDecoderFromFileHandle)(THIS_ ULONG_PTR file, const GUID *vendor, WICDecodeOptions options, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateComponentInfo)(THIS_ REFCLSID clsid, IWICComponentInfo **out) PURE;
    STDMETHOD(CreateDecoder)(THIS_ REFGUID format, const GUID *vendor, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateEncoder)(THIS_ REFGUID format, const GUID *vendor, IWICBitmapEncoder **out) PURE;
    STDMETHOD(CreatePalette)(THIS_ IWICPalette **out) PURE;
    STDMETHOD(CreateFormatConverter)(THIS_ IWICFormatConverter **out) PURE;
    STDMETHOD(CreateBitmapScaler)(THIS_ IWICBitmapScaler **out) PURE;
    STDMETHOD(CreateBitmapClipper)(THIS_ IWICBitmapClipper **out) PURE;
    STDMETHOD(CreateBitmapFlipRotator)(THIS_ IWICBitmapFlipRotator **out) PURE;
    STDMETHOD(CreateStream)(THIS_ IWICStream **out) PURE;
    STDMETHOD(CreateColorContext)(THIS_ IWICColorContext **out) PURE;
    STDMETHOD(CreateColorTransformer)(THIS_ IWICColorTransform **out) PURE;
    STDMETHOD(CreateBitmap)(THIS_ UINT width, UINT height, REFWICPixelFormatGUID format, WICBitmapCreateCacheOption option, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromSource)(THIS_ IWICBitmapSource *source, WICBitmapCreateCacheOption option, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromSourceRect)(THIS_ IWICBitmapSource *source, UINT x, UINT y, UINT width, UINT height, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromMemory)(THIS_ UINT width, UINT height, REFWICPixelFormatGUID format, UINT stride, UINT size, BYTE *buffer, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromHBITMAP)(THIS_ HBITMAP bitmap, HPALETTE palette, WICBitmapAlphaChannelOption option, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromHICON)(THIS_ HICON icon, IWICBitmap **out) PURE;
    STDMETHOD(CreateComponentEnumerator)(THIS_ DWORD types, DWORD options, IEnumUnknown **out) PURE;
    STDMETHOD(CreateFastMetadataEncoderFromDecoder)(THIS_ IWICBitmapDecoder *decoder, IWICFastMetadataEncoder **out) PURE;
    STDMETHOD(CreateFastMetadataEncoderFromFrameDecode)(THIS_ IWICBitmapFrameDecode *frame, IWICFastMetadataEncoder **out) PURE;
    STDMETHOD(CreateQueryWriter)(THIS_ REFGUID format, const GUID *vendor, IWICMetadataQueryWriter **out) PURE;
    STDMETHOD(CreateQueryWriterFromReader)(THIS_ IWICMetadataQueryReader *reader, const GUID *vendor, IWICMetadataQueryWriter **out) PURE;
#endif
    STDMETHOD(CreateImageEncoder)(THIS_ ID2D1Device *device, IWICImageEncoder **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICComponentFactory
DECLARE_INTERFACE_(IWICComponentFactory, IWICImagingFactory)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
    STDMETHOD(CreateDecoderFromFilename)(THIS_ LPCWSTR name, const GUID *vendor, DWORD access, WICDecodeOptions options, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateDecoderFromStream)(THIS_ IStream *stream, const GUID *vendor, WICDecodeOptions options, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateDecoderFromFileHandle)(THIS_ ULONG_PTR file, const GUID *vendor, WICDecodeOptions options, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateComponentInfo)(THIS_ REFCLSID clsid, IWICComponentInfo **out) PURE;
    STDMETHOD(CreateDecoder)(THIS_ REFGUID format, const GUID *vendor, IWICBitmapDecoder **out) PURE;
    STDMETHOD(CreateEncoder)(THIS_ REFGUID format, const GUID *vendor, IWICBitmapEncoder **out) PURE;
    STDMETHOD(CreatePalette)(THIS_ IWICPalette **out) PURE;
    STDMETHOD(CreateFormatConverter)(THIS_ IWICFormatConverter **out) PURE;
    STDMETHOD(CreateBitmapScaler)(THIS_ IWICBitmapScaler **out) PURE;
    STDMETHOD(CreateBitmapClipper)(THIS_ IWICBitmapClipper **out) PURE;
    STDMETHOD(CreateBitmapFlipRotator)(THIS_ IWICBitmapFlipRotator **out) PURE;
    STDMETHOD(CreateStream)(THIS_ IWICStream **out) PURE;
    STDMETHOD(CreateColorContext)(THIS_ IWICColorContext **out) PURE;
    STDMETHOD(CreateColorTransformer)(THIS_ IWICColorTransform **out) PURE;
    STDMETHOD(CreateBitmap)(THIS_ UINT width, UINT height, REFWICPixelFormatGUID format, WICBitmapCreateCacheOption option, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromSource)(THIS_ IWICBitmapSource *source, WICBitmapCreateCacheOption option, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromSourceRect)(THIS_ IWICBitmapSource *source, UINT x, UINT y, UINT width, UINT height, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromMemory)(THIS_ UINT width, UINT height, REFWICPixelFormatGUID format, UINT stride, UINT size, BYTE *buffer, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromHBITMAP)(THIS_ HBITMAP bitmap, HPALETTE palette, WICBitmapAlphaChannelOption option, IWICBitmap **out) PURE;
    STDMETHOD(CreateBitmapFromHICON)(THIS_ HICON icon, IWICBitmap **out) PURE;
    STDMETHOD(CreateComponentEnumerator)(THIS_ DWORD types, DWORD options, IEnumUnknown **out) PURE;
    STDMETHOD(CreateFastMetadataEncoderFromDecoder)(THIS_ IWICBitmapDecoder *decoder, IWICFastMetadataEncoder **out) PURE;
    STDMETHOD(CreateFastMetadataEncoderFromFrameDecode)(THIS_ IWICBitmapFrameDecode *frame, IWICFastMetadataEncoder **out) PURE;
    STDMETHOD(CreateQueryWriter)(THIS_ REFGUID format, const GUID *vendor, IWICMetadataQueryWriter **out) PURE;
    STDMETHOD(CreateQueryWriterFromReader)(THIS_ IWICMetadataQueryReader *reader, const GUID *vendor, IWICMetadataQueryWriter **out) PURE;
#endif
    STDMETHOD(CreateMetadataReader)(THIS_ REFGUID format, const GUID *vendor, DWORD options, IStream *stream, IWICMetadataReader **out) PURE;
    STDMETHOD(CreateMetadataReaderFromContainer)(THIS_ REFGUID format, const GUID *vendor, DWORD options, IStream *stream, IWICMetadataReader **out) PURE;
    STDMETHOD(CreateMetadataWriter)(THIS_ REFGUID format, const GUID *vendor, DWORD options, IWICMetadataWriter **out) PURE;
    STDMETHOD(CreateMetadataWriterFromReader)(THIS_ IWICMetadataReader *reader, const GUID *vendor, IWICMetadataWriter **out) PURE;
    STDMETHOD(CreateQueryReaderFromBlockReader)(THIS_ IWICMetadataBlockReader *reader, IWICMetadataQueryReader **out) PURE;
    STDMETHOD(CreateQueryWriterFromBlockWriter)(THIS_ IWICMetadataBlockWriter *writer, IWICMetadataQueryWriter **out) PURE;
    STDMETHOD(CreateEncoderPropertyBag)(THIS_ PROPBAG2 *options, UINT count, IPropertyBag2 **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IWICMetadataBlockReader
DECLARE_INTERFACE_(IWICMetadataBlockReader, IUnknown)
{
    BEGIN_INTERFACE
#ifndef __cplusplus
    NOVA_IUNKNOWN_METHODS
#endif
    STDMETHOD(GetContainerFormat)(THIS_ GUID *format) PURE;
    STDMETHOD(GetCount)(THIS_ UINT *count) PURE;
    STDMETHOD(GetReaderByIndex)(THIS_ UINT index, IWICMetadataReader **out) PURE;
    STDMETHOD(GetEnumerator)(THIS_ IEnumUnknown **out) PURE;
    END_INTERFACE
};

/* WindowsCodecs.dll's flat functions */
#ifdef NOVA_WIC_BUILD
#define NOVA_WIC_API __declspec(dllexport)
#else
#define NOVA_WIC_API __declspec(dllimport)
#endif
NOVA_WIC_API HRESULT WINAPI WICConvertBitmapSource(REFWICPixelFormatGUID format, IWICBitmapSource *source, IWICBitmapSource **out);
NOVA_WIC_API HRESULT WINAPI WICCreateBitmapFromSection(UINT width, UINT height, REFWICPixelFormatGUID format, HANDLE section, UINT stride, UINT offset, IWICBitmap **out);
NOVA_WIC_API HRESULT WINAPI WICCreateBitmapFromSectionEx(UINT width, UINT height, REFWICPixelFormatGUID format, HANDLE section, UINT stride, UINT offset, int access, IWICBitmap **out);
NOVA_WIC_API HRESULT WINAPI WICMapGuidToShortName(REFGUID guid, UINT cch, WCHAR *name, UINT *actual);
NOVA_WIC_API HRESULT WINAPI WICMapShortNameToGuid(LPCWSTR name, GUID *guid);
NOVA_WIC_API HRESULT WINAPI WICMapSchemaToName(REFGUID format, LPWSTR schema, UINT cch, WCHAR *name, UINT *actual);
NOVA_WIC_API HRESULT WINAPI WICCreateImagingFactory_Proxy(UINT version, IWICImagingFactory **out);

_NOVA_END
