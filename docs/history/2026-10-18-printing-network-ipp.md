## Printing: network printers (IPP) and Settings > Printers

Second part of roadmap step 33.5 ([design](design/printing.md)).
NovaOS printed only to PDF files; now it prints to the printers people
have at home and at work, without a driver, the way Windows 11 does with
its "Microsoft IPP Class Driver":

- **IPP ports in the Print Spooler.** A printer whose port is an
  `ipp://`, `ipps://`, `http://` or `https://` address is an IPP printer.
  The spooler asks it for its attributes (`Get-Printer-Attributes`) and
  sends each job with `Print-Job` over WinHTTP: a PDF as it is when the
  printer takes PDF, otherwise PWG Raster made from the PDF's pages at a
  resolution the printer has (sRGB, or grey for a grey-only printer); raw
  jobs go as they are. A printer that does not answer keeps its job,
  shown as offline, and the spooler tries again every 30 seconds.
- **Settings > Printers** lists the printers with their state and
  location, sets the default, removes printers and adds a network printer
  by its address. The spooler then names the new printer after its make
  and model, as Windows' "Add a printer" does.
- **Tests:** `tools/ippprinter.py`, a fake IPP Everywhere printer on the
  CI host, takes jobs in the network suite and hands back what it got:
  `spooltest ipp` checks that the PDF arrives byte for byte, a raw job as
  it is, a page sent as PWG Raster exactly as GDI drew it, the naming of a
  printer added the way Settings adds one, and an unreachable printer
  holding its job.

Next: the step's "Done when": Notepad and SumatraPDF printing to the PDF
printer and to the IPP printer in the app corpus.
