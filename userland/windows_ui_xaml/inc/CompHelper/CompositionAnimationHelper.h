/* NovaOS: the Windows App SDK's CompHelper/CompositionAnimationHelper.h
 * (shipped closed with the lifted composition code).  XAML's animations
 * (components/animation: CTimeManager, CDoubleAnimation, CColorAnimation,
 * CTimeline) hand an animation they converted to a composition one to a
 * CompositionAnimationHelper, which starts it on the animated composition
 * object's property and hands back a controller the timeline keeps
 * (CTimeline::m_wucAnimator) to pause, resume (Start), seek and stop it.
 * Declared here as XAML calls it; NovaOS defines it in
 * src/composition_animation_helper.cpp. */
#pragma once
#include <windows.h>
#include <microsoft.ui.composition.h>

// a started composition animation, as CompositionAnimationHelper hands it back
struct __declspec(novtable) ICompositionAnimationController : public IUnknown
{
};

class CompositionAnimationHelper
{
public:
    // starts @animation on @animatingObject's @propertyName
    _Check_return_ HRESULT ConnectAnimation(
        _In_ ABI::Microsoft::UI::Composition::ICompositionObject *animatingObject,
        _In_z_ const wchar_t *propertyName,
        _In_ ABI::Microsoft::UI::Composition::ICompositionAnimation *animation,
        _Outptr_ ICompositionAnimationController **controller);
    // starts or resumes, pauses, stops, and moves to @offset100ns
    _Check_return_ HRESULT Start(_In_opt_ ICompositionAnimationController *controller);
    _Check_return_ HRESULT Pause(_In_opt_ ICompositionAnimationController *controller);
    _Check_return_ HRESULT Stop(_In_opt_ ICompositionAnimationController *controller);
    _Check_return_ HRESULT Seek(_In_opt_ ICompositionAnimationController *controller, UINT64 offset100ns);
};
