## The ArtCraft apps draw their windows (HLSL const copies)

After the shader model 5.1 descriptor arrays, the seven Direct3D 12 ArtCraft
apps opened their windows but the windows stayed black.  wgpu's naga writes
every `let` in a shader as a const local, and egui's vertex shader ends with
`const VertexOutput vertexoutput = _e9; return vertexoutput;`.  vkd3d-shader
1.19 treated any const local initialized from a load as a compile-time
constant and moved its initializer into the static initializers, which run
before the function body, so the shader returned registers nothing had
written.  Its triangles landed anywhere, lavapipe's rasterizer threads never
finished, and no frame was ever presented.

- **`third_party/vkd3d-shader`** (its second NovaOS change, listed in
  `NOVA-VENDOR.txt`, taken from upstream's later fix): a const local goes to
  the static initializers only when its initializer is a compile-time
  constant (`is_compile_time_const`): literals, operators, and loads of
  variables the static initializers set themselves.  Every shader of vkd3d
  1.19's own HLSL tests compiles as before.
- **`tools/hlsltest`**: a vertex shader that returns a const copy of a local
  must still read both of its inputs (`vs_5_0` and `vs_5_1`).
- **`kernel/CMakeLists.txt`**: the userland image is rebuilt when
  `third_party/vkd3d-shader` changes, so `d3dcompiler_47.dll` is not left
  stale by an incremental build.

All seven apps (FilmCraft, LightCraft, DesignCraft, SoundCraft, DeckCraft,
CADCraft and GridCraft) now show their windows with the App Store's Mesa 3D,
DXVK and VKD3D-Proton, about two and a half minutes after start without KVM,
and are rated Bronze; the nightly corpus starts each one
(`tests/appcorpus/891-*.py`) and checks that its window is not one flat
colour.  LightCraft's denoise network still needs stores to indexed groupshared
arrays (docs/compatibility).
