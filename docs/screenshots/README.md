# Screenshots

The images in the README's "Screenshots" section, and how each was made.
All are of NovaOS running in QEMU, saved as optimized PNG files.

- **Desktop and app windows** are QEMU `screendump`s of NovaOS at
  2560x1600 and 200% scale, resized to 1280x800 with Pillow (Lanczos).
- **Full-screen games** that switch the display to a lower resolution are
  kept at that resolution (640x480 or 800x600) and reduced to 256
  colours.

| File | Size | How it was taken |
|---|---|---|
| `desktop.png` | 1280x800 | The `latest` ISO of 2026-10-14 booted live in QEMU (q35, 4 CPUs, 4 GB, standard VGA at 2560x1600), the installer closed with Esc: the default Aurora desktop with nothing open.  Taken with QMP `screendump`. |
| `install.png` | 1280x800 | The same boot before Esc: **Install NovaOS**, which a live start opens by itself. |
| `notepad++.png` | 1280x800 | Notepad++ 8.8.3 portable (the app corpus's download) staged with `--put DIR=C:\Apps\npp`, opened on `kernel/ke/scheduler.c` staged in `C:\Projects\src`. |
| `app-store.png` | 1280x800 | The App Store opened from the dock, with the Terminal minimized. |
| `task-manager.png`, `task-manager-performance.png` | 1280x800 | Task Manager opened with Ctrl+Shift+Esc over `winhello`, on Processes with `winhello.exe` selected and then on Performance: the screenshots `tests/selftest/core/166-taskmgr.py` takes (`tools/selftest.py --suite core --only taskmgr`), QEMU with 2 CPUs. |
| `firefox.png`, `vlc.png`, `keepassxc.png`, `inkscape.png` | 1280x800 | Copies of the `tests/reference/` screenshots that `tools/appcorpus.py` takes of those programs. |
| `teeworlds.png` | 1280x800 | A copy of `tests/reference/teeworlds.png`: the corpus starts Teeworlds in full screen on the built-in OpenGL and answers its first-start questions with Enter. |
| `beneath-a-steel-sky.png` | 1280x800 | A copy of `tests/reference/beneath a steel sky.png`: the corpus installs ScummVM, starts the game, skips the intro and walks Foster with a click. |
| `opentyrian.png` | 1280x800 | OpenTyrian in full screen playing Tyrian's first level, drawn with Direct3D 9 through DXVK: the steps of `tests/appcorpus/930-opentyrian.py`, then Enter on "Play Next Level" and on Tyrian, with Space held to fire. |
| `blobby-volley-2.png` | 800x600 | The full-screen screenshot of `tests/appcorpus/935-blobby-volley.py`, at the display's 800x600, reduced to 256 colours. |
| `lbreakout2.png` | 640x480 | LBreakout2's first level in full screen: the steps of `tests/appcorpus/940-lbreakout2.py`, then Local Game, Start Original Set; reduced to 256 colours. |
| `cave-story.png` | 640x480 | Cave Story's First Cave in full screen: the steps of `tests/appcorpus/945-cave-story.py`, then Z held through the opening lines; reduced to 256 colours. |
| `device-manager.png` | 1280x800 | A copy of the devices suite's "devmgr" screenshot (`tests/selftest/devices/devmgr/010-devmgr.py`): `devmgmt --check` with every class open and the display adapter selected, resized from 2560x1600. |
| `supertux.png` | 640x480 | SuperTux's first level, "Welcome to Antarctica", in full screen, with Tux walked right using the game pad (the steps of `tests/appcorpus/947-supertux.py`); reduced to 256 colours. |

`aurora-preview.png` (1672x941) is not in the README.  It is a preview of
the Aurora desktop rendered on a Linux host with NovaOS's own drawing code
(see [history/2026-10-05-aurora-desktop.md](../history/2026-10-05-aurora-desktop.md)),
not a screenshot of NovaOS running, and
[design/native-desktop.md](../design/native-desktop.md) refers to it.

## Refreshing a screenshot

1. Take it the same way: `!shot NAME.png` in a `tools/novarun.py`
   command, or `tools/appcorpus.py --only NAME --update-reference` for a
   corpus program (which writes its reference screenshot in `tests/reference/`).
2. Resize a desktop screenshot to 1280x800, or reduce a full-screen game
   screenshot to 256 colours.
3. Replace the file here, and update its row above if the steps changed.
