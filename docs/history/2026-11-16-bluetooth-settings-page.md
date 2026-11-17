## Bluetooth: Settings' Bluetooth & devices page and reconnects the device starts

The rest of roadmap step 28.2 that QEMU can show
([drivers.md](drivers.md#bluetooth-bthusb-and-the-host-stack)).

- **Device-started reconnects** (`kernel/nt/bthport.c`, `hidbth.c`,
  `bthaudio.c`): the host used to refuse every Connection Request, so a
  mouse moved after sleep waited up to 10 seconds for NovaOS to page it.
  Now a paired device with a service on is accepted; it authenticates
  with its stored key and encrypts the link, and a mouse or keyboard
  opens HID's control and interrupt channels itself, which the host gives
  to the device's `hidbth` node (others are refused). If it opens none
  within three seconds, `hidbth` opens them; a headset that connects gets
  its profiles opened at once.
- **Settings > Bluetooth & devices** (`kernel/apps/settings_bluetooth.c`,
  a new page after System, found by "bluetooth" and
  `ms-settings:bluetooth`): the radio, paired devices with their state,
  and the devices a search found; **Add device** searches, **Pair** pairs
  and switches the device's services on as Windows does, **Remove
  device** forgets it, all in the background through the host code
  `bthprops.cpl` uses.
- **Tests:** the devices suite's `bluetooth` boot: `bttest reconnect`
  (tools/btpeer.py's new `away mouse` and `back mouse`: the mouse's link
  drops, then it pages the PC, authenticates, encrypts and opens its HID
  channels) and `bttest settings` (the mouse paired in Settings with
  Start, "bluetooth", Down, Enter, then moving the cursor).
- **Not yet:** pairing a real mouse and headset on the T14 with Intel's
  driver, including after sleep (Dean's check that ticks 28.2).
