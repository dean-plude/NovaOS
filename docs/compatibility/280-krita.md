program: **Krita 5.3.4** (App Store; Qt 5, portable zip)
kind: program
rating: Silver
works: Installs from the App Store (the Store unpacks its zip), opens a new image on the built-in OpenGL (Mesa's llvmpipe; nothing else to install), paints a stroke with the default brush and saves it as a `.kra` through its Save dialog; the saved file holds the document, its layers and the merged image ([history](history/2026-10-14-krita-saves.md))
problems: Under QEMU without KVM it takes about ten minutes from start to a saved image; exporting to other formats (PNG, JPEG) and opening a saved file again not checked
checked: nightly corpus ([`885-krita.py`](../tests/appcorpus/885-krita.py))
