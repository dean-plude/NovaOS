/* NovaOS: the Windows SDK's windows.graphics.imaging.interop.h: a software
 * bitmap's native access and the factory that makes a
 * Windows.Graphics.Imaging.SoftwareBitmap of a WIC bitmap (the imaging
 * component's SoftwareBitmapUtility; the class ID is in src/uuids.cpp). */
#pragma once
#include <inspectable.h>
#include <wincodec.h>

struct IMF2DBuffer2;
struct _MFVideoArea;

struct __declspec(uuid("94bc8415-04ea-4b2e-af13-4de95aa898eb")) __declspec(novtable) ISoftwareBitmapNative : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetData(REFIID riid, void **ppv) = 0;
};

struct __declspec(uuid("c3c181ec-2914-4791-af02-02d224a10b43")) __declspec(novtable) ISoftwareBitmapNativeFactory : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE CreateFromWICBitmap(IWICBitmap *data, BOOL forceReadOnly, REFIID riid, void **ppv) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromMF2DBuffer2(IMF2DBuffer2 *data, REFGUID subtype, UINT32 width, UINT32 height,
                                                            BOOL forceReadOnly, const struct _MFVideoArea *minDisplayAperture,
                                                            REFIID riid, void **ppv) = 0;
};

extern "C" const CLSID CLSID_SoftwareBitmapNativeFactory;
