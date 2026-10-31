## Windows Calculator's launch, fifth slice: XAML images

The fifth part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  The system XAML now builds
images: the core's image element and its image provider, and the
imaging component (image cache, downloads, WIC decoding, the decoded
image cache, software bitmaps), with the framework's `Image`,
`ImageSource`, `BitmapSource` and `BitmapImage`, vendored from
microsoft-ui-xaml.  The SVG image source and the render target bitmap's
sprite-visual renderer (a closed Windows App SDK helper) are left out.

- **Title bar:** `Window.SetTitleBar` on a UWP window, which WinUI 3
  refuses, is now answered by NovaOS.  The window keeps its frame's
  caption, and the element stays content.
- **Moved properties:** a UWP app's compiled XAML names `TabIndex`,
  `IsTabStop`, `FocusState`, `UseSystemFocusVisuals`, `XYFocus*` and
  `AccessKey` by their pre-1709 `Control` and `FrameworkElement`
  indexes.  NovaOS maps them to the `UIElement` properties they moved to.
- **Not yet:** Calculator now gets through its title bar and
  `NavigationView` and stops on the popups `NavigationView` makes.
