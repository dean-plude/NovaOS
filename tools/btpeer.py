#!/usr/bin/env python3
"""A USB Bluetooth radio, with two Bluetooth devices in range, for testing
NovaOS, as a QEMU usb-redir device.

    tools/btpeer.py --port 10718 [--ctl PORT]

QEMU has no Bluetooth radio of its own any more (its old usb-bt-dongle
went in QEMU 5), so this program is one, behind QEMU's usb-redir device
(the usbredir protocol, as tools/padpeer.py speaks it):

    -chardev socket,id=bt,host=127.0.0.1,port=10718
    -device usb-redir,chardev=bt,bus=xhci.0

It is, at full speed, a USB Bluetooth radio as the Bluetooth Core
Specification's USB transport (Vol 4, Part B) lays one out, the way Intel's
AX211 combo card shows its Bluetooth half: device class E0/01/01
(1209:000d, "NovaOS Test Bluetooth Radio"), interface 0 with HCI events on
interrupt IN endpoint 0x81 and ACL data on bulk endpoints 0x82 (IN) and
0x02 (OUT), interface 1 for SCO (isochronous 0x83/0x03, setting 0 without
bandwidth); HCI commands arrive as class requests to the device on the
control endpoint.  It answers the commands a host sends to bring a radio
up and use it (Vol 4, Part E, 7): Reset, the local version, features,
buffer sizes and address, the event mask, name, class of device, scan
and inquiry modes and Simple Pairing, then Inquiry, Remote Name Request,
Create Connection, Authentication Requested, Set Connection Encryption
and Disconnect, and the replies a host gives the controller's pairing
questions (link key, PIN code, IO capability, user confirmation).

In range, as the radio's inquiry finds them, are:

  4E:4F:56:41:00:01  "NovaOS Test Mouse": a mouse (class of device
                     0x002580) that pairs by Secure Simple Pairing,
                     "Just Works", with an SDP server listing a HID
                     service (0x1124: report descriptor of three buttons,
                     X, Y and a wheel) and a PnP Information record
                     (VID 1209 from the USB list, PID 000e), and HID's two
                     L2CAP channels (PSM 0x11 control, 0x13 interrupt);
  4E:4F:56:41:00:02  "NovaOS Test Headset": a headset (0x240404) that pairs
                     the legacy way with PIN 0000 and lists Hands-Free and
                     Audio Sink services (no audio yet: step 28.2's next
                     slice).

The controller half is emulated here, and so is each device's own host
half: its L2CAP signalling (connection, configuration, disconnection),
its SDP server (ServiceSearchAttribute) and the mouse's HID channels.  A
pairing makes a link key the radio gives the host (Link Key
Notification) and keeps; a later connection's Link Key Request must be
answered with that key, or authentication fails (0x06), as a real device
that lost or never had the pairing refuses.

The test drives the mouse through --ctl (default --port + 100), a TCP port
that takes lines of name=value pairs and answers "ok" once the report is
sent on the HID interrupt channel ("error not connected" when it is not
open): buttons (bit 0 left, 1 right, 2 middle), dx, dy (-127..127),
wheel.  What happens is printed: commands by name, "inquiry", "paired
ADDR (ssp|pin)", "connected ADDR", "l2cap PSM open", "sdp ADDR",
"hid open", "disconnected ADDR".
"""
import argparse, os, socket, struct, sys, threading

HELLO, DEVICE_CONNECT, RESET, INTERFACE_INFO, EP_INFO = 0, 1, 3, 4, 5
SET_CONFIGURATION, GET_CONFIGURATION, CONFIGURATION_STATUS = 6, 7, 8
SET_ALT_SETTING, GET_ALT_SETTING, ALT_SETTING_STATUS = 9, 10, 11
START_INTERRUPT_RECEIVING, STOP_INTERRUPT_RECEIVING, INTERRUPT_RECEIVING_STATUS = 15, 16, 17
CANCEL_DATA_PACKET = 21
CONTROL_PACKET, BULK_PACKET, INTERRUPT_PACKET = 100, 101, 103
CAPS = (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6)
CAP_64BIT_IDS, CAP_32BIT_BULK = 1 << 5, 1 << 6
SPEED_FULL = 1
OK, CANCELLED, STALL = 0, 1, 4
EP_EVT, EP_ACL_IN, EP_ACL_OUT = 0x81, 0x82, 0x02
VID, PID = 0x1209, 0x000D
EVT_MPS, ACL_MPS = 64, 64
ACL_LEN, ACL_PKTS = 310, 8
RADIO_ADDR = bytes([0x00, 0x00, 0x41, 0x56, 0x4F, 0x4E])      # 4E:4F:56:41:00:00, little-endian as HCI sends it

