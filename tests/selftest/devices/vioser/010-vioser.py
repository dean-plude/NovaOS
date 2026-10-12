# vioser: the first third-party driver (step 30.5).  QEMU's virtio-serial
# controller (virtio-serial-pci, PCI\VEN_1AF4&DEV_1003) has no NovaOS
# driver; its one port (virtserialport org.novaos.test.0) is backed by
# tools/drivertest/serialpeer.py on the host, which sends every byte back
# XORed with 0x5A.  virtio-win's signed vioser package, as published in
# virtio-win 0.1.302 (tools/drivertest/vioser.py), is in C:\Tests\Vioser.
# "pnputil /add-driver vioser.inf /install" stages it (its catalog is
# Microsoft's: Windows Hardware Compatibility Publisher) and installs it on
# the controller: the PnP manager loads vioser.sys, a KMDF driver, which
# binds to Microsoft's framework (Wdf01000.sys, test-signed by NovaOS's
# build, hence test signing on), starts with the device's BARs and its
# line-based interrupt, and reports each port the host announces as a
# child device: the PnP manager starts it raw (vioser runs it too).
# viosertest finds the port's interface, opens \\.\Global\org.novaos.test.0
# and exchanges a line and 64 KiB with the host; after a restart vioser
# starts at boot and the port works again.
DOC = ('virtio-win\'s signed vioser.sys (KMDF) installed with `pnputil /add-driver /install` on QEMU\'s virtio-serial '
       'controller: it runs on Microsoft\'s framework, its port is a child device the PnP manager starts, and '
       '`viosertest` exchanges bytes with the host through `\\\\.\\Global\\org.novaos.test.0`, again after a restart')
VIOSER_ID = '"PCI\\VEN_1AF4&DEV_1003"'
PORT = r'\{6FDE7547-1B65-48ae-B628-80BE62016026\}\\VIOSerialPort\\\S+'
TESTS = [
    Test('vioser test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2),
    Test('vioser restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('vioser install', r'pnputil /add-driver C:\Tests\Vioser\vioser.inf /install',
         [r'Driver package installed on device: PCI\\VEN_1AF4&DEV_1003',
          r'\[PNP\] PCI\\VEN_1AF4&DEV_1003\S*: started \(driver VirtioSerial\)'], settle=5),
    Test('vioser signer', 'pnputil /enum-drivers',
         [r'Original Name: +vioser\.inf(?:\n[^\n]+)*?\nSigner Name: +Microsoft Windows Hardware Compatibility Publisher']),
    Test('vioser devices', 'pnputil /enum-devices /deviceid ' + VIOSER_ID,
         [r'Status: +Started', r'Driver Name: +VirtioSerial']),
    Test('vioser port', 'viosertest',
         [r'port interface: \\\\\?\\\{6FDE7547-1B65-48ae-B628-80BE62016026\}#VIOSerialPort#',
          r'line: \d+ bytes to the host and back', r'bulk: 65536 bytes to the host and back',
          r'viosertest: \d+ passed, 0 failed'],
         boot_expect=[r'\[PNP\] ' + PORT + r': started \(raw, bus driver VirtioSerial\)']),
    Test('vioser start at boot', 'shutdown /r /t 0',
         [r'\[PNP\] PCI\\VEN_1AF4&DEV_1003\S*: started \(driver VirtioSerial\)'], reboot=True),
    Test('vioser after boot', 'viosertest', [r'viosertest: \d+ passed, 0 failed'], settle=5),
]
