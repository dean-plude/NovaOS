- **Direct3D 12** (After 0.1, step 24.4): VKD3D-Proton (LGPL-2.1, from the
  App Store as its own unchanged DLLs, `d3d12_vkd3d.dll` and `d3d12core.dll`)
  behind NovaOS's `d3d12.dll`.  ~~A Direct3D 12 sample renders~~ Done:
  `d3d12test` draws, reads back and presents a triangle on lavapipe,
  64- and 32-bit ([Direct3D 12](HISTORY.md#direct3d-12-through-vkd3d-proton),
  [32-bit present](HISTORY.md#direct3d-12-presents-in-32-bit-programs)).
  ~~One Direct3D 12 game renders~~ Done: Squash the Creeps on Godot 4.7's
  Forward+ renderer ([A Direct3D 12 game](HISTORY.md#a-direct3d-12-game-squash-the-creeps-on-godot)).
  ~~Sound in Godot (WASAPI)~~ Done: the game's music plays
  ([Godot's sound](HISTORY.md#godots-sound-through-wasapi)).
  Still to do: two of Godot's compute pipelines lavapipe refuses
  (`VK_ERROR_UNKNOWN`).
