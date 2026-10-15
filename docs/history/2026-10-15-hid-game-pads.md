## Game pads through the HID class driver

USB HID game pads and joysticks were the last USB HID devices to bypass
NovaOS's HID class driver: `gamepad.c` read them straight from their
USB interrupt endpoint and gave them HID paths of its own.  They now
take the Windows path: the device runs `HidUsb` under the HID class
driver, and its game controller collection (`HID_DEVICE_SYSTEM_GAME`)
gets `gamehid`, the fourth service of `kernel/nt/kbdhid.c`.  `gamehid`
reads the collection's reports and hands them to `gamepad.c`, which
decodes them into the controller slot DirectInput, winmm's joystick API
and XInput read, so games see the pad as before.  Raw Input, setupapi,
cfgmgr32 and `hid.dll` now use the collection itself, as on Windows:
its interface path is the device's name, its reports come through the
class driver, and `HidD_GetInputReport` asks the device.  DirectInput's
`DIPROP_GUIDANDPATH` names the same path.  Xbox controllers are not HID
devices and keep `xpad`.

Test: `padtest` and `rawpadtest` in the devices suite's "gamepad" boot,
unchanged, now pass with the HID game pad (`tools/padpeer.py --kind
hid`) on the class driver.
