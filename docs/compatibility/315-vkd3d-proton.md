program: **VKD3D-Proton 3.0.1** (App Store, Runtimes; built by `tools/build_vkd3d_proton.py` with a newer dxil-spirv)
kind: runtime
rating: Gold
works: Direct3D 12 on Vulkan (install "Mesa 3D" and "DXVK" first): devices, root signatures, pipelines, drawing, read-back and fences, 64- and 32-bit programs, both presenting through DXVK's swap chains; Godot 4.7's Forward+ renderer plays Squash the Creeps on it with every pipeline built, and a compute pipeline from Godot's SDFGI shader builds (`d3d12test compute`)
problems: —
checked: CI graphics tests (`d3d12test`), nightly corpus ([`955-squash-the-creeps.py`](../tests/appcorpus/955-squash-the-creeps.py))
