// NovaOS: the one member of the framework's window, CJupiterWindow
// (dxaml/lib JupiterWindow.cpp, which includes headers Microsoft does not
// publish), that the control layer calls before the window itself builds:
// the window's size in physical pixels, which CJupiterControl hands the
// browser host when the window is resized.  As JupiterWindow.cpp has it
// without XamlOneCoreTransforms (off on the desktop): the client area of
// the window the message came for.
#include "JupiterWindow.h"

XSIZE CJupiterWindow::GetJupiterWindowPhysicalSize(HWND hwndContext) const
{
    FAIL_FAST_ASSERT(hwndContext);
    XSIZE size = { 0, 0 };
    RECT clientRect;
    if (GetClientRect(hwndContext, &clientRect))
    {
        size.Width = clientRect.right;
        size.Height = clientRect.bottom;
    }
    return size;
}
