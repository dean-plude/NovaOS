## DirectWrite's Windows 10 font model

The third part of step 32.4 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)), in the Phase 32 push: the
DirectWrite interfaces XAML's text services start from as a `Window` is
created.

- **DirectWrite up to `IDWriteFactory7`** (`userland/dwrite`): system
  font collections in the typographic or weight-stretch-style family
  model (`IDWriteFontCollection2`/`3`), font sets with filtering by
  property and axis range, font resources, font references with axis
  values, font set builders that add files, in-memory font file loaders,
  families and font lists 2 with axis matching, font faces 4 to 6, font
  fallback by axis values, text formats 1 to 3, text layouts 1 to 4 and
  text analyzers 1 and 2 (script properties, justification, glyph
  orientation, typographic features).  NovaOS's fonts are static, so
  axis values map to weight, stretch and style.
- **Segoe UI Variable**, XAML's default font, maps to NovaOS's sans
  serif face as Segoe UI does.
- **Test:** `dw6test` (graphics suite, 64-bit and 32-bit).
- **Next (32.4):** the framework's `XamlIsland` peer, which a desktop
  `Window` now reaches, then a page drawn, typed into and clicked.
