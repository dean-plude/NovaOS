# umdf: a UMDF 2 driver (step 30.10).  QEMU's edu device (-device edu, PCI
# 1234:11e8) gets umdftest.dll (tools/drivertest/umdftest.c, in
# C:\Tests\Umdf), a user-mode driver written against the WDK's UMDF 2.33
# headers.  With test signing on (the reflector, WUDFRd.sys, is test-signed
# by NovaOS's build), "pnputil /add-driver umdftest.inf /install" stages the
# package and installs it: the INF's .Wdf section registers the UMDF
# service, the PnP manager starts the reflector as the device's function
# driver, and the UMDF service (wudfsvc) starts a driver host, WUDFHost.exe,
# which loads the framework (Wudfx02000.dll) and the driver: DriverEntry,
# EvtDriverDeviceAdd, EvtDevicePrepareHardware with the device's resources
# and EvtDeviceD0Entry run in the host.  umdftest.exe opens the driver's
# interface and its reads, writes and device controls travel through the
# reflector to the host and back.  Disabling the device removes it in the
# host (which exits), enabling it starts a new host, and after a restart
# the driver starts at boot.
DOC = ('a UMDF 2 driver (umdftest.dll) on QEMU\'s edu device, installed with `pnputil /add-driver /install`: the '
       'reflector WUDFRd.sys starts a driver host (WUDFHost.exe) that runs it on Microsoft\'s user-mode framework, '
       '`umdftest` reads, writes and sends device controls to it, then it is disabled, enabled and started at boot')
EDU_ID = '"PCI\\VEN_1234&DEV_11E8"'
TESTS = [
    Test('umdf test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2),
    Test('umdf restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('umdf install', r'pnputil /add-driver C:\Tests\Umdf\umdftest.inf /install',
         [r'Driver package installed on device: PCI\\VEN_1234&DEV_11E8',
          r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver WUDFRd\)'], settle=5),
    Test('umdf devices', 'pnputil /enum-devices /deviceid ' + EDU_ID,
         [r'Status: +Started', r'Driver Name: +WUDFRd']),
    Test('umdf driver', 'umdftest',
         [r'read back: \d+ bytes \'hello from a user-mode driver\'', r'reversed: \'fedcba\'',
          r'UMDF 2\.33, driver host \d+, requests from \d+', r'umdftest: \d+ passed, 0 failed'],
         boot_expect=[r'wudfsvc: driver host \d+ started for PCI\\VEN_1234&DEV_11E8', r'WUDFRd: host connected',
                      r'umdftest: DriverEntry: UMDF 2\.33',
                      r'umdftest: EvtDriverDeviceAdd: device, interface and queue created in host process \d+',
                      r'umdftest: EvtDevicePrepareHardware: 2 resources, 1 memory range\(s\) \(1048576 bytes\), '
                      r'1 interrupt\(s\)',
                      r'umdftest: EvtDeviceD0Entry']),
    Test('umdf disable', 'pnputil /disable-device /deviceid ' + EDU_ID,
         [r'Device disabled successfully', r'WUDFHost: query-remove', r'WUDFHost: remove',
          r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: removed \(driver WUDFRd\)']),
    Test('umdf enable', 'pnputil /enable-device /deviceid ' + EDU_ID,
         [r'Device enabled successfully', r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver WUDFRd\)'],
         settle=5),
    Test('umdf after enable', 'umdftest', [r'umdftest: \d+ passed, 0 failed'],
         boot_expect=[r'WUDFHost: PCI\\VEN_1234&DEV_11E8\S*: removed, exiting']),
    Test('umdf start at boot', 'shutdown /r /t 0',
         [r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver WUDFRd\)'], reboot=True),
    Test('umdf after boot', 'umdftest', [r'umdftest: \d+ passed, 0 failed'], settle=5),
]
