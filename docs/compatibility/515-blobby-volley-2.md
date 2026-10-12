program: **Blobby Volley 2 1.1.1** (App Store; free and open source, GPL; 32-bit, SDL2)
kind: game
rating: Gold
works: Installs from the App Store (the official zip) and plays in an 800x600 window. Its Fullscreen option is a real display-mode switch: SDL changes the display to 800x600 (`ChangeDisplaySettingsEx`) and draws with Direct3D 9 in exclusive full screen through DXVK on Mesa 3D's Vulkan (install "Mesa 3D" and "DXVK" first; without them it draws with SDL's software renderer); the display goes back to its own mode when the game ends
problems: Full screen needs "Mesa 3D" and "DXVK" from the App Store first (without them it draws with SDL's software renderer)
checked: nightly corpus ([`935-blobby-volley.py`](../tests/appcorpus/935-blobby-volley.py))
