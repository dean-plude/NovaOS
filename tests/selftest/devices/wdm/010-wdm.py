# wdm: a Windows kernel-mode driver (steps 30.1 and 30.7).  wdmtest.sys
# (tools/drivertest/wdmtest.c, built with clang and lld-link /driver, in
# C:\Tests\Wdm) installed as a kernel service with sc, and the signing
# policy Windows 11 has (kernel/nt/ci.c).  wdmtest.sys carries a test
# signature (mksys.py's "NovaOS Test Driver Signer", under a root nobody
# trusts): before test signing is on, starting it fails with
# ERROR_INVALID_IMAGE_HASH (577) and nothing is loaded, as does the
# catalog-signed wdmcat.sys that pnputil installed with its test-signed
# catalog.  pnputil refuses wdmbad, whose .sys was changed after its
# catalog was made.  With HKLM\SYSTEM\CurrentControlSet\Control\NovaOS
# TestSigning set and a restart, an unsigned copy and a copy changed after
# signing are still refused, while sc start loads wdmtest.sys (DriverEntry
# creates \Device\WdmTest and \DosDevices\WdmTest), wdmtest.exe's ReadFile,
# WriteFile and DeviceIoControl round-trip through it as IRPs (step 30.2:
# direct I/O through MDLs, the driver's checks of the IRQL, DPCs, spin
# locks, pool, events, semaphores, timers and fast mutexes, and an IRP the
# driver pends and completes from a timer's DPC), sc stop unloads it,
# naming the pool block the driver left, and the catalog-signed wdmcat.sys
# then loads in its place.
DOC = ('a Windows kernel-mode driver (wdmtest.sys, clang and lld-link /driver) under Windows\' signing policy: '
       'test-signed and catalog-signed copies refused before test signing is on, an unsigned and a tampered copy '
       'refused even with it, a package whose file does not match its catalog refused by `pnputil`; then installed '
       'with `sc create type= kernel`, started, driven by `wdmtest` (buffered, neither and direct I/O, the IRQL, DPCs, '
       'spin locks, pool, events, timers, a pending IRP completed from a DPC) and unloaded with `sc stop`')
TESTS = [
    Test('wdm create', r'sc create WdmTest type= kernel binPath= C:\Tests\Wdm\wdmtest.sys',
         [r'\[SC\] CreateService SUCCESS']),
    Test('wdm create unsigned', r'sc create WdmUnsigned type= kernel binPath= C:\Tests\Wdm\wdmunsigned.sys',
         [r'\[SC\] CreateService SUCCESS']),
    Test('wdm create tampered', r'sc create WdmTampered type= kernel binPath= C:\Tests\Wdm\wdmtampered.sys',
         [r'\[SC\] CreateService SUCCESS']),
    # (sc's exit code is the Win32 error, 577 here: the output decides)
    Test('wdm test signature refused without test signing', 'sc start WdmTest',
         [r'\[SC\] StartService FAILED 577',
          r'\[NT\] wdmtest\.sys: not loaded: signed by NovaOS Test Driver Signer \(embedded signature\), '
          r'which is not a Microsoft driver signature'], builtin=True),
    Test('wdm package changed after signing refused', r'pnputil /add-driver C:\Tests\Wdm\wdmbad\wdmbad.inf',
         [r'Failed to add driver package: The hash for the file is not present in the specified catalog file'],
         builtin=True),
    Test('wdm catalog-signed package added', r'pnputil /add-driver C:\Tests\Wdm\wdmcat\wdmcat.inf',
         [r'Driver package added successfully', r'Published Name:\s+oem\d+\.inf']),
    Test('wdm package signer', 'pnputil /enum-drivers', [r'Original Name:\s+wdmcat\.inf(.|\n)*?Signer Name:\s+NovaOS Test Driver Signer']),
    Test('wdm create catalog-signed', r'sc create WdmCat type= kernel binPath= C:\Tests\Wdm\wdmcat\wdmcat.sys',
         [r'\[SC\] CreateService SUCCESS']),
    Test('wdm catalog test signature refused without test signing', 'sc start WdmCat',
         [r'\[SC\] StartService FAILED 577',
          r'\[NT\] wdmcat\.sys: not loaded: signed by NovaOS Test Driver Signer \(oem\d+\.cat\), '
          r'which is not a Microsoft driver signature'], builtin=True),
    Test('wdm test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2),
    Test('wdm restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('wdm unsigned refused with test signing', 'sc start WdmUnsigned',
         [r'\[SC\] StartService FAILED 577', r'\[NT\] wdmunsigned\.sys: not loaded: it is not signed'], builtin=True),
    Test('wdm tampered refused with test signing', 'sc start WdmTampered',
         [r'\[SC\] StartService FAILED 577', r'\[NT\] wdmtampered\.sys: not loaded: the image does not match its signature'],
         builtin=True),
    Test('wdm start', 'sc start WdmTest',
         [r'STATE\s+: 4  RUNNING',
          r'\[NT\] wdmtest\.sys: signed by NovaOS Test Driver Signer \(embedded signature\): a test signature',
          r'\[DbgPrint\] wdmtest: DriverEntry \\Registry\\Machine\\System\\CurrentControlSet\\Services\\WdmTest',
          r'\[NT\] wdmtest\.sys loaded at 0x[0-9a-f]+ \(service WdmTest\): DriverEntry returned 0x00000000']),
    Test('wdm round trip', 'wdmtest', [r'read back: "hello from user mode"', r'pending: \d+ ms, DPC at IRQL 2',
                                       r'wdmtest: \d+ passed, 0 failed']),
    Test('wdm stop', 'sc stop WdmTest',
         [r'STATE\s+: 1  STOPPED', r'\[DbgPrint\] wdmtest: unloaded',
          r"\[NT\] wdmtest\.sys: 1 pool block \(32 bytes, tags 'Leak'\) never freed",
          r'\[NT\] wdmtest\.sys unloaded \(service WdmTest\)']),
    Test('wdm device gone', 'wdmtest absent', [r'wdmtest absent: 1 passed, 0 failed']),
    Test('wdm catalog-signed start', 'sc start WdmCat',
         [r'STATE\s+: 4  RUNNING', r'\[NT\] wdmcat\.sys: signed by NovaOS Test Driver Signer \(oem\d+\.cat\): a test signature']),
    Test('wdm catalog-signed stop', 'sc stop WdmCat', [r'STATE\s+: 1  STOPPED', r'\[NT\] wdmcat\.sys unloaded']),
]
