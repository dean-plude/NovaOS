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
  frame on the host straight into a texture whose guest memory is the
  primary's screen (the VGA framebuffer, both pages when it flips, or a
  virtio output's picture), in the frame's own format so that the copy
  is a plain one, and a transfer writes that part into it.  The host
  draws most of a frame, so it copies each frame once; the window keeps
  the program's last shown buffer (it draws the next frame into the
  other one) to write it there again.  (Showing a texture as the
  output itself would save that write, but QEMU then has no picture to
  take a screenshot of.)  It says yes only for a window on the primary at
  scale 1 with no window, shadow, popup, menu, the dock or the pointer
  over that part.  The window's bitmap keeps its last GDI frame, so each
  time the desktop draws over that part of the screen that frame is
  written there again; once something covers it, the program gets
  `WM_PAINT` and presents through the GDI until the window is clear
  again.  `NtNovaGpuCtl` 14 reports the bytes moved to show frames.
- **gltest present** (graphics suite, 64- and 32-bit): four coloured
  quadrants are shown from the host while the window is alone, and the
  screen itself shows them there the right way up; they go through the
  GDI while another window covers part of it, and from the host again
  once it has gone.
- **gltest fps** also runs virgl with `NOVA_PRESENT=gdi`, the two ways
  in turn (half the time each, twice, so that whatever else the host
  does weighs on both alike): direct presentation must draw more frames
  per second and move fewer bytes through guest memory a frame
  (read-backs, window bitmaps drawn, the desktop's back buffer copied to
  the screen, transfers to a virtio output).  In the QEMU VM without
  KVM: 23.6 against 18.3 frames a second (42.4 against 54.6 ms a frame),
  20 KiB against 3561 KiB a frame through guest memory.  A first version
  copied each frame on the host twice (into the window's own texture,
  then onto the screen) and was slower than the GDI under KVM, where the
  guest's copies cost little and the host's time is all.
- Vulkan's frames (Venus: DXVK and VKD3D-Proton) still come back through
  the GDI; they are the next step.
