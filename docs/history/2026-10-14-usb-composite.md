## Composite USB devices

The next HID step: a USB device with several functions behind one
address (a keyboard with a vendor-defined control interface, a headset
with audio and HID, a webcam with a microphone) now gets a driver per
function, as Windows' `usbccgp.sys` arranges it.

- **A node and a PDO per function.** The device tree already had a
  `USB\VID_v&PID_p&MI_nn` child per function under the "USB Composite
  Device" node; the USB core now remembers which interfaces each child
  is made of (`UsbFnByPnp`), and a child no NovaOS driver took goes to
  Plug and Play like a whole device does, so an installed Windows driver
  or a built-in one (`HidUsb` on `USB\Class_03`, `usbvideo` on
  `USB\Class_0E`) starts on it.
- **The function's own view** (`kernel/nt/usbd.c`): its PDO hands the
  driver a configuration descriptor holding only the function's
  interfaces (and their interface association), refuses to select
  another function's interface, and keeps its own pipes, so `hidusb`,
  which takes the first HID interface it sees, takes the right one
  unchanged.
- **HID IDs**: the HID class driver adds the function's interface to
  its collections' IDs (`HID\VID_1209&PID_000A&MI_01`, then `&Colnn`
  when there are several), as `hidclass.sys` does.

New devices-suite boot `composite`: `tools/compositepeer.py` is a
1209:000A device with a boot keyboard and two vendor-defined HID
functions. NovaOS's keyboard driver takes the first, the HID class
driver each of the others; `usbcompositetest` checks the tree's IDs,
services and status, each function's reports and feature report through
`hid.dll`, Device Manager lists every node as working, and the device is
unplugged. Not checked yet: a real composite device (the T14's webcam
also needs isochronous transfers), and a function made of several
interfaces under a Windows driver.
