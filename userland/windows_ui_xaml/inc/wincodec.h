/* NovaOS: the Windows SDK's wincodec.h is MinGW-w64's, with what the SDK
 * has and it lacks as far as XAML's imaging component names it: the
 * high-quality cubic interpolation mode (Windows 10), and
 * IWICBitmapSourceTransform, which a scaler asks its source for (the
 * imaging component's choice of interpolation mode does too). */
#pragma once
#include_next <wincodec.h>

#ifndef WICBitmapInterpolationModeHighQualityCubic
#define WICBitmapInterpolationModeHighQualityCubic ((WICBitmapInterpolationMode)4)
#endif

#if defined(__cplusplus) && !defined(__IWICBitmapSourceTransform_INTERFACE_DEFINED__)
#define __IWICBitmapSourceTransform_INTERFACE_DEFINED__
struct __declspec(uuid("3b16811b-6a43-4ec9-b713-3d5a0c13b940")) __declspec(novtable) IWICBitmapSourceTransform : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect *prc, UINT uiWidth, UINT uiHeight,
                                                 WICPixelFormatGUID *pguidDstFormat, WICBitmapTransformOptions dstTransform,
                                                 UINT nStride, UINT cbBufferSize, BYTE *pbBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClosestSize(UINT *puiWidth, UINT *puiHeight) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClosestPixelFormat(WICPixelFormatGUID *pguidDstFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE DoesSupportTransform(WICBitmapTransformOptions dstTransform,
                                                           BOOL *pfIsSupported) = 0;
};
#endif
