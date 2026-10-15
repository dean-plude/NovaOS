## Touch screens, pens and touchpads through the HID class driver

USB touch screens, pens and precision touchpads used to bypass NovaOS's
HID class driver, as keyboards and mice did before `kbdhid` and
`mouhid`.  They now take the Windows path too: the device runs `HidUsb`
under the HID class driver, and each of its digitizer collections (touch
screen, pen, a touchpad's finger collection, a digitizer's configuration
collection) gets `hiddigi`, a third service of the class driver in
`kernel/nt/kbdhid.c`, standing in for what win32k reads raw on Windows.
Each collection decodes only its own reports, so a touch screen with a
pen works as both, and contacts reach programs as before: `WM_POINTER`
and `WM_TOUCH` for touch, `PT_PEN` pointer messages and wintab32 for the
pen, the pointer, taps and two-finger scrolling for the touchpad.  On a
precision touchpad `hiddigi` sets the Input Mode feature on the
configuration collection, as Windows does, and the touchpad's mouse
collection runs on `mouhid`.  Raw Input lists the digitizer collections
as HID devices, as on Windows.  `GetPointerInfo`, `GetPointerTouchInfo`
and the frame calls now answer for a touch contact as it was in the
message the thread is handling, as Windows does, not as it is by then (a
quick tap's contact could already read as lifted in its own
`WM_POINTERDOWN`).

Game pads and I2C touchpads keep NovaOS's own drivers for now.  Test:
`digitest` in the devices suite's "touch" boot drives a USB digitizer
(`tools/padpeer.py --kind digitizer`): a touch, the pen with pressure,
the touchpad moving the pointer, a tap, two-finger scrolling, and
unplugging.
