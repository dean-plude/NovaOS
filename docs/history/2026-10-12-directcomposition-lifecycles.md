## DirectComposition on the GPU through resizes, teardown and other processes

The last part of WebView2's GPU-composited output (roadmap 25.1): the path
Chromium takes on Venus now holds up through what a browser does to it
after the first frame, in 64-bit and 32-bit programs alike.

- **Resizes**: a composition swap chain's `ResizeBuffers` keeps the last
  frame on the screen until the next present (a `Commit` in between used
  to draw the new, empty buffers), and refuses no size, as a composition
  swap chain has no window to take one from; a virtual surface's `Resize`
  keeps the pixels both sizes cover (it used to start empty), and it
  answers `IDCompositionVirtualSurface`.
- **Teardown while frames are presented**: a swap chain's present calls
  its visual with the chain's lock held, and a visual let go of its swap
  chain waits for such a present to finish, so a visual released on one
  thread while another presents no longer leaves that present drawing a
  freed visual; dcomp.dll sets and takes back that link with its own lock
  not held (no deadlock with a present), and a visual taking back its link
  leaves the link another visual made to the same swap chain alone.
- **Windows of another process**: a top-level window resized by its own
  program (the GPU process's window embedded in the browser's) now tells
  the desktop its new client area, so the browser's `GetClientRect` and
  its DCs on that window no longer stop at the old size.
- **dcomptest life** (graphics suite, DXVK on Venus, 64- and 32-bit): the
  resizes above; a target, its visual, the DirectComposition device, the
  Direct3D device and the window released while another thread presents;
  and the GPU process's window composited on the GPU inside the browser's
  window, seen from the browser's process, resized by the browser, and the GPU process
  killed while it presents (its window goes, the browser's draws again).
