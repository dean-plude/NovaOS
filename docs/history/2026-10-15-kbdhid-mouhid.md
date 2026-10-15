## Keyboards and mice through kbdhid and mouhid

USB keyboards and mice used to bypass NovaOS's HID class driver: the
USB core handed them straight to NovaOS's own `usbhid` driver.  They now
take the Windows path.  The USB device runs `HidUsb` under the HID class
driver, which makes a collection per top-level collection; a keyboard
collection (hardware ID `HID_DEVICE_SYSTEM_KEYBOARD`) gets the new
`kbdhid` class driver and a mouse or pointer collection
(`HID_DEVICE_SYSTEM_MOUSE`) gets `mouhid`, as `keyboard.inf` and
`msmouse.inf` install them on Windows.  Both are built into the kernel
(`kernel/nt/kbdhid.c`): they read the collection's input reports through
`IRP_MJ_READ`, decode them with NovaOS's report parser and post keys and
motion to the desktop, write the keyboard lights with `IRP_MJ_WRITE`,
and arm remote wakeup with `IOCTL_HID_ENABLE_WAKE_ON_SX`.  Device
Manager shows them under Keyboards and Mice, and Raw Input lists them
as the keyboard and mouse rather than as HID devices.

USB touch screens, pens, touchpads, boot-protocol-only devices and game
pads keep NovaOS's own USB drivers for now.  Test: `kbdmoutest` in the
devices suite's "devmgr" boot plugs in a second keyboard and tablet,
moves the pointer, types a line and unplugs them.
