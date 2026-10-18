## Printing: the print spooler, Microsoft Print to PDF and the print dialogs

First part of roadmap step 33.5 ([design](../design/printing.md)).
NovaOS had no printers: `winspool.drv` was 113 lines of stubs and every
print menu said no printer was installed. Now programs print the Windows
way:

- **The Print Spooler service** (`spoolsv.exe`, service `Spooler`) keeps
  the queue in `C:\Windows\System32\spool\PRINTERS` and sends finished
  jobs to their printer's port; jobs complete at a shutdown print after
  the next start, and a paused printer or job keeps its jobs waiting.
- **`winspool.drv`** is the real API: printers are their registry keys,
  `EnumPrinters`, `GetPrinter`, `AddPrinter`, the default printer, the job
  calls, `EnumJobs`/`SetJob`, `DocumentProperties` and
  `DeviceCapabilities`. It is built under its Windows name.
- **Microsoft Print to PDF** is installed: a printer DC (`CreateDC` with
  the printer's name) is a 300 dpi page, and each page drawn on it becomes
  a page of a PDF (asked for with Save As when the program names no file).
- **`PrintDlg`, `PrintDlgEx` and `PageSetupDlg`** show the Print, Print
  Setup and Page Setup dialogs and hand back the DEVMODE, DEVNAMES and DC.
- **DEVMODE** has its printer fields (`dmPaperSize`, `dmCopies`...).
- New self-test `spooltest` (64- and 32-bit): a GDI page printed to PDF
  must be exactly what GDI draws.

Next: IPP Everywhere network printing, then Settings' Printers page.
