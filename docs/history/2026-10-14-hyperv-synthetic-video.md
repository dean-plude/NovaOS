## Hyper-V synthetic video

The third step of Hyper-V Generation 2 support.  A Gen 2 VM starts on
the firmware's frame buffer, whose mode cannot be changed without a
driver for Hyper-V's synthetic video device on VMBus.  NovaOS now drives
it (`kernel/drivers/hv_video.c`, the synthvid protocol as Linux's and
FreeBSD's drivers speak it): it agrees on a protocol version (3.5, 3.2
or 3.0), tells the host the frame buffer is the video memory, lists the
modes the host offers that fit in it, and reports the whole screen as
changed 20 times a second while the host asks for that.  The display
layer takes it over from the GOP frame buffer, so Settings > Display
changes the resolution in a Hyper-V VM.  VMBus drivers can now ask to be
called on each poll, and see how much MMIO space the host gave their
device.  A fake Hyper-V host in a host test checks the driver
([hyperv.md](hyperv.md)).
