/*
 * mf.dll — Media Foundation's capture devices: MFEnumDeviceSources lists
 * the cameras and MFCreateDeviceSource opens one (both in mfplat.dll's
 * camera.c, where the media source lives)
 */
#define NOVA_MF_BUILD
#include <mfidl.h>

__declspec(dllimport) HRESULT WINAPI NovaMFEnumDeviceSources(IMFAttributes *attrs, IMFActivate ***sources, UINT32 *count);
__declspec(dllimport) HRESULT WINAPI NovaMFCreateDeviceSource(IMFAttributes *attrs, IMFMediaSource **source);

HRESULT WINAPI MFEnumDeviceSources(IMFAttributes *attrs, IMFActivate ***sources, UINT32 *count)
{
    return NovaMFEnumDeviceSources(attrs, sources, count);
}

HRESULT WINAPI MFCreateDeviceSource(IMFAttributes *attrs, IMFMediaSource **source)
{
    return NovaMFCreateDeviceSource(attrs, source);
}
