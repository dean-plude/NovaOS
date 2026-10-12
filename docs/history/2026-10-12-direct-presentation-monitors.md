## Direct presentation on every monitor and at scale 2

The Phase 25 step after [Vulkan's frames](#direct-presentation-of-vulkans-frames).
Direct presentation (the host writing a program's OpenGL or Vulkan frame
straight into the screen) only worked for a window on the primary monitor
while the desktop drew at scale 1; on a second monitor, or once any
monitor was at scale 2, every frame went the GDI way again.  Now the
kernel shows it on whichever monitor the window is wholly on:

- **Any monitor**: `DisplayHeadScreen` says where a head's pixels are
  (the primary's VGA framebuffer or virtio output as before, a further
  `secondary-vga`'s video memory, or another virtio output's picture), and
  the kernel's virgl context keeps a texture over each screen it writes
  (the last four).  The display's damage hook now reports which head was
  drawn, so the overlays on that screen go back over what the desktop
  drew there, and the pointer only stops a window's direct frames when it
  is over that window on the same screen.  A window across two monitors
  still goes the GDI way.  The dock only covers windows on the primary.
- **Scale 2**: a window's frame is placed in that monitor's own pixels.
  On a monitor at scale 2 a frame of logical pixels (a program that is not
  DPI-aware) is blitted by the host with each pixel twice as wide and
  high, as the GDI shows it; a frame drawn for scale 2 on a monitor at
  scale 1 is blitted at half its size (each 2x2 block averaged, as the GDI
  shows a monitor at half the desktop's scale).  Otherwise frames are
  copied as before.
- **gltest monitors** (graphics suite, 64-bit): the four quadrants on the
  second monitor, again with that monitor in 2560x1600 (scale 2), and on
  the primary beside it; each screen's screenshot must show them where
  the program says, and nine frames in ten must come from the host.
- Still to do: DirectComposition's swap chains (WebView2's GPU process).
