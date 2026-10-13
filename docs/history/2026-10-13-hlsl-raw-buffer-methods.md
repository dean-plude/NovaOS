## RWByteAddressBuffer methods in the HLSL compiler

LightCraft (ArtCraft's photo developer) runs its GPU preview as wgpu
compute shaders.  wgpu's naga writes every WGSL storage buffer as a
`RWByteAddressBuffer` and reads it with `Load`/`Load2`/`Load3`/`Load4`,
asks its size with `GetDimensions` and does atomics with its
`Interlocked*()` methods, but vkd3d-shader 1.19 (and upstream as of October
2026) knows only its `Store` methods, so every such shader stopped at
"Method 'Load' is not defined on type 'RWByteAddressBuffer'".

- **`third_party/vkd3d-shader`** (listed in its `NOVA-VENDOR.txt`):
  `RWByteAddressBuffer.Load`, `Load2`, `Load3` and `Load4` (`ld_raw` on the
  UAV), `GetDimensions` on raw and typed buffers, read or write (`bufinfo`,
  shader model 5.0 and later), the `Interlocked*()` methods with a byte
  offset (`atomic_iadd`, `imm_atomic_cmp_exch` and the rest), and
  `GetDimensions` on `RWTexture*` (as upstream master has it).  Every shader
  of vkd3d 1.19's own HLSL tests compiles as before (7113 compiles), except
  one `Buffer.GetDimensions` test that used to stop and now compiles.
  LightCraft's 27 compute kernels, taken from its program and turned into
  HLSL with naga 30, went from 3 compiling to 26; the last, its denoise
  network's convolution, stores to an indexed `groupshared` array, which
  vkd3d-shader 1.19 does not do yet.
- **`tools/hlsltest`**: a `cs_5_0` shader with those methods, checked in
  its disassembly and dispatched on Direct3D 11 (DXVK) with the buffer read
  back.

LightCraft's window still stays black, like the other six Direct3D 12
ArtCraft apps (a separate step).
