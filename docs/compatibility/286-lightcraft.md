program: **LightCraft 0.6.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Bronze
works: Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens its photo library with a sample photo, histogram and Light sliders, about two and a half minutes after start under QEMU without KVM
problems: Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own; its GPU preview's compute shaders compile since 2026-10-13 (RWByteAddressBuffer's Load, GetDimensions and Interlocked methods; 26 of its 27 kernels, checked outside the app), except the denoise network's convolution, which stores to an indexed groupshared array ("Store to indexed TGSM", not yet in vkd3d-shader 1.19)
checked: nightly corpus ([`891-lightcraft.py`](../tests/appcorpus/891-lightcraft.py))
