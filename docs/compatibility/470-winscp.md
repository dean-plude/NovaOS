program: **WinSCP 6.3.5** (App Store; 32-bit, C++Builder VCL, Inno Setup installer)
kind: program
rating: Bronze
works: Installs from the App Store (its Inno Setup installer, silently, registering its 64-bit shell extension through `regsvr32`) and opens its Login dialog over its main window, whose two panels open on local folders ([history](history/2026-10-14-winscp.md))
problems: The Login dialog's site tree and the file panels list their items but draw none of them, and the port number box is blank; no connection or transfer checked
checked: nightly corpus ([`952-winscp.py`](../tests/appcorpus/952-winscp.py))
