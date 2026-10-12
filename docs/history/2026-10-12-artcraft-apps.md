## The ArtCraft apps in the App Store

The App Store lists the thirteen downloads on
[getartcraft.com/apps](https://getartcraft.com/apps): ArtCraft's twelve
open-source "Crafting Apps" (PhotoCraft, VectorCraft, FilmCraft,
LightCraft, PdfCraft, EffectCraft, DesignCraft, WordCraft, SoundCraft,
DeckCraft, CADCraft and GridCraft) and the ArtCraft Launcher.  Each is a
Rust program under MIT or Apache-2.0; the catalog installs the portable
x64 zip from the app's GitHub release, unpacked with 7-Zip into
`C:\Programs\NAME`.

PhotoCraft, the first of them, opened a window that stayed black.  The
apps draw with egui on wgpu, which on NovaOS picks its OpenGL back end
(llvmpipe).  wgpu keeps that back end's context on a hidden window that it
creates on a thread of its own, a thread that never reads messages.  When
the app's own window became active, NovaOS's `user32` sent
`WM_ACTIVATEAPP` to every top-level window of the program and waited for
each to answer, so the main thread waited on the hidden window forever
before drawing its first frame.  Windows activates per thread: the
activating thread's windows are sent the message, and those of the
program's other threads are no longer waited for; they now get it without
a wait.

- **PhotoCraft** (app corpus, `886-photocraft.py`): installed with the
  Store's own button, it must open its editor window
  (`tests/reference/photocraft.png`).  It says "Switched to CPU rendering":
  its GPU image compositor does not run on wgpu's OpenGL back end on
  llvmpipe, so it composites on the CPU while the window keeps OpenGL.
- The other apps share PhotoCraft's toolkit and are untested.
