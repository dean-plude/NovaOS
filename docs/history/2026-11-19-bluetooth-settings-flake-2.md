## CI: Bluetooth's stale HID reconnect, the rest of it

The fix for the `bluetooth settings` flake (#632) made a stale
`BthHidConnect` give up when its link dropped, but CI showed it could
still act first. On #635 (run 1590), when `bttest remove` forgot the
mouse, `hidbth` saw its channels close and called `BthHidConnect` once
more; the link was already down, so it sent Create Connection to the
mouse, and the device was forgotten under it. The controller completed
that link, which no one waited for any more, so Settings' own Create
Connection a moment later failed with 0x0B ("connection already
exists") and the pairing was refused (`0xc000009d`).

- Switching a device's HID service off, or forgetting the device, now
  first marks its node as no longer the device's (`hid_stop()`), then
  closes the channels: `hidbth`, which reconnects when it sees them
  close, is refused from the start.
- A Connection Complete that nobody waits for (no device connecting to
  that address) is disconnected at once instead of being left open.
- `acl_send` no longer spends one of the controller's ACL buffers on a
  packet it does not send because the link is down.
- `L2CAP PSM ... did not open` now logs the channel's state and the free
  ACL buffers. The headset's `L2CAP PSM 0001 did not open` on main run
  1589 (SDP to a freshly paired headset got no answer, so its Hands-Free
  and Audio Sink services stayed off, error 1060) happened with no stale
  thread running and is not explained by these logs; the new line is
  there to pin it down if it comes back.
