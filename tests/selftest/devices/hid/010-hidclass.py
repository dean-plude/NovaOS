# hid: a vendor-defined USB HID device on the xHCI controller,
# tools/padpeer.py --kind vendor behind a QEMU usb-redir device (port
# 10713).  NovaOS's own drivers leave it alone, so Plug and Play gives it
# the HID class driver on its USB minidriver (kernel/nt/hidclass.c,
# hidusb.c), which must log the minidriver, the device and its
# collection.  hidclasstest opens the collection through setupapi and
# hid.dll as a Windows program does and checks its attributes, strings,
# preparsed data and reports (the peer echoes each output report as input
# report 1 and keeps the feature report) and through Raw Input (its
# device list, WM_INPUT for an echoed report), 32-bit first.  hidapitest
# (hidapi's own Windows backend, third_party/hidapi, 64-bit then 32-bit)
# must enumerate it as a USB device by the device interface and device
# node properties hidapi reads (cfgmgr32), with its instance ID, product
# and serial number, and talk to it.  Then the
# 64-bit run says "[HID] step 1", the test unplugs the device, its
# handle's reads must fail and Raw Input must announce the removal.


def _unplug(nova):
    r = nova.qmp.cmd('device_del', id='hiddev')
    if 'error' in r:
        print('device_del:', r['error'], flush=True)


TESTS = [
    Test('hid class x86', r'C:\Programs\x86\hidclasstest.exe', [r'hidclasstest: \d+ passed, 0 failed'], timeout=300,
         boot_expect=[r'\[HID\] \\Driver\\HidUsb registered as a HID minidriver',
                      r'\[HIDUSB\] 1209:0008: interface 0, report descriptor \d+ bytes, interrupt IN and OUT',
                      r'\[HID\] 1209:0008 collection 0: usage ff00:0001']),
    Test('hidapi', 'hidapitest', [r'hidapitest: \d+ passed, 0 failed'], timeout=300),
    Test('hidapi x86', r'C:\Programs\x86\hidapitest.exe', [r'hidapitest: \d+ passed, 0 failed'], timeout=300),
    Test('hid class', 'hidclasstest unplug', [r'hidclasstest unplug: \d+ passed, 0 failed'], timeout=300,
         acts=[(r'\[HID\] step 1:', _unplug)]),
]
