program: **GIMP 2.10.38** (App Store; 64-bit, GTK 2, Inno Setup installer)
kind: program
rating: Bronze
works: Installs from the App Store (its Inno Setup installer, silently) and opens a PNG: on the first start it registers its plug-ins (each one a program it starts and talks to over pipes), then shows the picture in its single-window layout ([history](history/2026-10-14-gimp.md))
problems: The TWAIN plug-in (scanning; a 32-bit program) does not start; one Python plug-in (Spyrogimp) crashed once while registering (a fault in `GetProcAddress` called from `libintl`, not reproduced); no printing, editing or saving checked yet; the first start takes minutes without KVM
checked: nightly corpus ([`957-gimp.py`](../tests/appcorpus/957-gimp.py))
