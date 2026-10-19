# kmdfedu: a KMDF Plug and Play driver on real resources (step 30.4).  QEMU's
# edu device (-device edu, PCI 1234:11e8) gets kmdfedu.sys
# (tools/drivertest/kmdfedu.c, in C:\Tests\KmdfEdu), the KMDF counterpart of
# the edu suite's edutest.sys, built against the KMDF 1.33 headers and run
# on Wdf01000.sys, Microsoft's framework built from its MIT source.  With
# test signing on, "pnputil /add-driver kmdfedu.inf /install" installs it:
# WdfLdr.sys binds it to the framework, EvtDriverDeviceAdd makes the device,
# its interface, an interrupt object and a queue, EvtDevicePrepareHardware
# maps the memory BAR and makes DMA enablers for edu's 28-bit DMA, and the
# framework connects the interrupt at D0.  edutest.exe, the WDM driver's
# program, drives it through the same interface: a factorial through the
# interrupt object's ISR and DPC, DMA through a scatter/gather transaction,
# a common buffer and a packet transaction.  EvtDeviceQueryRemove vetoes
# removal while it is open; pnputil disables it (the device is deleted and
# the driver unloads) and enables it again.  "edutest msi 1" opts the device
# in to MSI as the edu suite does: after a restart of the device the
# framework connects the interrupt object to the message, also after a
# restart of the machine, where the driver starts at boot.  The driver
# asks for S0 idle (2 s): left alone, the framework sends a D3 IRP down the
# device's stack (PoRequestPowerIrp, EvtDeviceD0Exit, the PDO records D3)
# and the next request brings it back to D0 (step 28.4).
DOC = ('a KMDF driver (kmdfedu.sys) on QEMU\'s edu PCI device, installed with `pnputil /add-driver /install`: '
       'Microsoft\'s framework (Wdf01000.sys) hands it the memory BAR, connects its interrupt object (line, then MSI) '
       'and runs its DMA transactions and common buffer; `edutest` computes a factorial through the interrupt and '
       'moves buffers by DMA, left idle it goes to D3 (S0 idle) and the next request brings it back, removal is '
       'vetoed while open, then it is disabled, enabled and started at boot')
EDU_ID = '"PCI\\VEN_1234&DEV_11E8"'
TESTS = [
    Test('kmdfedu test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2, elevated=True),
    Test('kmdfedu restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('kmdfedu install', r'pnputil /add-driver C:\Tests\KmdfEdu\kmdfedu.inf /install',
         [r'Driver package installed on device: PCI\\VEN_1234&DEV_11E8',
          r'\[DbgPrint\] kmdfedu: DriverEntry 00000000',
          r'\[DbgPrint\] kmdfedu: EvtDriverDeviceAdd: device, interface, interrupt and queue created',
          r'\[DbgPrint\] kmdfedu: EvtDevicePrepareHardware: BAR at [0-9a-f]+ \(1048576 bytes\), ident 010000ed',
          r'\[DbgPrint\] kmdfedu: started from D\d, line interrupt vector \d+ at IRQL \d+',
          r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver KmdfEdu\)']),
    Test('kmdfedu devices', 'pnputil /enum-devices /deviceid ' + EDU_ID,
         [r'Status: +Started', r'Driver Name: +KmdfEdu']),
    Test('kmdfedu driver', 'edutest line', [r'edu: a KMDF 1\.33 driver, its interrupt object on the line',
                                            r'factorial 12 = 479001600', r'dma 4096 bytes: .* ok, .* ok',
                                            r'dma 1500 bytes: .* ok, .* ok', r'edutest: \d+ passed, 0 failed']),
    Test('kmdfedu idle', 'edutest idle',
         [r'\[DbgPrint\] kmdfedu: D0Exit to D3', r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: D3 \(no PCI power management, D0 before\)',
          r'edutest idle: \d+ passed, 0 failed']),
    Test('kmdfedu after idle', 'edutest line',
         [r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: D0 \(no PCI power management, D3 before\)',
          r'\[DbgPrint\] kmdfedu: started from D3, line interrupt', r'factorial 12 = 479001600', r'edutest: \d+ passed, 0 failed']),
    Test('kmdfedu removal vetoed', 'edutest veto',
         [r'\[DbgPrint\] kmdfedu: removal vetoed, 1 handle\(s\) open', r'edutest veto: \d+ passed, 0 failed']),
    Test('kmdfedu disable', 'pnputil /disable-device /deviceid ' + EDU_ID,
         [r'Device disabled successfully', r'\[DbgPrint\] kmdfedu: query-remove accepted',
          r'\[DbgPrint\] kmdfedu: hardware released', r'\[DbgPrint\] kmdfedu: device deleted',
          r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: removed \(driver KmdfEdu\)', r'\[DbgPrint\] kmdfedu: unloaded']),
    Test('kmdfedu gone', 'edutest absent', [r'edutest absent: 3 passed, 0 failed']),
    Test('kmdfedu enable', 'pnputil /enable-device /deviceid ' + EDU_ID,
         [r'Device enabled successfully', r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver KmdfEdu\)']),
    Test('kmdfedu after enable', 'edutest line', [r'edutest: \d+ passed, 0 failed']),
    Test('kmdfedu msi on', 'edutest msi 1', [r'edu: MSISupported = 1', r'edutest msi: \d+ passed, 0 failed']),
    Test('kmdfedu msi restart', 'pnputil /restart-device /deviceid ' + EDU_ID,
         [r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: MSI, 1 message\(s\) from vector 0x[0-9a-f]+ at IRQL \d+',
          r'\[DbgPrint\] kmdfedu: started from D\d, message interrupt vector \d+ at IRQL \d+',
          r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver KmdfEdu\)']),
    Test('kmdfedu msi driver', 'edutest message', [r'edu: a KMDF 1\.33 driver, its interrupt object message-signalled',
                                                   r'edu: MSI: 1 message\(s\) from vector \d+', r'factorial 12 = 479001600',
                                                   r'dma 4096 bytes: .* ok, .* ok', r'edutest: \d+ passed, 0 failed']),
    Test('kmdfedu start at boot', 'shutdown /r /t 0',
         [r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver KmdfEdu\)'], reboot=True),
    Test('kmdfedu after boot', 'edutest message', [r'edutest: \d+ passed, 0 failed']),
]
