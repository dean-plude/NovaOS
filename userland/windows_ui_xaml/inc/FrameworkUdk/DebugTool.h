/* NovaOS: the Windows App SDK's FrameworkUdk/DebugTool.h (FrameworkUdk,
 * shipped closed): the debugger tools' side of XAML's debug interop, whose
 * interfaces XAML declares itself (dxaml/lib InternalDebugInteropModel.h,
 * which includes this): the connect message, and the
 * WRL module header the SDK's includes (XAML registers its debug interop
 * class with ActivatableClass). */
#pragma once
#include <wrl/module.h>

namespace DebugTool {

/* the message a diagnostics tool connects to the visual tree with, as the
 * visual diagnostics port (FrameworkUdk/VisualDiagnosticsPort.h) hands it
 * on.  Its fields are the tool's and FrameworkUdk's: XAML only checks its
 * size and passes it to its diagnostics, which NovaOS does not build, and
 * no tool connects on NovaOS, so it is opaque here. */
struct Msg_ConnectToVisualTree
{
    static const Msg_ConnectToVisualTree *SafeCast(const void *payload, int size)
    {
        return size == static_cast<int>(sizeof(Msg_ConnectToVisualTree))
            ? static_cast<const Msg_ConnectToVisualTree *>(payload) : nullptr;
    }
};

} // namespace DebugTool
