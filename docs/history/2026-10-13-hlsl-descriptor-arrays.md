## Shader model 5.1 descriptor arrays in the HLSL compiler

Seven of the ArtCraft apps (FilmCraft, LightCraft, DesignCraft, SoundCraft,
DeckCraft, CADCraft and GridCraft) draw only with Direct3D 12, through
wgpu, which compiles its shaders with `D3DCompile` for shader model 5.1.
wgpu's naga gives every shader one heap of samplers,
`SamplerState nagaSamplerHeap[2048] : register(s0, space0)`, and picks each
sampler through a `StructuredBuffer<uint>` of indices.  vkd3d-shader 1.19
(and upstream as of October 2026) stopped at "Shader model 5.1+ resource
array", so with the App Store's Mesa 3D, DXVK and VKD3D-Proton installed the
apps still quit before their window.

- **`third_party/vkd3d-shader`** (NovaOS's first change to it, listed in its
  `NOVA-VENDOR.txt`): a one-dimensional uniform array of samplers, textures
  or UAVs in shader model 5.1 is one descriptor range
  (`dcl_sampler s0[0:2047], space=0`) and may be indexed by any expression,
  which goes in the register's relative address (`s0[r0.x + 0]`), as FXC
  writes it.  Every shader of vkd3d 1.19's own HLSL tests compiles as
  before, except 18 shader model 5.1 ones that used to stop at the same
  message and now compile.
- **`tools/hlsltest`**: a naga-style `ps_5_1` shader: one sampler range
  indexed at run time, a texture array indexed by a constant buffer value,
  and the heap's 2048 bindings in `D3DReflect`.

The seven apps now get past their shaders and open their windows (Alt+F4
closes them), but the windows stay black, and LightCraft's GPU preview
needs `RWByteAddressBuffer.Load`, which vkd3d-shader lacks too: both next
steps (docs/compatibility).
