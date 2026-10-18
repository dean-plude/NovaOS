# bluetooth: NovaOS's Bluetooth stack (step 28.2).  QEMU has no Bluetooth
# radio, so tools/btpeer.py plays one behind a QEMU usb-redir device (port
# 10718, its control port 10818): 1209:000d "NovaOS Test Bluetooth Radio",
# a USB HCI radio as Intel's are (class E0/01/01: HCI commands as class
# requests, events on interrupt IN, ACL data on bulk), with two devices in
# range: "NovaOS Test Mouse" (Secure Simple Pairing, a HID service with a
# mouse's report descriptor and a PnP record, 1209:000e) and "NovaOS Test
# Headset" (legacy pairing with PIN 0000, hands-free and audio sink
# records).  Plug and Play gives the radio bthusb (kernel/nt/bthusb.c),
# which runs the host stack (kernel/nt/bthport.c) on it: the radio comes
# up (reset, version, features, buffer sizes, address, local name, page
# scan) and registers its interface.  bttest (bthprops.cpl's API) checks
# the radio, inquires (both devices with their names and classes), pairs
# both (link keys stored in BTHPORT\Parameters\Keys), reads their services
# by SDP, turns the mouse's HID service on and the headset's hands-free one
# down (not supported yet): the mouse gets a BTHENUM node, hidbth
# (kernel/nt/hidbth.c) opens its HID control and interrupt channels, and
# moving it moves the cursor.  After a restart the radio
# knows both devices from the registry and the mouse reconnects by its
# link key without pairing again.  Then btfilter.sys (tools/drivertest/
# btfilter.c), laid out as Intel's ibtusb.sys is (Include = bth.inf, Needs
# = BthUsb.NT..., a LowerFilters filter under bthusb), is installed with
# pnputil: the radio restarts with it under bthusb, every HCI command
# passing through it, and the mouse works again.  Last, removing both
# devices forgets them and the mouse's node goes.
import socket
import time

PORT = 10718

DOC = ('a USB Bluetooth radio (tools/btpeer.py behind usb-redir) with a mouse and a headset in range: bthusb and '
       'NovaOS\'s host stack bring it up, bttest inquires, pairs both (Secure Simple Pairing and PIN 0000), reads '
       'their services by SDP and turns the mouse\'s HID service on, which moves the cursor through hidbth; after a '
       'restart the mouse reconnects by its stored link key; a lower filter packaged as Intel\'s ibtusb is '
       '(Include = bth.inf) installs with pnputil under bthusb and sees the HCI traffic; removing the devices forgets '
       'them')


def _move(nova):
    """Moves the mouse through btpeer's control port until its interrupt
    channel is open"""
    for _ in range(40):
        try:
            with socket.create_connection(('127.0.0.1', PORT + 100), timeout=10) as s:
                f = s.makefile('rw')
                f.write('dx=40 dy=30\n')
                f.flush()
                if f.readline().strip() == 'ok':
                    time.sleep(0.2)
                    f.write('dx=40 dy=30\n')
                    f.flush()
                    f.readline()
                    return
        except OSError:
            pass
        time.sleep(0.25)
    print('btpeer: the mouse never connected', flush=True)


TESTS = [
    Test('bluetooth', 'bttest', [r'bttest: \d+ passed, 0 failed'], timeout=300, elevated=True,
         acts=[(r'\[BT\] step: move', _move)],
         boot_expect=[r'\[BTHUSB\] 1209:000d: HCI events on interrupt IN \(64 bytes\), ACL on bulk IN and OUT',
                      r'\[BTH\] radio 4E:4F:56:41:00:00 up: HCI \d+\.\d+, LMP \d+, manufacturer ffff']),
    Test('bluetooth test signing on', r'reg add HKLM\SYSTEM\CurrentControlSet\Control\NovaOS /v TestSigning /t REG_DWORD /d 1 /f',
         [r'completed successfully'], settle=2, elevated=True),
    Test('bluetooth restart', 'shutdown /r /t 0', [r'\[NT\] Test signing on'], reboot=True),
    # (the radio comes up as the desktop does: what it logged is checked
    # once bttest has waited for it)
    Test('bluetooth after restart', 'bttest after', [r'bttest after: \d+ passed, 0 failed'], timeout=300,
         acts=[(r'\[BT\] step: move', _move)],
         boot_expect=[r'\[BTH\] paired before: 4E:4F:56:41:00:01 "NovaOS Test Mouse", HID on',
                      r'\[BTH\] paired before: 4E:4F:56:41:00:02 "NovaOS Test Headset"',
                      r'\[HIDBTH\] NovaOS Test Mouse: connected']),
    Test('bluetooth vendor filter', r'pnputil /add-driver C:\Tests\BtFilter\btfilter.inf /install',
         [r'Driver package installed on device: USB\\VID_1209&PID_000D',
          r'\[DbgPrint\] btfilter: AddDevice, under the radio\'s function driver',
          r'\[PNP\] USB\\VID_1209&PID_000D\\NOVA0BT0: filter driver btfilter added'], timeout=180, elevated=True),
    Test('bluetooth through the filter', 'bttest after', [r'bttest after: \d+ passed, 0 failed'], timeout=300,
         acts=[(r'\[BT\] step: move', _move)],
         boot_expect=[r'\[DbgPrint\] btfilter: HCI command 0c03 \(1 so far\)',
                      r'\[DbgPrint\] btfilter: HCI command 0c24 \(8 so far\)']),
    Test('bluetooth remove', 'bttest remove', [r'bttest remove: \d+ passed, 0 failed'], timeout=120, elevated=True),
]
