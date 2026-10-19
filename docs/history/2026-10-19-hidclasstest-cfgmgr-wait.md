## HID class test: waits for cfgmgr32 to drop an unplugged device

`hidclasstest` waited for setupapi to stop listing an unplugged device but
checked cfgmgr32 once, so it sometimes looked a moment before the
interface was gone (one CI run). It now waits for cfgmgr32 the same way,
up to 10 s, as an app would on Windows. Test-only change.

