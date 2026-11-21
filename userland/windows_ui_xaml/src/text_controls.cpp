// NovaOS: what XAML's text boxes (core/native/text/Controls) take from
// outside Microsoft's open sources: the Rich Edit interface ID they give
// their OLE callback (RichEditOleCallback::QueryInterface), which Windows
// programs take from the SDK's uuid.lib (the others they read from
// WinUIEdit.dll, userland/textserv).  (The selection's default colour,
// GetDefaultSelectionHighlightColor, is the core's own since
// TextSelectionManager.cpp builds: the system's accent colour.)
#include <windows.h>

extern "C" const IID IID_IRichEditOleCallback = {0x00020d03, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

