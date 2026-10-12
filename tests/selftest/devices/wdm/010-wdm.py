# wdm: a Windows kernel-mode driver (step 30.1).  wdmtest.sys
# (tools/drivertest/wdmtest.c, built with clang and lld-link /driver, in
# C:\Tests\Wdm) installed as a kernel service with sc.  Before test signing
# is on, starting it fails with ERROR_INVALID_IMAGE_HASH (577) and nothing
# is loaded; with HKLM\SYSTEM\CurrentControlSet\Control\NovaOS TestSigning
# set and a restart, sc start loads it (DriverEntry creates \Device\WdmTest
# and \DosDevices\WdmTest), wdmtest.exe's ReadFile, WriteFile and
# DeviceIoControl round-trip through it as IRPs (step 30.2: direct I/O
# through MDLs, the driver's checks of the IRQL, DPCs, spin locks, pool,
# events, semaphores, timers and fast mutexes, and an IRP the driver pends
# and completes from a timer's DPC), and sc stop unloads it, naming the
# pool block the driver left.
DOC = ('a Windows kernel-mode driver (wdmtest.sys, clang and lld-link /driver) refused before test signing is on, '
       'then installed with `sc create type= kernel`, started, driven by `wdmtest` (buffered, neither and direct I/O, '
       'the IRQL, DPCs, spin locks, pool, events, timers, a pending IRP completed from a DPC) and unloaded with `sc stop`')
TESTS = [
    Test('wdm create', r'sc create WdmTest type= kernel binPath= C:\Tests\Wdm\wdmtest.sys',
         [r'\[SC\] CreateService SUCCESS']),
    # (sc's exit code is the Win32 error, 577 here: the output decides)
    Test('wdm refused without test signing', 'sc start WdmTest',
         [r'\[SC\] StartService FAILED 577', r'\[NT\] wdmtest\.sys: not loaded: .* test signing'], builtin=True),
    Test('wdm test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2),
    Test('wdm restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('wdm start', 'sc start WdmTest',
         [r'STATE\s+: 4  RUNNING', r'\[DbgPrint\] wdmtest: DriverEntry '
          r'\\Registry\\Machine\\System\\CurrentControlSet\\Services\\WdmTest',
          r'\[NT\] wdmtest\.sys loaded at 0x[0-9a-f]+ \(service WdmTest\): DriverEntry returned 0x00000000']),
    Test('wdm round trip', 'wdmtest', [r'read back: "hello from user mode"', r'pending: \d+ ms, DPC at IRQL 2',
                                       r'wdmtest: \d+ passed, 0 failed']),
    Test('wdm stop', 'sc stop WdmTest',
         [r'STATE\s+: 1  STOPPED', r'\[DbgPrint\] wdmtest: unloaded',
          r"\[NT\] wdmtest\.sys: 1 pool block \(32 bytes, tags 'Leak'\) never freed",
          r'\[NT\] wdmtest\.sys unloaded \(service WdmTest\)']),
    Test('wdm device gone', 'wdmtest absent', [r'wdmtest absent: 1 passed, 0 failed']),
]
