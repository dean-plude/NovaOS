# vhidmini: a third-party KMDF HID minidriver, Microsoft's vhidmini2 sample
# (Windows-driver-samples hid/vhidmini2, built unchanged by
# tools/kmdf/build_kmdf.py into C:\Tests\VhidMini), on a root-enumerated
# device, as "devcon install vhidmini.inf root\vhidmini" makes it on
# Windows.  "vhidtest create" registers ROOT\HIDCLASS\0000 with hardware ID
# root\vhidmini through SetupDiCreateDeviceInfo and DIF_REGISTERDEVICE;
# with test signing on, "pnputil /add-driver vhidmini.inf /install" installs
# the package on it: its INF includes NovaOS's MsHidKmdf.inf, which makes
# mshidkmdf (kernel/nt/mshidkmdf.c) the function driver, and adds
# vhidmini.sys below it as a lower filter.  hidclass makes the HID
# collection; vhidtest opens it through hid.dll (32-bit, then 64-bit),
# reads its attributes, strings and caps, and moves input, output and
# feature reports and ReadFile/WriteFile reports through the sample's
# queues; Device Manager lists both nodes as working.  After a restart the
# device comes back from its Enum\ROOT key and starts at boot;
# "pnputil /remove-device" deletes it.
DOC = ('a third-party KMDF HID minidriver, Microsoft\'s vhidmini2 sample built unchanged, on a root-enumerated device '
       '(ROOT\\HIDCLASS\\0000, hardware ID root\\vhidmini): `pnputil /add-driver /install` puts mshidkmdf on it as '
       'the function driver and vhidmini.sys below it as a lower filter; `vhidtest` opens the HID collection through '
       'hid.dll and moves feature, input, output and read/write reports; Device Manager lists it, it starts at boot, '
       'and `pnputil /remove-device` deletes it')
ROOT_ID = r'ROOT\\HIDCLASS\\0000'
_WORKING = r'[^\n]*\] This device is working properly\.'
TESTS = [
    Test('vhidmini test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2),
    Test('vhidmini restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    Test('vhidmini create device', 'vhidtest create',
         [r'vhidtest: registered ' + ROOT_ID, r'vhidtest create: \d+ passed, 0 failed']),
    Test('vhidmini install', r'pnputil /add-driver C:\Tests\VhidMini\vhidmini.inf /install',
         [r'Driver package installed on device: ' + ROOT_ID,
          r'\[PNP\] ' + ROOT_ID + r': filter driver vhidmini added',
          r'\[PNP\] ' + ROOT_ID + r': started \(driver mshidkmdf\)']),
    Test('vhidmini x86', r'C:\Programs\x86\vhidtest.exe', [r'vhidtest: \d+ passed, 0 failed'], timeout=300),
    Test('vhidmini x64', 'vhidtest', [r'vhidtest: \d+ passed, 0 failed'], timeout=300),
    Test('vhidmini device manager', 'devmgmt --check',
         [r'devmgmt: \d+ passed, 0 failed',
          r'devmgmt: Human Interface Devices: [^\n]*\[' + ROOT_ID + _WORKING,
          r'devmgmt: [^\n]*\[HID\\VID_DEED&PID_FEED\\' + _WORKING],
         timeout=120),
    Test('vhidmini start at boot', 'shutdown /r /t 0',
         [r'\[PNP\] ' + ROOT_ID + r': started \(driver mshidkmdf\)'], reboot=True),
    Test('vhidmini after boot', 'vhidtest', [r'vhidtest: \d+ passed, 0 failed'], timeout=300),
    Test('vhidmini remove', r'pnputil /remove-device ROOT\HIDCLASS\0000',
         [r'Device removed successfully', r'\[PNP\] ' + ROOT_ID + r': device removed']),
    Test('vhidmini gone', 'vhidtest gone', [r'vhidtest gone: \d+ passed, 0 failed']),
]
