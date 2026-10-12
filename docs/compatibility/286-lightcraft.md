program: **LightCraft 0.6.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Borked
works: Installs from the App Store
problems: Exits at start: on Windows it asks only for Direct3D 12 at first, and NovaOS has no Direct3D 12 adapter until "Mesa 3D", "DXVK" and "VKD3D-Proton" are installed from the App Store; with them it gets a Direct3D 12 device, but NovaOS's shader compiler (`d3dcompiler_47`, vkd3d-shader) cannot yet compile wgpu's shader model 5.1 sampler arrays, so it stops before its window ("Shader model 5.1+ resource array")
checked: by hand in QEMU (2026-10-12)
