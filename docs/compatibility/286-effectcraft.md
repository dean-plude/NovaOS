program: **EffectCraft 0.8.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Bronze
works: Installs from the App Store and opens full screen on its welcome page with an untitled project, drawn with the built-in OpenGL (llvmpipe)
problems: Compositions and rendering not checked; the desktop's title bar sits above the app's own (it draws its own buttons too)
checked: nightly corpus ([`888-effectcraft.py`](../tests/appcorpus/888-effectcraft.py))
