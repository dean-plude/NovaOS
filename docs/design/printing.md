# Printing

Roadmap step 33.5. NovaOS prints the Windows way: programs call the same
spooler API (`winspool.drv`), GDI printer DCs and print dialogs (`comdlg32`)
they call on Windows, and the Print Spooler service (`spoolsv.exe`) keeps
the queue and sends each job to its printer's port. Nothing in a program
changes.

## What is there (part 1)

```
program ── PrintDlg (comdlg32) ──> DEVMODE + DEVNAMES + printer DC
   │
   ├── GDI on the printer DC (gdi32 print.c): StartDoc / StartPage /
   │   drawing / EndPage / EndDoc ── the driver renders a PDF ──┐
   │                                                            │
   └── or raw bytes: OpenPrinter / StartDocPrinter / WritePrinter
                                                                │
winspool.drv ── \\.\pipe\spoolss (novaspool.h) ──> spoolsv.exe ─┘
   printers = registry keys            queue, spool\PRINTERS\*.SPL/*.SHD
                                       ──> the port: a file, nul:
```

- **The Print Spooler service** (`userland/programs/spoolsv.c`, service
  `Spooler`, automatic start). It numbers jobs, names their spool files
  (`C:\Windows\System32\spool\PRINTERS\NNNNN.SPL`, with a shadow file
  `NNNNN.SHD` holding the job), counts their pages and, when a job is
  complete, sends it to its printer's port and deletes it. Jobs that were
  complete when the computer stopped print after the next start, as on
  Windows. A paused printer or job keeps its jobs waiting. The ports it
  sends to so far: `PORTPROMPT:` and `FILE:` (the job's output file), a file
  path (a local port named after a file) and `nul:`.
- **`winspool.drv`** (`userland/winspool/winspool.c`): the printers are the
  keys under `HKLM\SYSTEM\CurrentControlSet\Control\Print\Printers`, as on
  Windows (`Name`, `Port`, `Printer Driver`, `Attributes`...), and the
  default printer is `HKCU\Software\Microsoft\Windows NT\CurrentVersion\
  Windows` `Device`. `EnumPrinters` (levels 1, 2, 4 and 5, wide and ANSI),
  `OpenPrinter`, `GetPrinter`, `SetPrinter` (pause, resume, purge, level 2
  and 5), `AddPrinter`, `DeletePrinter`, `Get/SetDefaultPrinter`, the job
  calls (`StartDocPrinter` to `EndDocPrinter`, `EnumJobs`, `GetJob`,
  `SetJob`), `DocumentProperties` and `DeviceCapabilities` (Letter, Legal,
  A3, A4, A5; 300 dpi; colour), `EnumForms`, `EnumPorts`,
  `EnumPrinterDrivers`. A job on a file port with no output file asks where
  to save with the Save As dialog ("Save Print Output As"), as Microsoft
  Print to PDF does. With the spooler stopped, `StartDocPrinter` fails with
  `RPC_S_SERVER_UNAVAILABLE`, as on Windows. The DLL is built as
  `winspool.drv` (its `dll.json` `"file"`), so programs link it by
  Windows' name.
- **Microsoft Print to PDF.** The kernel makes the printer's key once at
  boot (`kernel/um/um_registry.c`; a printer deleted since stays deleted):
  port `PORTPROMPT:`, driver `Microsoft Print To PDF`.
- **Printer DCs** (`userland/gdi32/print.c`): `CreateDC` with a printer's
  name gives a DC whose surface is the page, at the printer's resolution
  and paper size from its DEVMODE (300 dpi Letter: 2550 x 3300 pixels).
  `GetDeviceCaps` answers for the page (`TECHNOLOGY` = `DT_RASPRINTER`,
  `LOGPIXELSX`, `PHYSICALWIDTH`, `PHYSICALOFFSETX` = 0...). The driver
  renders each page into the job as a PDF page: an RGB image of the page,
  Flate-compressed with stb's zlib writer (the copy plutovg already ships,
  `third_party/plutovg`, public domain/MIT). The PDF is written as it goes
  through `WritePrinter`, with its cross-reference table at the end, and
  holds no dates, so the same pages make the same file. `SetAbortProc`
  and the old `Escape` codes (`NEWFRAME`, `STARTDOC`, `GETPHYSPAGESIZE`...)
  work.
