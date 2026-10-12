program: **SuperTux 0.1.3** (App Store; GPL; 32-bit, SDL 1.2, Inno Setup 4 installer)
kind: game
rating: Platinum
works: Installs from the App Store (its Inno Setup installer, silently or through its wizard) and plays in full screen at 640x480 with the keyboard or a game pad: SDL 1.2 reads the pad through winmm's joystick functions (`joyGetPosEx`), which NovaOS answers from its game controllers (Xbox 360 and other XInput pads, HID pads)
problems: Sound not checked (the test machine has no sound card)
checked: nightly corpus ([`947-supertux.py`](../tests/appcorpus/947-supertux.py))
