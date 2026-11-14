// NovaOS: the framework's non-localized error messages (a parser error's
// "The property '%0' was not found in type '%1'."), which the error
// service asks the browser host for by ID.  Microsoft's browser host
// (host/win/desktop/BrowserHost.cpp, not built) loads them from the string
// table of its resource DLL, MUX.resources.common.dll, made from
// win/agcore.debug/CommonErrors.rc; NovaOS's build makes that table's rows
// (build.py's error_strings(), xaml_error_strings.g.inc) and looks them up
// here.  An ID with no message reads "ID An error has occurred.", as
// Microsoft's does.
//
// The same host also answers the framework's localized strings
// (GetLocalizedResourceString, localizedResource.h's ids): the UI
// Automation names of controls and parts, and the window title a windowed
// popup gives its host window (CPopup, UIA_WINDOWED_POPUP_HOST; WinUI 2's
// NavigationView opens one while it lays out, so Calculator stopped on
// "not implemented" there).  Microsoft's loads them from its localized
// satellite DLL, whose string table is not in the public repository, so
// NovaOS keeps the English text of the ids its built parts ask for here.
// An id with no text reads as the empty string, as Microsoft's
// GetResourceString answers when its table has no row for the id.
#include "precomp.h"
#include "WinBrowserHost.h"
#include <xcperrorresource.h>
#include <localizedResource.h>

namespace
{
    struct ErrorString
    {
        XUINT32 id;
        const WCHAR *text;
    };

