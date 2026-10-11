## Mesa's OpenGL built in

OpenGL 2 and later used to need the App Store's **Mesa 3D** first: without
it NovaOS's `opengl32.dll` had only its own OpenGL 1.1, so Krita (OpenGL 3
for its canvas and Qt Quick), Chocolate Doom, Teeworlds and GOG GALAXY's
Qt all waited on a download.  On Windows a display driver's OpenGL is
simply there, so NovaOS now carries Mesa's llvmpipe on drive C: from the
start.

- `tools/bundle_mesa.py` (called by `tools/build_userland.py`, as Firefox's
  bundler is) downloads the same mesa-dist-win 24.2.4 archive the App
  Store lists, checks its SHA-256, and stages the unmodified 64- and
  32-bit `opengl32.dll` (as `opengl32_mesa.dll`), `libgallium_wgl.dll` and
  `libglapi.dll` into System32 and SysWOW64; CI caches the archive.
  NovaOS's `opengl32.dll` already hands every program to
  `opengl32_mesa.dll` when it is there (virgl first on a 3D virtio-gpu
  with Venus installed), so every program gets OpenGL 4.5 on the CPU.
- `GALLIUM_DRIVER=generic` asks for NovaOS's own OpenGL 1.1 instead (as
  with no driver); `glgeneric` uses it.
- The App Store's **Mesa 3D** now installs only lavapipe (Vulkan, for
  DXVK), and shows as installed by `vulkan_lvp.dll`.
- New core self-test `gl2test` (64- and 32-bit, nothing installed):
  OpenGL 3.0 or later on llvmpipe, a GLSL program compiled, linked and
  drawn with, read back and swapped to the window, and an OpenGL 3.3
  core-profile context from `wglCreateContextAttribsARB`.
- The nightly corpus starts Krita with nothing installed first.

The two architectures' llvmpipe add about 97 MB to drive C:'s system
files (they are unpacked into memory at boot, as the other system files
are).
