## XAML's core services, made for real

Tenth part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](../design/windows-ui-xaml.md)): the XAML core is now made the
way the framework makes it, `CCoreServices::Create`, on Microsoft's own
platform layer.

- **The platform layer:** `CWindowsServices` (`plat/win/browserdesktop`),
  the platform services the core runs on (its queues, work items and
  security token), handed to the core through `ObtainPlatformServices`
  and `ObtainCoreServices`.
- **What `Create` makes:** the media queue, render target bitmaps, the
  geometry builder, the text core (`CTextCore` and `WinTextCore`) and the
  system theming, all built from Microsoft's sources.
- **Stand-ins:** the few closed or missing headers those include (ATL's
  declarations, WPP tracing, DirectManipulation's helper, the shell's
  `shlobj_core.h`) and Direct2D 1.1's interface IDs for `__uuidof`.
- **A fix:** a generated stand-in for a constructor that is not built
  yet now stops the program naming the function; before, it called
  itself until the stack ran out.
- `xamlloadtest` makes its core with `CCoreServices::Create` and loads
  XAML through it: 34 checks.
