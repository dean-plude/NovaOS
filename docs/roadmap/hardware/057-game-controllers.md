- ~~Game controllers for Windows games~~ Done: wired Xbox 360 and Xbox
  One controllers (their motors and the Xbox 360 one's player light
  included) and HID game pads on USB, hot-plugged, for XInput
  (`xinput1_4`, `xinput1_3` and older, `xinput9_1_0`), DirectInput 8
  (game controllers, keyboards and mice, immediate and buffered), Raw
  Input (`WM_INPUT` with each controller's HID reports) and `hid.dll`
  (`HidD_*` on a controller's HID path, `HidP_*` on its report
  descriptor; an Xbox controller has the one Windows' Xbox driver
  gives; a HID game pad's path is its HID class collection's, on the
  `gamehid` class driver).  A vendor's USB HID device runs on the HID class driver, where
  `hid.dll` has output and feature reports and `IOCTL_HID_*` through
  `DeviceIoControl`, and overlapped `ReadFile`, `WriteFile` and
  `DeviceIoControl` with `CancelIo` work as hidapi uses them
  ([drivers.md](drivers.md#hid-the-class-driver-and-minidrivers));
  hidapi's own Windows backend, built unchanged, finds such a device as
  USB through the device interface and device node properties
  (`CM_Get_Device_Interface_PropertyW`, `DEVPKEY_Device_*`).
  Raw Input lists the class driver's collections too and sends their
  reports as `WM_INPUT`.
  Still to do: raw keyboard and mouse input, the Xbox
  360 wireless receiver, Bluetooth controllers, force feedback through
  DirectInput, and virtio game pads (QEMU has none).
