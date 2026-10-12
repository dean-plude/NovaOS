program: **VKD3D-Proton 2.14.1** (App Store, Runtimes)
kind: runtime
rating: Silver
works: Direct3D 12 on Vulkan (install "Mesa 3D" and "DXVK" first): devices, root signatures, pipelines, drawing, read-back and fences, 64- and 32-bit programs, both presenting through DXVK's swap chains; Godot 4.7's Forward+ renderer plays Squash the Creeps on it
problems: Lavapipe refuses two of Godot's compute pipelines (the game draws without them)
checked: CI graphics tests (`d3d12test`), nightly corpus ([`955-squash-the-creeps.py`](../tests/appcorpus/955-squash-the-creeps.py))
