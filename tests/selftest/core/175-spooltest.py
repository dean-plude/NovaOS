# Printing (userland/programs/spooltest.c): the print spooler service
# (spoolsv.exe), winspool.drv, the printer DC with the Microsoft Print to PDF
# driver (gdi32's print.c) and the print dialogs (comdlg32's printdlg.c).
# spooltest lists the printers, prints raw jobs to files and to a printer it
# adds, holds a job on a paused printer, answers the PDF printer's Save As,
# prints two GDI pages to a PDF whose first page must be exactly what GDI
# draws, and answers the Print dialog (the screenshot is the reference
# picture, docs/screenshots/print-dialog.png).  64- and 32-bit.
DOC = ('`spooltest` (the print spooler and `winspool.drv`: printers, raw jobs, the queue, AddPrinter; a GDI page '
       'printed to Microsoft Print to PDF matching what GDI draws; PrintDlg and PageSetupDlg; 64- and 32-bit)')

TESTS = [
    Test('spooltest x64', 'spooltest', [r'spooltest: \d+ passed, 0 failed'], timeout=240,
         shot=r'spooltest: print dialog open',
         boot_expect=[r'\[SPOOLER\] Print spooler running'], elevated=True),
    Test('spooltest x86', r'C:\Programs\x86\spooltest.exe', [r'spooltest: \d+ passed, 0 failed'], timeout=240, elevated=True),
]
