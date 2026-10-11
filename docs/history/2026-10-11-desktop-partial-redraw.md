## The desktop redraws only what changed

Every change on screen made the desktop draw the whole frame: every
window, each built-in app's painter (under the big kernel lock and the
file-system lock), the acrylic blur under File Explorer and a copy of the
whole screen.  A program presenting its window, the pointer moving over a
program's window, a key press and `InvalidateRect` all did that, so each
app open made every other app's drawing slower.

A frame now redraws only the part of the screen that changed, when it is
a part: the windows outside it are not drawn, the program's pixels are
read only there, and only that part is copied to the screen (with page
flipping, also what the last frame changed).  user32 tells the desktop
which part of its window it presented.  Input to a program's window
redraws nothing (the program redraws itself); input to a built-in app
redraws its window.  Acrylic backdrops are blurred at a quarter of the
resolution and scaled back up, a sixteenth of the work.  The profiler
reports the desktop's frames.

Firefox and File Explorer started together (`profile on 50 6000`, 4 GB,
2 CPUs, with the lock change of the same day): the desktop drew 82 frames
in the minute against 49 (34 of them a part of the screen), its longest
frame took 0.9 s against 4.2 s, programs got 47% of the CPU against 38%,
and the desktop watchdog (3 s without a frame) never fired.
