# driversearch: downloading a driver by hardware ID (pnputil /scan-devices
# /search-online, step 30.6).  QEMU's virtio-serial port (virtio-serial-pci,
# PCI\VEN_1AF4&DEV_1003) has no NovaOS driver, so it waits with problem
# code 28.  pnputil searches for its hardware IDs, most specific first, on
# tools/drivertest/catalog_fixture.py at 10.0.2.2:18091, which answers as
# the Microsoft Update Catalog does from pages recorded from it (catalog/
# here): nothing for the ID with REV_00, three driver updates for the next
# one.  The Windows 11 client updates rank first; the newest of them has a
# SHA-256 that does not match its cabinet and is refused, and the next one
# is downloaded, checked, expanded, and its package (novaser.inf) staged in
# the driver store with the Catalog update it came from.  pnputil then
# lists the package as matching the port, and a second search finds it in
# the store without downloading again.
import re

SOURCE = 'http://10.0.2.2:18091'
NOVASER = r'PCI\\VEN_1AF4&DEV_1003&SUBSYS_00031AF4'
GOOD = '18ae1212-f6c4-4a13-99bb-d0c14fae18e3'

TESTS = [
    Test('driversearch problem 28', 'pnputil /enum-devices /connected /ids',
         [r'Instance ID: +' + NOVASER + r'\\\S+\s+Device Description:[^\n]*\n(?:[^\n]+\n)*?'
          r'Problem Code: +28 \(0x1C\) \[CM_PROB_FAILED_INSTALL\]']),
    Test('driversearch download', f'pnputil /scan-devices /search-online /source {SOURCE}',
         [r'Searching for drivers online: ' + re.escape(SOURCE),
          r'Searching for: +' + NOVASER + r'&REV_00 \(0 updates, 0 for this PC\)',
          r'Searching for: +' + NOVASER + r' \(3 updates, 3 for this PC\)',
          r'Update ID: +958a8300-effe-4113-98d8-f7ded68166c2\s+Products:[^\n]*Windows 11 Client[^\n]*\n'
          r'\s+Not used: +its SHA-256 is not the one the Catalog gives',
          r'Update ID: +' + GOOD + r'\s+Products:[^\n]*\n\s+Downloaded: +C:\\Windows\\SoftwareDistribution\\Download'
          r'\\Drivers\\' + GOOD + r'\\\S+\.cab \(\d+ bytes, SHA-256 [0-9a-f]{64} as the Catalog gives\)',
          r'Matching Device ID: +' + NOVASER + r'&REV_00',
          r'Driver package added successfully\.\s+Published Name: +oem\d+\.inf',
          r'Drivers found: +1'], timeout=240),
    Test('driversearch package source', 'pnputil /enum-drivers',
         [r'Original Name: +novaser\.inf(?:\n[^\n]+)*?\nSource: +Microsoft Update Catalog \(update ' + GOOD + r'\)']),
    Test('driversearch package matches', 'pnputil /enum-devices /connected /drivers',
         [r'Instance ID: +' + NOVASER + r'\\\S+(?:\n[^\n]+)*?\n +Original Name: +novaser\.inf'
          r'(?:\n[^\n]+)*?\n +Matching Device ID: +' + NOVASER + r'&REV_00']),
    Test('driversearch already staged', f'pnputil /scan-devices /search-online /source {SOURCE}',
         [r'Driver package already in the driver store: oem\d+\.inf']),
]
