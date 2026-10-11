## Direct presentation on the virtio GPU

Roadmap step 25.2.  An OpenGL program on Mesa's virgl drew its frames on
the host's GPU, and then every frame came back the long way: read back
into guest memory, copied into Mesa's display target, drawn into the
window's bitmap through the GDI, drawn by the desktop into its back
buffer and copied to the screen.  Now, while nothing else is drawn over
the window, the host's GPU copies the frame and the host writes it
straight into the screen's memory: no pixel is copied by the guest.

- **virgl's winsys** (`third_party/mesa-venus/virgl_nova_winsys.c`): a
  present asks the kernel first (`NtNovaGpuCtl` 13, the window and where
  it is on screen), keeping at most one frame ahead of the host; when
  the kernel says no it reads the frame back as before.
  `NOVA_PRESENT=gdi` always takes the old way.
- **Kernel**: a virgl context of the kernel's own copies the program's
  frame on the host into the window's overlay texture and from there
  into a texture whose guest memory is the primary's screen (the VGA
  framebuffer, both pages when it flips, or a virtio output's picture),
  and a transfer writes that part into it.  (Showing a texture as the
  output itself would save that write, but QEMU then has no picture to
  take a screenshot of.)  It says yes only for a window on the primary at
  scale 1 with no window, shadow, popup, menu, the dock or the pointer
  over that part.  The window's bitmap keeps its last GDI frame, so each
  time the desktop draws over that part of the screen the overlay is
  written there again; once something covers it, the program gets
  `WM_PAINT` and presents through the GDI until the window is clear
  again.  `NtNovaGpuCtl` 14 reports the bytes moved to show frames.
- **gltest present** (graphics suite, 64- and 32-bit): four coloured
  quadrants are shown from the host while the window is alone, and the
  screen itself shows them there the right way up; they go through the
  GDI while another window covers part of it, and from the host again
  once it has gone.
- **gltest fps** also runs virgl with `NOVA_PRESENT=gdi`: direct
  presentation must draw more frames per second and move fewer bytes
  through guest memory a frame (read-backs, window bitmaps drawn, the
  desktop's back buffer copied to the screen, transfers to a virtio
  output).  In the QEMU VM without KVM: 20.5
  against 18.7 frames a second (48.7 against 53.6 ms a frame), 12 KiB
  against 3549 KiB a frame through guest memory.
- Vulkan's frames (Venus: DXVK and VKD3D-Proton) still come back through
  the GDI; they are the next step.
