- The WebView2 runtime (Roblox's login page and many other programs show
  web content with it): ~~Edge Update's Windows APIs~~ Done
  (`edgeupdtest`); ~~MSXML 6~~ Done (`msxmltest`); ~~rpcrt4's NDR engine
  for COM proxy/stub DLLs~~ Done (`ndrtest`); ~~COM calls between
  processes~~ Done (`comoop`); ~~Edge Update's check of Microsoft's
  signature on the runtime's package~~ Done (`authtest`); ~~Edge Update
  seeing its own install running~~ Done (`proclisttest`); ~~delete on
  close~~ Done (`filetest`); ~~the runtime's setup unpacking its archive,
  `wer.dll`~~ Done (`wvsetuptest`); ~~the setup's permissions on its
  install folder~~ Done (`acltest`); ~~starting the runtime's browser
  process~~ Done (`wvstarttest`, `unwindtest`); ~~the browser keeping
  its host's connection (pipe process ids)~~ Done (`pipetest`); ~~windows
  of one process inside another's (the host's `SetParent` and
  `SetWindowPos` on the browser's window, another process drawing into
  it; the controller, a page and a script)~~ Done (`xpwin`,
  `chrometest`); ~~drawing the page without a GPU (the Basic Render
  Driver adapter, a WARP device, DirectComposition, the GPU process's
  window inside the browser's)~~ Done (`dcomptest`); ~~Windows'
  `WOW6432Node` registry view for machine-wide installs~~ Done (`regtest`).
  ~~WebView2 with DXVK installed and Venus the GPU (the GPU process keeps
  ANGLE's Direct3D 11 device on Venus and presents through the WARP device
  and DirectComposition instead of restarting until the browser crashed)~~
  Done (`dcomptest dxvk`).  Still to do: the GPU-composited output path
  (DirectComposition on the GPU): ~~composition swap chains on the GPU
  device~~ Done (`dcomptest dxvk`); ~~`DCompositionCreateDevice3`
  (`IDCompositionDesktopDevice`, `IDCompositionDevice3`, surfaces, clips),
  the DXVK adapter's factory making the swap chains, `IDXGISwapChain3`, so
  Chromium turns the path on~~ Done (`dcomptest dxvk`; WebView2's GPU
  process composites the page through a swap chain on Venus); ~~that
  path through resizes, teardown while frames are presented, and the GPU
  process's window inside the browser's, 64- and 32-bit~~ Done
  (`dcomptest life`).  Next: presenting without the CPU readback and GDI
  copy (direct virtio GPU scan-out): done for OpenGL's frames (direct
  presentation), still to do for Venus's Vulkan frames, which WebView2's
  are.