MOUSE_REPORT = bytes([
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
    0x75, 0x05, 0x95, 0x01, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
])


def addr_text(a):
    return ':'.join(f'{b:02X}' for b in reversed(a))


def log(text):
    print('btpeer: ' + text, flush=True)


# ---- SDP data elements (Core Vol 3, Part B, 3) ----

def de_uint(v, n):
    return bytes([0x08 | {1: 0, 2: 1, 4: 2}[n]]) + v.to_bytes(n, 'big')


def de_uuid16(v):
    return bytes([0x19]) + v.to_bytes(2, 'big')


def de_bool(v):
    return bytes([0x28, 1 if v else 0])


def de_str(s):
    b = s if isinstance(s, bytes) else s.encode()
    return (bytes([0x25, len(b)]) if len(b) < 256 else bytes([0x26]) + len(b).to_bytes(2, 'big')) + b


def de_seq(*items):
    b = b''.join(items)
    return (bytes([0x35, len(b)]) if len(b) < 256 else bytes([0x36]) + len(b).to_bytes(2, 'big')) + b


def record(attrs):
    """A service record: attribute ID and value pairs in ID order"""
    return de_seq(*[de_uint(k, 2) + attrs[k] for k in sorted(attrs)])


def proto(*layers):
    return de_seq(*[de_seq(*layer) for layer in layers])


def mouse_records():
    hid = {
        0x0000: de_uint(0x00010000, 4),
        0x0001: de_seq(de_uuid16(0x1124)),
        0x0004: proto([de_uuid16(0x0100), de_uint(0x11, 2)], [de_uuid16(0x0011)]),
        0x0005: de_seq(de_uuid16(0x1002)),
        0x0009: de_seq(de_seq(de_uuid16(0x1124), de_uint(0x0101, 2))),
        0x000D: de_seq(proto([de_uuid16(0x0100), de_uint(0x13, 2)], [de_uuid16(0x0011)])),
        0x0100: de_str('NovaOS Test Mouse'),
        0x0201: de_uint(0x0111, 2),
        0x0202: de_uint(0x80, 1),                  # (subclass: pointing device)
        0x0203: de_uint(0, 1),
        0x0204: de_bool(True),                     # (virtual cable)
        0x0205: de_bool(True),                     # (reconnect initiate)
        0x0206: de_seq(de_seq(de_uint(0x22, 1), de_str(MOUSE_REPORT))),
        0x0207: de_seq(de_seq(de_uint(0x0409, 2), de_uint(0x0100, 2))),
        0x020E: de_bool(True),                     # (boot device)
    }
    pnp = {
        0x0000: de_uint(0x00010001, 4),
        0x0001: de_seq(de_uuid16(0x1200)),
        0x0004: proto([de_uuid16(0x0100), de_uint(0x01, 2)], [de_uuid16(0x0001)]),
        0x0200: de_uint(0x0103, 2),
        0x0201: de_uint(VID, 2),
        0x0202: de_uint(0x000E, 2),
        0x0203: de_uint(0x0100, 2),
        0x0204: de_bool(True),
        0x0205: de_uint(0x0002, 2),                # (the vendor ID is from the USB list)
    }
    return [record(hid), record(pnp)]


def headset_records():
    hfp = {
        0x0000: de_uint(0x00010000, 4),
        0x0001: de_seq(de_uuid16(0x111E), de_uuid16(0x1203)),
        0x0004: proto([de_uuid16(0x0100)], [de_uuid16(0x0003), de_uint(1, 1)]),
        0x0009: de_seq(de_seq(de_uuid16(0x111E), de_uint(0x0108, 2))),
        0x0100: de_str('Hands-Free unit'),
    }
    sink = {
        0x0000: de_uint(0x00010001, 4),
        0x0001: de_seq(de_uuid16(0x110B)),
        0x0004: proto([de_uuid16(0x0100), de_uint(0x19, 2)], [de_uuid16(0x0019), de_uint(0x0103, 2)]),
        0x0009: de_seq(de_seq(de_uuid16(0x110D), de_uint(0x0103, 2))),
        0x0100: de_str('Audio Sink'),
    }
    return [record(hfp), record(sink)]


