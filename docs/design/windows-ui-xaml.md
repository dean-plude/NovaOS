# Windows.UI.Xaml on NovaOS

UWP apps (Windows Calculator, the Store's apps, many DirectX games packaged
for the Store) and C++/WinRT or C# programs that host XAML draw their
windows with the Windows Runtime's XAML framework, `Windows.UI.Xaml`.
This page is the plan for running that framework on NovaOS: what
Windows' own stack looks like, which parts NovaOS takes from Microsoft's
MIT-licensed source, which parts NovaOS has to provide because they are
the operating system, and the steps (roadmap Phase 32).

## The stack on Windows

From the bottom up:

| Layer | Windows | On NovaOS |
|---|---|---|
| Windows Runtime core | `combase.dll`: `HSTRING`, `RoGetActivationFactory`, `RoActivateInstance`, the registry's `ActivatableClassId` keys, `.winmd` metadata | `ole32.dll` (`userland/ole32/winrt.c`) |
| App model | `twinapi.appcore.dll` (`CoreApplication`, views), `Windows.UI.dll` (`CoreWindow`, `CoreDispatcher`) | `twinapi.appcore.dll` (`userland/twinapi_appcore`), step 32.1 |
| Composition | `Windows.UI.Composition` (the system compositor's WinRT API, over DirectComposition and Direct3D 11) | to write (NovaOS has `dcomp.dll`), step 32.2 |
| XAML | `Windows.UI.Xaml.dll` (system XAML) and WinUI 2 (`Microsoft.UI.Xaml.dll`, controls on top of it) | Microsoft's MIT XAML source, built for NovaOS, steps 32.3 on |
| Packages | AppX/MSIX deployment, `resources.pri` (MRT), the app container | to write, after XAML draws |

## Where the XAML framework comes from

Dean's standing rule is to reuse what Microsoft publishes under MIT
rather than write it again. The system `Windows.UI.Xaml.dll` itself is not
published, but its successor is: WinUI 3 is the same XAML framework
"lifted" out of Windows into `Microsoft.UI.Xaml`, and its full source,
the `dxaml` tree (the XAML core, parser, layout, rendering, controls and
their templates), is MIT in
[github.com/microsoft/microsoft-ui-xaml](https://github.com/microsoft/microsoft-ui-xaml),
together with WinUI 2 (the `controls` tree). C++/WinRT
([github.com/microsoft/cppwinrt](https://github.com/microsoft/cppwinrt))
and WIL ([github.com/microsoft/wil](https://github.com/microsoft/wil)),
which that source is written against, are MIT too.

So NovaOS builds `dxaml` (unchanged where possible, as with WDF and
CalcManager) under both names:

- as **`Windows.UI.Xaml.dll`**, with its types registered under the
  `Windows.UI.Xaml` namespace, for UWP apps and WinUI 2 (the type tables
  and projections are generated from the framework's IDL and metadata, so
  the namespace is a code-generation choice; WinUI 3 began as this same
  code under `Windows.UI.Xaml`);
- later as **`Microsoft.UI.Xaml.dll`**, for Windows App SDK apps.

What the `dxaml` build needs that Microsoft does not publish is the
"lifted" Interactive Experiences layer it links against
(`Microsoft.UI.Composition`, `Microsoft.UI.Input`, `CoreMessaging` and
`Microsoft.Internal.FrameworkUdk`, shipped closed in the Windows App
SDK). On Windows the system XAML uses the in-box equivalents instead:
`Windows.UI.Composition`, `CoreWindow` and the system's input. Those are
operating-system components, so NovaOS writes them (steps 32.1 and 32.2)
and points the XAML build at them. NovaOS does not ship Microsoft's
closed binaries.

The build is clang in MSVC mode (as for the rest of NovaOS's C++), with
the generated sources the repository checks in. How to produce the
headers its IDL would give through MIDL (a closed tool) without it is
the first question step 32.3 answers.

## The steps (Phase 32)

1. **32.1 WinRT activation from the registry; CoreApplication and
   CoreWindow.** `RoGetActivationFactory` loads the in-process server the
   registry names (`HKLM\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\<class>`,
   `DllPath`, `ActivationType` 0) and calls its `DllGetActivationFactory`,
   as combase does. `twinapi.appcore.dll` serves
   `Windows.ApplicationModel.Core.CoreApplication` (`Run` drives an
   `IFrameworkView`: `Initialize`, `SetWindow`, `Load`, the launch's
   `Activated`, `Run`, `Uninitialize`; `Exit`; the main view) and
   `Windows.UI.Core.CoreWindow` (a top-level window with its
   `ICoreWindowInterop` HWND, `Activate`, `Close`, bounds, key states,
   pointer capture and cursor; events `Activated`, `CharacterReceived`,
   `Closed`, `KeyDown`, `KeyUp`, `SizeChanged`, `VisibilityChanged`; the
   `CoreDispatcher` with `ProcessEvents` and `RunAsync`). DXGI's
   `CreateSwapChainForCoreWindow` presents into it, so a DirectX UWP
   app's frame loop runs. Test: `coreapptest`.
2. **32.2 Windows.UI.Composition.** `Compositor`, container and sprite
   visuals, colour and surface brushes, `CompositionGraphicsDevice`
   drawing surfaces over Direct2D, the desktop and CoreWindow targets,
   and the interop interfaces XAML uses, on NovaOS's DirectComposition.
   CoreWindow's pointer events (`PointerPoint`) join here.
3. **32.3 The XAML core builds.** `dxaml`'s core, parser and metadata
   built as `Windows.UI.Xaml.dll` against 32.1 and 32.2;
   `Windows.UI.Xaml.Application.Start` runs and `XamlReader.Load` parses a
   page into its object tree.
4. **32.4 A XAML page draws.** Layout and rendering through composition:
   a `Grid` with a `TextBlock` and a `Button` from `generic.xaml`, typed
   into and clicked in a self-test, with a reference screenshot.
5. **32.5 WinUI 2.** The `controls` tree as `Microsoft.UI.Xaml.dll` 2.x on
   top of the system XAML, with its `.pri` resources.
6. **32.6 Packages.** Install an `.msix`/`.appx` (manifest, activatable
   classes from `InProcessServer` extensions, `resources.pri` read by
   MRT), start it from the Start menu; then the Calculator package
   (roadmap 27.x leftovers).

Out-of-process WinRT servers, the app container sandbox and the
.NET Native/UWP .NET runtime are later work.
