# Network printing (IPP): spooltest's network half against tools/ippprinter.py
# on the host (10.0.2.2:18631), a fake IPP Everywhere printer that hands back
# what it printed.  A printer added with the IPP class driver gets the
# PDF of two GDI pages byte for byte (the same bytes Microsoft Print to PDF
# writes) and a raw job as it is; a printer added the way Settings >
# Printers adds one is named by the spooler after its make and model, and,
# taking only PWG Raster, gets the page as raster lines that must be exactly
# what GDI draws; a printer that does not answer keeps its job, offline.
DOC = ('`spooltest ipp` (network printing: a fake IPP printer on the host gets the PDF byte for byte, raw jobs, '
       'and PWG Raster pages exactly as GDI draws them; a printer added as Settings adds one is named after its model; '
       'an unreachable printer keeps its job offline)')
TESTS = [
    Test('spooltest IPP', 'spooltest ipp http://10.0.2.2:18631', [r'spooltest: \d+ passed, 0 failed'], timeout=300),
]
