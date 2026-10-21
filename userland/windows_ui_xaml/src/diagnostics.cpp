// NovaOS: XAML's diagnostics interop (dxaml/lib DiagnosticsInterop.cpp),
// which Visual Studio's live visual tree and hot reload attach through, is
// not built yet.  The framework asks for it whenever its tree or window
// changes, and asks to make it only when the XamlDiagnostics runtime
// feature is on (off unless a debugger turns it on), so the stand-in has
// none to give.
#include <memory>

namespace Diagnostics
{
    class DiagnosticsInterop;

    std::shared_ptr<DiagnosticsInterop> GetDiagnosticsInterop(bool)
    {
        return nullptr;
    }
}
