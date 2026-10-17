/* NovaOS: the Windows App SDK's ContentExternalLinkHelper.h (FrameworkUdk,
 * shipped closed).  A windowed popup draws the system backdrop (Mica,
 * acrylic) behind its content through a ContentExternalBackdropLink, which
 * this helper wraps (core/core/elements/Popup.cpp): Create makes one on
 * XAML's compositor, its placement visual goes at the bottom of the
 * popup's visuals, the border mode softens its edges, and
 * AsSystemBackdropTarget is where XAML sets the backdrop brush.  Declared
 * here as Popup.cpp uses it; NovaOS defines it with its lifted composition
 * when the popup code builds. */
#pragma once
#include <memory>
#include <microsoft.ui.composition.h>

class ContentExternalBackdropLinkHelper
{
public:
    static HRESULT Create(ABI::Microsoft::UI::Composition::ICompositor *compositor,
                          std::unique_ptr<ContentExternalBackdropLinkHelper> &link);
    ~ContentExternalBackdropLinkHelper();

    HRESULT SetExternalBackdropBorderMode(ABI::Microsoft::UI::Composition::CompositionBorderMode mode);
    HRESULT GetPlacementVisual(ABI::Microsoft::UI::Composition::IVisual **visual);
    HRESULT AsSystemBackdropTarget(ABI::Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop **target);

private:
    ContentExternalBackdropLinkHelper() = default;
    /* the link (a Microsoft.UI.Content.ContentExternalBackdropLink) */
    IUnknown *m_link = nullptr;
};
