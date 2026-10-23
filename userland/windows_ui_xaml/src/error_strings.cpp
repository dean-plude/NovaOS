// NovaOS: the framework's non-localized error messages (a parser error's
// "The property '%0' was not found in type '%1'."), which the error
// service asks the browser host for by ID.  Microsoft's browser host
// (host/win/desktop/BrowserHost.cpp, not built) loads them from the string
// table of its resource DLL, MUX.resources.common.dll, made from
// win/agcore.debug/CommonErrors.rc; NovaOS's build makes that table's rows
// (build.py's error_strings(), xaml_error_strings.g.inc) and looks them up
// here.  An ID with no message reads "ID An error has occurred.", as
// Microsoft's does.
#include "precomp.h"
#include "WinBrowserHost.h"
#include <xcperrorresource.h>

namespace
{
    struct ErrorString
    {
        XUINT32 id;
        const WCHAR *text;
    };

    const ErrorString s_errorStrings[] = {
#include <xaml_error_strings.g.inc>
    };
}

_Check_return_ HRESULT CXcpBrowserHost::GetNonLocalizedErrorString(_In_ XUINT32 stringId, _Out_ xstring_ptr* errorString)
{
    for (const auto& entry : s_errorStrings)
    {
        if (entry.id == stringId)
        {
            return xstring_ptr::CloneBuffer(entry.text, static_cast<XUINT32>(wcslen(entry.text)), errorString);
        }
    }

    WCHAR text[64];
    swprintf_s(text, L"%u An error has occurred.", stringId);
    return xstring_ptr::CloneBuffer(text, static_cast<XUINT32>(wcslen(text)), errorString);
}
