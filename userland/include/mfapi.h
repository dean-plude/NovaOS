/*
 * mfapi.h — Media Foundation platform functions (mfplat.dll) and the
 * media-type attributes and formats
 */
#pragma once
#include <mfobjects.h>

#define MF_SDK_VERSION 0x0002
#define MF_API_VERSION 0x0070
#define MF_VERSION     (MF_SDK_VERSION << 16 | MF_API_VERSION)
#define MFSTARTUP_NOSOCKET 0x1
#define MFSTARTUP_LITE     MFSTARTUP_NOSOCKET
#define MFSTARTUP_FULL     0

/* Media types: the major types and video formats */
DEFINE_GUID(MFMediaType_Video, 0x73646976, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
DEFINE_GUID(MFMediaType_Audio, 0x73647561, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
#define NOVA_MF_FORMAT(n, l) DEFINE_GUID(n, l, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71)
NOVA_MF_FORMAT(MFVideoFormat_RGB32,  0x00000016);
NOVA_MF_FORMAT(MFVideoFormat_ARGB32, 0x00000015);
NOVA_MF_FORMAT(MFVideoFormat_RGB24,  0x00000014);
NOVA_MF_FORMAT(MFVideoFormat_YUY2,   0x32595559);
NOVA_MF_FORMAT(MFVideoFormat_NV12,   0x3231564E);
NOVA_MF_FORMAT(MFVideoFormat_I420,   0x30323449);
NOVA_MF_FORMAT(MFVideoFormat_MJPG,   0x47504A4D);

DEFINE_GUID(MF_MT_MAJOR_TYPE,             0x48EBA18E, 0xF8C9, 0x4687, 0xBF, 0x11, 0x0A, 0x74, 0xC9, 0xF9, 0x6A, 0x8F);
DEFINE_GUID(MF_MT_SUBTYPE,                0xF7E34C9A, 0x42E8, 0x4714, 0xB7, 0x4B, 0xCB, 0x29, 0xD7, 0x2C, 0x35, 0xE5);
DEFINE_GUID(MF_MT_FRAME_SIZE,             0x1652C33D, 0xD6B2, 0x4012, 0xB8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xA3, 0x7D);
DEFINE_GUID(MF_MT_FRAME_RATE,             0xC459A2E8, 0x3D2C, 0x4E44, 0xB1, 0x32, 0xFE, 0xE5, 0x15, 0x6C, 0x7B, 0xB0);
DEFINE_GUID(MF_MT_FRAME_RATE_RANGE_MAX,   0xE3371D41, 0xB4CF, 0x4A05, 0xBD, 0x4E, 0x20, 0xB8, 0x8B, 0xB2, 0xC4, 0xD6);
DEFINE_GUID(MF_MT_FRAME_RATE_RANGE_MIN,   0xD2E7558C, 0xDC1F, 0x403F, 0x9A, 0x72, 0xD2, 0x8B, 0xB1, 0xEB, 0x3B, 0x5E);
DEFINE_GUID(MF_MT_PIXEL_ASPECT_RATIO,     0xC6376A1E, 0x8D0A, 0x4027, 0xBE, 0x45, 0x6D, 0x9A, 0x0A, 0xD3, 0x9B, 0xB6);
DEFINE_GUID(MF_MT_INTERLACE_MODE,         0xE2724BB8, 0xE676, 0x4806, 0xB4, 0xB2, 0xA8, 0xD6, 0xEF, 0xB4, 0x4C, 0xCD);
DEFINE_GUID(MF_MT_ALL_SAMPLES_INDEPENDENT, 0xC9173739, 0x5E56, 0x461C, 0xB7, 0x13, 0x46, 0xFB, 0x99, 0x5C, 0xB9, 0x5F);
DEFINE_GUID(MF_MT_FIXED_SIZE_SAMPLES,     0xB8EBEFAF, 0xB718, 0x4E04, 0xB0, 0xA9, 0x11, 0x67, 0x75, 0xE3, 0x32, 0x1B);
DEFINE_GUID(MF_MT_SAMPLE_SIZE,            0xDAD3AB78, 0x1990, 0x408B, 0xBC, 0xE2, 0xEB, 0xA6, 0x73, 0xDA, 0xCC, 0x10);
DEFINE_GUID(MF_MT_DEFAULT_STRIDE,         0x644B4E48, 0x1E02, 0x4516, 0xB0, 0xEB, 0xC0, 0x1C, 0xA9, 0xD4, 0x9A, 0xC6);
DEFINE_GUID(MF_MT_AVG_BITRATE,            0x20332624, 0xFB0D, 0x4D9E, 0xBD, 0x0D, 0xCB, 0xF6, 0x78, 0x6C, 0x10, 0x2E);
#define MFVideoInterlace_Progressive 2

#ifdef __cplusplus
extern "C" {
#endif
#define NOVA_MFAPI NOVA_MF_API HRESULT WINAPI
NOVA_MFAPI MFStartup(ULONG version, DWORD flags);
NOVA_MFAPI MFShutdown(void);
NOVA_MFAPI MFCreateAttributes(IMFAttributes **attributes, UINT32 size);
NOVA_MFAPI MFCreateMediaType(IMFMediaType **type);
NOVA_MFAPI MFCreateMemoryBuffer(DWORD max_length, IMFMediaBuffer **buffer);
NOVA_MFAPI MFCreateAlignedMemoryBuffer(DWORD max_length, DWORD alignment, IMFMediaBuffer **buffer);
NOVA_MFAPI MFCreateSample(IMFSample **sample);
NOVA_MFAPI MFCalculateImageSize(REFGUID subtype, UINT32 width, UINT32 height, UINT32 *size);
NOVA_MFAPI MFGetStrideForBitmapInfoHeader(DWORD fourcc, DWORD width, LONG *stride);
NOVA_MF_API LONGLONG WINAPI MFGetSystemTime(void);
#undef NOVA_MFAPI

/* (inline in Windows' mfapi.h too) */
#ifdef __cplusplus
#define NOVA_MF_CALL(o, m, ...) (o)->m(__VA_ARGS__)
#define NOVA_MF_KEY(k) (k)
#else
#define NOVA_MF_CALL(o, m, ...) (o)->lpVtbl->m((o), __VA_ARGS__)
#define NOVA_MF_KEY(k) (k)
#endif
static inline HRESULT MFGetAttribute2UINT32asUINT64(IMFAttributes *a, REFGUID key, UINT32 *hi, UINT32 *lo)
{
    UINT64 v = 0;
    HRESULT hr = NOVA_MF_CALL(a, GetUINT64, NOVA_MF_KEY(key), &v);
    if (hr >= 0) { *hi = (UINT32)(v >> 32); *lo = (UINT32)v; }
    return hr;
}
static inline HRESULT MFSetAttribute2UINT32asUINT64(IMFAttributes *a, REFGUID key, UINT32 hi, UINT32 lo)
{
    return NOVA_MF_CALL(a, SetUINT64, NOVA_MF_KEY(key), (UINT64)hi << 32 | lo);
}
#define MFGetAttributeSize(a, key, w, h)  MFGetAttribute2UINT32asUINT64((IMFAttributes *)(a), key, w, h)
#define MFSetAttributeSize(a, key, w, h)  MFSetAttribute2UINT32asUINT64((IMFAttributes *)(a), key, w, h)
#define MFGetAttributeRatio(a, key, n, d) MFGetAttribute2UINT32asUINT64((IMFAttributes *)(a), key, n, d)
#define MFSetAttributeRatio(a, key, n, d) MFSetAttribute2UINT32asUINT64((IMFAttributes *)(a), key, n, d)
static inline UINT32 MFGetAttributeUINT32(IMFAttributes *a, REFGUID key, UINT32 dflt)
{
    UINT32 v = dflt;
    if (NOVA_MF_CALL(a, GetUINT32, NOVA_MF_KEY(key), &v) < 0) v = dflt;
    return v;
}
#ifdef __cplusplus
}
#endif
