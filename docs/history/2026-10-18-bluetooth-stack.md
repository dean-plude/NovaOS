## Bluetooth: the host stack, pairing and HID over Bluetooth

First part of roadmap step 28.2.  NovaOS had no Bluetooth at all.  Now a
USB Bluetooth radio comes up, finds devices, pairs with them and runs a
Bluetooth mouse, the Windows way:

- **`bthusb` and the host stack** (`kernel/nt/bthusb.c`, `bthport.c`, in
  place of Windows' `bthusb.sys` and `bthport.sys`, written from the Core
  Specification 5.3): HCI over USB (class requests, the interrupt and bulk
  endpoints), controller bring-up, inquiry with names and classes, Secure
  Simple Pairing and legacy PIN pairing, link keys in
  `BTHPORT\Parameters\Keys`, authentication and encryption, L2CAP and an
  SDP client.
- **`bthprops.cpl`**: the radio and device API programs use (find radios
  and devices, inquire, pair, list and turn services on, remove).
- **HID over Bluetooth** (`kernel/nt/hidbth.c`): a paired device's HID
  service gets a `BTHENUM` node and a HID minidriver, so a Bluetooth mouse
  moves the cursor as a USB one does, and comes back after a restart by
  its link key.
- **Vendor radio packages**: the inbox `bth.inf` has the sections
  Intel's `ibtusb.inf` includes, and setupapi now writes a `.HW`
  section's `LowerFilters` into the device's key, so Intel's filter
  driver goes under `bthusb` as on Windows.
- **Tested** in the devices suite's new `bluetooth` boot: a fake radio
  (`tools/btpeer.py`, behind QEMU's usb-redir) with a mouse and a headset
  in range, `bttest`, a restart, and `btfilter.sys`, a test filter laid
  out as `ibtusb` is.

Not yet: headset audio (SCO, A2DP, hands-free), connections the device
starts, Bluetooth Low Energy, Settings' Bluetooth page, and Intel's
driver on the T14's real radio.
