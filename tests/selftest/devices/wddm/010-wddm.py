# wddm: a Windows display driver on the boot display (Phase 25.3, step 1 of
# docs/design/intel-gpu.md).  The machine's standard VGA (-vga std, PCI
# 1234:1111) is drawn by NovaOS's built-in Bochs VBE driver.  With test
# signing on, "pnputil /add-driver wddmdod.inf /install" stages wddmdod.sys
# (tools/drivertest/wddmdod.c, a WDDM display-only driver written as
# Microsoft's KMDOD sample is, in C:\Tests\Wddm) and installs it on the
# adapter: the built-in driver steps aside, the driver registers with
# \Device\DxgKrnl (kernel/nt/dxgkrnl.c), starts, reports its output and its
# modes through a VidPN, and the desktop moves to it between two frames
# (hal/display.c): from then on each drawn rectangle reaches the driver's
# DxgkDdiPresentDisplayOnly.  disptest changes the mode through
# ChangeDisplaySettings, the same DesktopSetHeadMode the Settings app's
# resolution chips call: each change is a VidPN the driver checks and
# commits.  pnputil /disable-device removes it: it stops, says what it left
# on the screen (DxgkDdiStopDeviceAndReleasePostDisplayOwnership), unloads,
# and the built-in driver draws again in that mode; /enable-device starts it
# again, and a restart starts it at boot.
DOC = ('a WDDM display-only driver (wddmdod.sys) on QEMU\'s standard VGA, installed with `pnputil /add-driver /install`: '
       'the built-in Bochs driver steps aside, the desktop moves to the driver through NovaOS\'s dxgkrnl host '
       '(VidPN modes, DxgkDdiPresentDisplayOnly), `disptest` changes its mode as Settings does, disabling it gives '
       'the screen back to the built-in driver, then enabled and started at boot')
VGA_ID = '"PCI\\VEN_1234&DEV_1111"'
TESTS = [
    Test('wddm test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2, elevated=True),
    Test('wddm restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('wddm install', r'pnputil /add-driver C:\Tests\Wddm\wddmdod.inf /install',
         [r'Driver package installed on device: PCI\\VEN_1234&DEV_1111',
          r'\[PCI\] [0-9a-f:.]+: Bochs VBE steps aside for an installed Windows driver',
          r'\[DXGK\] WddmDod: display-only miniport registered \(DDI version 0x300e\)',
          r'\[DbgPrint\] wddmdod: started on 1234:1111 \(DISPI b0c[0-9a-f], \d+ MiB\), the screen at \d+x\d+',
          r'\[DXGK\] WddmDod: started, 1 source\(s\), 1 output\(s\), WDDM 1\.2',
          r'\[DXGK\] WddmDod: \d+ source mode\(s\), \d+ target mode\(s\) on output 0',
          r'\[PNP\] PCI\\VEN_1234&DEV_1111\S*: started \(driver WddmDod\)'], settle=3),
    Test('wddm devices', 'pnputil /enum-devices /deviceid ' + VGA_ID,
         [r'Status: +Started', r'Driver Name: +WddmDod(?! \(a NovaOS)'],
         boot_expect=[r'\[DISPLAY\] WddmDod \(a Windows display-only driver\) drives \S.*: \d+x\d+, \d+ mode\(s\)',
                      r'\[DXGK\] WddmDod: VidPN committed, source 0 at \d+x\d+ on output 0']),
    Test('wddm modes', 'disptest',
         [r'\[DbgPrint\] wddmdod: mode 1024x768', r'\[DISPLAY\] Mode 1024x768 \(Windows display driver WddmDod\)',
          r'\d+ passed, 0 failed']),
    Test('wddm set mode', 'disptest 1024 768',
         [r'ChangeDisplaySettings: 0', r'current 1024 x 768', r'\[DbgPrint\] wddmdod: mode 1024x768'], settle=3),
    Test('wddm disable', 'pnputil /disable-device /deviceid ' + VGA_ID,
         [r'Device disabled successfully', r'\[DbgPrint\] wddmdod: display released at 1024x768',
          r'\[DXGK\] WddmDod: stopped; it left 1024x768',
          r'\[DISPLAY\] WddmDod gave the screen back: Bochs VBE at 1024x768',
          r'\[PNP\] PCI\\VEN_1234&DEV_1111\S*: removed \(driver WddmDod\)', r'\[DbgPrint\] wddmdod: unloaded']),
    Test('wddm built-in again', 'disptest 800 600',
         [r'ChangeDisplaySettings: 0', r'current 800 x 600', r'\[DISPLAY\] Mode 800x600 \(stride'], settle=3),
    Test('wddm enable', 'pnputil /enable-device /deviceid ' + VGA_ID,
         [r'Device enabled successfully', r'\[DbgPrint\] wddmdod: started on 1234:1111 .*the screen at 800x600',
          r'\[PNP\] PCI\\VEN_1234&DEV_1111\S*: started \(driver WddmDod\)'], settle=3),
    Test('wddm start at boot', 'shutdown /r /t 0',
         [r'\[PNP\] PCI\\VEN_1234&DEV_1111\S*: started \(driver WddmDod\)'], reboot=True),
    Test('wddm after boot', 'disptest saved 800 600', [r'\d+ passed, 0 failed']),
    Test('wddm mode after boot', 'disptest 1024 768',
         [r'ChangeDisplaySettings: 0', r'\[DbgPrint\] wddmdod: mode 1024x768',
          r'\[DISPLAY\] Mode 1024x768 \(Windows display driver WddmDod\)'], settle=3),
]
