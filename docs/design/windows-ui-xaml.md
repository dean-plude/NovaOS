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
| Composition | `Windows.UI.Composition` (the system compositor's WinRT API, over DirectComposition and Direct3D 11) | `dcomp.dll` (`userland/dcomp/composition.c`), step 32.2 |
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

The build is clang in MSVC mode (as for the rest of NovaOS's C++).  The
headers its IDL would give through MIDL (a closed tool) come from
`tools/winrt/abigen.py` instead (step 32.3's second part, below): it
reads the framework's MIDL 3 IDL and Windows Runtime metadata and writes
the same C++ ABI headers, at build time, from inputs the repository
vendors.

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
   CoreWindow's pointer events (`PointerPoint`) join here.  Done as
   follows: `dcomp.dll` serves `Windows.UI.Composition.Compositor` (as
   Windows' does) with container and sprite visuals (offset, size,
   anchor and centre points, scale, Z rotation, the transform matrix's
   2-D part, opacity, visibility, inset clips, the visual collection),
   colour and surface brushes (all four stretches, alignment ratios),
   targets on a window (`ICompositorDesktopInterop`) or on the thread's
   CoreWindow (`CreateTargetForCurrentView`), graphics devices on a
   Direct3D 11 device and their drawing surfaces
   (`ICompositorInterop`, `ICompositionDrawingSurfaceInterop`: the
   surface's texture, read back at `EndDraw` as DirectComposition's own
   surfaces are) and surfaces showing a composition swap chain.  A thread
   of the compositor's draws each target's tree in software after every
   change and copies it into the window with the GDI.  Not yet:
   animations (accepted, not run), expression animations, property sets,
   effects, batches, 3-D transforms, clips other than insets and bilinear
   sampling (drawing surfaces on an `ID2D1Device` came with 32.3's first
   part).  `twinapi.appcore`'s CoreWindow raises
   `PointerEntered`, `PointerMoved`, `PointerPressed`, `PointerReleased`,
   `PointerWheelChanged`, `PointerExited` and `PointerCaptureLost` from
   the mouse's messages with a `PointerPoint` (pointer 1, a mouse
   `PointerDevice`) and its `PointerPointProperties`.  Test: `comptest`,
   and `coreapptest`'s pointer and CoreWindow-target checks.
3. **32.3 The XAML core builds.** `dxaml`'s core, parser and metadata
   built as `Windows.UI.Xaml.dll` against 32.1 and 32.2;
   `Windows.UI.Xaml.Application.Start` runs and `XamlReader.Load` parses a
   page into its object tree.  Several pull requests:
   - *Direct2D device contexts (done).*  XAML draws its text and shapes
     with Direct2D 1.1: it makes an `ID2D1Factory1` (and takes its
     `ID2D1Multithread`), an `ID2D1Device` on its Direct3D 11 device and
     that device's contexts, and draws into DirectComposition surfaces
     made through an `IDCompositionSurfaceFactory` on the `ID2D1Device`,
     whose `BeginDraw` hands it a device context (`dxaml/xcp/plat/win/desktop`:
     `D3D11DeviceInstance.cpp`, `DCompSurface.cpp`, `D2DAcceleratedRT.cpp`).
     `d2d1.dll` now has `ID2D1Factory1`, `ID2D1Device`,
     `ID2D1DeviceContext` (its target a bitmap of its own or of a DXGI
     surface, read from the texture at `BeginDraw` and written back at
     `Flush` and `EndDraw`, so any Direct3D 11 device works),
     `ID2D1Bitmap1` and the other 1.1 interfaces it hands out;
     DirectComposition and Windows.UI.Composition take an `ID2D1Device`
     as their rendering device and give out device contexts from
     `BeginDraw`.  Test: `d2dctxtest`, and `comptest`'s Direct2D drawing
     surface.  Not yet: effects, command lists, image brushes, colour
     contexts, `ID2D1DeviceContext1` and later (XAML asks for
     `ID2D1DeviceContext4`/`5` only for colour glyphs and SVG).
   - *The headers MIDL would make (done).*  `dxaml` is written against
     the C++ ABI headers MIDL makes from its IDL
     (`dxaml/xcp/dxaml/idl`, MIDL 3) and those of the Windows SDK and
     the Windows App SDK.  `tools/winrt/abigen.py` writes them without
     MIDL: `winmd.py` reads Windows Runtime metadata (`Windows.winmd` and
     the lifted `Microsoft.UI*` `.winmd` files from windows-rs, MIT,
     `third_party/winmd`), `idl.py` reads the MIDL 3 IDL and synthesizes
     the interfaces MIDL makes for a runtime class (`IFoo` for its
     instance members, `IFooStatics`, `IFooFactory` with a composable
     class's `baseInterface`/`innerInterface`, `IFooOverrides`,
     `IFooProtected`, `IFoo2`... for versioned member blocks, the names
     `[interface_name]` and its kin give), and the writer emits one
     `namespace.h` per namespace (interfaces in vtable order with
     `__declspec(uuid)`, delegates as `IFooHandler`, the generic
     instances the namespace's types use as `template <>`
     specializations with the IIDs Windows' pinterface rule gives,
     `__FIVector_1_...` names, `RuntimeClass_`/`InterfaceName_` strings)
     and a `namespace.values.h` (enums with MIDL's `Name_Value`
     constants, structs, the generic interfaces' `_impl` templates).
     MIDL 3 invents the IIDs of the interfaces it synthesizes; NovaOS
     takes them by name from Microsoft's published
     `Microsoft.UI.Xaml.winmd`, so they are the IIDs Windows App SDK
     apps use (interfaces only in the IDL, such as the private ones, get
     name-based IIDs of NovaOS's).  `abigen.py --check` confirms that the
     IDL's 2136 public types match that metadata in names, member order
     and signatures.  Features `FeatureFlags.h` marks `AlwaysDisabled`
     are left out, as MIDL does.  The userland build generates the headers
     for any program or DLL whose manifest asks (`"winrt_abi": true`).
     Test: `winrtabitest` (published IIDs, and the Compositor driven
     through the generated interfaces).
   - *The `dxaml` toolchain and the core's lowest layer (done).*
     `userland/windows_ui_xaml/build.py` compiles Microsoft's sources as
     Microsoft does: clang in MSVC mode, C++17 without C++ exceptions, over
     Microsoft's STL and MinGW-w64's Windows headers (as `msvcp140.dll`'s
     build), with each component's sources, include directories and
     definitions read from its MSBuild project (`vcxproj.py`) and
     `components/pch/precomp.h` forced in.  What the sources need beyond
     that: WIL (MIT, vendored in `third_party/wil`); in
     `userland/windows_ui_xaml/inc` NovaOS's own WRL (the Windows SDK's
     Windows Runtime C++ template library: `ComPtr`, `WeakRef`,
     `RuntimeClass` with `Implements`, `CloakedIid`, `ChainInterfaces`,
     `MixIn`, `ComposableBase` and `FtmBase`, `Make`,
     `MakeAndInitialize`, `Callback`, `EventSource`, `HString`,
     `HStringReference`, `Module`, the handle wrappers), `weakreference.h`,
     `xamlom.h` (XAML diagnostics, from its public documentation), the
     SAL annotations, Windows Runtime `HRESULT`s and `MIDL_INTERFACE`
     with its `uuid` that MinGW-w64 lacks, and TraceLogging compiled out;
     ETW compiled out too, with the events header `mc.exe` would make from
     XAML's manifest written by `etwgen.py`; `abigen.py` now also writes
     MIDL's per-IDL-file headers (`Microsoft.UI.Xaml.controls.controls.h`,
     `Microsoft.UI.Xaml.private.h`); and a directory of one-line headers
     maps each include spelling the sources use to the file a
     case-insensitive disk would find.  The components below
     `CDependencyObject` build into `xaml_components.lib`: `base`,
     `xstring`, `criticalsection`, `dependencyLocator`,
     `runtimeEnabledFeatures`, `com`, `threading`, `staticpal` and
     `qualifiers` (their sources joined `third_party/microsoft-ui-xaml`).
     A program says `"xaml": true` to be built the same way and linked
     with it.  Test: `xamlbasetest`.
   - *What is closed.*  The open-source tree is not all of XAML: 75
     headers it includes come from closed Windows App SDK pieces, chiefly
     FrameworkUdk (`InputSiteHelpers.h`, which `CDependencyObject.h`
     includes, so every component above the lowest layer stops there;
     `CoreWindow.h`, `Theming.h`, `MUIUDK.h`), the lifted input and
     composition interop headers (`microsoft.ui.composition.interop.h`,
     `microsoft.ui.composition.experimental.h`) and the PTLS text engine
     (`ptls-min.h`, `XamlTextHost.h`); plus SDK headers MinGW-w64 lacks
     (`d2d1_3.h`, `async.h`, `atlbase.h`, `rometadataresolution.h`).
     NovaOS writes these as its own, from how the open sources call them
     and public documentation.
   - *The closed headers, the metadata and `CDependencyObject` (done so
     far).*  `userland/windows_ui_xaml/inc` now has the stand-ins the
     core's lower layers include: FrameworkUdk's `InputSiteHelpers.h`
     (an island's input site and its window), `InputHelpers.h`,
     `ContentExternalLinkHelper.h`, `TextInputProducerHelper.h` and
     `FrameworkUdk/FrameworkInputView.h`; the lifted IXP's
     `microsoft.ui.composition.interop.h` (`ICompositorSwapChainInterop`),
     `microsoft.ui.composition.experimental.h` and
     `microsoft.ui.input.inputpretranslatesource.interop.h`;
     `Microsoft.DirectManipulation.h` (the Windows App SDK's additions to
     DirectManipulation); and from the Windows SDK, WRL's `async.h`
     (`AsyncBase`), `rometadataresolution.h`, a `d2d1_3.h` that so far
     gives Direct2D 1.1, the UI Automation interfaces MinGW-w64's
     `uiautomationcore.h` lacks (their IIDs from Windows' Win32
     metadata), `GUID_NULL` and constexpr enum-flag operators.  Lifted
     types newer than windows-rs's `Microsoft.UI.winmd`
     (`IInputPointerSource2`, `PopupWindowSiteBridge`) come from NovaOS's
     own IDL (`userland/windows_ui_xaml/idl`), which also declares the
     `IReference<T>` instances XAML's metadata names; `AsyncStatus` is a
     scoped enum, as in the SDK.  With them the type metadata (`metadata`,
     `DynamicMetadataStorage`: every built-in type, property and
     namespace) builds and links, and `CDependencyObject`'s property
     system (`DependencyObject`) compiles into `xaml_components.lib`.
     `xamlbasetest` looks types and properties up by index and by name;
     the metadata's two calls into the layers above (custom types'
     providers, class constructors) are Microsoft's own stubs from its
     isolated unit tests until those layers build.
   - *The collections (done so far).*  `Collection` builds and links
     with `CDependencyObject` and the value types under it: `CValue`
     (whose `CValueStores.cpp` NovaOS wraps with explicit member
     instantiations, `userland/windows_ui_xaml/src`, because clang does
     not instantiate members defined after a `template struct`), the
     flyweight values, the primitive dependency objects, `math` (with
     `pal/common`'s geometry helpers) and `theming`.  The composition
     tree's `CompHelper/CompositionHelper.h` and
     `CompositionVisualSurfaceHelper.h` are declare-only stand-ins until
     the composition tree builds; `CoreWindowSiteBridge` joined NovaOS's
     lifted IDL.  Microsoft's release build leaves `DBG` undefined, and
     so does NovaOS's now.  The layers above (the core's `CCoreServices`,
     `CUIElement`, the resources, the visual tree) are Microsoft's own
     isolated unit-test stubs and metadata mocks, built with
     `__XAML_UNITTESTS__` as `xaml_teststubs.lib` for `xamlbasetest`
     only, plus a few NovaOS stand-ins (`src/core_stubs.cpp`) for what
     those stubs leave out.  `xamlbasetest` fills, searches and empties
     a `DependencyObjectCollection`, as Microsoft's own collection tests
     do, and walks a `CollectionMoveView`.  NovaOS gained what the
     sources needed below them: `thread_local` destructors (`__tlregdtor`
     in `userland/lib/tlregdtr.c`, linked only into C++ programs that have one), the UCRT's `_MAX_*TOSTR_*_COUNT` sizes,
     and `windowsnumerics.h`'s `invert` (MinGW-w64 declares it only).
   - *The resources and the parser (done so far).*  The `resources`,
     `deferral`, `legacy`, `colors` and `winuri` components join
     `xaml_components.lib`, and the markup parser (`core/Parser`, built
     as Microsoft builds it: the core's include path read from its
     `sources.props` and the `.props` files it imports, its own
     precompiled header, C++ exceptions on) with the `pal/common`
     pieces it calls (`MemoryStream`, `xstrlen`, the resource helpers;
     `src/pal.cpp`).  The parser reads XML through XmlLite, so NovaOS
     now has `xmllite.dll` (`userland/xmllite`): `IXmlReader` over
     libxml2's `xmlTextReader` (third_party/libxml2, MIT), each node and
     its attributes, namespace declarations among them, copied out as
     UTF-16 until the next `Read`, with the encodings XmlLite infers.
     WinURI's `CWinUriFactory` needed urlmon's `IUriBuilder`, now in
     `userland/urlmon`.  Stand-ins for `windows.graphics.interop.h`,
     `d2d1svg.h` (declared, empty) and a `tchar.h` that lets WinURI
     include `strsafe.h` after it.  The heap's `HeapSize` now gives the
     size asked for, as Windows' does, not its size class
     (`CreateStreamOnHGlobal` streams were as long as the class).
     `xamlparsetest` (`"xaml": "parser"`, on Microsoft's mock parser
     services from its isolated parser unit tests) reads XML with
     XmlLite, parses XAML into the node stream (namespaces, objects,
     implicit collections, content properties, `x:String`,
     `mc:Ignorable`, `{StaticResource}`, a `ResourceDictionary` with
     `x:Key`), reports bad XML and bad markup extensions, and looks up
     resource keys in a dictionary's map and the theme walk's cache.
   - *The resource dictionary and the object writer (done so far).*
     Two more of the core's libraries in `CORE_LIBRARIES`: the parser's
     object writer runtime (`core/Parser/runtime`: `XamlNativeRuntime`,
     which creates the core's objects through the type table's
     constructors and sets their properties, collections and dictionary
     entries; the managed runtime; the binary object writer) and the
     elements' exception-built library (`elements.except.vcxproj`:
     `Resources.cpp`'s `CResourceDictionary`, `ThemeResourceExtension`, `VisualTransition`,
     `XamlPredicate`), each with its project's precompiled header, and
     two small components they call (`simple`'s simple properties and
     `namescope`'s name scopes) and the elements' `DeferredKeys.cpp`.
     What they call further up the core (`CCoreServices`' resource
     loading and theming, templates, bindings, the visual state manager,
     the framework callbacks) has stand-ins in `src/elements_stubs.cpp`
     that report it absent; two of Microsoft's unit-test stubs answer
     where the core's own code is wanted (`AddPeerReferenceToItem` says
     `E_NOTIMPL`, which fails every collection add, and no resource
     lookup logger), so `build.py`'s `STUB_RENAMES` renames them out of
     their stub files and the core's bodies stand in.
     Stand-ins for the closed or missing headers they include:
     `WinUIEdit.h` (the Windows App SDK's RichEdit, here the SDK's
     RichEdit, text services and TOM headers), `mfmediaengine.h`,
     `MFSharingEngine.h` and `castinginterop.h` (the media engine
     callback, the sharing engine factory and the casting interfaces,
     declared from their documentation) and
     `CompHelper/CompositionSurfaceHelper.h` (declared as
     `DCompSurface.cpp` calls it).  The SDK's namespace headers include
     the namespaces they import; `abigen.py`'s only declare them, which
     compiles each XAML source in about 60% of the time, so the elements
     are given the two namespaces whose interfaces their headers use
     whole (`Windows.ApplicationModel`, `Windows.Devices.Input`) first.
     `xamlloadtest` (`"xaml": "elements"`) loads XAML text into objects:
     XmlLite, the parser and the object writer make a live
     `CResourceDictionary` of `x:String`, `x:Double`, `x:Int32`,
     `x:Boolean` and `Color` resources, and refuse malformed markup; it
     also adds, finds, refuses and removes keys directly.  Until
     `CCoreServices` builds, its core is Microsoft's mock parser services
     laid where a `CCoreServices` holds them, with the few fields the
     dictionary reads set as the core's constructor sets them (through
     `DxamlCoreTestHooks`, the core's friend; theming over Microsoft's
     `TestThemingInterop`), and its type table constructs the six types
     it uses.
   - *Part 8: merged and theme dictionaries.*  `MergedDictionaries` and
     `ThemeDictionaries` are field-backed properties: the object writer
     reads and sets the field at the offset the core's generated metadata
     (`core/metadata/DynamicMetadata.g.cpp`,
     `c_aDependencyPropertyRuntimeData`) gives the property.  That table
     has a row for every property of every element and names every
     element class, so it cannot compile before the elements do.
     `src/property_data.cpp` keeps Microsoft's rows for the classes that
     build (the `ResourceDictionary`'s three, so far), found by property
     index, with `ReflectionAPI.cpp`'s getters over them (and its
     `CClassInfo::GetCoreConstructor`, from the type table) and
     `DependencyProperty.cpp`'s `CreateDefaultValueObject` (a property's
     default object made by its type's constructor: the merged
     dictionaries' collection, and the theme dictionaries' dictionary,
     marked as one); `build.py` renames the metadata mocks' versions out
     of the way.  A row is added as its class builds, until the whole
     table compiles with the core.  `xamlloadtest` loads a dictionary
     with two merged dictionaries (the last one wins a shared key) and
     `Light` and `Dark` theme dictionaries, and looks keys up through
     them.
   - *Next.*  The core services (`CCoreServices`, `core/dll/xcpcore.cpp`)
     with the core's metadata tables (`core/metadata`), and the rest of the
     core, then `dxaml/lib` compiled as
     `Windows.UI.Xaml.dll`, until
     `Application.Start` runs and `XamlReader.Load` parses a page, one
     component seam per pull request.
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
