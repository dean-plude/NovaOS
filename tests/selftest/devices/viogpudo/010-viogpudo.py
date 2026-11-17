# viogpudo: Red Hat's WDDM display-only driver for QEMU's virtio GPU, as
# virtio-win publishes it, unchanged on NovaOS's dxgkrnl host (Nebula item
# 3; the rehearsal on QEMU of step 2 of docs/design/intel-gpu.md).  The
# machine's display is a virtio-vga (-vga virtio, PCI 1AF4:1050), which
# NovaOS draws through its VGA-compatible DISPI registers (display.c).
# virtio-win 0.1.302's signed viogpudo package (tools/drivertest/viogpudo.py)
# is in C:\Tests\VioGpu: "pnputil /add-driver viogpudo.inf /install" stages
# it (its catalog is Microsoft's: Windows Hardware Compatibility Publisher,
# so no test signing) and installs it on the card by hardware ID: the
# built-in driver steps aside (virtio_gpu.c lets go of the card's virtio
# side), the driver's DriverEntry loads dxgkrnl and registers through
# displib's DxgkInitializeDisplayOnlyDriver, starts with the card's BARs
# and its line-based interrupt (the virtio control queue: GET_DISPLAY_INFO,
# its framebuffer as a 2D resource, SET_SCANOUT), reports its output and
# modes through a VidPN, and the desktop moves to it: each drawn rectangle
# goes through its DxgkDdiPresentDisplayOnly (TRANSFER_TO_HOST_2D and
# RESOURCE_FLUSH).  disptest changes the mode through
# ChangeDisplaySettings, the same DesktopSetHeadMode the Settings app's
# resolution chips call; disabling it gives the screen back to the built-in
# driver, enabling it starts it again, and after a restart it starts at boot.
DOC = ('Red Hat\'s signed WDDM display-only driver viogpudo.sys from virtio-win, unchanged, installed with '
       '`pnputil /add-driver /install` on QEMU\'s virtio-vga: the built-in driver steps aside, the desktop moves '
       'to it through NovaOS\'s dxgkrnl host, `disptest` changes its mode as Settings does, disabling it gives the '
       'screen back to the built-in driver, then enabled and started at boot')
GPU_ID = '"PCI\\VEN_1AF4&DEV_1050"'
TESTS = [
    Test('viogpudo install', r'pnputil /add-driver C:\Tests\VioGpu\viogpudo.inf /install',
         [r'Driver package installed on device: PCI\\VEN_1AF4&DEV_1050',
          r'\[PCI\] [0-9a-f:.]+: \S.* steps aside for an installed Windows driver',
          r'\[VGPU\] QEMU virtio-vga: left to a Windows display driver',
          r'\[DXGK\] VioGpuDod: display-only miniport registered \(DDI version 0x[0-9a-f]+\)',
          r'\[DXGK\] VioGpuDod: started, 1 source\(s\), 1 output\(s\)',
          r'\[DXGK\] VioGpuDod: \d+ source mode\(s\), \d+ target mode\(s\) on output 0',
          r'\[PNP\] PCI\\VEN_1AF4&DEV_1050\S*: started \(driver VioGpuDod\)'], settle=5),
    Test('viogpudo signer', 'pnputil /enum-drivers',
         [r'Original Name: +viogpudo\.inf(?:\n[^\n]+)*?\nSigner Name: +Microsoft Windows Hardware Compatibility Publisher']),
    Test('viogpudo devices', 'pnputil /enum-devices /deviceid ' + GPU_ID,
         [r'Status: +Started', r'Driver Name: +VioGpuDod'],
         boot_expect=[r'\[DISPLAY\] VioGpuDod \(a Windows display-only driver\) drives \S.*: \d+x\d+, \d+ mode\(s\)',
                      r'\[DXGK\] VioGpuDod: VidPN committed, source 0 at \d+x\d+ on output 0']),
    Test('viogpudo modes', 'disptest',
         [r'\[DISPLAY\] Mode 1024x768 \(Windows display driver VioGpuDod\)', r'\d+ passed, 0 failed']),
    Test('viogpudo set mode', 'disptest 1280 800',
         [r'ChangeDisplaySettings: 0', r'current 1280 x 800',
          r'\[DXGK\] VioGpuDod: VidPN committed, source 0 at 1280x800 on output 0'], settle=3),
    Test('viogpudo disable', 'pnputil /disable-device /deviceid ' + GPU_ID,
         [r'Device disabled successfully', r'\[DXGK\] VioGpuDod: stopped',
          r'\[DISPLAY\] VioGpuDod gave the screen back: Bochs VBE at \d+x\d+',
          r'\[PNP\] PCI\\VEN_1AF4&DEV_1050\S*: removed \(driver VioGpuDod\)']),
    Test('viogpudo built-in again', 'disptest 800 600',
         [r'ChangeDisplaySettings: 0', r'current 800 x 600', r'\[DISPLAY\] Mode 800x600 \(stride'], settle=3),
    Test('viogpudo enable', 'pnputil /enable-device /deviceid ' + GPU_ID,
         [r'Device enabled successfully', r'\[PNP\] PCI\\VEN_1AF4&DEV_1050\S*: started \(driver VioGpuDod\)'], settle=5),
    Test('viogpudo start at boot', 'shutdown /r /t 0',
         [r'\[PNP\] PCI\\VEN_1AF4&DEV_1050\S*: started \(driver VioGpuDod\)'], reboot=True),
    Test('viogpudo mode after boot', 'disptest 1024 768',
         [r'ChangeDisplaySettings: 0', r'\[DISPLAY\] Mode 1024x768 \(Windows display driver VioGpuDod\)'], settle=3),
]
