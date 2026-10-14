program: **LibreOffice 24.8** (App Store; 64-bit, Windows Installer package)
kind: program
rating: Silver
works: Installs silently from the App Store's download (`msiexec /qn`, its DLL custom actions run out of process); Writer opens a `.docx`, takes typing and saves it as Word, and Calc opens an `.xlsx`, takes a formula and saves it as Excel; the saved files keep the edits ([history](history/2026-10-14-libreoffice.md))
problems: No Insert > OLE Object dialog (oledlg answers Cancel); no font subsetting for PDF export (whole fonts are embedded); Skia falls back from Vulkan to its CPU renderer; Impress, Draw and Base not checked
checked: nightly corpus ([`953-libreoffice.py`](../tests/appcorpus/953-libreoffice.py))
