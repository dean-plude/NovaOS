## LibreOffice: an office suite from the App Store

Roadmap step 27.2: LibreOffice 24.8, the office suite the App Store
lists, edits Word and Excel files on NovaOS.  The nightly corpus installs
it from the Store's download (its Windows Installer package, with
`msiexec /qn`), opens `letter.docx` in Writer and types a line at its
start, opens `sums.xlsx` in Calc and puts `=SUM(A1:A2)` in A3, saves both
in their own formats with Ctrl+S, and reads the edits back out of the
saved files with 7-Zip (`tests/appcorpus/953-libreoffice.py`; the corpus
now makes the two files, alongside its PDF).  The installer ran as it
was; starting LibreOffice found these gaps:

- **Two missing DLLs**: `mergedlo.dll` imports `oledlg.dll` (the OLE
  Insert Object dialog) and Skia's `skialo.dll` imports `fontsub.dll`
  (font subsetting), so `soffice.bin` could not start at all.  Both are
  new: oledlg's dialogs answer Cancel, as a user closing them would, and
  `CreateFontPackage` fails so its caller embeds the whole font.
- **d2d1's ordinals**: LibreOffice imports `D2D1CreateFactory` by number
  (1) and `D2D1MakeRotateMatrix` by 2, as the Windows SDK's import library
  makes it.  NovaOS's d2d1 had its exports numbered alphabetically, so
  ordinal 1 was `D2D1ComputeMaximumScaleFactor`, which LibreOffice called
  as the factory and crashed.  d2d1 now has Windows' numbers.
- **The file dialog's OnFileOk**: only an `IFileDialogEvents::OnFileOk`
  that returns S_FALSE keeps the dialog open on Windows; LibreOffice's
  returns E_NOTIMPL, which NovaOS took as a refusal, so Open never
  closed.  Any other answer now accepts the choice.
- **Functions it imports**: user32's `GetUpdatedClipboardFormats`
  (asked as soon as a document window opens) and the DDE calls that never
  find a conversation; gdi32's `PolyDraw`, `CreateDIBPatternBrush` and
  `CreateScalableFontResource`; the C runtime's `_wsplitpath_s`,
  `_wmakepath_s`, `_byteswap_*`, `_Exit` and `_resetstkoflw`; crypt32's
  `CryptMemAlloc` family; propsys's `PropVariantToStringWithDefault`;
  kernel32's `FatalAppExit`.

`dlltest` (core self-tests, 64- and 32-bit) checks d2d1's ordinals, the
two new DLLs, the clipboard's format list and the `_s` path functions.
Still open: Impress, Draw and Base are not checked, and PDF export embeds
whole fonts.
