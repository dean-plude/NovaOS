program: **Roblox** (App Store, the current client)
kind: game
rating: Borked
works: Its installer runs: it fetches Roblox's settings and packages over HTTPS and installs the client; its Hyperion anti-cheat passes the system-call, alignment-fixup, 32-bit-code and thread-context checks ([Roblox and anti-cheat](#roblox-and-anti-cheat))
problems: Hyperion then reports "Virtual Machine detected" under QEMU, as Roblox does in any virtual machine, so no game starts; untested on a real PC
checked: nightly corpus (the installer, [`090-roblox.py`](../tests/appcorpus/090-roblox.py))