def uuids_in(data):
    """Every 16-bit UUID in a data element (deep)"""
    out, i = set(), 0
    while i < len(data):
        t, si = data[i] >> 3, data[i] & 7
        if t == 3 and si == 1:
            out.add(int.from_bytes(data[i + 1:i + 3], 'big'))
            i += 3
        elif si < 5:
            i += 1 + (0 if t == 0 else [1, 2, 4, 8, 16][si])
        else:
            i += 1 + [1, 2, 4][si - 5]                   # (into the container)
    return out


class Remote:
    """A Bluetooth device in range"""

    def __init__(self, addr, name, cod, ssp, records, psms):
        self.addr, self.name, self.cod, self.ssp = bytes(reversed(bytes.fromhex(addr.replace(':', '')))), name, cod, ssp
        self.records, self.psms = records, psms
        self.key = None                                   # (the link key of its pairing with the radio)


REMOTES = [
    Remote('4E:4F:56:41:00:01', 'NovaOS Test Mouse', 0x002580, True, mouse_records(), (0x01, 0x11, 0x13)),
    Remote('4E:4F:56:41:00:02', 'NovaOS Test Headset', 0x240404, False, headset_records(), (0x01,)),
]


class Link:
    """An ACL connection to a remote, and its L2CAP channels"""

    def __init__(self, remote, handle):
        self.remote, self.handle = remote, handle
        self.chans = {}                                   # our CID -> [psm, the host's CID, configured both ways]
        self.next_cid = 0x40
        self.rx = b''
        self.ident = 0x80
        self.auth = False
        self.pending_auth = None                          # ('ssp'|'pin') while pairing


