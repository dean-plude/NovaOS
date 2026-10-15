- **Text**: `novatext.dll`, one shared text core with HarfBuzz (shaping)
  and FreeType (fonts).
  - Uniscribe (`usp10`) itemizes text by script and direction and shapes
    it with HarfBuzz, and GDI's `ExtTextOut` sends complex scripts through
    it, as Windows' LPK does, so Arabic, Hebrew and the Indic scripts join,
    reorder and run right to left. GDI falls back to Noto Sans for Arabic
    and Devanagari.
  - DirectWrite (`dwrite`) lays text out on the same core, with font
    fallback, and its factory is an `IDWriteFactory3` with Windows 10's
    font sets and `IDWriteFontFallback`, which Chromium-based browsers ask
    for (`tools/dwtest`, `tools/dw3test`). Its `IDWriteTextAnalyzer` uses ICU
    for script, bidi and line-break analysis, then the shared HarfBuzz path
    for logical-order glyph shaping and placements.
