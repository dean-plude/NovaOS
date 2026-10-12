## A late present no longer puts a stale frame over DirectComposition's GDI frame

A CI flake in `dcomptest direct`: after a swap chain of another device
joined the tree, dcomp.dll went back to the GDI way and told the kernel
(`NtNovaGpuCtl` op 15) to stop showing its last frame from the host.  The
program's own checks passed (the window's bitmap was yellow) but the
harness's screenshot showed the quadrants of the last frame from the host,
for the whole 7 seconds.  Three times in a dozen graphics runs.

- **Cause**: DXVK finishes a present on its queue thread, after `Present`
  has returned, so a present dcomp.dll made just before stopping could
  reach the kernel just after it.  `UmGuiGpuPresent` found the window alone
  again and put that older frame back on the screen; from then on
  `gui_damage` laid it over every redraw of the desktop, and nothing drew
  over it.
- **Fix**: stopping now also refuses direct presentation for the window
  (`GuiWin.ovl_off`: such a present goes the GDI way, and the next frame
  drawn over it corrects the window) until the program says otherwise with
  the new `NtNovaGpuCtl` op 16, which dcomp.dll sends right before it
  presents a composited frame again.