class Radio:
    """The controller and the remotes' hosts, behind one usbredir connection"""

    def __init__(self, conn):
        self.conn = conn
        self.links = {}                                   # handle -> Link
        self.next_handle = 0x0001
        self.scan = 0
        self.lock = threading.RLock()

    # -- events and ACL to the host --
    def event(self, code, params):
        p = bytes([code, len(params)]) + params
        for i in range(0, len(p), EVT_MPS):
            self.conn.interrupt_in(p[i:i + EVT_MPS])

    def complete(self, opcode, ret=b'\x00'):
        self.event(0x0E, bytes([1]) + struct.pack('<H', opcode) + ret)

    def status(self, opcode, st=0):
        self.event(0x0F, bytes([st, 1]) + struct.pack('<H', opcode))

    def acl(self, link, cid, payload):
        frame = struct.pack('<HH', len(payload), cid) + payload
        first = True
        for i in range(0, len(frame), ACL_LEN):
            part = frame[i:i + ACL_LEN]
            pb = 0x2 if first else 0x1
            self.conn.acl_in(struct.pack('<HH', link.handle | pb << 12, len(part)) + part)
            first = False

    def link_by_addr(self, addr):
        for link in self.links.values():
            if link.remote.addr == addr:
                return link
        return None

    # -- HCI commands --
    def command(self, pkt):
        if len(pkt) < 3:
            return
        opcode, n = struct.unpack('<HB', pkt[:3])
        p = pkt[3:3 + n]
        ogf, ocf = opcode >> 10, opcode & 0x3FF
        with self.lock:
            self._command(opcode, ogf, ocf, p)

    def _command(self, opcode, ogf, ocf, p):
        name = {0x0C03: 'reset', 0x1001: 'read local version', 0x1002: 'read local commands',
                0x1003: 'read local features', 0x1005: 'read buffer size', 0x1009: 'read bd_addr',
                0x0C01: 'set event mask', 0x0C13: 'write local name', 0x0C14: 'read local name',
                0x0C24: 'write class of device', 0x0C1A: 'write scan enable', 0x0C45: 'write inquiry mode',
                0x0C56: 'write simple pairing mode', 0x0C16: 'write connection accept timeout',
                0x0C18: 'write page timeout', 0x0401: 'inquiry', 0x0402: 'inquiry cancel',
                0x0419: 'remote name request', 0x0405: 'create connection', 0x0406: 'disconnect',
                0x0411: 'authentication requested', 0x0413: 'set connection encryption',
                0x040B: 'link key reply', 0x040C: 'link key negative reply', 0x040D: 'pin code reply',
                0x040E: 'pin code negative reply', 0x042B: 'io capability reply',
                0x042C: 'user confirmation reply', 0x042D: 'user confirmation negative reply',
                0x0C05: 'set event filter', 0x0C33: 'host buffer size'}.get(opcode, f'command {opcode:04x}')
        log(name)
        if opcode == 0x0C03:                                       # Reset
            self.links.clear()
            self.scan = 0
            self.complete(opcode)
        elif opcode == 0x1001:                                     # Read Local Version Information
            self.complete(opcode, struct.pack('<BBHBHH', 0, 0x0C, 0x0001, 0x0C, 0xFFFF, 0x0001))
        elif opcode == 0x1002:
            self.complete(opcode, b'\x00' + bytes([0xFF] * 64))
        elif opcode == 0x1003:                                     # Read Local Supported Features
            feats = bytearray(8)
            feats[0] = 0xBF; feats[1] = 0xFE; feats[2] = 0x8F; feats[3] = 0xFE; feats[4] = 0xDB; feats[5] = 0xFF
            feats[6] = 0x7B; feats[7] = 0x87                     # (SSP: byte 6 bit 3; EIR: byte 6 bit 0)
            self.complete(opcode, b'\x00' + bytes(feats))
        elif opcode == 0x1005:                                     # Read Buffer Size
            self.complete(opcode, struct.pack('<BHBHH', 0, ACL_LEN, 64, ACL_PKTS, 8))
        elif opcode == 0x1009:                                     # Read BD_ADDR
            self.complete(opcode, b'\x00' + RADIO_ADDR)
        elif opcode == 0x0C14:
            self.complete(opcode, b'\x00' + b'NovaOS Test Radio'.ljust(248, b'\0'))
        elif opcode == 0x0C1A:                                     # Write Scan Enable
            self.scan = p[0] if p else 0
            self.complete(opcode)
        elif opcode == 0x0401:                                     # Inquiry: every remote, then complete
            self.status(opcode)
            for r in REMOTES:
                rssi = -40 if r is REMOTES[0] else -60
                self.event(0x22, bytes([1]) + r.addr + bytes([1, 0]) + r.cod.to_bytes(3, 'little') +
                           struct.pack('<Hb', 0x1234, rssi))
            log('inquiry')
            self.event(0x01, b'\x00')
        elif opcode == 0x0402:
            self.complete(opcode)
        elif opcode == 0x0419:                                     # Remote Name Request
            addr = p[:6]
            self.status(opcode)
            r = self.remote(addr)
            if r:
                self.event(0x07, b'\x00' + addr + r.name.encode().ljust(248, b'\0'))
            else:
                self.event(0x07, b'\x04' + addr + bytes(248))      # (page timeout)
        elif opcode == 0x0405:                                     # Create Connection
            addr = p[:6]
            r = self.remote(addr)
            self.status(opcode)
            if not r or self.link_by_addr(addr):
                self.event(0x03, bytes([0x04 if not r else 0x0B]) + struct.pack('<H', 0) + addr + b'\x01\x00')
                return
            h = self.next_handle
            self.next_handle += 1
            link = Link(r, h)
            self.links[h] = link
            log('connected ' + addr_text(addr))
            self.event(0x03, b'\x00' + struct.pack('<H', h) + addr + b'\x01\x00')
        elif opcode == 0x0406:                                     # Disconnect
            h, reason = struct.unpack('<HB', p[:3])
            link = self.links.pop(h & 0x0FFF, None)
            self.status(opcode, 0 if link else 0x02)
            if link:
                log('disconnected ' + addr_text(link.remote.addr))
                self.event(0x05, b'\x00' + struct.pack('<HB', h, 0x16))
        elif opcode == 0x0411:                                     # Authentication Requested
            link = self.links.get(struct.unpack('<H', p[:2])[0] & 0x0FFF)
            self.status(opcode, 0 if link else 0x02)
            if link:
                link.pending_auth = 'key'
                self.event(0x17, link.remote.addr)                 # (Link Key Request)
        elif opcode == 0x0413:                                     # Set Connection Encryption
            h, on = struct.unpack('<HB', p[:3])
            link = self.links.get(h & 0x0FFF)
            self.status(opcode, 0 if link and link.auth else 0x05 if link else 0x02)
            if link and link.auth:
                self.event(0x08, b'\x00' + struct.pack('<HB', h, on))
        elif opcode == 0x040B:                                     # Link Key Request Reply
            addr, key = p[:6], p[6:22]
            self.complete(opcode, b'\x00' + addr)
            link = self.link_by_addr(addr)
            if link and link.pending_auth == 'key':
                good = link.remote.key == key
                link.auth = good
                link.pending_auth = None
                log(('authenticated ' if good else 'wrong link key from ') + addr_text(addr))
                self.event(0x06, bytes([0 if good else 0x06]) + struct.pack('<H', link.handle))
        elif opcode == 0x040C:                                     # Link Key Request Negative Reply: pair
            addr = p[:6]
            self.complete(opcode, b'\x00' + addr)
            link = self.link_by_addr(addr)
            if link and link.pending_auth == 'key':
                if link.remote.ssp:
                    link.pending_auth = 'ssp'
                    self.event(0x31, addr)                         # (IO Capability Request)
                else:
                    link.pending_auth = 'pin'
                    self.event(0x16, addr)                         # (PIN Code Request)
        elif opcode == 0x042B:                                     # IO Capability Request Reply
            addr = p[:6]
            self.complete(opcode, b'\x00' + addr)
            link = self.link_by_addr(addr)
            if link and link.pending_auth == 'ssp':
                self.event(0x32, addr + bytes([3, 0, 0]))          # (IO Capability Response: NoInputNoOutput)
                self.event(0x33, addr + struct.pack('<I', 0))      # (User Confirmation Request: Just Works)
        elif opcode in (0x042C, 0x042D, 0x040D, 0x040E):           # the answers that end a pairing
            addr = p[:6]
            self.complete(opcode, b'\x00' + addr)
            link = self.link_by_addr(addr)
            if not link or link.pending_auth not in ('ssp', 'pin'):
                return
            ok = opcode == 0x042C or (opcode == 0x040D and p[6] == 4 and p[7:11] == b'0000')
            how = link.pending_auth
            link.pending_auth = None
            if how == 'ssp':
                self.event(0x36, bytes([0 if ok else 0x05]) + addr)   # (Simple Pairing Complete)
            if ok:
                link.remote.key = os.urandom(16)
                link.auth = True
                log(f'paired {addr_text(addr)} ({how})')
                self.event(0x18, addr + link.remote.key + bytes([4 if how == 'ssp' else 0]))
            self.event(0x06, bytes([0 if ok else 0x05]) + struct.pack('<H', link.handle))
        elif ogf == 0x08:                                          # (LE: none of it)
            self.complete(opcode, b'\x01')
        else:                                                      # (the rest: done, nothing to say)
            self.complete(opcode)

    def remote(self, addr):
        for r in REMOTES:
            if r.addr == addr:
                return r
        return None

    # -- ACL from the host: L2CAP --
    def acl_out(self, data):
        if len(data) < 4:
            return
        hf, n = struct.unpack('<HH', data[:4])
        with self.lock:
            link = self.links.get(hf & 0x0FFF)
            if not link:
                return
            pb = (hf >> 12) & 3
            if pb != 0x1:
                link.rx = b''
            link.rx += data[4:4 + n]
            self.event(0x13, bytes([1]) + struct.pack('<HH', link.handle, 1))   # (Number Of Completed Packets)
            while len(link.rx) >= 4:
                ln, cid = struct.unpack('<HH', link.rx[:4])
                if len(link.rx) < 4 + ln:
                    break
                frame, link.rx = link.rx[4:4 + ln], link.rx[4 + ln:]
                self.l2cap(link, cid, frame)

    def sig(self, link, code, ident, body):
        self.acl(link, 0x0001, struct.pack('<BBH', code, ident, len(body)) + body)

    def l2cap(self, link, cid, frame):
        if cid == 0x0001:
            i = 0
            while i + 4 <= len(frame):
                code, ident, ln = struct.unpack('<BBH', frame[i:i + 4])
                self.signal(link, code, ident, frame[i + 4:i + 4 + ln])
                i += 4 + ln
            return
        ch = link.chans.get(cid)
        if not ch:
            return
        if ch[0] == 0x01:
            self.sdp(link, cid, frame)
        elif ch[0] == 0x11 and frame:                               # HID control: HANDSHAKE successful
            log(f'hid control {frame.hex()}')
            self.acl(link, ch[1], b'\x00')
        elif ch[0] == 0x13:
            log(f'hid output {frame.hex()}')

    def signal(self, link, code, ident, body):
        if code == 0x02:                                            # Connection Request
            psm, scid = struct.unpack('<HH', body[:4])
            if psm not in link.remote.psms:
                self.sig(link, 0x03, ident, struct.pack('<HHHH', 0, scid, 0x0002, 0))   # (PSM not supported)
                return
            if psm != 0x01 and not link.auth:
                self.sig(link, 0x03, ident, struct.pack('<HHHH', 0, scid, 0x0003, 0))   # (security block)
                return
            dcid = link.next_cid
            link.next_cid += 1
            link.chans[dcid] = [psm, scid, False]
            self.sig(link, 0x03, ident, struct.pack('<HHHH', dcid, scid, 0, 0))
            link.ident = (link.ident + 1) & 0xFF or 1
            self.sig(link, 0x04, link.ident, struct.pack('<HH', scid, 0) + bytes([0x01, 2]) + struct.pack('<H', 672))
            log(f'l2cap {psm:#04x} open')
            if psm == 0x13:
                log('hid open')
        elif code == 0x04:                                          # Configuration Request: accepted as asked
            dcid, flags = struct.unpack('<HH', body[:4])
            ch = link.chans.get(dcid)
            self.sig(link, 0x05, ident, struct.pack('<HHH', ch[1] if ch else 0, 0, 0 if ch else 0x0002))
        elif code == 0x06:                                          # Disconnection Request
            dcid, scid = struct.unpack('<HH', body[:4])
            ch = link.chans.pop(dcid, None)
            self.sig(link, 0x07, ident, struct.pack('<HH', dcid, scid))
            if ch:
                log(f'l2cap {ch[0]:#04x} closed')
        elif code == 0x0A:                                          # Information Request
            info = struct.unpack('<H', body[:2])[0]
            if info == 2:
                self.sig(link, 0x0B, ident, struct.pack('<HHI', 2, 0, 0))
            elif info == 3:
                self.sig(link, 0x0B, ident, struct.pack('<HH', 3, 0) + bytes([0x02] + [0] * 7))
            else:
                self.sig(link, 0x0B, ident, struct.pack('<HH', info, 1))
        # (responses to our own requests: nothing to do)

    def sdp(self, link, cid, pdu):
        """ServiceSearchAttributeRequest: the records with a UUID of the
        pattern, each with every attribute (whole: they fit one PDU)"""
        if len(pdu) < 5:
            return
        pid, tid, ln = pdu[0], struct.unpack('>H', pdu[1:3])[0], struct.unpack('>H', pdu[3:5])[0]
        body = pdu[5:5 + ln]
        dst = link.chans[cid][1]
        if pid != 0x06:
            self.acl(link, dst, bytes([0x01]) + struct.pack('>HHH', tid, 2, 0x0003))   # (ErrorResponse: bad syntax)
            return
        log('sdp ' + addr_text(link.remote.addr))
        want = uuids_in(body[:2 + body[1]] if body and body[0] == 0x35 else body)
        found = [r for r in link.remote.records if not want or want & uuids_in(r)]
        lists = de_seq(*found)
        self.acl(link, dst, bytes([0x07]) + struct.pack('>HH', tid, len(lists) + 3) +
                 struct.pack('>H', len(lists)) + lists + b'\x00')

    def mouse_report(self, buttons, dx, dy, wheel):
        with self.lock:
            link = self.link_by_addr(REMOTES[0].addr)
            ch = next((c for c in (link.chans.values() if link else ()) if c[0] == 0x13), None)
            if not ch:
                return False
            self.acl(link, ch[1], bytes([0xA1, buttons & 7, dx & 0xFF, dy & 0xFF, wheel & 0xFF]))
            return True


