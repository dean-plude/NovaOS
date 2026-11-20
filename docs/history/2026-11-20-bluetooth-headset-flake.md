## CI: the Bluetooth headset's lost first packet

The `bluetooth headset` test failed now and then (main run 1589, #641's
run 1603) with `L2CAP PSM 0001 did not open (result 0000, state 1, its
configuration in, ...)`: SDP to the freshly paired headset never opened,
so its Hands-Free and Audio Sink services stayed off (error 1060) and the
stereo, hands-free and microphone tests after it failed too. The channel
was still "connecting" although the headset's Configuration Request,
which it sends right after its Connection Response, had arrived: the
Connection Response itself was lost on the way in.

It was the USB side. A Windows client's bulk IN transfer (`bthusb`'s ACL
reader) that waited a second with no data was taken back with Stop
Endpoint and queued again, once a second. Pairing is all HCI events, so
the ACL pipe had been quiet, and the headset's first packet could land
just as the transfer was being stopped; QEMU's usb-redir drops data for a
cancelled transfer (a real controller would report it as stopped, its
bytes discarded the same way). Interrupt IN pipes already kept their
transfer queued for this reason.

- **USB client driver interface** (`kernel/nt/usbd.c`): bulk IN pipes now
  keep a transfer that times out between polls queued for the next one,
  as interrupt pipes do (`UsbPipe.keep`, honoured by xHCI), and as
  Windows keeps one pending; nothing is cancelled while the client
  waits, so nothing that arrives meanwhile is lost.
