/* NovaOS: the Windows App SDK's
 * Microsoft.UI.Input.InputPreTranslateSource.Interop.h.  An island's
 * InputPreTranslateKeyboardSource lets its owner see keyboard messages
 * before they are translated: XAML sets a handler on its interop interface
 * (SetPreTranslateHandler) and is called with the MSG for a message aimed
 * at the island itself (OnDirectMessage) or at another island in its tree
 * (OnTreeMessage), setting *handled to stop it (dxaml/lib/
 * PreTranslateHandler.h, InputSiteAdapter.cpp).  The IIDs are NovaOS's:
 * its Microsoft.UI.Input is the one that answers to them. */
#pragma once
#include <microsoft.ui.input.h>

namespace ABI { namespace Microsoft { namespace UI { namespace Input {

struct IInputPreTranslateKeyboardSourceHandler;

struct __declspec(uuid("ef25ae3d-2e20-5113-9b59-bf115c4b9360")) __declspec(novtable) IInputPreTranslateKeyboardSourceInterop : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetPreTranslateHandler(IInputPreTranslateKeyboardSourceHandler *handler) = 0;
};

struct __declspec(uuid("e8035938-34b5-565c-994a-1b6ba980bf56")) __declspec(novtable) IInputPreTranslateKeyboardSourceHandler : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnDirectMessage(IInputPreTranslateKeyboardSourceInterop *source, const MSG *msg,
                                                      UINT keyboardModifiers, bool *handled) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnTreeMessage(IInputPreTranslateKeyboardSourceInterop *source, const MSG *msg,
                                                    UINT keyboardModifiers, bool *handled) = 0;
};

}}}}
