# digitest: a USB touch screen, pen and precision touchpad the Windows way
# (tools/padpeer.py --kind digitizer behind a usb-redir device, port
# 10717).  The device runs on HidUsb under the HID class driver; its
# digitizer collections on hiddigi, its mouse on mouhid
# (kernel/nt/kbdhid.c).  hiddigi puts the touchpad in touchpad mode at
# boot; the test touches the screen, uses the pen, moves a finger on the
# touchpad, taps it, scrolls with two fingers, taps and drags, flicks two
# fingers so the scrolling coasts, stops a coast with a touch and unplugs
# the device.
import socket
import time

PORT = 10717


def _send(*lines):
    """Reports through the digitizer's control port, one line at a time"""
    def act(nova):
        try:
            with socket.create_connection(('127.0.0.1', PORT + 100), timeout=10) as s:
                f = s.makefile('rw')
                for line in lines:
                    if isinstance(line, float):
                        time.sleep(line)
                        continue
                    f.write(line + '\n')
                    f.flush()
                    reply = f.readline().strip()
                    if reply != 'ok':
                        print(f'padpeer {PORT}: {reply}', flush=True)
                    time.sleep(0.05)
        except OSError as e:
            print(f'padpeer {PORT}: {e}', flush=True)
    return act


def _unplug(nova):
    r = nova.qmp.cmd('device_del', id='digi')
    if 'error' in r:
        print('device_del:', r['error'], flush=True)


# The touchpad: a finger 40 mm right (X 200 to 600 of 1000 over 10 cm), in 4 mm steps
_RIGHT = tuple(f'pad=1:{200 + 40 * i}:500' for i in range(11)) + ('pad=0:600:500',)
# Two fingers 40 mm down
_DOWN = tuple(f'pad=1:400:{200 + 40 * i}/1:600:{200 + 40 * i}' for i in range(11)) + ('pad=0:400:600/0:600:600',)
# A tap, then the finger down again at once and 40 mm right
_TAP_DRAG = ('pad=1:200:500 pad=0:200:500',) + _RIGHT
# Two fingers flicking 30 mm down (10 notches) and lifting while they move
_FLICK = tuple(f'pad=1:400:{200 + 100 * i}/1:600:{200 + 100 * i}' for i in range(4)) + ('pad=0:400:500/0:600:500',)
# ... then a finger touching 0.1 s later and moving 16 mm right
_STOP = _FLICK + (0.1,) + tuple(f'pad=1:{500 + 40 * i}:500' for i in range(5)) + ('pad=0:660:500',)

TESTS = [
    Test('usb digitizer class driver', 'digitest',
         [r'digitest: \d+ passed, 0 failed',
          r'\[HIDDIGI\] 1209:000b: first input report', r'\[HIDDIGI\] 1209:000b: removed'],
         timeout=240,
         boot_expect=[r'\[HIDDIGI\] 1209:000b: multi-touch screen, input reports \d+ bytes',
                      r'\[HIDDIGI\] 1209:000b: pen tablet, input reports \d+ bytes',
                      r'\[HIDDIGI\] 1209:000b: precision touchpad, input reports \d+ bytes',
                      r'\[HIDDIGI\] 1209:000b: precision touchpad configuration: touchpad mode on',
                      r'\[MOUHID\] 1209:000b: touchpad \(mouse mode\), input reports \d+ bytes'],
         acts=[(r'digitest step 1:', _send('touch=1:8192:16384', 0.3, 'touch=0:8192:16384')),
               (r'digitest step 2:', _send('pen=16:24575:16384', 0.2, 'pen=17:24575:16384:512', 0.3,
                                           'pen=16:24575:16384:0', 0.2, 'pen=0:24575:16384:0')),
               (r'digitest step 3:', _send(*_RIGHT)),
               (r'digitest step 4:', _send('pad=1:500:500 pad=0:500:500')),
               (r'digitest step 5:', _send(*_DOWN)),
               (r'digitest step 6:', _send(*_TAP_DRAG)),
               (r'digitest step 7:', _send(*_FLICK)),
               (r'digitest step 8:', _send(*_STOP)),
               (r'digitest step 9:', _unplug)]),
]
