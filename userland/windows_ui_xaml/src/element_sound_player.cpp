// NovaOS: ElementSoundPlayerService's statics, which the controls call as
// they open, close and are invoked (SplitView's pane, flyouts, buttons).
// Microsoft's ElementSoundPlayerService plays those sounds through an
// AudioGraph only when ElementSoundPlayer.State is On (or, Auto, on an
// Xbox); on a PC its default, Auto, plays none.  NovaOS plays none: each
// request is answered done, as Windows answers it there.
#include "precomp.h"
#include "ElementSoundPlayerService_Partial.h"

using namespace DirectUI;

_Check_return_ HRESULT ElementSoundPlayerService::RequestInteractionSoundForElementStatic(xaml::ElementSoundKind, _In_opt_ DependencyObject *)
{
    return S_OK;
}

_Check_return_ HRESULT ElementSoundPlayerService::RequestInteractionSoundForElementStatic(DirectUI::ElementSoundKind, _In_ CDependencyObject *)
{
    return S_OK;
}

_Check_return_ HRESULT ElementSoundPlayerService::PlayInteractionSoundStatic()
{
    return S_OK;
}
