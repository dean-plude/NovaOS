## Printing: Notepad++ and SumatraPDF print in the app corpus

Third and last part of roadmap step 33.5 ([design](../design/printing.md)),
which is now done. Two real Windows programs print on NovaOS the way
people print, from their own Print dialog:

- **Notepad++ and SumatraPDF** (the nightly app corpus) open their
  document, press Ctrl+P, pick the network printer in the dialog's list
  and print; then print again to Microsoft Print to PDF and save the file
  from its Save As box.
- **Checked against reference files:** the corpus host runs
  `tools/ippprinter.py`, a fake IPP printer; the page it gets must match
  `tests/reference/notepad++-print.png` and `sumatrapdf-print.png`, and
  `spooltest same` checks that the PDF saved by Microsoft Print to PDF is
  byte for byte what the network printer got. `spooltest add` adds the
  network printer as `AddPrinter` does.

Notepad++ stands in for Windows' Notepad, which is not redistributable.
Next for printing: finding printers on the network (DNS-SD,
WS-Discovery) and a print queue in Settings.
