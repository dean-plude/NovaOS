program: **DeckCraft 0.5.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Borked
works: Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device, compiles its shaders (shader model 5.1 sampler arrays, since 2026-10-13) and opens its window, which Alt+F4 closes
problems: Its window stays black (nothing drawn, after 10 minutes under QEMU without KVM); without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own
checked: tools/appcorpus.py run in QEMU without KVM (2026-10-13)