- **The print dialogs** (`userland/comdlg32/printdlg.c`): `PrintDlgW/A`
  (Print, and Print Setup with `PD_PRINTSETUP`), `PrintDlgExW` and
  `PageSetupDlgW`. Print shows the printers with their status, type and
  port, print to file, the page range and the copies; the choice comes back
  as a DEVMODE and a DEVNAMES in movable global memory, and a printer DC
  with `PD_RETURNDC`. `PD_RETURNDEFAULT` answers without a dialog. The
  controls keep Windows' IDs (`cmb4`, `rad1`, `edt1`...).
- **The test**: `spooltest` (core suite, 64- and 32-bit) lists the
  printers, prints raw jobs to files and to a printer it adds, holds a job
  on a paused printer, answers the Save As prompt, prints two GDI pages to
  a PDF and checks that the first page's image is exactly what GDI draws on
  a bitmap of the page's size, and answers the Print dialog
  ([screenshot](../screenshots/print-dialog.png)).

### Choices

- **The spooler protocol.** Windows' winspool speaks MS-RPRN RPC to the
  spooler over `\pipe\spoolss`. NovaOS uses the same pipe with fixed
  messages (`userland/include/novaspool.h`): only winspool and the spooler
  speak it. The printers themselves are registry keys that winspool reads
  and writes directly, as the spooler's own store.
- **The driver lives in gdi32.** On Windows a printer driver is a DLL the
  spooler loads (`mxdwdrv.dll`, v4 drivers). NovaOS's two drivers both
  render a PDF from the page, so the renderer is in gdi32's printer DC; the
  driver name is kept in the printer's key so a real driver can be chosen
  by it later.
- **Pages are images.** gdi32 is a rasterizer with no recorded drawing
  (no EMF spooling yet), so each page is the bitmap GDI drew. The PDF is
  exact but its text is not selectable. A PDF writer was looked for first:
  the small MIT/public-domain ones (PDFGen and others) draw their own text
  and shapes and would need GDI's drawing recorded to be of use, so the
  container is written directly (fixed objects, a few hundred bytes) and
  the compression reuses stb.
- **300 dpi.** Microsoft Print to PDF reports 600 dpi. At 600 dpi a Letter
  page is 135 MB of pixels; 300 dpi (34 MB) keeps a page in memory on a
  small machine and prints text sharply. A DEVMODE asking for another
  resolution (72 to 600 dpi) gets it.

## To do

1. **IPP Everywhere / driverless network printing** (Windows' "Microsoft
   IPP Class Driver", its default for modern printers since 2022): an
   `http://` or `ipp://` port in the spooler sends the job with an IPP
   Print-Job request over NovaOS's TCP stack (as PDF, or PWG Raster when
   the printer takes only that), reads the printer's attributes
   (`Get-Printer-Attributes`) for its paper sizes and status, and finds
   printers with DNS-SD (`_ipp._tcp`). Test: a fake IPP printer served by
   the CI host takes a job and the test compares what it got with a
   reference file.
2. **Settings > Printers**: the printers, the default, adding an IPP
   printer by address, the queue (pause, cancel), print a test page.
3. **The "Done when" check**: Notepad and SumatraPDF list the printer and
   print a page to the PDF printer and to the IPP printer in the app
   corpus, and the output matches a reference file.
4. **Later**: EMF spooling and a PDF with real text (selectable, smaller);
   real user-mode vendor drivers (v4 / XPS print path) under the Windows
   driver model rules ([windows-drivers.md](windows-drivers.md)); print
   processors; WIA scanning.