class Conn:
    """One QEMU usb-redir connection"""

    def __init__(self, sock):
        self.sock = sock
        self.wlock = threading.Lock()
        self.ids64 = self.bulk32 = False
        self.config = 0
        self.listening = False
        self.held_evt = []
        self.held_in = []                      # the host's bulk IN requests: (id, length)
        self.acl_queue = []                    # ACL packets for the host, waiting for a request
        self.radio = Radio(self)

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

    def device(self):
        return bytes([18, 1]) + struct.pack('<H', 0x0200) + bytes([0xE0, 0x01, 0x01, 64]) + \
            struct.pack('<HHH', VID, PID, 0x0100) + bytes([1, 2, 3, 1])

    def config_desc(self):
        d = bytes([9, 4, 0, 0, 3, 0xE0, 0x01, 0x01, 0])
        d += bytes([7, 5, EP_EVT, 3]) + struct.pack('<H', EVT_MPS) + bytes([1])
        d += bytes([7, 5, EP_ACL_IN, 2]) + struct.pack('<H', ACL_MPS) + bytes([0])
        d += bytes([7, 5, EP_ACL_OUT, 2]) + struct.pack('<H', ACL_MPS) + bytes([0])
        d += bytes([9, 4, 1, 0, 2, 0xE0, 0x01, 0x01, 0])
        d += bytes([7, 5, 0x83, 1]) + struct.pack('<H', 0) + bytes([1])
        d += bytes([7, 5, 0x03, 1]) + struct.pack('<H', 0) + bytes([1])
        return bytes([9, 2]) + struct.pack('<H', 9 + len(d)) + bytes([2, 1, 0, 0xE0, 50]) + d

    def string(self, i):
        if i == 0:
            return bytes([4, 3]) + struct.pack('<H', 0x0409)
        s = {1: 'NovaOS', 2: 'NovaOS Test Bluetooth Radio', 3: 'NOVA0BT0'}.get(i)
        if s is None:
            return None
        u = s.encode('utf-16-le')
        return bytes([2 + len(u), 3]) + u

    def endpoints(self):
        eps = [(0x00, 0, 0, 0, 64), (0x80, 0, 0, 0, 64)]
        if self.config:
            eps += [(EP_EVT, 3, 1, 0, EVT_MPS), (EP_ACL_IN, 2, 0, 0, ACL_MPS), (EP_ACL_OUT, 2, 0, 0, ACL_MPS),
                    (0x83, 1, 1, 1, 0), (0x03, 1, 1, 1, 0)]
        return eps

    def send_info(self):
        ifaces = [(0, 0xE0, 0x01, 0x01), (1, 0xE0, 0x01, 0x01)]
        cols = [[f[k] for f in ifaces] + [0] * (32 - len(ifaces)) for k in range(4)]
        self.send(INTERFACE_INFO, struct.pack('<I', len(ifaces)) + b''.join(bytes(c) for c in cols))
        types, intervals, ifs, mps = [255] * 32, [0] * 32, [0] * 32, [0] * 32
        for addr, t, iv, iface, m in self.endpoints():
            i = ((addr & 0x80) >> 3) | (addr & 0x0F)
            types[i], intervals[i], ifs[i], mps[i] = t, iv, iface, m
        self.send(EP_INFO, bytes(types) + bytes(intervals) + bytes(ifs) + struct.pack('<32H', *mps))

    # -- to the host --
    def interrupt_in(self, chunk):
        if not self.listening:
            self.held_evt.append(chunk)
            return
        self.send(INTERRUPT_PACKET, struct.pack('<BBH', EP_EVT, OK, len(chunk)), chunk)

    def bulk_reply(self, pid, ep, status, data=b'', n=None):
        """A bulk transfer's status: an IN one with its data, an OUT one with
        how much it took (@n: QEMU reports the length as the transfer's)"""
        if n is None:
            n = len(data)
        hdr = struct.pack('<BBHI', ep, status, n & 0xFFFF, 0)
        if self.bulk32:
            hdr += struct.pack('<H', n >> 16)
        self.send(BULK_PACKET, hdr, data, pid)

    def acl_in(self, pkt):
        with self.wlock:
            self.acl_queue.append(pkt)
        self.pump()

    def pump(self):
        while True:
            with self.wlock:
                if not self.acl_queue or not self.held_in:
                    return
                pid, want = self.held_in.pop(0)
                pkt = self.acl_queue.pop(0)
                if len(pkt) > want:                     # (the rest in the next request)
                    self.acl_queue.insert(0, pkt[want:])
                    pkt = pkt[:want]
            self.bulk_reply(pid, EP_ACL_IN, OK, pkt)

    def control(self, pid, hdr, data):
        ep, req, rtype, _, value, index, length = struct.unpack('<BBBBHHH', hdr)
        reply, status = b'', OK
        if rtype == 0x80 and req == 6:                  # GET_DESCRIPTOR
            kind, i = value >> 8, value & 0xFF
            reply = self.device() if kind == 1 else self.config_desc() if kind == 2 else self.string(i) if kind == 3 else None
            if reply is None:
                reply, status = b'', STALL
        elif rtype & 0x80 and req == 0:                 # GET_STATUS
            reply = b'\0\0'
        elif rtype & 0x80:
            reply = bytes(length)
        if rtype & 0x80:
            reply = reply[:length]
        self.send(CONTROL_PACKET, struct.pack('<BBBBHHH', ep, req, rtype, status, value, index,
                                              len(reply) if rtype & 0x80 else length), reply, pid)
        if rtype == 0x20 and req == 0 and data:         # (an HCI command: answered after its control status)
            self.radio.command(data)

    def run(self):
        self.send(HELLO, b'novaos btpeer'.ljust(64, b'\0') + struct.pack('<I', CAPS))
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
                self.bulk32 = bool(caps & CAP_32BIT_BULK)
                if not connected:
                    connected = True
                    self.send_info()
                    self.send(DEVICE_CONNECT, struct.pack('<BBBBHHH', SPEED_FULL, 0xE0, 0x01, 0x01, VID, PID, 0x0100))
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
                if body[0] == EP_EVT and not self.listening:
                    self.listening = True
                    held, self.held_evt = self.held_evt, []
                    for c in held:
                        self.interrupt_in(c)
            elif ptype == STOP_INTERRUPT_RECEIVING:
                if body[0] == EP_EVT:
                    self.listening = False
                self.send(INTERRUPT_RECEIVING_STATUS, bytes([OK, body[0]]), pid=pid)
            elif ptype == BULK_PACKET:
                ep, st, n, _ = struct.unpack('<BBHI', body[:8])
                hl = 8
                if self.bulk32:
                    n |= struct.unpack('<H', body[8:10])[0] << 16
                    hl = 10
                if ep & 0x80:
                    with self.wlock:
                        self.held_in.append((pid, n))
                    self.pump()
                else:
                    self.bulk_reply(pid, ep, OK, n=n)
                    self.radio.acl_out(body[hl:hl + n])
            elif ptype == CANCEL_DATA_PACKET:
                with self.wlock:
                    hit = [x for x in self.held_in if x[0] == pid]
                    self.held_in = [x for x in self.held_in if x[0] != pid]
                if hit:
                    self.bulk_reply(pid, EP_ACL_IN, CANCELLED)
            elif ptype == RESET:
                with self.radio.lock:
                    self.radio.links.clear()
                with self.wlock:
                    self.acl_queue = []


