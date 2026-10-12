- ~~**A GPU path in QEMU**~~ Done (Phase 19.7): the virtio GPU driver does
  3D, and Mesa's Venus (the App Store's "Venus") runs Vulkan, and Direct3D
  through DXVK, on the host's GPU through QEMU's `virtio-vga-gl,venus=on`;
  `d3dtest` passes on it and draws faster than on lavapipe, and ANGLE's
  Direct3D 11 back end (Chromium's GPU process) starts on it
  (`d3dtest angle`; [Chromium's GPU process](HISTORY.md#chromiums-gpu-process-on-direct3d-11)).
  ~~virgl (OpenGL on the host's GPU)~~ Done: the same App Store entry
  brings Mesa's virgl, which NovaOS's own `opengl32.dll` picks on that
  GPU; `gltest` passes on it and `gltest fps` draws faster than on
  llvmpipe; see [virgl](HISTORY.md#opengl-on-the-hosts-gpu-virgl).
  ~~Showing OpenGL's frames on the virtio GPU directly~~ Done (step
  25.2): virgl's frames are written into the screen by the host while the
  window is alone there, instead of being read back and drawn through the
  GDI (`gltest present`, and `gltest fps` against the GDI way); see
  [direct presentation](HISTORY.md#direct-presentation-on-the-virtio-gpu).
  ~~The same for Vulkan's frames (Venus, DXVK, VKD3D-Proton)~~ Done:
  a swapchain's frames are copied by the host from Venus's memory into
  the screen while the window is alone (`d3dtest present`, and
  `d3dtest fps` against the GDI way); see
  [Vulkan's frames](HISTORY.md#direct-presentation-of-vulkans-frames).
  Still to do: windows on the other monitors and at scale 2.
