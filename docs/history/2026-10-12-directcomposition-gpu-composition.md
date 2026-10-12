## DirectComposition composited on the GPU

The Phase 25 step after [the other monitors](#direct-presentation-on-every-monitor-and-at-scale-2).
WebView2's GPU process renders its page on Venus into a DirectComposition
swap chain, but every present then copied the changed part into a staging
texture, read it back into guest memory and drew it into the window with
the GDI.  Now `dcomp.dll` composites such a target on the GPU, and the
frame never comes back to the guest:

- **GPU composition**: a target whose visible tree shows only swap chains
  of one GPU device (BGRA or RGBA ones) gets a picture of its window's
  size on that device and a swap chain of the device's own on the window
  (made by the device adapter's factory, DXVK's).  Each present copies
  the part that changed into the picture on the GPU, a Commit after a
  change to the tree copies all of it, and the picture is copied into the
  window's swap chain and presented.  DXVK's frame goes through Mesa's
  Win32 swapchain, so the kernel writes it straight into the screen while
  the window is alone ([Vulkan's frames](#direct-presentation-of-vulkans-frames)),
  and Mesa draws it in with the GDI while something covers it.  Parts of
  the window no visual covers show black.
- **Read-back only when asked for**: `dxgi.dll`'s GPU composition swap
  chains no longer read each present back; the parts presented since are
  read back when `dcomp.dll` asks for the frame (the GDI way).
- **Back to the GDI way**: once the tree no longer qualifies (a
  DirectComposition surface, a software swap chain, a second device), the
  target is drawn with the GDI again and tells the kernel
  (`NtNovaGpuCtl` 15), which stops showing the window's last frame from
  the host and redraws the window's own pixels.  `NOVA_PRESENT=gdi` keeps
  every target on the GDI way.
- **Mesa's GDI way for RGBA swapchains**: DXVK picks an RGBA Vulkan
  swapchain for an 8-bit DXGI one, and Mesa's Win32 swapchain copied its
  frames into a BGRA bitmap as they were, red and blue swapped; it now
  swaps them back (the frames shown from the host were already right).
- **Embedded windows**: the kernel now shows direct frames for a window
  embedded in another process's (the window WebView2's GPU process
  composites into, inside the host's), by the part of it that shows.
- **dcomptest direct** (graphics suite, 64- and 32-bit): the quadrants
  from the host while the window is alone (a screenshot of the screen
  must show them), through the GDI while another window covers it, from
  the host again, then the GDI way once a swap chain of another device
  joins the tree (the screen must show the frame drawn that way), and the
  GPU process's embedded window shown from the host.  `dcomptest dxvk`
  and `dcomptest life` keep the GDI way, since they check frames with
  `GetPixel`.
- Not covered: DirectComposition surfaces still go the GDI way (each
  `EndDraw` reads back), and WebView2 itself was not rerun for this step.
