# devmgr: USB keyboards and mice the Windows way.  The boot's own USB
# keyboard and tablet run on HidUsb under the HID class driver, their
# collections on the keyboard and mouse class drivers (kbdhid, mouhid:
# kernel/nt/kbdhid.c); the commands of every devices boot with a USB
# keyboard are typed through them.  kbdmoutest asks for a second keyboard
# and tablet, which must come up the same way and stay out of Raw Input's
# HID devices; the test moves the new tablet and types on the new keyboard
# (QEMU sends input to the newest), the pointer and the line must arrive,
# and both are unplugged again.
import re, time


def _count(nova, pattern):
    return len(re.findall(pattern, open(nova.serial_path, 'rb').read().decode('latin-1')))


def _plug(nova):
    kbd = _count(nova, r'\[KBDHID\] 0627:0001: keyboard')
    mou = _count(nova, r'\[MOUHID\] 0627:0001: absolute pointer')
    nova.qmp.cmd('device_add', driver='usb-kbd', id='kbdx', bus='xhci.0')
    nova.qmp.cmd('device_add', driver='usb-tablet', id='tabx', bus='xhci.0')
    for _ in range(240):
        if (_count(nova, r'\[KBDHID\] 0627:0001: keyboard') > kbd and
                _count(nova, r'\[MOUHID\] 0627:0001: absolute pointer') > mou):
            break
        time.sleep(0.25)
    time.sleep(1)


def _move(nova):
    for _ in range(3):                        # (the middle of the width, a quarter of the height)
        nova.qmp.cmd('input-send-event', events=[{'type': 'abs', 'data': {'axis': 'x', 'value': 16384}},
                                                 {'type': 'abs', 'data': {'axis': 'y', 'value': 8192}}])
        time.sleep(0.3)


def _type(nova):
    time.sleep(0.5)
    nova.qmp.type('nova\n')


def _unplug(nova):
    nova.qmp.cmd('device_del', id='kbdx')
    nova.qmp.cmd('device_del', id='tabx')


TESTS = [
    Test('usb keyboard and mouse class drivers', 'kbdmoutest',
         [r'kbdmoutest: \d+ passed, 0 failed',
          r'\[KBDHID\] 0627:0001: first input report', r'\[MOUHID\] 0627:0001: first input report',
          r'\[KBDHID\] 0627:0001: removed', r'\[MOUHID\] 0627:0001: removed'],
         timeout=180,
         boot_expect=[r'\[USB\] [^\n]*: interface 0: a HID keyboard or mouse, for the HID class driver',
                      r'\[KBDHID\] 0627:0001: keyboard, input reports \d+ bytes, wakes the machine',
                      r'\[MOUHID\] 0627:0001: absolute pointer, input reports \d+ bytes'],
         acts=[(r'\[HID\] kbdmou step 1:', _plug), (r'\[HID\] kbdmou step 2:', _move),
               (r'\[HID\] kbdmou step 3:', _type), (r'\[HID\] kbdmou step 4:', _unplug)]),
]
