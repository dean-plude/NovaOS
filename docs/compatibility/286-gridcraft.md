program: **GridCraft 0.5.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Bronze
works: Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens an empty workbook under its ribbon, about two and a half minutes after start under QEMU without KVM
problems: Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own
checked: nightly corpus ([`891-gridcraft.py`](../tests/appcorpus/891-gridcraft.py))
