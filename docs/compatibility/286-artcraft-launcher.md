program: **ArtCraft Launcher 0.2.1** (ArtCraft; Rust, egui on wgpu)
kind: tool
rating: Bronze
works: Installs from the App Store and opens its window on the built-in OpenGL (llvmpipe), listing the twelve ArtCraft apps
problems: Installing and updating apps needs the internet, which the test machine lacks (it shows them as offline); closing the window keeps it running in the tray, its default; the desktop's title bar sits above the app's own (it draws its own buttons too)
checked: by hand in QEMU (2026-10-12)
