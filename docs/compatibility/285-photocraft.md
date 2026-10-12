program: **PhotoCraft 0.7.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Bronze
works: Installs from the App Store and opens its editor window on the built-in OpenGL (llvmpipe)
problems: Its image compositor falls back to the CPU; editing not checked. The other eleven ArtCraft apps and the ArtCraft Launcher (App Store) share its toolkit and are untested
checked: nightly corpus ([`886-photocraft.py`](../tests/appcorpus/886-photocraft.py))
