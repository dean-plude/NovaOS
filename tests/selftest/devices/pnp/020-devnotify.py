# pnp: what Windows programs hear of devices arriving and leaving, after
# 010.  devnotifytest checks the PlugPlay service runs and wrote the
# device tree into HKLM\SYSTEM\CurrentControlSet\Enum, registers windows
# (RegisterDeviceNotification) and callbacks (CM_Register_Notification),
# and at "[PNP] step 1" the test plugs a USB keyboard and a FAT-formatted
# USB stick into the xHCI controller, at "[PNP] step 2" takes them out:
# WM_DEVICECHANGE's interface arrivals and removals, the stick's drive
# letter (DEV_BROADCAST_VOLUME), DBT_DEVNODES_CHANGED and the callbacks
# must all come.
import os
import subprocess


def _qmp(nova, cmd, **args):
    r = nova.qmp.cmd(cmd, **args)
    if 'error' in r:
        print(f'{cmd}:', r['error'], flush=True)


def _plug(nova):
    stick = os.path.join(nova.work, 'fatstick.img')
    if os.path.exists(stick):
        os.unlink(stick)
    subprocess.run(['mkfs.fat', '-F', '32', '-n', 'PNPSTICK', '-C', stick, str(65536)], check=True,
                   stdout=subprocess.DEVNULL)
    _qmp(nova, 'blockdev-add', driver='raw', **{'node-name': 'fatstick'},
         file={'driver': 'file', 'filename': stick})
    _qmp(nova, 'device_add', driver='usb-kbd', id='kbd2', bus='xhci.0', port='1')
    _qmp(nova, 'device_add', driver='usb-storage', id='stick2', drive='fatstick', bus='xhci.0', port='2')


def _unplug(nova):
    _qmp(nova, 'device_del', id='kbd2')
    _qmp(nova, 'device_del', id='stick2')


TESTS = [
    Test('pnp device notifications', 'devnotifytest', [r'devnotifytest: \d+ passed, 0 failed'], timeout=240,
         boot_expect=[r'\[PNP\] PlugPlay service running: \d+ devices'],
         acts=[(r'\[PNP\] step 1:', _plug), (r'\[PNP\] step 2:', _unplug)]),
]
