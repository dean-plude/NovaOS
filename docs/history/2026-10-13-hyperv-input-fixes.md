## Hyper-V keyboard and mouse fixes

The Hyper-V mouse never attached: NovaOS read the device information the
host sends before the HID descriptor as 30 bytes, but its first field (the
size) is 32 bits, so the block is 32 bytes and the report descriptor was
looked for two bytes early and rejected.  The VMBus connection is also
sturdier on a real host: the SynIC MSRs keep their reserved bits, each
message gets five seconds instead of one, a connection left over from the
firmware is dropped (UNLOAD) and retried, and the log says what each step
got.  Under Hyper-V, which has no HPET, the TSC and APIC timer rates come
from Hyper-V's frequency MSRs instead of a guess ([hyperv.md](../hyperv.md)).
