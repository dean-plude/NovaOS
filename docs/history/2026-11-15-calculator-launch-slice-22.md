## Windows Calculator's launch, twenty-second slice: the error behind XAML's parse failure

The twenty-second part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)).  Calculator's main page stopped
with XAML parse error 0x802B000A and nothing beneath it: page loading,
template application and a data template's content turn any failure into
that one code.  The framework now writes the real failure to the debug log
first (`xaml: LoadComponent failed hr=...`, and the same for
`ApplyTemplate` and `DataTemplate.LoadContent`), every error the XAML
error service is given (`xaml: error N hr=... line L col C: message`),
and the name of a type the parser could not resolve when it is asked to
create one (`xaml: cannot create NAME: type not resolved`).

- **Tests:** `wuxamltest` loads markup naming an unknown element, which
  fails, with the parser's error and its line in the debug log.
- **Found:** the failure is Calculator's `Pivot` (`DockPivot`, the
  history and memory panel): the type comes from `Microsoft.UI.Xaml.Phone.dll`
  in the Windows App SDK, which is not open source and is not part of
  NovaOS's framework, so the parser cannot resolve it and loading the page
  fails with E_FAIL.  The page does not draw yet.
