#!/usr/bin/env python3
"""A composite USB device for testing NovaOS, as a QEMU usb-redir device.

    tools/compositepeer.py --port 10715

A composite device has several functions behind one address, each its own
interface: a keyboard with a vendor-defined control interface (for its
lights or macros), a headset with audio and HID.  Windows gives each
function a node and a driver of its own (usbccgp.sys); this program is
such a device, behind QEMU's usb-redir device as tools/padpeer.py's
controllers are:

    -chardev socket,id=combo,host=127.0.0.1,port=10715
    -device usb-redir,chardev=combo,bus=xhci.0

It is, at full speed, 1209:000A ("NovaOS Test Composite Device", serial
NOVA0003, device class 0: each interface says what it is) with three
interfaces:

  0  a boot keyboard (class 3/1/1, interrupt IN 0x81): HidUsb runs it under
     the HID class driver, its keyboard collection on kbdhid; it never
     sends a key (its LED reports are ignored);
  1  a vendor-defined HID function, usage page FF00 usage 1 (interrupt
     IN 0x82, OUT 0x02);
  2  a vendor-defined HID function, usage page FF00 usage 2 (interrupt
     IN 0x83, OUT 0x03).

Interfaces 1 and 2 are tools/padpeer.py's vendor device each (input
report 1, output report 2, feature report 3 of 8 bytes): an output report
(on its OUT endpoint, or SET_REPORT to its interface) comes back as
input report 1 on that interface's IN endpoint only, and each keeps its
own feature report.  What the host sends is printed ("out IFACE HEX",
"set_report IFACE HEX").
"""
import argparse, os, socket, struct, sys, threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from padpeer import (HELLO, DEVICE_CONNECT, INTERFACE_INFO, EP_INFO, SET_CONFIGURATION, GET_CONFIGURATION,  # noqa: E402
                     CONFIGURATION_STATUS, SET_ALT_SETTING, GET_ALT_SETTING, ALT_SETTING_STATUS,
                     START_INTERRUPT_RECEIVING, STOP_INTERRUPT_RECEIVING, INTERRUPT_RECEIVING_STATUS,
                     CONTROL_PACKET, INTERRUPT_PACKET, CAPS, CAP_64BIT_IDS, SPEED_FULL, OK, STALL, VENDOR_REPORT, le16)

VID, PID, REV = 0x1209, 0x000A, 0x0114

KEYBOARD_REPORT = bytes([
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
])


def vendor_report(usage):
    """padpeer's vendor report descriptor with the collection's usage @usage"""
    r = bytearray(VENDOR_REPORT)
    assert r[3:5] == b'\x09\x01'
    r[4] = usage
    return bytes(r)


class Fn:
    """One interface: its descriptors and, for the vendor ones, state"""

    def __init__(self, number, sub, proto, report, ep_in, ep_out):
        self.number, self.sub, self.proto, self.report = number, sub, proto, report
        self.ep_in, self.ep_out = ep_in, ep_out
        self.feature = bytes(8)
        self.last = None              # the last input report (vendor)


FNS = [Fn(0, 1, 1, KEYBOARD_REPORT, 0x81, None),
       Fn(1, 0, 0, vendor_report(1), 0x82, 0x02),
       Fn(2, 0, 0, vendor_report(2), 0x83, 0x03)]
MPS, INTERVAL = 16, 4


def device_desc():
    return bytes([18, 1]) + le16(0x0200) + bytes([0, 0, 0, 64]) + le16(VID) + le16(PID) + le16(REV) + bytes([1, 2, 3, 1])


def config_desc():
    d = b''
    for f in FNS:
        d += bytes([9, 4, f.number, 0, 2 if f.ep_out else 1, 3, f.sub, f.proto, 0])
        d += bytes([9, 0x21]) + le16(0x0111) + bytes([0, 1, 0x22]) + le16(len(f.report))
        d += bytes([7, 5, f.ep_in, 3]) + le16(8 if f.proto else MPS) + bytes([10 if f.proto else INTERVAL])
        if f.ep_out:
            d += bytes([7, 5, f.ep_out, 3]) + le16(MPS) + bytes([INTERVAL])
    return bytes([9, 2]) + le16(9 + len(d)) + bytes([len(FNS), 1, 0, 0x80, 250]) + d


