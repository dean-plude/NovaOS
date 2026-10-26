## WinUI 2's controls get their default styles

The eighth part of step 32.5 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)), in the Phase 32 push: with
`XamlControlsResources` merged into the app's resources, as an app's
`App.xaml` merges it, WinUI 2's controls take their default styles and
apply their templates.

- **Controls:** `InfoBadge`, and of the controls Calculator uses
  `ProgressRing` (with its animated visual player), `ProgressBar`,
  `SwipeControl` with its `SwipeItems`, and
  `BackdropMaterial.ApplyToRootOrPageBackground` (WinUI 2 now builds
  `ProgressBar`, `ProgressRing` and `SwipeControl`).
- **Framework pieces they need:** `RangeBase`, `UserControl`, the theme
  animations, `Shape` with `Rectangle` and `Ellipse`, the transforms,
  `StackPanel`, row and column definitions, visual states and
  transitions, `BrushTransition` and `ElementCompositionPreview`
  (vendored from Microsoft's WinUI 3 source).
- **What they ask the system:** `Windows.UI.Colors`, `AccessibilitySettings`
  and weak references to `UISettings` (ole32); composition visuals that
  report property changes (dcomp); and on a desktop thread a hidden
  `CoreWindow`, as XAML islands make on Windows (twinapi.appcore's
  `PrivateCreateCoreWindow`; `CoreWindow.GetForCurrentThread` answers and
  the window can be held weakly).  `Application.Suspending` and
  `Resuming` take handlers on a desktop app and are never raised, as on
  Windows.
- **Test:** `winui2test` makes each control from markup and checks its
  template's root, then loads a page as Calculator's declares these
  controls (82 checks).
- **Next (32.5):** `NavigationView` with `ItemsRepeater`, and
  `RadioButtons`, the rest of Calculator's controls.
