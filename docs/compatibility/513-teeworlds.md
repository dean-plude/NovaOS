program: **Teeworlds 0.7.5** (App Store; free and open source, SDL2)
kind: game
rating: Silver
works: Installs from the App Store and starts in full screen on the built-in OpenGL (Mesa's llvmpipe; it needs OpenGL 1.2), plays its menu music through the sound card and goes through its first-start questions to its start menu with the keyboard, and its menus follow the mouse with its default settings (SDL reads the pointer's moves and recentres it with `SetCursorPos`; with `inp_grab 1` it reads Raw Input instead); joins a game on its own server and stays in it
problems: Joining a game times out under QEMU without KVM, while Mesa compiles its shaders; internet servers not checked (the test network is offline)
checked: nightly corpus ([`925-teeworlds.py`](../tests/appcorpus/925-teeworlds.py), [`926-teeworlds-join.py`](../tests/appcorpus/926-teeworlds-join.py))
