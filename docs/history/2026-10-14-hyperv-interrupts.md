## Hyper-V channel interrupts and devices added or removed

The fifth step of Hyper-V Generation 2 support.  VMBus channels were read
every 10 ms by the device poll thread, and a device the host added or
removed after start went unnoticed.  The host's interrupt (the SynIC's
SINT2, with the channel's bit in the event flags) now wakes a `vmbus`
thread that reads the channel at once, and that thread also handles the
host's later messages: a device added (a network adapter, an integration
service ticked in Hyper-V Manager) is opened as at start, and one removed
is let go by its driver, closed, its memory handed back and its number
released, as FreeBSD's BSD-licensed `vmbus` driver does.  A network
adapter added while running becomes the network stack's adapter.  The
reference TSC page and synthetic timers turned out not to be needed
([hyperv.md](hyperv.md)).
