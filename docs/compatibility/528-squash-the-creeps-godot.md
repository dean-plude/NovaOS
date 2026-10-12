program: **Squash the Creeps** on **Godot 4.7** (MIT; the official 64-bit Windows build running the game from Godot's demo projects)
kind: game
rating: Gold
works: Godot imports the game's assets (`--headless --import`) and plays it with its default Forward+ renderer on Direct3D 12 (`--rendering-driver d3d12`, through VKD3D-Proton on lavapipe from the App Store's Mesa 3D 26.2.0), every pipeline it asks for built, its SDFGI compute shaders included (VKD3D-Proton 3.0.1 as NovaOS builds it); its music plays through WASAPI (Godot's `IAudioClient3` path)
problems: Needs "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store first
checked: nightly corpus ([`955-squash-the-creeps.py`](../tests/appcorpus/955-squash-the-creeps.py))
