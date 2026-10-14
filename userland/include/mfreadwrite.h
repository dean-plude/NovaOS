/*
 * mfreadwrite.h — the Media Foundation source reader (mfreadwrite.dll)
 */
#pragma once
#include <mfidl.h>

DEFINE_GUID(IID_IMFSourceReader,         0x70AE66F2, 0xC809, 0x4E4F, 0x89, 0x15, 0xBD, 0xCB, 0x40, 0x6B, 0x79, 0x93);
DEFINE_GUID(IID_IMFSourceReaderCallback, 0xDEEC8D99, 0xFA1D, 0x4D82, 0x84, 0xC2, 0x2C, 0x89, 0x69, 0x94, 0x48, 0x67);
DEFINE_GUID(MF_SOURCE_READER_ASYNC_CALLBACK,                   0x1E3DBEAC, 0xBB43, 0x4C35, 0xB5, 0x07, 0xCD, 0x64, 0x44, 0x64, 0xC9, 0x65);
DEFINE_GUID(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,          0xFB394F3D, 0xCCF1, 0x42EE, 0xBB, 0xB3, 0xF9, 0xB8, 0x45, 0xD5, 0x68, 0x1D);
DEFINE_GUID(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, 0x0F81DA2C, 0xB537, 0x4672, 0xA8, 0xB2, 0xA6, 0x81, 0xB1, 0x73, 0x07, 0xA3);
DEFINE_GUID(MF_SOURCE_READER_DISABLE_DXVA,                     0xAA456CFD, 0x3943, 0x4A1E, 0xA7, 0x7D, 0x18, 0x38, 0xC0, 0xEA, 0x2E, 0x35);
DEFINE_GUID(MF_READWRITE_DISABLE_CONVERTERS,                   0x98D5B065, 0x1374, 0x4847, 0x8D, 0x5D, 0x31, 0x52, 0x0F, 0xEE, 0x71, 0x56);

#define MF_SOURCE_READER_INVALID_STREAM_INDEX 0xFFFFFFFF
#define MF_SOURCE_READER_ALL_STREAMS          0xFFFFFFFE
#define MF_SOURCE_READER_ANY_STREAM           0xFFFFFFFE
#define MF_SOURCE_READER_FIRST_AUDIO_STREAM   0xFFFFFFFD
#define MF_SOURCE_READER_FIRST_VIDEO_STREAM   0xFFFFFFFC
#define MF_SOURCE_READER_MEDIASOURCE          0xFFFFFFFF

#define MF_SOURCE_READERF_ERROR                   0x00000001
#define MF_SOURCE_READERF_ENDOFSTREAM             0x00000002
#define MF_SOURCE_READERF_NEWSTREAM               0x00000004
#define MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED  0x00000010
#define MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED 0x00000020
#define MF_SOURCE_READERF_STREAMTICK              0x00000100
#define MF_SOURCE_READER_CONTROLF_DRAIN           0x00000001

/* ---- IMFSourceReader ---- */
#undef INTERFACE
#define INTERFACE IMFSourceReader
DECLARE_INTERFACE_(IMFSourceReader, IUnknown)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE()
    STDMETHOD(GetStreamSelection)(THIS_ DWORD index, BOOL *selected) PURE;
    STDMETHOD(SetStreamSelection)(THIS_ DWORD index, BOOL selected) PURE;
    STDMETHOD(GetNativeMediaType)(THIS_ DWORD index, DWORD type_index, IMFMediaType **type) PURE;
    STDMETHOD(GetCurrentMediaType)(THIS_ DWORD index, IMFMediaType **type) PURE;
    STDMETHOD(SetCurrentMediaType)(THIS_ DWORD index, DWORD *reserved, IMFMediaType *type) PURE;
    STDMETHOD(SetCurrentPosition)(THIS_ REFGUID format, const PROPVARIANT *position) PURE;
    STDMETHOD(ReadSample)(THIS_ DWORD index, DWORD flags, DWORD *actual_index, DWORD *stream_flags,
                          LONGLONG *timestamp, IMFSample **sample) PURE;
    STDMETHOD(Flush)(THIS_ DWORD index) PURE;
    STDMETHOD(GetServiceForStream)(THIS_ DWORD index, REFGUID service, REFIID riid, void **ppv) PURE;
    STDMETHOD(GetPresentationAttribute)(THIS_ DWORD index, REFGUID attribute, PROPVARIANT *value) PURE;
    END_INTERFACE
};

/* ---- IMFSourceReaderCallback ---- */
#undef INTERFACE
#define INTERFACE IMFSourceReaderCallback
DECLARE_INTERFACE_(IMFSourceReaderCallback, IUnknown)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE()
    STDMETHOD(OnReadSample)(THIS_ HRESULT status, DWORD index, DWORD stream_flags, LONGLONG timestamp, IMFSample *sample) PURE;
    STDMETHOD(OnFlush)(THIS_ DWORD index) PURE;
    STDMETHOD(OnEvent)(THIS_ DWORD index, IMFMediaEvent *event) PURE;
    END_INTERFACE
};
#undef INTERFACE

#ifdef __cplusplus
extern "C" {
#endif
NOVA_MF_API HRESULT WINAPI MFCreateSourceReaderFromMediaSource(IMFMediaSource *source, IMFAttributes *attributes,
                                                                        IMFSourceReader **reader);
NOVA_MF_API HRESULT WINAPI MFCreateSourceReaderFromURL(LPCWSTR url, IMFAttributes *attributes, IMFSourceReader **reader);
NOVA_MF_API HRESULT WINAPI MFCreateSourceReaderFromByteStream(IUnknown *stream, IMFAttributes *attributes, IMFSourceReader **reader);
#ifdef __cplusplus
}
#endif
