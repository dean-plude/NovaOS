program: **Microsoft Edge WebView2 runtime** (the evergreen offline installer)
kind: runtime
rating: Silver
works: Its installer, Microsoft Edge Update, installs itself, accepts Microsoft's signature on the runtime's package and starts the runtime's own setup, which installs the runtime; a WebView2 host starts the runtime's browser process (`msedgewebview2.exe`) and its GPU, network and storage processes and gets a WebView2 environment; the host puts the browser's window inside its own, a page loads, a script runs in it and the page is drawn (Chromium's software compositor, through the Basic Render Driver, a WARP device and DirectComposition), also with DXVK installed and Venus the GPU, where the GPU process keeps ANGLE's Direct3D 11 device on the GPU and composites the page through DirectComposition on it ([WebView2](#webview2))
problems: Programs that host it (Roblox's and Steam's sign-in) not checked end to end
checked: nightly corpus (the installer, `wv2host`, [`095-webview2.py`](../tests/appcorpus/095-webview2.py))
