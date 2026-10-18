# ax211: Intel's Wi-Fi driver for the AX211 (step 30.9's Wi-Fi half).  QEMU
# has no Intel Wi-Fi card, so the boot passes NovaOS
# opt/novaos/pci-standin (kernel/hal/pci.c), which makes QEMU's edu card
# (1234:11e8) answer configuration reads as a ThinkPad T14 Gen 4's AX211
# (8086:51f1, subsystem 8086:0094, class 0280).  It has no NovaOS driver, so
# it waits with problem code 28.  Test signing goes on first, as for
# vioser: Netwtw14.sys is a KMDF client too, and NovaOS's own WdfLdr.sys
# is test-signed by its build (Netwtw14.sys itself must still carry
# Microsoft's signature, and does).  "pnputil /scan-devices /search-online
# /install" finds Intel's own package by its hardware ID on
# tools/drivertest/ax211_catalog.py at 10.0.2.2:18093 (the search page
# recorded from the Microsoft Update Catalog; the cabinet is Intel's real
# 24.40.0.4, signed by Microsoft) and installs Netwtw6e.inf: the PnP
# manager loads Netwtw14.sys, which registers with ndis.sys as a WDI
# miniport (kernel/nt/ndis.c); ndis.sys has it allocate its adapter and
# open it, which is where the driver loads its firmware (netwfw14.dat,
# through IoGetDriverDirectory and ZwReadFile) and probes its hardware.
# edu is not an AX211, so the probe fails and the open completes with an
# error; the real card, scanning and joining a network are the T14's
# (docs/drivers.md, roadmap step 28.1).
DOC = ('Intel\'s AX211 Wi-Fi driver (Netwtw14.sys 24.40.0.4) downloaded by hardware ID from the Update Catalog '
       '(recorded) and installed with `pnputil /scan-devices /search-online /install` on QEMU\'s edu card standing '
       'in for the AX211: it registers as a WDI miniport with NovaOS\'s ndis.sys, which allocates and opens its '
       'adapter, and the driver reaches its hardware probe (the real card is checked on a T14)')
SOURCE = 'http://10.0.2.2:18093'
AX211 = r'PCI\\VEN_8086&DEV_51F1&SUBSYS_00948086'
TESTS = [
    Test('ax211 test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2, elevated=True),
    Test('ax211 restart', 'shutdown /r /t 0',
         [r'\[PCI\] [0-9a-f:.]+ 1234:11e8 stands in for 8086:51f1, subsystem 00948086', r'\[NT\] Test signing on'],
         reboot=True),
    Test('ax211 stand-in', 'pnputil /enum-devices /connected /ids',
         [r'Instance ID: +' + AX211 + r'\S*\s+Device Description:[^\n]*\n(?:[^\n]+\n)*?'
          r'Problem Code: +28 \(0x1C\)']),
    Test('ax211 install', f'pnputil /scan-devices /search-online /source {SOURCE} /install',
         [r'Searching for: +' + AX211 + r' \(\d+ updates?, \d+ for this PC\)',
          r'Driver package installed on device: ' + AX211,
          r'\[NT\] Netwtw14\.sys: signed by Microsoft Windows Hardware Compatibility Publisher',
          r'\[NDIS\] Netwtw14: WDI miniport registered \(NDIS 6\.\d+, WDI version 0x[0-9a-f]+\)',
          r'\[NDIS\] Netwtw14: WDI: MiniportWdiAllocateAdapter',
          r'\[NDIS\] Netwtw14: WDI: MiniportWdiOpenAdapter',
          r'\[NDIS\] Netwtw14: WDI: open adapter completed \(0x[0-9a-f]{8}\)',
          # (edu is not an AX211: the driver's hardware probe fails, and the device stops there)
          r'The device has a problem: code 10'], timeout=600),
]
