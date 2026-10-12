program: **Squash the Creeps** on **Godot 4.7** (MIT; the official 64-bit Windows build running the game from Godot's demo projects)
kind: game
rating: Silver
works: Godot imports the game's assets (`--headless --import`) and plays it with its default Forward+ renderer on Direct3D 12 (`--rendering-driver d3d12`, through VKD3D-Proton on lavapipe from the App Store's Mesa 3D 26.2.0); its music plays through WASAPI (Godot's `IAudioClient3` path)
problems: Needs "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store first. Lavapipe refuses two of Godot's compute pipelines (`VK_ERROR_UNKNOWN`); the game draws without them
checked: nightly corpus ([`955-squash-the-creeps.py`](../tests/appcorpus/955-squash-the-creeps.py))
