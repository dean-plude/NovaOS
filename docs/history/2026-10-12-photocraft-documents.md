## PhotoCraft makes documents; why it composites on the CPU

PhotoCraft (App Store, ArtCraft) closed itself the moment a document
opened: over the canvas it sets a custom brush cursor, whose bitmap it
writes with `SetBitmapBits`, and `gdi32` had no such export, so the
program ended with "entry point not found".  `gdi32` now has
`SetBitmapBits`, and `GetBitmapBits` returns the bits as Windows does
(blue, green, red, then alpha, rows top-down) instead of NovaOS's own
pixel order.

Its "Switched to CPU rendering" box is PhotoCraft's own rule, not a
failure on NovaOS: when the graphics adapter wgpu gives it is a CPU one
(llvmpipe and lavapipe, as Windows' Basic Render Driver), PhotoCraft
never starts its GPU image compositor and says it uses the CPU.  A test
build that gave llvmpipe another name in `GL_RENDERER` showed the GPU
compositor running on NovaOS's OpenGL (the start page without the box,
then a 1920x1080 document on the GPU canvas), so on a machine with a
hardware adapter, such as QEMU's virgl or Venus on a host GPU, it should
start on the GPU; that has not been checked yet.

- **PhotoCraft** (app corpus, `886-photocraft.py`): it keeps the CPU,
  makes a new 1920x1080 document and must show its white canvas
  (`tests/reference/photocraft.png`).
