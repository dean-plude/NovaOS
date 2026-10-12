## Godot's compute pipelines on lavapipe

Every pipeline Squash the Creeps asks for now builds (a leftover of
roadmap 24.4).  Godot 4.7's Forward+ renderer had two compute pipelines
fail with `VK_ERROR_UNKNOWN` on lavapipe, on Mesa 24.2 and 26.2 alike:
its SDFGI shaders (`sdfgi_direct_light` among them), so the game drew
without that lighting pass.

The fault was not NovaOS's, nor Mesa's.  With `MESA_SPIRV_FAIL_DUMP_PATH`
(Mesa writes the SPIR-V its reader refused) and `VKD3D_SHADER_DUMP_PATH`
(VKD3D-Proton writes each shader's DXIL and SPIR-V) set for the game, the
refused SPIR-V came out of the VM: `spirv-val` rejects it ("exits the
selection ... but not via a structured exit") and Linux's lavapipe refuses
it the same way, so VKD3D-Proton's DXIL-to-SPIR-V translator (dxil-spirv)
had nested Godot's branches wrongly.  Its 3.0.1 release does the same;
bisecting dxil-spirv on the captured DXIL found the upstream fix, "Add yet
another tie-breaking case for selection merges" (August 2026), which no
VKD3D-Proton release carries yet.

- **`tools/build_vkd3d_proton.py`**: VKD3D-Proton 3.0.1, unmodified, built
  with dxil-spirv at that fix (MinGW-w64, 64- and 32-bit, about four
  minutes).  CI builds it in the graphics job (cached) and publishes
  `vkd3d-proton.7z` beside `nova.iso` on the "latest" release, where the
  App Store's "VKD3D-Proton" entry now downloads it, as it does Venus.  The
  app corpus builds it for programs that need it
  (`CORPUS_LOCAL_DOWNLOADS`), so a change is tested before the release has
  it.  When a VKD3D-Proton release carries the fix, the Store can go back
  to the official download.
- **gdi32 `D3DKMT*`**: VKD3D-Proton 3.0 imports eighteen of the WDDM
  kernel thunks (`D3DKMTOpenAdapterFromLuid` and the shared-resource and
  sync-object calls) to share resources across APIs.  NovaOS has no WDDM
  kernel-mode adapters, so they answer `STATUS_NOT_SUPPORTED` and
  VKD3D-Proton carries on without sharing, as it is written to.
- **`d3d12test compute`** (graphics suite, 64- and 32-bit): builds a
  compute pipeline from Godot's SDFGI direct-light shader, its DXIL as
  Godot hands it to `CreateComputePipelineState`, with a root signature
  covering its resources.
- **Squash the Creeps** (app corpus) now fails if Godot's log reports a
  pipeline Direct3D 12 refused.