    const ErrorString s_localizedStrings[] = {
        { TEXT_CONTEXT_MENU_CUT, L"Cut" },
        { TEXT_CONTEXT_MENU_COPY, L"Copy" },
        { TEXT_CONTEXT_MENU_PASTE, L"Paste" },
        { TEXT_CONTEXT_MENU_UNDO, L"Undo" },
        { TEXT_CONTEXT_MENU_REDO, L"Redo" },
        { TEXT_CONTEXT_MENU_SELECT_ALL, L"Select All" },
        { TEXT_TOGGLESWITCH_ON, L"On" },
        { TEXT_TOGGLESWITCH_OFF, L"Off" },
        { TEXT_PASSWORDBOX_SHOW_PASSWORD, L"Show password" },
        { KEYBOARD_ACCELERATOR_TEXT_JOIN, L"+" },
        { UIA_AP_BUTTON, L"button" },
        { UIA_AP_CALENDAR, L"calendar" },
        { UIA_AP_CHECKBOX, L"check box" },
        { UIA_AP_COMBOBOX, L"combo box" },
        { UIA_AP_EDIT, L"edit" },
        { UIA_AP_HYPERLINK, L"hyperlink" },
        { UIA_AP_IMAGE, L"image" },
        { UIA_AP_LISTITEM, L"list item" },
        { UIA_AP_LIST, L"list" },
        { UIA_AP_MENU, L"menu" },
        { UIA_AP_MENUBAR, L"menu bar" },
        { UIA_AP_MENUITEM, L"menu item" },
        { UIA_AP_PROGRESSBAR, L"progress bar" },
        { UIA_AP_RADIOBUTTON, L"radio button" },
        { UIA_AP_SCROLLBAR, L"scroll bar" },
        { UIA_AP_SLIDER, L"slider" },
        { UIA_AP_SPINNER, L"spinner" },
        { UIA_AP_STATUSBAR, L"status bar" },
        { UIA_AP_TAB, L"tab" },
        { UIA_AP_TABITEM, L"tab item" },
        { UIA_AP_TEXT, L"text" },
        { UIA_AP_TOOLBAR, L"tool bar" },
        { UIA_AP_TOOLTIP, L"tool tip" },
        { UIA_AP_TREE, L"tree" },
        { UIA_AP_TREEITEM, L"tree item" },
        { UIA_AP_CUSTOM, L"custom" },
        { UIA_AP_GROUP, L"group" },
        { UIA_AP_THUMB, L"thumb" },
        { UIA_AP_DATAGRID, L"data grid" },
        { UIA_AP_DATAITEM, L"data item" },
        { UIA_AP_DOCUMENT, L"document" },
        { UIA_AP_SPLITBUTTON, L"split button" },
        { UIA_AP_WINDOW, L"window" },
        { UIA_AP_PANE, L"pane" },
        { UIA_AP_HEADER, L"header" },
        { UIA_AP_HEADERITEM, L"header item" },
        { UIA_AP_TABLE, L"table" },
        { UIA_AP_TITLEBAR, L"title bar" },
        { UIA_AP_SEPARATOR, L"separator" },
        { UIA_AP_SEMANTICZOOM, L"semantic zoom" },
        { UIA_AP_TOGGLESWITCH, L"toggle switch" },
        { UIA_AP_DATEPICKER, L"date picker" },
        { UIA_AP_TIMEPICKER, L"time picker" },
        { UIA_AP_APPBAR, L"app bar" },
        { UIA_AP_APPBAR_BUTTON, L"app bar button" },
        { UIA_AP_APPBAR_TOGGLEBUTTON, L"app bar toggle button" },
        { UIA_AP_FLIPVIEW, L"flip view" },
        { UIA_AP_PROGRESSRING, L"progress ring" },
        { UIA_POPUP_NAME, L"Popup" },
        { UIA_LIGHTDISMISS_NAME, L"Close" },
        { UIA_WINDOWED_POPUP_HOST, L"PopupHost" },
        { UIA_SCROLLBAR_VERTICAL, L"Vertical Scroll Bar" },
        { UIA_SCROLLBAR_HORIZONTAL, L"Horizontal Scroll Bar" },
        { UIA_SCROLLBAR_HORIZONTALLARGEINCREASE, L"Page right" },
        { UIA_SCROLLBAR_HORIZONTALSMALLINCREASE, L"Scroll right" },
        { UIA_SCROLLBAR_HORIZONTALLARGEDECREASE, L"Page left" },
        { UIA_SCROLLBAR_HORIZONTALSMALLDECREASE, L"Scroll left" },
        { UIA_SCROLLBAR_HORIZONTALTHUMB, L"Position" },
        { UIA_SCROLLBAR_VERTICALALLARGEINCREASE, L"Page down" },
        { UIA_SCROLLBAR_VERTICALSMALLINCREASE, L"Scroll down" },
        { UIA_SCROLLBAR_VERTICALLARGEDECREASE, L"Page up" },
        { UIA_SCROLLBAR_VERTICALSMALLDECREASE, L"Scroll up" },
        { UIA_SCROLLBAR_VERTICALTHUMB, L"Position" },
        { UIA_MORE_BUTTON, L"More options" },
        { UIA_LESS_BUTTON, L"Fewer options" },
        { UIA_TEXTBOX_DELETE, L"Clear" },
        { UIA_PASSWORDBOX_REVEAL, L"Reveal password" },
        { UIA_AUTOSUGGESTBOX_QUERY, L"Search" },
        { UIA_PROGRESSRING_BUSY, L"Busy" },
        { UIA_PROGRESSRING_NOTBUSY, L"Not busy" },
        { TEXT_COMMAND_LABEL_CUT, L"Cut" },
        { TEXT_COMMAND_LABEL_COPY, L"Copy" },
        { TEXT_COMMAND_LABEL_PASTE, L"Paste" },
        { TEXT_COMMAND_LABEL_SELECTALL, L"Select All" },
        { TEXT_COMMAND_LABEL_DELETE, L"Delete" },
        { TEXT_COMMAND_LABEL_UNDO, L"Undo" },
        { TEXT_COMMAND_LABEL_REDO, L"Redo" },
    };

    _Check_return_ HRESULT FindString(const ErrorString* table, size_t count, XUINT32 stringId, _Out_ xstring_ptr* text)
    {
        for (size_t i = 0; i < count; i++)
        {
            if (table[i].id == stringId)
            {
                return xstring_ptr::CloneBuffer(table[i].text, static_cast<XUINT32>(wcslen(table[i].text)), text);
            }
        }
        *text = xstring_ptr::EmptyString();
        return S_OK;
    }

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

_Check_return_ HRESULT CXcpBrowserHost::GetLocalizedResourceString(_In_ XUINT32 stringId, _Out_ xstring_ptr* resourceString)
{
    return FindString(s_localizedStrings, ARRAYSIZE(s_localizedStrings), stringId, resourceString);
}

// The non-localized strings (nonLocalizedResource.h, strings under a
// feature not yet localized) are not in the public repository either: each
// reads as the empty string.
_Check_return_ HRESULT CXcpBrowserHost::GetNonLocalizedResourceString(_In_ XUINT32 stringId, _Out_ xstring_ptr* resourceString)
{
    *resourceString = xstring_ptr::EmptyString();
    return S_OK;
}
