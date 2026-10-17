## Touchpad tap-and-drag and coasting scroll

Precision touchpads (the T14's I2C one and USB ones alike) gained the two
Windows 11 gestures they lacked:

- **Tap and drag.**  A tap, then the finger down again within 300 ms,
  holds the left button down while that finger stays on the pad, so
  moving it drags (a window, a selection, a file).  Lifting the finger
  ends the drag: there is no drag lock, as Windows 11's default "Tap
  twice and drag to multi-select" behaves.  Touching again quickly
  without moving is the second click of a double-click.
- **Coasting scroll.**  Two fingers that lift while still scrolling leave
  the scrolling going at their speed, slowing down over about a second,
  as Windows' scrolling does; fingers that stop before they lift do not
  coast.  A touch on the pad stops it, and that touch is no tap.  The
  coasting keeps coming as mouse-wheel notches, so every program's
  `WM_MOUSEWHEEL` and `WM_MOUSEHWHEEL` handling sees it.

Both are in the gesture code in `kernel/drivers/usbhid.c`, which every
touchpad in touchpad mode goes through.  The tap and drag timeouts are
libinput's (MIT); its tap state machine is built around its own device
model, so the rules were taken rather than the code.  Tests: `hwcheck`'s
modelled touchpad checks tap-and-drag (and that a touch 400 ms after a
tap does not drag), and `digitest` in the devices suite's "touch" boot
drags with a USB precision touchpad, flicks it so the scrolling coasts on
past the fingers' ten notches, and stops a coast with a touch.
