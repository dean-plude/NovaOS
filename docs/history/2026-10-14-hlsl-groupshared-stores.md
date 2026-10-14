## Stores to indexed groupshared arrays in the HLSL compiler

LightCraft's denoise network runs as a wgpu compute shader, a convolution
that stages its inputs in workgroup memory.  naga writes WGSL workgroup
arrays as HLSL `groupshared` arrays, zero-initialises them whole and stores
to their elements (`As[i] = v`), but vkd3d-shader 1.19 stopped at "Store to
indexed TGSM", so this last of LightCraft's 27 kernels did not compile.

- **`third_party/vkd3d-shader`** (listed in its `NOVA-VENDOR.txt`, NovaOS's
  own code): a store to an element of a `groupshared` array becomes
  `store_structured` (the first index is the structure index, the rest of
  the path a byte offset into it), a store into any other `groupshared`
  variable `store_raw` at a byte address; arrays and structs stored whole
  are written element by element, and writes to some components of a
  vector are split so each store writes consecutive dwords.  Loads of a
  second index (`a[i][j]`) read the wrong element before (the inner index
  was taken as the structure index) and are fixed too.  Of vkd3d 1.19's
  own HLSL test shaders, five groupshared tests that stopped now compile
  and every other shader compiles exactly as before.  LightCraft's 27
  compute kernels (from its program, turned into HLSL with naga 30) now
  all compile.
- **`tools/hlsltest`**: a `cs_5_0` shader storing to a groupshared array,
  a two-dimensional one, a struct array's fields and a vector's
  components, checked in its disassembly and dispatched on Direct3D 11
  (DXVK) with the results read back.
