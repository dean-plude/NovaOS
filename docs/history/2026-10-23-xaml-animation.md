## XAML animation and WinUI 2's inherited properties

The fifth part of step 32.5 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)), in the Phase 32 push: the
framework's animation, which the core's start-up and every control's
visual states run on, and WinUI 2 types that inherit framework
properties.

- **Animation, built from Microsoft's sources:** the core's animation
  library (timelines, storyboards, animations, key frames, triggers and
  the timer under `DispatcherTimer`), the animation component, and the
  framework's `DispatcherTimer`, `Timeline`, `Storyboard` and animation,
  key frame and easing classes.
- **Composition animations:** NovaOS's `CompositionAnimationHelper` (the
  Windows App SDK's is closed) starts and stops them on NovaOS's
  `Windows.UI.Composition`.
- **UWP start-up:** FrameworkUdk's `CoreWindow` position listener; the
  UWP route now passes the core's `DispatcherTimer` and reaches the
  view's window.
- **WinUI 2's inherited properties:** WinUI 2 names its types' Windows
  bases as primitive type names; the faces now map those to the
  framework's types too, so `AcrylicBrush` finds `FallbackColor`.
- **Test:** `winui2test` loads an `AcrylicBrush` with its `FallbackColor`
  from markup (64-bit).
- **Next (32.5):** the UWP window's present target (32.4's composition)
  on to `Window.Current`; WinUI 2's theme resources past
  `InkToolbarFlyoutItem`, a type the framework lacks; its controls'
  default styles.
