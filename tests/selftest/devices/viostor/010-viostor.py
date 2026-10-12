# viostor: a real Windows storage driver on NovaOS's own Storport host
# (step 30.11).  The boot has a second disk on QEMU's virtio-blk card (PCI
# 1af4:1001), which no NovaOS driver runs, holding an NTFS volume labelled
# VIOSTOR with a known file, and virtio-win's signed viostor package
# (tools/drivertest/viostor.py) in C:\Tests\VioStor.  "pnputil /add-driver
# viostor.inf /install" installs it on the card: the PnP manager loads
# viostor.sys, which registers as a Storport miniport with storport.sys
# (kernel/nt/storport.c); the adapter starts, the host finds its disk and
# hands it to the block layer, and the volume mounts as D:.  The known file
# reads back, a new one is written through viostor, and after a restart
# viostor starts on the card at boot and the new file is still there.
DOC = ('virtio-win\'s signed viostor.sys installed with `pnputil /add-driver /install` on QEMU\'s virtio-blk card: '
       'viostor runs on NovaOS\'s own Storport host, its disk\'s NTFS volume mounts as D:, a known file reads back, '
       'a new one is written, and after a restart viostor starts at boot and the file is still there')
BLK_ID = '"PCI\\VEN_1AF4&DEV_1001"'
VOLUME = r'\[DRIVES\] D: is NTFS volume "VIOSTOR" on stor0 \(read-write\)'


TESTS = [
    Test('viostor install', r'pnputil /add-driver C:\Tests\VioStor\viostor.inf /install',
         [r'Driver package installed on device: PCI\\VEN_1AF4&DEV_1001',
          r'\[STOR\] viostor: Storport miniport registered',
          r'\[STOR\] viostor: adapter running',
          r'\[PNP\] PCI\\VEN_1AF4&DEV_1001\S*: started \(driver viostor\)',
          r'\[STOR\] viostor: disk "[^"]*" on LUN 0:0:0, \d+ MiB, through Storport',
          VOLUME], timeout=180),
    Test('viostor devices', 'pnputil /enum-devices /deviceid ' + BLK_ID, [r'Status: +Started', r'Driver Name: +viostor']),
    Test('viostor read', r'cmd.exe /c type D:\viostor.txt', [r'NovaOS reads this file through viostor\.sys']),
    Test('viostor write', r'cmd.exe /c "echo written through Storport> D:\written.txt & type D:\written.txt"',
         [r'written through Storport']),
    Test('viostor start at boot', 'shutdown /r /t 0',
         [r'\[PNP\] PCI\\VEN_1AF4&DEV_1001\S*: started \(driver viostor\)', r'\[STOR\] viostor: adapter running', VOLUME],
         reboot=True),
    Test('viostor after boot', r'cmd.exe /c "type D:\viostor.txt & type D:\written.txt"',
         [r'NovaOS reads this file through viostor\.sys', r'written through Storport']),
]
