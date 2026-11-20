/* NovaOS: Windows.UI.Popups' IID constants by the names MIDL's header gives
 * them in their namespace (ABI::Windows::UI::Popups::IID_IUICommandInvokedHandler),
 * which tools/winrt/abigen.py's headers do not declare: they carry the IIDs
 * as __declspec(uuid) only.  The text boxes' context menu command handler
 * (TextContextMenuCommandHandler.cpp) compares with it; build.py forces this
 * header first into that source only. */
#pragma once
#include <windows.ui.popups.h>
namespace ABI { namespace Windows { namespace UI { namespace Popups {
static const IID IID_IUICommandInvokedHandler = __uuidof(IUICommandInvokedHandler);
} } } }
