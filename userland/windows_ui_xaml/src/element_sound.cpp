// NovaOS: the static entry points of the framework's ElementSoundPlayerService
// (dxaml/lib/ElementSoundPlayerService_Partial.h), which a control calls as
// it is invoked, focused or moved through (ButtonBase's click asks for the
// Invoke sound).  Microsoft's implementation plays the sounds through an
// AudioGraph set up by a closed Windows App SDK header
// (AudioGraphSettingsExtension.h); NovaOS's answers as the framework does on
// a desktop PC, where ElementSoundPlayer.State's default, Auto, means no
// element sounds (only Xbox plays them by default): the kind of sound is
// checked and nothing is played.
#include "precomp.h"
#include "ElementSoundPlayerService_Partial.h"

using namespace DirectUI;

static HRESULT check_sound(int sound)
{
    return sound >= xaml::ElementSoundKind_Focus && sound <= xaml::ElementSoundKind_GoBack ? S_OK : E_INVALIDARG;
}

_Check_return_ HRESULT ElementSoundPlayerService::RequestInteractionSoundForElementStatic(xaml::ElementSoundKind sound,
                                                                                         _In_opt_ DependencyObject *)
{
    return check_sound(sound);
}

_Check_return_ HRESULT ElementSoundPlayerService::RequestInteractionSoundForElementStatic(DirectUI::ElementSoundKind sound,
                                                                                         _In_ CDependencyObject *)
{
    return check_sound(static_cast<int>(sound));
}

_Check_return_ HRESULT ElementSoundPlayerService::PlayInteractionSoundStatic()
{
    return S_OK;
}
