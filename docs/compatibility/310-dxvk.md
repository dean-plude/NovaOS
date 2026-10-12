program: **DXVK 2.5.3** (App Store, Runtimes)
kind: runtime
rating: Platinum
works: Direct3D 8 to 11 on Vulkan, 64- and 32-bit; ANGLE's Direct3D 11 back end (Chromium's GPU process: Steam's browser, WebView2, Qt WebEngine) starts on it
problems: Needs Mesa 3D's Vulkan (App Store) first
checked: CI graphics tests (`d3dtest`, `d3dtest angle`, `d3dtest present`: frames shown straight from the host's GPU on Venus)
