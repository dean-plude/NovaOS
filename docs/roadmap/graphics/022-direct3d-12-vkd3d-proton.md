- **Direct3D 12** (After 0.1, step 24.4): VKD3D-Proton (LGPL-2.1, from the
  App Store as its own unchanged DLLs, `d3d12_vkd3d.dll` and `d3d12core.dll`)
  behind NovaOS's `d3d12.dll`.  ~~A Direct3D 12 sample renders~~ Done:
  `d3d12test` draws, reads back and (64-bit) presents a triangle on
  lavapipe ([Direct3D 12](HISTORY.md#direct3d-12-through-vkd3d-proton)).
  Still to do: 32-bit programs' first present (it crashes inside
  lavapipe's 32-bit window-system code), and one Direct3D 12 game that
  renders.
