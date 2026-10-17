/* NovaOS: the Windows App SDK's InputSiteHelpers.h (FrameworkUdk, shipped
 * closed).  XAML reaches the input site of a ContentIsland through it: the
 * object DirectManipulation and the input services register against, and
 * the window that receives the island's input.  How XAML's sources use it
 * (core/input/InputServices.cpp, core/core/elements/XamlIslandRoot.cpp,
 * plat/win/browserdesktop/DirectManipulationService.cpp): an IUnknown it
 * holds in a ComPtr and compares, GetIslandInputSite(island, &site) and
 * GetUnderlyingInputWindowId(site, &id) (false once the site is closed).
 *
 * NovaOS's ContentIsland hands out the site as an interface of its own
 * (IIslandInputSite, a NovaOS IID); the helpers ask the island and the
 * site for it. */
#pragma once
#include <microsoft.ui.content.h>

namespace InputSiteHelper
{
    struct __declspec(uuid("5bad72c0-f7b0-5c64-9aea-80a5250b121b")) __declspec(novtable) IIslandInputSite : public IUnknown
    {
        /* the window that receives the island's input; fails once the
           site is closed */
        virtual HRESULT STDMETHODCALLTYPE GetInputWindowId(ABI::Microsoft::UI::WindowId *windowId) = 0;
    };

    /* the input site of @island (an interface the island implements) */
    inline HRESULT GetIslandInputSite(ABI::Microsoft::UI::Content::IContentIsland *island, IIslandInputSite **site)
    {
        if (!site)
            return E_POINTER;
        *site = nullptr;
        if (!island)
            return E_INVALIDARG;
        return island->QueryInterface(__uuidof(IIslandInputSite), reinterpret_cast<void **>(site));
    }

    /* the window @site's input arrives at; false when it is closed */
    inline bool GetUnderlyingInputWindowId(IIslandInputSite *site, ABI::Microsoft::UI::WindowId *windowId)
    {
        return site && windowId && SUCCEEDED(site->GetInputWindowId(windowId));
    }
}
