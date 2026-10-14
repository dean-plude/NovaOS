program: **LightCraft 0.6.0** (ArtCraft; Rust, egui on wgpu)
kind: program
rating: Bronze
works: Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens its photo library with a sample photo, histogram and Light sliders, about two and a half minutes after start under QEMU without KVM
problems: Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own; all 27 of its GPU preview's compute shaders compile (RWByteAddressBuffer's Load, GetDimensions and Interlocked methods since 2026-10-13; the denoise network's convolution, which stores to indexed groupshared arrays, since 2026-10-14; checked outside the app, the denoise itself not yet run in it)
checked: nightly corpus ([`891-lightcraft.py`](../tests/appcorpus/891-lightcraft.py))
