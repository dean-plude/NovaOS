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
  Windows. A paused printer or job keeps its jobs waiting. The local ports
  it sends to: `PORTPROMPT:` and `FILE:` (the job's output file), a file
  path (a local port named after a file) and `nul:`; network printers are
  below.
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
  ([screenshot](../screenshots/print-dialog.png)). `delaytest`, which
  loads `winspool.drv` by name the way Firefox's delay-load helper does,
  checks that the installed PDF printer is listed, is the default and
  opens.

## Network printers (part 2)

```
spoolsv.exe ── an ipp:// ipps:// http:// https:// port (spoolsv/ipp.c)
   │  Get-Printer-Attributes: make and model, location, state,
   │      document formats, PWG Raster resolutions and colour spaces
   └─ Print-Job over WinHTTP (POST application/ipp):
        PDF printer      the job's PDF as it is
        PWG Raster only  the PDF's page images ─ inflate ─ scale to a
                         resolution the printer has ─ PWG Raster lines
        raw job          application/octet-stream
```

- **The IPP port** (`userland/programs/spoolsv/ipp.c`). A printer whose
  port is an `ipp://`, `ipps://`, `http://` or `https://` URL is an IPP
  printer (RFC 8010/8011; IPP Everywhere, PWG 5100.14), Windows'
  "Microsoft IPP Class Driver". `ipp://host/path` is `http://host:631/path`
  and `ipps://` the same over TLS; a printer's self-signed certificate is
  accepted, as CUPS and Windows accept them. Before each job the spooler
  asks the printer for its attributes, then sends the job with
  `Print-Job`: a PDF as it is when `document-format-supported` has
  `application/pdf`, else PWG Raster (PWG 5102.4) when it has
  `image/pwg-raster`, at the printer's resolution nearest the page's
  (pixels scaled), sRGB or, for a grey-only printer, sGray; raw jobs go
  as `application/octet-stream`. A printer that does not answer marks the
  printer offline (`PRINTER_STATUS_OFFLINE`) and its job
  `JOB_STATUS_ERROR | JOB_STATUS_OFFLINE`; the spooler tries such jobs
  again every 30 seconds. A printer that refuses a job (a format it does
  not take) leaves it in error.
- **PWG Raster from the PDF.** gdi32's driver writes each page as one RGB
  image (Flate), so the spooler reads its own PDFs back: each page's
  MediaBox and image, inflated with stb_image's zlib decoder (public
  domain/MIT, `third_party/stb`, already in the tree). Lines are encoded
  as PWG Raster's runs (a repeated pixel or up to 128 literal pixels) with
  a repeat count for equal lines, so a mostly white page is small. Only
  NovaOS's own PDFs (its producer string) are turned into raster; a PDF
  from elsewhere goes to a PDF printer only.
- **Settings > Printers** (`kernel/apps/settings.c`) lists the printers
  with their state (Ready, Paused, Offline, Setting up) and location, sets
  the default (`HKCU ...\Windows` `Device`), removes a printer (its key)
  and adds a network printer by its IP address, host name or URL: the
  printer's key on the IPP port with the IPP class driver and a
  `Pending Setup` value. The spooler sees that value within two seconds,
  asks the printer for its attributes, renames the key after
  `printer-make-and-model` ("... (2)" when taken), records
  `printer-location` and keeps the default pointing at it, as Windows' Add
  a printer names a printer after its model.
- **The test**: `spooltest ipp http://10.0.2.2:18631` in the network
  suite, against `tools/ippprinter.py` on the CI host (two fake IPP
  Everywhere printers that hand back what they printed): a printer added
  with `AddPrinter` gets the PDF of two GDI pages byte for byte (the same
  bytes Microsoft Print to PDF writes for the same pages) and a raw job
  unchanged; a printer added the way Settings adds one is renamed after
  its model and, taking only PWG Raster, gets a page whose decoded pixels
  must be exactly what GDI draws; a printer nobody answers for keeps its
  job, offline.

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

1. ~~**The "Done when" check**~~ Done: in the app corpus
   (`tools/appcorpus.py`, `App(printer=True)`), Notepad++ (Windows'
   Notepad is not redistributable) and SumatraPDF print from their Print
   dialog (Ctrl+P; the printer chosen in its list) to the corpus host's
   `tools/ippprinter.py`, whose page must match
   `tests/reference/notepad++-print.png` / `sumatrapdf-print.png`, and to
   Microsoft Print to PDF through its Save As box; `spooltest same` checks
   the saved PDF is byte for byte what the IPP printer got. Step 33.5 is
   done.
2. **Finding printers**: DNS-SD (`_ipp._tcp`, `_ipps._tcp` over mDNS) and
   WS-Discovery, so Settings lists the printers on the network instead of
   asking for an address; the queue in Settings (pause, cancel) and a test
   page; Apple's URF raster for AirPrint-only printers; IPP job state
   (`Get-Job-Attributes`) shown in the queue.
3. **Later**: EMF spooling and a PDF with real text (selectable, smaller);
   real user-mode vendor drivers (v4 / XPS print path) under the Windows
   driver model rules ([windows-drivers.md](windows-drivers.md)); print
   processors; WIA scanning.
