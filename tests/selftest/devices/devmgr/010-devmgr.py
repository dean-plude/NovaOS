# devmgr: Device Manager (devmgmt.exe) on the devices QEMU presents: the
# q35 machine's own (AHCI, the standard VGA, HD Audio, the e1000e card),
# an xHCI controller with a USB keyboard and a USB tablet, and QEMU's edu
# device, which has no driver.  Device Manager is started from the Start
# menu first (Win+S, "Device Manager", Enter) and closed again; then
# "devmgmt --check" checks setupapi's device list against cfgmgr32's tree
# (each present device reached once from the root, the class GUIDs
# agree), logs the tree by type with each device's status and each PCI
# device's resources, and shows it with the display adapter selected: the
# screenshot is the reference picture (docs/screenshots/device-manager.png).
import re, time


def _wait(nova, pattern, timeout=60):
    for _ in range(int(timeout / 0.25)):
        if re.search(pattern, open(nova.serial_path, 'rb').read().decode('latin-1')):
            return True
        time.sleep(0.25)
    return False


def _from_start_menu(nova):
    nova.qmp.key('meta_l', 's')
    time.sleep(2)
    nova.qmp.type('Device Manager')
    time.sleep(2)
    nova.qmp.key('ret')
    if _wait(nova, r'devmgmt: showing \d+ devices in \d+ classes'):
        time.sleep(3)
        nova.qmp.key('alt', 'f4')
        _wait(nova, r'\[UM\] devmgmt\.exe \(PID \d+\) exited', 30)
    else:
        nova.qmp.key('esc')
        time.sleep(0.5)
        nova.qmp.key('esc')
    time.sleep(2)


TESTS = [
    Test('device manager start menu', 'devmgmt --check',
         [r'devmgmt: \d+ passed, 0 failed',
          r'devmgmt: Display adapters: [^\n]*\[PCI\\VEN_1234&DEV_1111[^\n]*\] This device is working properly\.',
          r'devmgmt: IDE ATA/ATAPI controllers: Standard SATA AHCI Controller \[PCI\\VEN_8086&DEV_2922',
          r'devmgmt: Universal Serial Bus controllers: USB xHCI Compliant Host Controller \[PCI\\VEN_1B36&DEV_000D',
          r'devmgmt: Network adapters: [^\n]*\[PCI\\VEN_8086&DEV_10D3',
          r'devmgmt: Keyboards: [^\n]*\[HID\\VID_0627&PID_0001',
          r'devmgmt: [^\n]*\[PCI\\VEN_1234&DEV_11E8[^\n]*\] The drivers for this device are not installed\. \(Code 28\)',
          r'devmgmt: resources of PCI\\VEN_1234&DEV_1111[^\n]*: Memory Range [0-9A-F]{16} - [0-9A-F]{16}',
          r'devmgmt: resources of PCI\\VEN_8086&DEV_10D3[^\n]*: IRQ 0x[0-9A-F]{8} \(\d+\)'],
         timeout=120, before=_from_start_menu, shot=r'devmgmt: showing the tree',
         boot_expect=[r'command line: "C:\\Windows\\System32\\devmgmt\.exe"']),     # (the Start menu's launch)
]
