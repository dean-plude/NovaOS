program: **LightCraft 0.6.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Borked
works: Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device, compiles its shaders (shader model 5.1 sampler arrays, since 2026-10-13) and opens its window, which Alt+F4 closes
problems: Its window stays black (nothing drawn, after 10 minutes under QEMU without KVM; VKD3D-Proton logs "pending command lists" on wgpu's command allocator resets); without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own; its GPU preview's compute shaders compile since 2026-10-13 (RWByteAddressBuffer's Load, GetDimensions and Interlocked methods; 26 of its 27 kernels, checked outside the app), except the denoise network's convolution, which stores to an indexed groupshared array ("Store to indexed TGSM", not yet in vkd3d-shader 1.19)
checked: tools/appcorpus.py run in QEMU without KVM (2026-10-13)
