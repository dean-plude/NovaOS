/* NovaOS: the Windows SDK's windows.graphics.interop.h, as far as XAML uses
 * it: IGeometrySource2DInterop, by which a Windows.Graphics
 * IGeometrySource2D (XAML's geometries, core/inc/geometry.h) hands
 * composition the Direct2D geometry behind it. */
#pragma once
#include <windows.graphics.h>
#include <d2d1.h>

namespace ABI { namespace Windows { namespace Graphics {

struct __declspec(uuid("0657af73-53fd-47cf-84ff-c8492d2a80a3")) __declspec(novtable) IGeometrySource2DInterop : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetGeometry(ID2D1Geometry **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetGeometryUsingFactory(ID2D1Factory *factory, ID2D1Geometry **value) = 0;
};

}}}
