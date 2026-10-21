// NovaOS: what the framework's sources (dxaml/lib) see after their first
// headers: the Windows SDK's name for CoreWindow.Closed's handler,
// IWindowClosedEventHandler (the SDK's windows.ui.core.h names the
// TypedEventHandler<CoreWindow, CoreWindowEventArgs> instance so; NovaOS's
// ABI headers, made from the metadata, have only the instance), which the
// hidden window's implementation (UWPWindowImpl.cpp) subscribes with
#pragma once
#include <windows.ui.core.h>
namespace ABI { namespace Windows { namespace UI { namespace Core {
typedef ABI::Windows::Foundation::ITypedEventHandler<CoreWindow *, CoreWindowEventArgs *> IWindowClosedEventHandler;
}}}}

// and the gamepad key routing the framework's light-up code turns on at
// Application.Start (DXamlCoreLightup.cpp: Windows.UI.Input's
// GamepadKeyRoutingConfiguration, UniversalApiContract 19), which no
// published Windows metadata (third_party/winmd, nor windows-rs' newer)
// declares yet: the class and its statics as the code calls them.  The code asks
// ApiInformation for contract 19 first, which NovaOS does not report, so
// the class is never activated; the IID below is NovaOS's own placeholder,
// not Windows'.
namespace ABI { namespace Windows { namespace UI { namespace Input {
struct __declspec(uuid("a30992a2-d688-4f81-b71f-0010a60ced3b")) __declspec(novtable)
IGamepadKeyRoutingConfigurationStatics : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE TrySetKeyRoutingEnabled(boolean enabled, boolean *result) = 0;
};
}}}}
extern const __declspec(selectany) wchar_t RuntimeClass_Windows_UI_Input_GamepadKeyRoutingConfiguration[] =
    L"Windows.UI.Input.GamepadKeyRoutingConfiguration";

// and Windows.Media.Core's media sources, which the activation of a type
// from markup (ActivationAPI.cpp) makes for a MediaPlayerElement's Source
// (the Windows SDK's headers reach windows.media.core.h through the
// framework's; NovaOS's per-namespace ones do not)
#include <windows.media.core.h>
