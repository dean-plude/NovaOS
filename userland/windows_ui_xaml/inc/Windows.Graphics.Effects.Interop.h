// NovaOS: the SDK's Windows.Graphics.Effects.Interop.h, the interface a
// Windows.Graphics.Effects effect describes its Direct2D effect with (the
// effect's CLSID, its properties and sources), under ABI as the SDK
// declares it
#pragma once
#include <windows.graphics.effects.h>
#include <windows.foundation.h>

namespace ABI { namespace Windows { namespace Graphics { namespace Effects {

typedef enum GRAPHICS_EFFECT_PROPERTY_MAPPING
{
    GRAPHICS_EFFECT_PROPERTY_MAPPING_UNKNOWN,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_VECTORX,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_VECTORY,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_VECTORZ,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_VECTORW,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_RECT_TO_VECTOR4,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_RADIANS_TO_DEGREES,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_COLORMATRIX_ALPHA_MODE,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_COLOR_TO_VECTOR3,
    GRAPHICS_EFFECT_PROPERTY_MAPPING_COLOR_TO_VECTOR4
} GRAPHICS_EFFECT_PROPERTY_MAPPING;

MIDL_INTERFACE("2FC57384-A068-44D7-A331-30982FCF7177")
IGraphicsEffectD2D1Interop : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetEffectId(GUID *id) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR name, UINT *index,
                                                              GRAPHICS_EFFECT_PROPERTY_MAPPING *mapping) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT *count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSource(UINT index, IGraphicsEffectSource **source) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSourceCount(UINT *count) = 0;
};

}}}}
