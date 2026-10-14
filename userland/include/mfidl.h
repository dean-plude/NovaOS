/*
 * mfidl.h — Media Foundation media sources and their descriptors, and the
 * capture-device enumeration (mf.dll's MFEnumDeviceSources)
 */
#pragma once
#include <mfobjects.h>

DEFINE_GUID(IID_IMFMediaSource,            0x279A808D, 0xAEC7, 0x40C8, 0x9C, 0x6B, 0xA6, 0xB4, 0x92, 0xC7, 0x8A, 0x66);
DEFINE_GUID(IID_IMFPresentationDescriptor, 0x03CB2711, 0x24D7, 0x4DB6, 0xA1, 0x7F, 0xF3, 0xA7, 0xA4, 0x79, 0xA5, 0x36);
DEFINE_GUID(IID_IMFStreamDescriptor,       0x56C03D9C, 0x9DBB, 0x45F5, 0xAB, 0x4B, 0xD8, 0x0F, 0x47, 0xC0, 0x59, 0x38);
DEFINE_GUID(IID_IMFMediaTypeHandler,       0xE93DCF6C, 0x4B07, 0x4E1E, 0x81, 0x23, 0xAA, 0x16, 0xED, 0x6E, 0xAD, 0xF5);

/* Capture devices (MFEnumDeviceSources' attributes) */
DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,               0xC60AC5FE, 0x252A, 0x478F, 0xA0, 0xEF, 0xBC, 0x8F, 0xA5, 0xF7, 0xCA, 0xD3);
DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID,   0x8AC3587A, 0x4AE7, 0x42D8, 0x99, 0xE0, 0x0A, 0x60, 0x13, 0xEE, 0xF9, 0x0F);
DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID,   0x14DD9A1C, 0x7CFF, 0x41BE, 0xB1, 0xB9, 0xBA, 0x1A, 0xC6, 0xEC, 0xB5, 0x71);
DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,             0x60D0E559, 0x52F8, 0x4FA2, 0xBB, 0xCE, 0xAC, 0xDB, 0x34, 0xA8, 0xEC, 0x01);
DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, 0x58F0AAD8, 0x22BF, 0x4F8A, 0xBB, 0x3D, 0xD2, 0xC4, 0x97, 0x8C, 0x6E, 0x2F);
DEFINE_GUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, 0x77F0AE69, 0xC3BD, 0x4509, 0x94, 0x1D, 0x46, 0x7E, 0x4D, 0x24, 0x89, 0x9E);
DEFINE_GUID(KSCATEGORY_VIDEO_CAMERA, 0xE5323777, 0xF976, 0x4F5B, 0x9B, 0x55, 0xB9, 0x46, 0x99, 0xC4, 0x6E, 0x44);

#define MFMEDIASOURCE_IS_LIVE            0x1
#define MFMEDIASOURCE_CAN_SEEK           0x2
#define MFMEDIASOURCE_CAN_PAUSE          0x4
#define MFMEDIASOURCE_HAS_SLOW_SEEK      0x8

typedef interface IMFStreamDescriptor IMFStreamDescriptor;

/* ---- IMFMediaTypeHandler ---- */
#undef INTERFACE
#define INTERFACE IMFMediaTypeHandler
DECLARE_INTERFACE_(IMFMediaTypeHandler, IUnknown)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE()
    STDMETHOD(IsMediaTypeSupported)(THIS_ IMFMediaType *type, IMFMediaType **closest) PURE;
    STDMETHOD(GetMediaTypeCount)(THIS_ DWORD *count) PURE;
    STDMETHOD(GetMediaTypeByIndex)(THIS_ DWORD index, IMFMediaType **type) PURE;
    STDMETHOD(SetCurrentMediaType)(THIS_ IMFMediaType *type) PURE;
    STDMETHOD(GetCurrentMediaType)(THIS_ IMFMediaType **type) PURE;
    STDMETHOD(GetMajorType)(THIS_ GUID *major) PURE;
    END_INTERFACE
};

/* ---- IMFStreamDescriptor ---- */
#undef INTERFACE
#define INTERFACE IMFStreamDescriptor
DECLARE_INTERFACE_(IMFStreamDescriptor, IMFAttributes)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE(NOVA_IMFATTRIBUTES_METHODS)
    STDMETHOD(GetStreamIdentifier)(THIS_ DWORD *id) PURE;
    STDMETHOD(GetMediaTypeHandler)(THIS_ IMFMediaTypeHandler **handler) PURE;
    END_INTERFACE
};

/* ---- IMFPresentationDescriptor ---- */
#undef INTERFACE
#define INTERFACE IMFPresentationDescriptor
DECLARE_INTERFACE_(IMFPresentationDescriptor, IMFAttributes)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE(NOVA_IMFATTRIBUTES_METHODS)
    STDMETHOD(GetStreamDescriptorCount)(THIS_ DWORD *count) PURE;
    STDMETHOD(GetStreamDescriptorByIndex)(THIS_ DWORD index, BOOL *selected, IMFStreamDescriptor **descriptor) PURE;
    STDMETHOD(SelectStream)(THIS_ DWORD index) PURE;
    STDMETHOD(DeselectStream)(THIS_ DWORD index) PURE;
    STDMETHOD(Clone)(THIS_ IMFPresentationDescriptor **descriptor) PURE;
    END_INTERFACE
};

/* ---- IMFMediaSource ---- */
#undef INTERFACE
#define INTERFACE IMFMediaSource
DECLARE_INTERFACE_(IMFMediaSource, IMFMediaEventGenerator)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE(NOVA_IMFMEDIAEVENTGENERATOR_METHODS)
    STDMETHOD(GetCharacteristics)(THIS_ DWORD *characteristics) PURE;
    STDMETHOD(CreatePresentationDescriptor)(THIS_ IMFPresentationDescriptor **descriptor) PURE;
    STDMETHOD(Start)(THIS_ IMFPresentationDescriptor *descriptor, const GUID *time_format, const PROPVARIANT *start) PURE;
    STDMETHOD(Stop)(THIS) PURE;
    STDMETHOD(Pause)(THIS) PURE;
    STDMETHOD(Shutdown)(THIS) PURE;
    END_INTERFACE
};
#undef INTERFACE

#ifdef __cplusplus
extern "C" {
#endif
NOVA_MF_API HRESULT WINAPI MFEnumDeviceSources(IMFAttributes *attributes, IMFActivate ***sources, UINT32 *count);
NOVA_MF_API HRESULT WINAPI MFCreateDeviceSource(IMFAttributes *attributes, IMFMediaSource **source);
#ifdef __cplusplus
}
#endif
