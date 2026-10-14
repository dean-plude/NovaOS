- **Graphics**: drawing runs on the CPU by default, and on the host's GPU
  under QEMU with a 3D virtio-gpu.
  - **OpenGL**: `opengl32` hands every call to Mesa: llvmpipe (OpenGL 4.5
    on the CPU) is on drive C: from the start, and virgl (the App Store's
    "Venus" package) runs OpenGL on the host GPU. With neither, NovaOS's own
    OpenGL 1.1 draws 2D, as ScummVM needs.
  - **Vulkan and Direct3D**: `vulkan-1` loads Mesa's drivers from the App
    Store: lavapipe on the CPU ("Mesa 3D") or Venus on the host GPU. DXVK
    runs Direct3D 8 to 11 and VKD3D-Proton runs Direct3D 12 on them, with
    `d3d9`, `d3d11` and `d3d12` handing their calls over once installed.
  - **Without them**: `d3d9` lists the adapters with no device, so programs
    take their software paths; `dxgi`, `d3d11` and `dcomp` give Windows'
    Microsoft Basic Render Driver adapter, a CPU device that Chromium's
    software compositor draws WebView2 with; `ddraw` is cnc-ddraw for
    32-bit DirectDraw games.
  - **Shaders**: `d3dcompiler_47` compiles HLSL to Direct3D 9 to 12
    bytecode (up to shader model 5.1) on Wine's vkd3d-shader, for ANGLE,
    wgpu and games, with `D3DReflect` and `D3DDisassemble`.
  - **virtio-gpu** (`kernel/drivers/virtio_gpu.c`): QEMU's virtio-vga and
    virtio-gpu-pci with several outputs plugged in and out at run time; on
    `virtio-vga-gl` it gives Venus and virgl their 3D contexts, blobs and
    fences, and a window alone on its monitor has its OpenGL or Vulkan
    frames written to the screen by the host (direct presentation).
  - Real GPUs: a display-only WDDM host runs Windows display drivers for
    the screen alone; 3D on Intel's GPU through Intel's own Windows driver
    is planned ([design/intel-gpu.md](design/intel-gpu.md)).
