# kmdf: a KMDF driver on Microsoft's own framework (step 30.4), in the wdm
# boot after 010-wdm.py turned test signing on.  kmdftest.sys
# (tools/drivertest/kmdftest.c, built against the KMDF 1.33 headers with the
# client stub tools/kmdf/stub.c, in C:\Tests\Kmdf) installed with sc: its
# start loads WDFLDR.SYS, the export driver it imports (NovaOS's loader,
# tools/kmdf/wdfldr.c), whose WdfVersionBind starts Wdf01000.sys, the
# framework built from Microsoft's MIT-licensed Windows-Driver-Frameworks
# source (tools/kmdf/build_kmdf.py; both in System32\drivers, test-signed
# for now).  The framework registers with the loader, the driver binds to
# KMDF 1.33, and its DriverEntry runs WdfDriverCreate and makes a control
# device with a default queue; kmdftest.exe's ReadFile, WriteFile and
# DeviceIoControl reach its EvtIoRead, EvtIoWrite and EvtIoDeviceControl,
# and sc stop runs EvtDriverUnload and unbinds it, the framework deleting
# the device.
DOC = ('a KMDF driver on Microsoft\'s framework: `kmdftest.sys` started with `sc` loads `WdfLdr.sys` and `Wdf01000.sys` '
       '(built from Microsoft\'s Windows-Driver-Frameworks source), binds to KMDF 1.33, and `kmdftest` reaches its '
       'queue\'s read, write and control callbacks; `sc stop` unloads it')
TESTS = [
    Test('kmdf create', r'sc create KmdfTest type= kernel binPath= C:\Tests\Kmdf\kmdftest.sys',
         [r'\[SC\] CreateService SUCCESS']),
    Test('kmdf start', 'sc start KmdfTest',
         [r'STATE\s+: 4  RUNNING',
          r'\[NT\] WDFLDR\.SYS loaded at 0x[0-9a-f]+ as an export driver: DllInitialize returned 0x00000000',
          r'\[NT\] Wdf01000\.sys loaded at 0x[0-9a-f]+ \(service Wdf01000\): DriverEntry returned 0x00000000',
          r'\[DbgPrint\] WdfLdr: Wdf01000 registered, KMDF 1\.33',
          r'\[DbgPrint\] WdfLdr: \\Registry\\Machine\\System\\CurrentControlSet\\Services\\KmdfTest bound to '
          r'Wdf01000 \(KMDF 1\.33\)',
          r'\[DbgPrint\] KmdfTest: DriverEntry 00000000, KMDF 1\.33',
          r'\[NT\] kmdftest\.sys loaded at 0x[0-9a-f]+ \(service KmdfTest\): DriverEntry returned 0x00000000']),
    Test('kmdf round trip', 'kmdftest', [r'read back: "hello from user mode"', r'KMDF 1\.33, creates 1, writes 1',
                                         r'kmdftest: \d+ passed, 0 failed']),
    Test('kmdf stop', 'sc stop KmdfTest',
         [r'STATE\s+: 1  STOPPED', r'\[DbgPrint\] KmdfTest: unloading', r'\[NT\] kmdftest\.sys unloaded \(service KmdfTest\)']),
    Test('kmdf device gone', 'kmdftest absent', [r'kmdftest absent: 1 passed, 0 failed']),
]
