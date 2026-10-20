/* NovaOS: the Windows SDK's d2d1_3.h (Direct2D 1.3), which MinGW-w64 lacks.
 * So far it holds what XAML's core headers need from it: Direct2D 1.1
 * (core/inc/Timeline.h includes d2d1_3.h and uses nothing newer), and the
 * Direct2D 1.2 device, ID2D1Device2 (with ID2D1Device1 under it), whose
 * DXGI device XAML's composition helpers take from a D2D device
 * (components/comptree/DCompTreeHost.cpp).  The other Direct2D 1.2-1.3
 * interfaces XAML's imaging and text components call
 * (ID2D1DeviceContext4-7 for colour glyphs, the SVG document) join when
 * those components build, declared in vtable order with their IIDs from
 * Microsoft's Win32 metadata. */
#pragma once
#include <d2d1_1.h>
#include <d2d1effects.h>

#ifdef __cplusplus
interface ID2D1DeviceContext1;
interface ID2D1DeviceContext2;
typedef enum D2D1_RENDERING_PRIORITY { D2D1_RENDERING_PRIORITY_NORMAL = 0, D2D1_RENDERING_PRIORITY_LOW = 1,
                                       D2D1_RENDERING_PRIORITY_FORCE_DWORD = 0xffffffff } D2D1_RENDERING_PRIORITY;
struct __declspec(uuid("d21768e1-23a4-4823-a14b-7c3eba85d658")) __declspec(novtable) ID2D1Device1 : public ID2D1Device
{
    virtual D2D1_RENDERING_PRIORITY STDMETHODCALLTYPE GetRenderingPriority() = 0;
    virtual void STDMETHODCALLTYPE SetRenderingPriority(D2D1_RENDERING_PRIORITY renderingPriority) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext1 **deviceContext1) = 0;
    using ID2D1Device::CreateDeviceContext;
};
struct __declspec(uuid("a44472e1-8dfb-4e60-8492-6e2861c9ca8b")) __declspec(novtable) ID2D1Device2 : public ID2D1Device1
{
    virtual HRESULT STDMETHODCALLTYPE CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS options, ID2D1DeviceContext2 **deviceContext2) = 0;
    virtual void STDMETHODCALLTYPE FlushDeviceContexts(ID2D1Bitmap *bitmap) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDxgiDevice(IDXGIDevice **dxgiDevice) = 0;
    using ID2D1Device1::CreateDeviceContext;
};
#endif
