// NovaOS: the marker a program with the XAML framework linked into it (the
// XAML self-tests, "xaml": "core" or "framework" in their NAME.json)
// exports.  Such a program serves the framework's classes itself
// (RoRegisterActivationFactories); the system Windows.UI.Xaml.dll
// (userland/windows_ui_xaml_dll), which the registry names for every
// Windows.UI.Xaml class, sees the marker in the program and answers none,
// so a class the program does not serve fails as it did before the DLL
// existed instead of starting a second framework in the process.
extern "C" __declspec(dllexport) const int nova_xaml_in_program = 1;
