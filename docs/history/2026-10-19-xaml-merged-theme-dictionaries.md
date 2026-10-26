## XAML's merged and theme dictionaries

Eighth part of step 32.3 of `Windows.UI.Xaml` on NovaOS (Phase 32,
[design](design/windows-ui-xaml.md)): a `ResourceDictionary` loaded
from XAML now keeps its `MergedDictionaries` and `ThemeDictionaries`,
and looking a key up searches them as Windows does.

- **Field-backed properties:** the field offsets, default objects and
  getters the core's generated metadata gives a property
  (`c_aDependencyPropertyRuntimeData`, `ReflectionAPI.cpp`,
  `DependencyProperty.cpp`), copied from Microsoft's for the classes that
  build (`userland/windows_ui_xaml/src/property_data.cpp`), in place of
  the metadata mocks', which knew no offsets.
- `xamlloadtest` loads two merged dictionaries (the last one wins a
  shared key, the first still answers its own) and `Light` and `Dark`
  theme dictionaries, and finds the Dark theme's resource.
