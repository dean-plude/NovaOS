program: **ShareX 19.0.1** (App Store; portable, Windows Forms on its own .NET 9)
kind: program
rating: Bronze
works: Installs from the App Store (its portable zip, which carries its own .NET 9), opens its main window, and Print Screen (ShareX's default hot key) captures the whole screen to a PNG in its `Screenshots` folder, with the notification that follows ([history](history/2026-10-15-sharex.md))
problems: Region capture, screen recording, uploads and the image editor not checked yet; the taskbar shows no progress or overlay icons (the TaskbarList calls succeed and do nothing); the first start takes minutes without KVM
checked: nightly corpus ([`960-sharex.py`](../tests/appcorpus/960-sharex.py))
