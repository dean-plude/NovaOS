program: **PhotoCraft 0.7.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Silver
works: Installs from the App Store, opens its editor window on the built-in OpenGL (llvmpipe) and makes a new document
problems: On software graphics (llvmpipe) it composites on the CPU by its own design and says so in a box at start; its GPU compositor ran on NovaOS's OpenGL in a test, but no hardware adapter has been tried. Editing not checked. The other eleven ArtCraft apps and the ArtCraft Launcher (App Store) share its toolkit and are untested
checked: nightly corpus ([`886-photocraft.py`](../tests/appcorpus/886-photocraft.py))
