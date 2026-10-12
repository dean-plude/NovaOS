## The other ArtCraft apps tried on NovaOS

Each of the eleven ArtCraft apps besides PhotoCraft, and the ArtCraft
Launcher, was installed from the App Store and started once.  None needs
a function NovaOS lacks (`tools/pe_imports.py`), and they split by the
graphics they ask wgpu for:

- **VectorCraft, EffectCraft, PdfCraft, WordCraft and the ArtCraft
  Launcher** draw with OpenGL and open their windows on NovaOS's built-in
  llvmpipe: welcome pages, WordCraft's blank document, the launcher's list
  of apps (shown offline without the internet).  The first four join the
  app corpus (`887-vectorcraft.py`, `888-effectcraft.py`,
  `889-pdfcraft.py`, `889-wordcraft.py`).
- **FilmCraft, LightCraft, DesignCraft, SoundCraft, DeckCraft, CADCraft
  and GridCraft** ask only for Direct3D 12 on Windows (FilmCraft also
  Vulkan), where every PC has a Direct3D 12 adapter, Microsoft's software
  one at least.  On NovaOS that adapter comes from "Mesa 3D", "DXVK" and
  "VKD3D-Proton" in the App Store; without them the apps exit with "No
  suitable graphics adapter found".  With them they get a Direct3D 12
  device, then stop: wgpu compiles their shaders with `D3DCompile`, and
  NovaOS's `d3dcompiler_47` (vkd3d-shader 1.19) cannot yet compile the
  shader model 5.1 sampler arrays wgpu's HLSL uses.  That compiler feature
  is the next fix for all seven.

Their ratings are in [compatibility.md](../compatibility.md).
