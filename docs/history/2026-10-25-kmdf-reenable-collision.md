## A re-enabled KMDF driver starts every time

A CI fix: `pnputil /enable-device` on the edu device sometimes failed
because `kmdfedu.sys`'s `DriverEntry` returned
`STATUS_OBJECT_NAME_COLLISION` (0xc0000035), seen twice in the devices
shards.

- **Cause:** the extensions `IoAllocateDriverObjectExtension` hands out
  outlived their driver object.  `Wdf01000.sys` stays loaded and keys its
  per-driver extension by the same client id each time, so when the next
  load's driver object landed at the freed one's address, the stale entry
  matched and KMDF's driver creation failed.
- **Fix:** a driver object's extensions are freed with it
  (`kernel/nt/io.c`, `NtIoFreeDriverExtensions`), as Windows frees them
  when it deletes the object.
