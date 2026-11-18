/* NovaOS: TextInput.h, Windows' (closed) header for the private settings
 * of the text input framework (TSF3) XAML's text boxes set on the edit
 * context their windowless Rich Edit gives (EM_GETTSF): spell checking,
 * text prediction, capitalisation, the candidate window, the input pane.
 * Here what XAML's sources (core/native/text/Controls) name: the settings
 * as flag pairs, and the two calls, which have nothing to set on NovaOS
 * (no TSF3 edit context: NovaOS's WinUIEdit.dll answers EM_GETTSF with
 * none, which XAML takes as no private settings). */
#pragma once

enum class TextInput_InputSettings : unsigned int {
    None = 0,
    EnableSpellCheck = 0x1, DisableSpellCheck = 0x2,
    EnableAutoCorrect = 0x4, DisableAutoCorrect = 0x8,
    EnableAutoCapitalization = 0x10, DisableAutoCapitalization = 0x20,
    EnableCandidatesOnDemand = 0x40, DisableCandidatesOnDemand = 0x80,
    EnableAutoSuggest = 0x100, DisableAutoSuggest = 0x200,
    EnableHaveTrailer = 0x400, DisableHaveTrailer = 0x800,
    EnableManualInputPane = 0x1000, DisableManualInputPane = 0x2000,
    PasswordObfuscated = 0x4000,
    BottomEdgeCandidateWindowAlignment = 0x8000,
    /* (two bits, one of them DisablePrivateInputSetting's, as XAML's comment says) */
    DisablePrivateInputSetting = 0x10000, EnablePrivateInputSetting = 0x30000,
};

inline HRESULT TextInputProxy_putInputSettings(IUnknown *editContext, TextInput_InputSettings settings)
{
    (void)editContext; (void)settings;
    return S_OK;
}

inline HRESULT TextInput_SetTextPrediction(HWND inputWindow)
{
    (void)inputWindow;
    return S_OK;
}
