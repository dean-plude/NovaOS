# composite: a composite USB device on the xHCI controller,
# tools/compositepeer.py behind a QEMU usb-redir device (port 10715):
# 1209:000A with a boot keyboard on interface 0 and two vendor-defined
# HID functions on interfaces 1 and 2.  As Windows' usbccgp.sys does, each
# interface is a node of its own (USB\VID_1209&PID_000A&MI_nn): NovaOS's
# keyboard driver takes interface 0, and Plug and Play gives each of the
# other two a PDO of its own (kernel/nt/usbd.c) with the HID class driver
# on its USB minidriver, which must see only its own interface.
# usbcompositetest checks the device tree and both functions' reports
# (32-bit first), Device Manager lists the device and each interface as
# working, then the 64-bit run says "[USB] composite step 1", the test
# unplugs the device and both functions must go.


def _unplug(nova):
    r = nova.qmp.cmd('device_del', id='combo')
    if 'error' in r:
        print('device_del:', r['error'], flush=True)


_WORKING = r'[^\n]*\] This device is working properly\.'

TESTS = [
    Test('usb composite x86', r'C:\Programs\x86\usbcompositetest.exe', [r'usbcompositetest: \d+ passed, 0 failed'],
         timeout=300,
         boot_expect=[r'\[USBD\] [^\n]*: \\Device\\USBPDO-\d+ for 1209:000a function \(interfaces 0x2\)',
                      r'\[USBD\] [^\n]*: \\Device\\USBPDO-\d+ for 1209:000a function \(interfaces 0x4\)',
                      r'\[HIDUSB\] 1209:000a: interface 1, report descriptor \d+ bytes, interrupt IN and OUT',
                      r'\[HIDUSB\] 1209:000a: interface 2, report descriptor \d+ bytes, interrupt IN and OUT']),
    Test('usb composite device manager', 'devmgmt --check',
         [r'devmgmt: \d+ passed, 0 failed',
          r'devmgmt: Universal Serial Bus controllers: [^\n]*\[USB\\VID_1209&PID_000A\\' + _WORKING,
          r'devmgmt: [^\n]*\[USB\\VID_1209&PID_000A&MI_00\\' + _WORKING,
          r'devmgmt: Human Interface Devices: [^\n]*\[USB\\VID_1209&PID_000A&MI_01\\' + _WORKING,
          r'devmgmt: Human Interface Devices: [^\n]*\[USB\\VID_1209&PID_000A&MI_02\\' + _WORKING,
          r'devmgmt: Keyboards: [^\n]*\[HID\\VID_1209&PID_000A&MI_00\\' + _WORKING,
          r'devmgmt: [^\n]*\[HID\\VID_1209&PID_000A&MI_01\\' + _WORKING,
          r'devmgmt: [^\n]*\[HID\\VID_1209&PID_000A&MI_02\\' + _WORKING],
         timeout=120),
    Test('usb composite unplug', 'usbcompositetest unplug', [r'usbcompositetest unplug: \d+ passed, 0 failed'],
         timeout=300, acts=[(r'\[USB\] composite step 1:', _unplug)]),
]
