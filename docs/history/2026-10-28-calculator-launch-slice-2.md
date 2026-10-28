## Windows Calculator's launch, second slice: Windows.Globalization

The second part of item 3 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)),
step 32.6 of `Windows.UI.Xaml` on NovaOS
([design](design/windows-ui-xaml.md)): a new `Windows.Globalization.dll`
with the eleven globalization classes Windows Calculator's view models
name, so its launch gets past them.

- **Languages and regions:** `ApplicationLanguages` (the user's
  language, a `PrimaryLanguageOverride` first), `Language` (a BCP-47 tag
  as Windows spells it, its names, script, layout direction and
  extension subtags; `IsWellFormed`), `GeographicRegion` (codes, names,
  currencies), the user's `GlobalizationPreferences`, the
  `CalendarIdentifiers` and `ClockIdentifiers` names and
  `Fonts.LanguageFontGroup`, all from kernel32's locale data.
- **Numbers:** `DecimalFormatter` and `CurrencyFormatter` with Windows'
  options (least integer, fraction and significant digits, grouping, a
  decimal point always shown, a signed zero, numeral systems, a number
  rounder and `ApplyRoundingForCurrency`'s ten algorithms), a currency
  by symbol or code in the language's patterns, and parsing back; a
  double shows as its shortest text that reads back the same.
- **Dates:** `Calendar` (the Gregorian calendar from year 1 to 9999,
  in the machine's time zone or UTC, field by field, with its names and
  the 12- or 24-hour clock) and `DateTimeFormatter` (templates such as
  `longdate` or `month day` take the language's own picture, `{...}`
  patterns, `Patterns` and the `Include...` formats).
- **Tests:** `wrtglobtest` (new, 64- and 32-bit).
- **Not yet:** Calculator now stops on `CoreDragDropManager`,
  `AsyncCausalityTracer` and an unhandled `E_FAIL` inside
  `Windows.UI.Xaml.dll`; other calendar systems and time zones,
  `PercentFormatter`, the rounder classes and the manifest's languages
  are not there.
