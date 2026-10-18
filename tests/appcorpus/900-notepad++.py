# Notepad++ takes the keyboard (windowed: runs after the console programs).
# It opens a file; its screenshot must match tests/reference/notepad++.png
# (tools/appcorpus.py).  Then it prints the file from its Print dialog to a
# network printer and to Microsoft Print to PDF (Phase 33.5: print_check)
DOC = 'Notepad++ (prints a page to a network printer and to Microsoft Print to PDF)'
APP = App('Notepad++', '8.8.3',
          'https://github.com/notepad-plus-plus/notepad-plus-plus/releases/download/v8.8.3/npp.8.8.3.portable.x64.zip',
          'npp', [Test('add a printer', rf'spooltest.exe add "{IPP_PRINTER}" {IPP_BASE}/ipp/print', timeout=60),
                  Test('open a file', rf'start {A}\npp\notepad++.exe {A}\data\hello.txt', timeout=60),
                  Test('print it', rf'spooltest.exe same C:\Temp\notepad++.pdf {IPP_BASE}', timeout=120)],
          gui=True, printer=True)
