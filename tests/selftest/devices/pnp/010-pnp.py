# pnp: Plug and Play.  An xHCI controller with nothing plugged in at boot;
# pnptest lists the device tree (the root, the PCI bus and functions, the
# controller's root hub), then the test plugs a USB keyboard into port 1
# and a USB hub into port 2 with a USB stick behind it (QEMU's usb-kbd,
# usb-hub and usb-storage, through the QMP monitor) and pnptest waits for
# their nodes to arrive: the keyboard as a USB input device with a
# Keyboard-class HID child, the stick as a DiskDrive-class USBSTOR child
# of its mass-storage function.  Unplugging the hub takes the stick's
# nodes away with it, unplugging the keyboard takes its own; both stay
# behind as devices not present (Windows' phantoms).
import os
import re
import time


def _qmp(nova, cmd, **args):
    r = nova.qmp.cmd(cmd, **args)
    if 'error' in r:
        print(f'{cmd}:', r['error'], flush=True)


def _plug_keyboard(nova):
    """(QEMU sends its keys to the newest keyboard: the test types the
    command once NovaOS has taken it, or the first keys are lost)"""
    _qmp(nova, 'device_add', driver='usb-kbd', id='kbd1', bus='xhci.0', port='1')
    for _ in range(240):
        if re.search(r'\[PNP\] arrival: HID\\VID_0627', open(nova.serial_path, 'rb').read().decode('latin-1')):
            break
        time.sleep(0.25)
    time.sleep(1)


def _plug_hub_and_stick(nova):
    stick = os.path.join(nova.work, 'pnpstick.img')
    with open(stick, 'wb') as f:
        f.truncate(64 << 20)
    _qmp(nova, 'blockdev-add', driver='raw', **{'node-name': 'pnpstick'},
         file={'driver': 'file', 'filename': stick})
    _qmp(nova, 'device_add', driver='usb-hub', id='hub1', bus='xhci.0', port='2')
    _qmp(nova, 'device_add', driver='usb-storage', id='stick1', drive='pnpstick', bus='xhci.0', port='2.1')


def _unplug(dev):
    def act(nova):
        _qmp(nova, 'device_del', id=dev)
    return act


TESTS = [
    Test('pnp list', 'pnptest list',
         [r'HTREE\\ROOT\\0', r'ACPI\\PNP0A08\\0 \[System\] PCI Express Root Complex \(pci\)',
          r'PCI\\VEN_1B36&DEV_000D&SUBSYS_[0-9A-F]{8}\\3&0&0&[0-9A-F]{2} \[USB\] USB xHCI Compliant Host Controller \(xHCI\)',
          r'USB\\ROOT_HUB30\\4&1&0&0 \[USB\] USB Root Hub \(USB 3\.0\) \(usb\)',
          r'pnptest: \d+ devices present, 0 not present, 0 failed']),
    Test('pnp keyboard arrival', r'pnptest arrival HID\VID_0627',
         [r'arrival: HID\\VID_0627&PID_0001\\[^ ]+&0000 \[Keyboard\] HID Keyboard Device \(usbhid\)',
          r'arrival: USB\\VID_0627&PID_0001\\[^ ]+ \[HIDClass\] USB Input Device \(usbhid\)'],
         before=_plug_keyboard),
    Test('pnp stick arrival', r'pnptest arrival USBSTOR\Disk',
         [r'arrival: USB\\VID_0409&PID_55AA\\[^ ]+ \[USB\] Generic USB Hub \(usbhub\)',
          r'arrival: USB\\VID_46F4&PID_0001\\[^ ]+ \[USB\] USB Mass Storage Device \(usbmsc\)',
          r'arrival: USBSTOR\\Disk&Ven_&Prod_[^ ]+ \[DiskDrive\] .* USB Device \(usbmsc\)'],
         before=_plug_hub_and_stick),
    Test('pnp hub removal', r'pnptest removal USB\VID_0409',
         [r'removal: USBSTOR\\Disk', r'removal: USB\\VID_46F4&PID_0001', r'removal: USB\\VID_0409&PID_55AA'],
         before=_unplug('hub1')),
    Test('pnp keyboard removal', r'pnptest removal USB\VID_0627',
         [r'removal: HID\\VID_0627&PID_0001', r'removal: USB\\VID_0627&PID_0001'],
         before=_unplug('kbd1')),
    Test('pnp phantoms', 'pnptest list', [r'pnptest: \d+ devices present, 5 not present, 0 failed']),
]