def ctl_loop(port, conns):
    """The test's control port: mouse reports"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', port))
    srv.listen(4)
    while True:
        c, _ = srv.accept()
        f = c.makefile('rw')
        for line in f:
            try:
                v = {'buttons': 0, 'dx': 0, 'dy': 0, 'wheel': 0}
                for kv in line.split():
                    k, x = kv.split('=')
                    if k not in v:
                        raise KeyError(k)
                    v[k] = int(x, 0)
                sent = any(conn.radio.mouse_report(v['buttons'], v['dx'], v['dy'], v['wheel']) for conn in list(conns))
                f.write('ok\n' if sent else 'error not connected\n')
            except (ValueError, KeyError, OSError) as e:
                f.write(f'error {e}\n')
            f.flush()
        c.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--ctl', type=int)
    a = ap.parse_args()
    conns = []
    threading.Thread(target=ctl_loop, args=(a.ctl or a.port + 100, conns), daemon=True).start()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', a.port))
    srv.listen(1)
    log(f'listening on {a.port}')
    while True:
        s, _ = srv.accept()
        log('QEMU connected')
        conn = Conn(s)
        conns.append(conn)
        try:
            conn.run()
        except (EOFError, OSError):
            pass
        conns.remove(conn)
        log('QEMU went away')
        s.close()


if __name__ == '__main__':
    sys.exit(main())
