## The XAML framework's theme resources

The second part of step 32.4 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)), in the Phase 32 push: the
control styles and theme resources a XAML `Window` loads as it is
created.

- **Microsoft.UI.Xaml.Resources.19H1.dll** (new,
  `userland/microsoft_ui_xaml_resources_19h1`): the framework's resource
  DLL, made at build time from Microsoft's `themes/generic.xaml`, split
  as Microsoft's `SplitGenericXaml.cs` splits it into `Styles.xbf` and
  `ThemeResources.xbf`.  Windows ships them as XBF from the XAML
  compiler, which runs on Windows only, so NovaOS ships the split XAML as
  text, which the framework's parser reads as it reads a page.
- **Built from Microsoft's sources:** the platform's resource provider
  (`CommonResourceProvider`, the provider Microsoft's desktop platform
  falls back to without MRT Core) with its file resources, the
  framework's metadata provider (`XamlLibMetadataProvider`), and the
  framework classes of the data and items panel templates, template
  content and transitions the resources make.
- **Left out for now:** the styles of the phone extension library's
  `Pivot`, looping selector and pickers and of WinUI's `ProgressBar` and
  text command bar flyout (with the text boxes' styles that use it),
  whose libraries are not built yet.
- **Test:** `xamlapptest` finds both resources as the framework does and
  makes the core's resource manager (64-bit).
- **Next (32.4):** DirectWrite's Windows 10 font model, where the theme
  resources' first `FontFamily` now stops a `Window`'s creation; then a
  page drawn, typed into and clicked.
