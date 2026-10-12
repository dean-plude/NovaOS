## Direct presentation of Vulkan's frames

The Phase 25 step after 25.2.  OpenGL's frames on virgl were already
written into the screen by the host ([direct
presentation](#direct-presentation-on-the-virtio-gpu)); Vulkan's on Venus,
so every Direct3D 9, 10 and 11 program on DXVK and every Direct3D 12 one
on VKD3D-Proton, still came back the long way: Mesa's Win32 swapchain
copied each frame from the GPU's memory into a DIB and drew it into the
window through the GDI, the desktop drew the window's bitmap into its
back buffer and copied that to the screen.  Now, while the window is
alone on screen, the host copies the frame into the screen and the guest
copies no pixel.

- **Why not the OpenGL way**: a Venus context on the host
  (virglrenderer's render-server proxy) has no transfers, and a Venus
  blob must not be attached to a virgl context, so the kernel cannot read
  a Venus frame back into the screen as it does virgl's.  Instead the
  kernel's own virgl context keeps a texture for each swapchain image's
  memory whose guest memory is that blob's place in the virtio GPU's
  host-visible region: a transfer to the host copies the frame from the
  blob into the texture, and from there it goes into the screen as
  virgl's frames do (a copy into the texture over the screen's memory and
  a transfer into the screen's rows).  All three copies are the host's.
  Frames in red-first order (DXVK takes `R8G8B8A8` for a Direct3D 9
  `X8R8G8B8` back buffer) are blitted into the screen's order on the way.
  When the blob goes, the texture gives its memory back first (QEMU waits
  for a region to be free before unmapping it); the window's overlay may
  keep the texture, which keeps the last frame for redraws.
- **Mesa** (`third_party/mesa-venus/novaos.patch`): the Win32 WSI gets a
  `present_memory` hook, which Venus sets; a present offers the frame to
  it (after the copy into it has finished) before copying it through the
  GDI.  `vn_renderer_nova_show` (`vn_renderer_nova.c`) asks the kernel
  (`NtNovaGpuCtl` 13 with the blob, the format, the row pitch and the
  offset: the call grew from 24 to 40 bytes, flagged, so virgl's shorter
  form still works) when the window's client area is the frame's size.
  `NOVA_PRESENT=gdi` takes the old way.
- **d3dtest present** (graphics suite, 64- and 32-bit): Direct3D 9's
  four coloured quadrants on Venus are shown from the host while the
  window is alone (the screen itself must show them there, the right way
  up and in the right colours), through the GDI while another window
  covers part of it, and from the host again once it has gone.
- **d3dtest fps** now runs Venus both ways in turn (direct, GDI, GDI,
  direct), after a turn that is not counted (the host compiles the
  scene's shaders for the first program), and needs direct presentation
  to move fewer bytes through guest memory a frame and, without KVM, to
  be faster (under KVM, a sixteenth of the bytes or less, as `gltest
  fps`).  In the QEMU VM without KVM (lavapipe on the host): 47.9 against
  59.1 ms a frame (20.9 against 16.9 frames a second), 173 KiB against
  2259 KiB a frame through guest memory; another run 61.2 against 66.8 ms,
  162 against 2184 KiB.
- Still to do: windows on the other monitors and at scale 2 (the kernel
  refuses both, so they go through the GDI), and the composition swap
  chains (DirectComposition) that WebView2's GPU process presents into.
