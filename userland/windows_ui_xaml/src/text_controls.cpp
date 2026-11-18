// NovaOS: what XAML's text boxes (core/native/text/Controls) take from
// outside Microsoft's open sources: the Rich Edit interface ID they give
// their OLE callback (RichEditOleCallback::QueryInterface), which Windows
// programs take from the SDK's uuid.lib (the others they read from
// WinUIEdit.dll, userland/textserv), and the selection's default colour
// (core/inc/corep.h), which Windows takes from the system's accent.
#include <windows.h>

extern "C" const IID IID_IRichEditOleCallback = {0x00020d03, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// (ARGB: the system's highlight colour, opaque)
unsigned int GetDefaultSelectionHighlightColor()
{
    COLORREF c = GetSysColor(COLOR_HIGHLIGHT);
    return 0xff000000u | (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
}
