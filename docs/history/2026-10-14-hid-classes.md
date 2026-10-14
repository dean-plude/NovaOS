## The HID class driver

Dean's ask "work on HID Classes": the first step of a Windows HID stack,
the part `hidclass.sys` plays, so a HID device NovaOS has no driver of
its own for opens through `hid.dll` as on Windows.

- **Class driver** (`kernel/nt/hidclass.c`, exports `HidRegisterMinidriver`
  and `HidNotifyPresence` as `hidclass.sys`): a minidriver registers from
  its `DriverEntry`; the class driver takes its `AddDevice` and dispatch
  table, reads the HID and report descriptors and attributes through the
  internal `IOCTL_HID_*` requests, and reports one child device per
  top-level collection with `GUID_DEVINTERFACE_HID` turned on.  A reader
  thread keeps `IOCTL_HID_READ_REPORT` going and queues each input report
  for every open handle (32 by default).  The collection answers the
  `IOCTL_HID_*` requests `hid.dll` sends: collection information,
  preparsed data (the "HidP KDR" format `novahidp.h` builds, now shared
  with the kernel and per top-level collection), feature, input and
  output reports, strings, the queue's size and flush; `ReadFile` and
  `WriteFile` take input and send output reports.
- **USB minidriver** (`kernel/nt/hidusb.c`, the service `HidUsb`, built
  into the kernel: drivers in `ldr.c`'s built-in table load without an
  image): Plug and Play gives it a USB device whose compatible IDs say
  HID (`USB\Class_03`) and that no NovaOS driver took.  Interrupt IN for
  input reports, interrupt OUT or `SET_REPORT` for output, `GET_REPORT`
  and `SET_REPORT` for feature and input reports, string descriptors.
  `usbd.c`'s URB layouts moved to `kernel/nt/usbdi.h` for it.
- **hid.dll**: on a class driver's collection, `HidD_*` are now the
  `IOCTL_HID_*` requests through `DeviceIoControl`, as on Windows
  (`HidD_SetOutputReport`, `Get`/`SetFeature`, `GetInputReport`, the
  manufacturer and serial number strings, `GetIndexedString`, the input
  buffers and `FlushQueue` included).  Game controllers keep their own
  path.  cfgmgr32 (and so setupapi) lists the collections under
  `GUID_DEVINTERFACE_HID`.

New devices-suite boot `hid`: `tools/padpeer.py --kind vendor` is a
vendor-defined HID device (1209:0008) with input, output and feature
reports that echoes output reports back as input; `hidclasstest` checks
it through setupapi and `hid.dll` on two handles, then the test unplugs
it.  Next steps (Raw Input for these devices, composite USB devices, a
third-party minidriver, timed overlapped reads for hidapi) are in
[drivers.md](drivers.md#hid-the-class-driver-and-minidrivers).
