/* NovaOS: the Windows App SDK's microsoft.ui.composition.experimental.h
 * (the lifted composition's experimental API, which Microsoft publishes
 * no metadata for).  XAML uses one piece of it: a composition object
 * telling a listener that an animated property changed
 * (IExpCompositionPropertyChanged), declared here as XAML's own copy
 * declares it (components/comptree/inc/ExpCompositionPropertyChanged.h,
 * whose static_asserts check the IIDs). */
#pragma once
#include <microsoft.ui.composition.h>

namespace ABI { namespace Microsoft { namespace UI { namespace Composition { namespace Experimental {

enum ExpExpressionNotificationProperty : int
{
    ExpExpressionNotificationProperty_Undefined = 0,
    ExpExpressionNotificationProperty_Clip = 1,
    ExpExpressionNotificationProperty_Offset = 2,
    ExpExpressionNotificationProperty_Opacity = 3,
    ExpExpressionNotificationProperty_Size = 4,
    ExpExpressionNotificationProperty_RelativeOffset = 5,
    ExpExpressionNotificationProperty_RelativeSize = 6,
    ExpExpressionNotificationProperty_AnchorPoint = 7,
    ExpExpressionNotificationProperty_CenterPoint = 8,
    ExpExpressionNotificationProperty_Orientation = 9,
    ExpExpressionNotificationProperty_RotationAngle = 10,
    ExpExpressionNotificationProperty_RotationAxis = 11,
    ExpExpressionNotificationProperty_Scale = 12,
    ExpExpressionNotificationProperty_TransformMatrix = 13,
    ExpExpressionNotificationProperty_BottomInset = 14,
    ExpExpressionNotificationProperty_LeftInset = 15,
    ExpExpressionNotificationProperty_RightInset = 16,
    ExpExpressionNotificationProperty_TopInset = 17,
    ExpExpressionNotificationProperty_LeftRadiusX = 18,
    ExpExpressionNotificationProperty_LeftRadiusY = 19,
    ExpExpressionNotificationProperty_BottomRightRadiusX = 20,
    ExpExpressionNotificationProperty_BottomRightRadiusY = 21,
    ExpExpressionNotificationProperty_TopLeftRadiusX = 22,
    ExpExpressionNotificationProperty_TopLeftRadiusY = 23,
    ExpExpressionNotificationProperty_TopRightRadiusX = 24,
    ExpExpressionNotificationProperty_TopRightRadiusY = 25,
};

struct __declspec(uuid("5f9c3d96-1e77-5980-8b28-7a9b8614a863")) __declspec(novtable) IExpCompositionPropertyChangedListener : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE NotifyBooleanPropertyChanged(ICompositionObject *target, ExpExpressionNotificationProperty property, boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyColorPropertyChanged(ICompositionObject *target, ExpExpressionNotificationProperty property, ABI::Windows::UI::Color value) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyMatrix3x2PropertyChanged(ICompositionObject *target, ExpExpressionNotificationProperty property, ABI::Windows::Foundation::Numerics::Matrix3x2 value) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyMatrix4x4PropertyChanged(ICompositionObject *target, ExpExpressionNotificationProperty property, ABI::Windows::Foundation::Numerics::Matrix4x4 value) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyReferencePropertyChanged(ICompositionObject *target, ExpExpressionNotificationProperty property) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifySinglePropertyChanged(ICompositionObject *target, ExpExpressionNotificationProperty property, FLOAT value) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyVector2PropertyChanged(ICompositionObject *target, ExpExpressionNotificationProperty property, ABI::Windows::Foundation::Numerics::Vector2 value) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyVector3PropertyChanged(ICompositionObject *target, ExpExpressionNotificationProperty property, ABI::Windows::Foundation::Numerics::Vector3 value) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyVector4PropertyChanged(ICompositionObject *target, ExpExpressionNotificationProperty property, ABI::Windows::Foundation::Numerics::Vector4 value) = 0;
};

struct __declspec(uuid("12b579a9-6a27-5cde-a2a1-c557bb7dfdb3")) __declspec(novtable) IExpCompositionPropertyChanged : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE SetPropertyChangedListener(ExpExpressionNotificationProperty property, IExpCompositionPropertyChangedListener *listener) = 0;
};

}}}}}
