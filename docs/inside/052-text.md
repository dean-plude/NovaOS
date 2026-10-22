- **Text**: `novatext.dll`, one shared text core with HarfBuzz (shaping)
  and FreeType (fonts).
  - Uniscribe (`usp10`) itemizes text by script and direction and shapes
    it with HarfBuzz, and GDI's `ExtTextOut` sends complex scripts through
    it, as Windows' LPK does, so Arabic, Hebrew and the Indic scripts join,
    reorder and run right to left. GDI falls back to Noto Sans for Arabic
    and Devanagari.
  - DirectWrite (`dwrite`) lays text out on the same core, with font
    fallback, and its factory is an `IDWriteFactory7` with Windows 10's
    font model: font sets and `IDWriteFontFallback`, which Chromium-based
    browsers ask for, and the typographic family model with axis values,
    font faces up to `IDWriteFontFace6` and text layouts up to
    `IDWriteTextLayout4`, which XAML's text core starts from
    (`tools/dwtest`, `tools/dw3test`, `tools/dw6test`). No font has
    variations, so axis values pick a family's weight, stretch and style.
    Its `IDWriteTextAnalyzer2` uses ICU for script, bidi and line-break
    analysis, then the shared HarfBuzz path for logical-order glyph shaping
    and placements.
