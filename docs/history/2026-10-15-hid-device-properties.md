## hidapi finds USB HID devices by their bus

The fifth HID step: hidapi's Windows backend (and SDL, which uses it)
asks the Configuration Manager what bus each HID device is on before it
reads its strings. It takes the HID interface's
`DEVPKEY_Device_InstanceId` with `CM_Get_Device_Interface_PropertyW`,
which NovaOS's cfgmgr32 did not have, so every device came back with
an unknown bus and no USB details.

- **cfgmgr32**: `CM_Get_Device_Interface_PropertyW` (and `_ExW`): an
  interface path names its device; its properties are the device's
  instance ID and `DEVPKEY_DeviceInterface_ClassGuid`, `_Enabled` and
  `_FriendlyName`, and a path no device exposes is
  `CR_NO_SUCH_DEVICE_INTERFACE`. A device node gains
  `DEVPKEY_Device_BusTypeGuid`, `DEVPKEY_Device_ContainerId` (shared by
  a removable device's functions and HID collections) and
  `DEVPKEY_NAME`, and `CM_Get_DevNode_Property_ExW`.
- **USB instance IDs**: a USB device with a usable serial number string
  gets it as its instance ID (`USB\VID_1209&PID_0008\NOVA0002`), as on
  Windows, so it keeps its ID on any port; one without, or one whose
  serial another device with the same IDs already uses, is named by its
  port as before.
- **SDK headers**: `guiddef.h`, `initguid.h`, `devpropdef.h`,
  `propkeydef.h`, `ntdef.h`, `winbase.h`, `MAKELANGID`, `ARRAYSIZE`,
  `PZZWSTR`, `MB_`/`WC_ERR_INVALID_CHARS`, and `wchar.h` declaring the
  wide character classes as Visual C++'s does.
- **Programs** can build a vendored C library beside them (a program
  manifest's `sources` and `include` now work for C too).

hidapi 0.15.0 (`third_party/hidapi`, BSD licence) builds unchanged into
`hidapitest`, which the devices suite's "hid" boot runs 64-bit and
32-bit: `hid_enumerate` reports the vendor-defined test device as USB
with its instance ID, product and serial number, and `hid_open`,
feature reports, `hid_write` and `hid_read_timeout` work on it.
