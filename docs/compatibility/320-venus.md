program: **Venus** (App Store, Runtimes)
kind: runtime
rating: Platinum
works: Vulkan, and Direct3D through DXVK, on the host GPU when QEMU gives NovaOS a 3D virtio-gpu
problems: Only in QEMU with a 3D virtio-gpu; real GPUs need their own driver
checked: CI graphics tests
