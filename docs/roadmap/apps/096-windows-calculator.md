- Windows Calculator (github.com/microsoft/calculator): ~~its engine,
  CalcManager, in a Win32 `calc.exe` with the Standard, Scientific and
  Programmer calculators~~ Done (`calc`); the converters (CalcManager's
  `UnitConverter` needs the Concurrency Runtime's `task<>`, and the unit
  and currency data comes from the app's C# view models); then the
  shipped UWP package itself, which needs MSIX deployment,
  `Windows.UI.Xaml` with WinUI 2 (Phase 32, under way: WinRT activation
  and the app model are step 32.1) and the UWP .NET runtime.
