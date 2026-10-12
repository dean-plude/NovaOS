program: **VectorCraft 0.9.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Bronze
works: Installs from the App Store and opens full screen on its welcome page, drawn with the built-in OpenGL (llvmpipe)
problems: Drawing and saving not checked; the desktop's title bar sits above the app's own (it draws its own buttons too)
checked: nightly corpus ([`887-vectorcraft.py`](../tests/appcorpus/887-vectorcraft.py))
