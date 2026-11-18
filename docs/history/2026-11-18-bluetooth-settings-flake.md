## CI: Settings' Bluetooth pairing lost to a stale HID connect

The `bluetooth settings` test (devices suite) failed now and then on pull
requests that did not touch Bluetooth (#618, #631): Settings paired the
mouse, but its HID service never came on. The serial log showed why. When
`bttest remove` switched the mouse's HID service off, `hidbth`'s link
thread saw its channels close and called `BthHidConnect` again before the
node was delisted; that call was left waiting (up to 30 seconds) for the
old link to authenticate after the link had dropped. When Settings then
paired the forgotten mouse a few seconds later, in the same device slot,
the stale call took the new link's answers as its own, sent its own Set
Connection Encryption (the log's `encryption failed (02)` next to
Settings' `authenticated and encrypted`), and on failing disconnected the
link, so Settings' SDP query found no link (`L2CAP PSM 0001 did not open`),
the mouse had no report descriptor, and its HID service was not switched
on.

Each device now counts the links that came down (`drops` in
`kernel/nt/bthport.c`, kept when the device is forgotten). `secure()`
gives up as soon as the link it started on drops, and `BthHidConnect`
returns without touching the link when its node stopped being the
device's or the link changed under it.

- **Tests:** the devices suite's Bluetooth boot (`bluetooth settings`,
  `bluetooth device reconnect`, `bluetooth remove`) covers the paths; the
  race itself needed CI's timing and is not forced by a test.
