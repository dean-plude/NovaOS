# edu: a Plug and Play driver with real resources (step 30.3).  QEMU's edu
# device (-device edu, PCI 1234:11e8) has no NovaOS driver.  With test
# signing on, "pnputil /add-driver edutest.inf /install" stages edutest.sys
# (tools/drivertest/edutest.c, in C:\Tests\Edu) and installs it on the
# device: the PnP manager (kernel/nt/pnp.c) loads it, calls AddDevice and
# sends IRP_MN_START_DEVICE with the device's memory BAR and interrupt.
# edutest.exe finds the device through the driver's interface and has it
# compute factorials (the interrupt reaches the driver's ISR and DPC) and
# move buffers by DMA (a scatter/gather list, a common buffer and map
# registers, within edu's 28-bit reach).  With a handle open the driver
# vetoes removal; pnputil /disable-device then removes it (query-remove,
# remove; the driver unloads), /enable-device starts it again, a restart
# starts it at boot, and a device left disabled stays so across a restart.
# (NovaOS has no PCI hot-unplug yet, so surprise removal is not exercised.)
DOC = ('QEMU\'s edu PCI device: a Plug and Play driver (edutest.sys) installed with `pnputil /add-driver /install`, '
       'started by the PnP manager with the device\'s memory BAR and interrupt, driven by `edutest` (a factorial '
       'through the interrupt, ISR and DPC; DMA by scatter/gather list, common buffer and map registers), removal '
       'vetoed while open, then disabled, enabled and started at boot')
EDU_ID = '"PCI\\VEN_1234&DEV_11E8"'
TESTS = [
    Test('edu test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2),
    Test('edu restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('edu install', r'pnputil /add-driver C:\Tests\Edu\edutest.inf /install',
         [r'Driver package installed on device: PCI\\VEN_1234&DEV_11E8',
          r'\[DbgPrint\] edutest: AddDevice, interface \\\?\?\\PCI#VEN_1234&DEV_11E8',
          r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver EduTest\)',
          r'\[DbgPrint\] edutest: started, interrupt vector \d+ at IRQL \d+, \d+ map registers']),
    Test('edu devices', 'pnputil /enum-devices /deviceid ' + EDU_ID,
         [r'Status: +Started', r'Driver Name: +EduTest(?! \(a NovaOS)']),
    Test('edu driver', 'edutest', [r'factorial 12 = 479001600', r'dma 4096 bytes: .* ok, .* ok',
                                   r'edutest: \d+ passed, 0 failed']),
    Test('edu removal vetoed', 'edutest veto',
         [r'\[DbgPrint\] edutest: removal vetoed, 1 handle\(s\) open', r'edutest veto: \d+ passed, 0 failed']),
    Test('edu disable', 'pnputil /disable-device /deviceid ' + EDU_ID,
         [r'Device disabled successfully', r'\[DbgPrint\] edutest: query-remove accepted',
          r'\[DbgPrint\] edutest: hardware released', r'\[DbgPrint\] edutest: removed',
          r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: removed \(driver EduTest\)', r'\[DbgPrint\] edutest: unloaded']),
    Test('edu gone', 'edutest absent', [r'edutest absent: 3 passed, 0 failed']),
    Test('edu enable', 'pnputil /enable-device /deviceid ' + EDU_ID,
         [r'Device enabled successfully', r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver EduTest\)']),
    Test('edu after enable', 'edutest', [r'edutest: \d+ passed, 0 failed']),
    Test('edu start at boot', 'shutdown /r /t 0',
         [r'\[PNP\] PCI\\VEN_1234&DEV_11E8\S*: started \(driver EduTest\)'], reboot=True),
    Test('edu after boot', 'edutest', [r'edutest: \d+ passed, 0 failed']),
    Test('edu disable again', 'pnputil /disable-device /deviceid ' + EDU_ID, [r'Device disabled successfully']),
    Test('edu stays disabled', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('edu still disabled', 'pnputil /enum-devices /deviceid ' + EDU_ID, [r'Problem Code: +22 ']),
]