def string(i):
    if i == 0:
        return bytes([4, 3]) + le16(0x0409)
    s = {1: 'NovaOS', 2: 'NovaOS Test Composite Device', 3: 'NOVA0003'}.get(i)
    if s is None:
        return None
    u = s.encode('utf-16-le')
    return bytes([2 + len(u), 3]) + u


def fn_of_ep(ep):
    for f in FNS:
        if ep in (f.ep_in, f.ep_out):
            return f
    return None


class Conn:
    """One QEMU usb-redir connection"""

    def __init__(self, sock):
        self.sock = sock
        self.wlock = threading.Lock()
        self.ids64 = False
        self.config = 0
        self.listening = set()        # the IN endpoints the host polls
        self.held = {}                # IN endpoint: reports kept until it does

    def send(self, ptype, hdr=b'', data=b'', pid=0):
        h = struct.pack('<II', ptype, len(hdr) + len(data)) + struct.pack('<Q' if self.ids64 else '<I', pid)
        with self.wlock:
            self.sock.sendall(h + hdr + data)

    def recv_exact(self, n):
        buf = b''
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise EOFError
            buf += chunk
        return buf

    def send_info(self):
        n = len(FNS)
        cols = [[f.number for f in FNS], [3] * n, [f.sub for f in FNS], [f.proto for f in FNS]]
        self.send(INTERFACE_INFO, struct.pack('<I', n) + b''.join(bytes(c + [0] * (32 - n)) for c in cols))
        types, intervals, ifs, mps = [255] * 32, [0] * 32, [0] * 32, [0] * 32
        eps = [(0x00, 0, 0, 0, 64), (0x80, 0, 0, 0, 64)]
        if self.config:
            for f in FNS:
                eps.append((f.ep_in, 3, 10 if f.proto else INTERVAL, f.number, 8 if f.proto else MPS))
                if f.ep_out:
                    eps.append((f.ep_out, 3, INTERVAL, f.number, MPS))
        for addr, t, iv, iface, m in eps:
            i = ((addr & 0x80) >> 3) | (addr & 0x0F)
            types[i], intervals[i], ifs[i], mps[i] = t, iv, iface, m
        self.send(EP_INFO, bytes(types) + bytes(intervals) + bytes(ifs) + struct.pack('<32H', *mps))

    def input_report(self, f, data):
        """Input report 1 with @data on @f's IN endpoint (held until the host polls it)"""
        r = bytes([1]) + data
        f.last = data
        if f.ep_in not in self.listening:
            self.held.setdefault(f.ep_in, []).append(r)
            return
        self.send(INTERRUPT_PACKET, struct.pack('<BBH', f.ep_in, OK, len(r)), r)

    def set_report(self, f, value, data):
        if f.proto:                    # (the keyboard: its lights)
            return
        if value == 0x0303 and len(data) == 9 and data[0] == 3:
            f.feature = data[1:]
        elif (value >> 8) == 2 and len(data) == 9 and data[0] == 2:
            self.input_report(f, data[1:])

    def control(self, pid, hdr, data):
        ep, req, rtype, _, value, index, length = struct.unpack('<BBBBHHH', hdr)
        reply, status = b'', OK
        f = FNS[index & 0xFF] if rtype & 0x1F == 1 and (index & 0xFF) < len(FNS) else None
        if rtype in (0x80, 0x81) and req == 6:             # GET_DESCRIPTOR
            kind, i = value >> 8, value & 0xFF
            reply = (device_desc() if kind == 1 else config_desc() if kind == 2 else string(i) if kind == 3 else
                     f.report if kind == 0x22 and f else None)
            if reply is None:
                reply, status = b'', STALL
        elif rtype & 0x80 and req == 0:                    # GET_STATUS
            reply = b'\0\0'
        elif rtype == 0xA1 and req == 1 and f and not f.proto:   # GET_REPORT
            if value == 0x0303:
                reply = bytes([3]) + f.feature
            elif value == 0x0101:
                reply = bytes([1]) + (f.last or bytes(8))
            else:
                reply, status = b'', STALL
        elif rtype & 0x80:
            reply = bytes(length)
        elif rtype == 0x21 and req == 9 and f:             # SET_REPORT
            print(f'compositepeer: set_report {f.number} {data.hex()}', flush=True)
            self.set_report(f, value, data)
        if rtype & 0x80:
            reply = reply[:length]
        self.send(CONTROL_PACKET, struct.pack('<BBBBHHH', ep, req, rtype, status, value, index,
                                              len(reply) if rtype & 0x80 else length), reply, pid)

    def run(self):
        self.send(HELLO, b'novaos compositepeer'.ljust(64, b'\0') + struct.pack('<I', CAPS))
        connected = False
        while True:
            hlen = 16 if self.ids64 else 12
            h = self.recv_exact(hlen)
            ptype, length = struct.unpack('<II', h[:8])
            pid = struct.unpack('<Q' if self.ids64 else '<I', h[8:])[0]
            body = self.recv_exact(length)
            if ptype == HELLO:
                caps = struct.unpack('<I', body[64:68])[0] if len(body) >= 68 else 0
                self.ids64 = bool(caps & CAP_64BIT_IDS)
                if not connected:
                    connected = True
                    self.send_info()
                    self.send(DEVICE_CONNECT, struct.pack('<BBBBHHH', SPEED_FULL, 0, 0, 0, VID, PID, REV))
            elif ptype == CONTROL_PACKET:
                self.control(pid, body[:10], body[10:])
            elif ptype == SET_CONFIGURATION:
                self.config = body[0]
                self.send_info()
                self.send(CONFIGURATION_STATUS, bytes([OK, self.config]), pid=pid)
            elif ptype == GET_CONFIGURATION:
                self.send(CONFIGURATION_STATUS, bytes([OK, self.config]), pid=pid)
            elif ptype == SET_ALT_SETTING:
                self.send(ALT_SETTING_STATUS, bytes([OK if body[1] == 0 else STALL, body[0], body[1]]), pid=pid)
            elif ptype == GET_ALT_SETTING:
                self.send(ALT_SETTING_STATUS, bytes([OK, body[0], 0]), pid=pid)
            elif ptype == START_INTERRUPT_RECEIVING:
                self.send(INTERRUPT_RECEIVING_STATUS, bytes([OK, body[0]]), pid=pid)
                if body[0] not in self.listening:
                    self.listening.add(body[0])
                    held = self.held.pop(body[0], [])
                    print(f'compositepeer: listening on {body[0]:02x} ({len(held)} held)', flush=True)
                    for r in held:
                        self.send(INTERRUPT_PACKET, struct.pack('<BBH', body[0], OK, len(r)), r)
            elif ptype == STOP_INTERRUPT_RECEIVING:
                self.listening.discard(body[0])
                self.send(INTERRUPT_RECEIVING_STATUS, bytes([OK, body[0]]), pid=pid)
            elif ptype == INTERRUPT_PACKET:
                ep, st, n = struct.unpack('<BBH', body[:4])
                data = body[4:4 + n]
                f = fn_of_ep(ep)
                print(f'compositepeer: out {f.number if f else "?"} {data.hex()}', flush=True)
                self.send(INTERRUPT_PACKET, struct.pack('<BBH', ep, OK, len(data)), pid=pid)
                if f:
                    self.set_report(f, 0x0202, data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, required=True)
    a = ap.parse_args()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', a.port))
    srv.listen(1)
    print(f'compositepeer: listening on {a.port}', flush=True)
    while True:
        s, _ = srv.accept()
        print('compositepeer: QEMU connected', flush=True)
        try:
            Conn(s).run()
        except (EOFError, OSError):
            pass
        for f in FNS:
            f.last, f.feature = None, bytes(8)
        print('compositepeer: QEMU went away', flush=True)
        s.close()


if __name__ == '__main__':
    sys.exit(main())
