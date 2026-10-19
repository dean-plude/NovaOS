#!/usr/bin/env python3
"""A USB Bluetooth radio, with two Bluetooth devices in range, for testing
NovaOS, as a QEMU usb-redir device.

    tools/btpeer.py --port 10718 [--ctl PORT] [--audio DIR]
    tools/btpeer.py --decode DIR/bt-a2dp.sbc DIR/bt-a2dp.wav

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
0x02 (OUT), interface 1 for SCO voice (isochronous 0x83/0x03, the
specification's six settings: 0 without bandwidth, then 9, 17, 25, 33 and
49-byte packets every millisecond); HCI commands arrive as class requests
to the device on the control endpoint.  It answers the commands a host
sends to bring a radio up and use it (Vol 4, Part E, 7): Reset, the local
version, features, buffer sizes and address, the event mask, name, class
of device, scan and inquiry modes, voice setting and Simple Pairing, then
Inquiry, Remote Name Request, Create Connection, Authentication
Requested, Set Connection Encryption, Setup Synchronous Connection (an
eSCO link, CVSD) and Disconnect, and the replies a host gives the
controller's pairing questions (link key, PIN code, IO capability, user
confirmation).  SCO data goes in isochronous packets: the host's SCO
packets are put back together from interface 1's OUT endpoint, and the
headset's are sent on its IN endpoint, a packet every millisecond.

In range, as the radio's inquiry finds them, are:

  4E:4F:56:41:00:01  "NovaOS Test Mouse": a mouse (class of device
                     0x002580) that pairs by Secure Simple Pairing,
                     "Just Works", with an SDP server listing a HID
                     service (0x1124: report descriptor of three buttons,
                     X, Y and a wheel) and a PnP Information record
                     (VID 1209 from the USB list, PID 000e), and HID's two
                     L2CAP channels (PSM 0x11 control, 0x13 interrupt);
  4E:4F:56:41:00:02  "NovaOS Test Headset": a headset (0x240404) that pairs
                     the legacy way with PIN 0000 and lists Hands-Free
                     (RFCOMM channel 1) and Audio Sink services.  As a
                     hands-free unit it answers RFCOMM (TS 07.10's
                     multiplexer, parameter negotiation with credit-based
                     flow control, modem status) on PSM 3 and sets up the
                     service level connection with AT commands (AT+BRSF,
                     AT+CIND=?, AT+CIND?, AT+CMER, then AT+VGS); on the
                     voice link its microphone hears a 600 Hz tone and
                     what the host sends is written to DIR/bt-hfp.wav (8
                     kHz mono).  As an A2DP sink it answers AVDTP on PSM
                     0x19 (one SBC sink end point: 44.1 or 48 kHz, every
                     channel mode, block length and allocation, 8 or 4
                     subbands, bitpool 2-53) and writes the stream's SBC
                     frames to DIR/bt-a2dp.sbc, which --decode turns into a
                     WAV file afterwards (decoding SBC in Python while the
                     stream runs would slow the radio down).

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
"hid open", "disconnected ADDR", "rfcomm DLCI open", "hfp slc",
"voice link HANDLE open", "a2dp configured ...", "a2dp start".
"""
import argparse, math, os, socket, struct, sys, threading, time

HELLO, DEVICE_CONNECT, RESET, INTERFACE_INFO, EP_INFO = 0, 1, 3, 4, 5
SET_CONFIGURATION, GET_CONFIGURATION, CONFIGURATION_STATUS = 6, 7, 8
SET_ALT_SETTING, GET_ALT_SETTING, ALT_SETTING_STATUS = 9, 10, 11
START_INTERRUPT_RECEIVING, STOP_INTERRUPT_RECEIVING, INTERRUPT_RECEIVING_STATUS = 15, 16, 17
START_ISO_STREAM, STOP_ISO_STREAM, ISO_STREAM_STATUS = 12, 13, 14
CANCEL_DATA_PACKET = 21
CONTROL_PACKET, BULK_PACKET, ISO_PACKET, INTERRUPT_PACKET = 100, 101, 102, 103
CAPS = (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6)
CAP_64BIT_IDS, CAP_32BIT_BULK = 1 << 5, 1 << 6
SPEED_FULL = 1
OK, CANCELLED, STALL = 0, 1, 4
EP_EVT, EP_ACL_IN, EP_ACL_OUT, EP_SCO_IN, EP_SCO_OUT = 0x81, 0x82, 0x02, 0x83, 0x03
SCO_MPS = [0, 9, 17, 25, 33, 49]                                # interface 1's settings (Vol 4, Part B, 2.1.1)
MIC_HZ = 600                                                    # what the headset's microphone hears
AUDIO_DIR = None                                                # --audio
VID, PID = 0x1209, 0x000D
EVT_MPS, ACL_MPS = 64, 64
ACL_LEN, ACL_PKTS = 1021, 8                                     # (as Intel's radios)
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


class Wav:
    """A 16-bit WAV file, its header kept current as samples come"""

    def __init__(self, path, rate, channels):
        self.f, self.rate, self.ch, self.n = open(path, 'wb'), rate, channels, 0
        self.header()

    def header(self):
        self.f.seek(0)
        self.f.write(b'RIFF' + struct.pack('<I', 36 + self.n) + b'WAVEfmt ' +
                     struct.pack('<IHHIIHH', 16, 1, self.ch, self.rate, self.rate * 2 * self.ch, 2 * self.ch, 16) +
                     b'data' + struct.pack('<I', self.n))
        self.f.seek(0, 2)

    def add(self, pcm):
        self.f.write(pcm)
        self.n += len(pcm)
        self.header()
        self.f.flush()


SBC_H = [0.0, 1.56575398E-04, 3.43256425E-04, 5.54620202E-04, 8.23919506E-04, 1.13992507E-03, 1.47640169E-03,
         1.78371725E-03, 2.01182542E-03, 2.10371989E-03, 1.99454554E-03, 1.61656283E-03, 9.02154502E-04,
         -1.78805361E-04, -1.64973098E-03, -3.49717454E-03, -5.65949473E-03, -8.02941163E-03, -1.04584443E-02,
         -1.27472335E-02, -1.46525263E-02, -1.59045603E-02, -1.62208471E-02, -1.53184106E-02, -1.29371806E-02,
         -8.85757540E-03, -2.92408442E-03, 4.91578024E-03, 1.46404076E-02, 2.61098752E-02, 3.90751381E-02,
         5.31873032E-02, 6.79989431E-02, 8.29847578E-02, 9.75753918E-02, 1.11196689E-01, 1.23264548E-01,
         1.33264415E-01, 1.40753505E-01, 1.45389847E-01, 1.46955068E-01]


class SbcDecoder:
    """SBC frames (8 subbands) to 16-bit PCM, after the A2DP specification's
    decoder (12.6, and the synthesis filter of its Appendix B): what the
    headset hears, for the test to check"""

    OFFSET8 = [[-2, 0, 0, 0, 0, 0, 0, 1], [-3, 0, 0, 0, 0, 0, 1, 2], [-4, 0, 0, 0, 0, 0, 1, 2], [-4, 0, 0, 0, 0, 0, 1, 2]]
    RATES = [16000, 32000, 44100, 48000]

    def __init__(self):
        h = SBC_H + [SBC_H[80 - n] for n in range(41, 80)]
        self.d = [-8 * h[i] * (-1 if (i // 16) % 2 else 1) for i in range(80)]
        self.n = [[math.cos((i + 0.5) * (k + 4) * math.pi / 8) for i in range(8)] for k in range(16)]
        self.v = [[0.0] * 160, [0.0] * 160]
        self.rate, self.channels = 0, 0

    def allocate(self, mode, snr, ri, bitpool, sf, nch):
        need = [[0] * 8 for _ in range(nch)]
        for ch in range(nch):
            for sb in range(8):
                if snr:
                    need[ch][sb] = sf[ch][sb]
                elif sf[ch][sb] == 0:
                    need[ch][sb] = -5
                else:
                    loud = sf[ch][sb] - self.OFFSET8[ri][sb]
                    need[ch][sb] = loud // 2 if loud > 0 else loud
        bits = [[0] * 8 for _ in range(nch)]
        groups = [[c] for c in range(nch)] if mode < 2 else [list(range(nch))]
        for g in groups:
            maxneed = max(need[c][s] for c in g for s in range(8))
            count, slices, sl = 0, 0, maxneed + 1
            while True:
                sl -= 1
                count += slices
                slices = 0
                for c in g:
                    for s in range(8):
                        if sl + 1 < need[c][s] < sl + 16:
                            slices += 1
                        elif need[c][s] == sl + 1:
                            slices += 2
                if count + slices >= bitpool:
                    break
            if count + slices == bitpool:
                count += slices
                sl -= 1
            for c in g:
                for s in range(8):
                    bits[c][s] = 0 if need[c][s] < sl + 2 else min(need[c][s] - sl, 16)
            for second in (False, True):
                i, s = 0, 0
                while count < bitpool and s < 8:
                    c = g[i]
                    if second:
                        if bits[c][s] < 16:
                            bits[c][s] += 1
                            count += 1
                    elif 2 <= bits[c][s] < 16:
                        bits[c][s] += 1
                        count += 1
                    elif need[c][s] == sl + 1 and bitpool > count + 1:
                        bits[c][s] = 2
                        count += 2
                    if i == len(g) - 1:
                        i, s = 0, s + 1
                    else:
                        i += 1
        return bits

    def frame(self, data):
        """One frame at the start of @data: (its length, its PCM as
        interleaved samples), or (0, None) if it is not one"""
        if len(data) < 4 or data[0] != 0x9C:
            return 0, None
        ri, blocks, mode = data[1] >> 6, ((data[1] >> 4) & 3) * 4 + 4, (data[1] >> 2) & 3
        snr, nsb, bitpool = (data[1] >> 1) & 1, 8 if data[1] & 1 else 4, data[2]
        if nsb != 8:
            return 0, None
        nch = 1 if mode == 0 else 2
        self.rate, self.channels = self.RATES[ri], nch
        pos = 32
        bitbuf = int.from_bytes(data, 'big')
        total = len(data) * 8

        def get(n):
            nonlocal pos
            v = (bitbuf >> (total - pos - n)) & ((1 << n) - 1)
            pos += n
            return v
        join = get(8) if mode == 3 else 0
        sf = [[get(4) for _ in range(8)] for _ in range(nch)]
        bits = self.allocate(mode, snr, ri, bitpool, sf, nch)
        sb = [[[0.0] * 8 for _ in range(nch)] for _ in range(blocks)]
        for blk in range(blocks):
            for ch in range(nch):
                for s in range(8):
                    n = bits[ch][s]
                    if n:
                        q = get(n)
                        scale = 2.0 ** (sf[ch][s] + 1)
                        sb[blk][ch][s] = scale * ((q * 2 + 1) / ((1 << n) - 1) - 1)
        length = (pos + 7) // 8
        out = []
        for blk in range(blocks):
            if mode == 3:
                for s in range(8):
                    if join & (0x80 >> s):
                        m, d = sb[blk][0][s], sb[blk][1][s]
                        sb[blk][0][s], sb[blk][1][s] = m + d, m - d
            pcm = [self.synth(ch, sb[blk][ch]) for ch in range(nch)]
            for j in range(8):
                for ch in range(nch):
                    out.append(max(-32768, min(32767, int(round(pcm[ch][j])))))
        return length, out

    def synth(self, ch, s):
        v = [sum(nk[i] * s[i] for i in range(8)) for nk in self.n] + self.v[ch][:144]
        self.v[ch] = v
        d = self.d
        out = []
        for j in range(8):
            acc = 0.0
            for i in range(5):
                acc += v[i * 32 + j] * d[i * 16 + j] + v[i * 32 + 24 + j] * d[i * 16 + 8 + j]
            out.append(acc)
        return out



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
    Remote('4E:4F:56:41:00:02', 'NovaOS Test Headset', 0x240404, False, headset_records(), (0x01, 0x03, 0x19)),
]


def rfcomm_fcs(data):
    """TS 07.10's frame check sequence (CRC-8, reflected 0xE0, from 0xFF)"""
    f = 0xFF
    for b in data:
        f ^= b
        for _ in range(8):
            f = (f >> 1) ^ 0xE0 if f & 1 else f >> 1
    return 0xFF - f


class HandsFree:
    """The headset's hands-free unit: RFCOMM's responding side (the host
    started the session: its commands carry C/R 1, ours 0) and HFP's
    service level connection, which the hands-free unit drives"""

    FEATURES = 0x1F                         # (EC/NR, three-way calling, CLI, voice recognition, remote volume)
    SLC = ['AT+BRSF=%d' % FEATURES, 'AT+CIND=?', 'AT+CIND?', 'AT+CMER=3,0,0,1', 'AT+VGS=15']

    def __init__(self, radio, link, cid):
        self.radio, self.link, self.cid = radio, link, cid
        self.dlci, self.cfc, self.credits, self.given = None, False, 0, 0
        self.msc_in = self.msc_out = False
        self.step, self.text, self.queue = -1, b'', []

    def send(self, dlci, ctl, info=b'', cr=False, credits=None):
        addr = dlci << 2 | (2 if cr else 0) | 1
        ln = bytes([len(info) << 1 | 1]) if len(info) < 128 else bytes([(len(info) << 1) & 0xFE, len(info) >> 7])
        body = bytes([addr, ctl | (0x10 if credits is not None else 0)]) + ln + \
            (bytes([credits]) if credits is not None else b'') + info
        self.radio.acl(self.link, self.cid, body + bytes([rfcomm_fcs(body[:2] if ctl & 0xEF == 0xEF else body[:3])]))

    def frame(self, data):
        if len(data) < 4:
            return
        dlci, ctl, pf = data[0] >> 2, data[1] & 0xEF, data[1] & 0x10
        ln, k = data[2] >> 1, 3
        if not data[2] & 1:
            ln, k = ln | data[3] << 7, 4
        if ctl == 0x2F:                                         # SABM: UA
            self.send(dlci, 0x73, cr=True)
            if dlci:
                self.dlci = dlci
                log(f'rfcomm {dlci} open')
        elif ctl == 0x43:                                       # DISC: UA
            self.send(dlci, 0x73, cr=True)
            if dlci == self.dlci:
                self.dlci = None
                log(f'rfcomm {dlci} closed')
        elif ctl == 0xEF:                                       # UIH
            if pf and self.cfc and dlci:
                self.credits += data[k]
                k += 1
            info = data[k:k + ln]
            if dlci == 0:
                self.mux(info)
            elif dlci == self.dlci and info:
                self.data(info)
            self.flush()

    def mux(self, m):
        if len(m) < 2:
            return
        typ, cmd, n = m[0] & 0xFC, m[0] & 2, m[1] >> 1
        v = m[2:2 + n]
        if typ == 0x80 and cmd and n >= 8:                      # PN: credit-based flow control if asked
            self.cfc = v[1] & 0xF0 == 0xF0
            self.credits = v[7] & 7 if self.cfc else 0
            r = bytearray(v)
            r[1] = (r[1] & 0x0F) | (0xE0 if self.cfc else 0)
            r[7] = 7
            self.given = 7
            self.send(0, 0xEF, bytes([0x81, m[1]]) + bytes(r))
        elif typ == 0xE0 and cmd:                               # MSC: answered, and ours
            self.send(0, 0xEF, bytes([0xE1, m[1]]) + v)
            self.send(0, 0xEF, bytes([0xE3, 0x05, v[0], 0x8D]))
            self.msc_in = True
        elif typ == 0xE0:
            self.msc_out = True
        elif cmd:
            self.send(0, 0xEF, bytes([0x11, 0x03, m[0]]))       # (NSC)
        if self.msc_in and self.msc_out and self.step < 0:
            self.step = 0
            self.at(self.SLC[0])

    def at(self, text):
        self.queue.append((text + '\r').encode())

    def flush(self):
        while self.queue and self.dlci and (not self.cfc or self.credits > 0):
            if self.cfc:
                self.credits -= 1
            self.send(self.dlci, 0xEF, self.queue.pop(0))

    def data(self, info):
        if self.cfc:
            self.given -= 1
            if self.given <= 2:
                self.send(self.dlci, 0xEF, credits=5)
                self.given += 5
        self.text += info
        while b'\r\n' in self.text:
            line, self.text = self.text.split(b'\r\n', 1)
            line = line.decode('latin-1').strip()
            if not line:
                continue
            log(f'hfp: {line}')
            if line in ('OK', 'ERROR') and 0 <= self.step < len(self.SLC):
                if line == 'ERROR':
                    log(f'hfp: {self.SLC[self.step]} refused')
                if self.SLC[self.step].startswith('AT+CMER'):
                    log('hfp slc')
                self.step += 1
                if self.step < len(self.SLC):
                    self.at(self.SLC[self.step])


class Sink:
    """The headset's A2DP sink: AVDTP's accepting side, one SBC stream
    end point, and the stream decoded into bt-a2dp.wav"""

    def __init__(self, radio, link):
        self.radio, self.link = radio, link
        self.sig = self.media = None                         # (the host's CIDs)
        self.sbc, self.packets = None, 0

    def signal(self, f):
        if len(f) < 2 or f[0] & 3:                              # (commands only: the host asks nothing of ours)
            return
        label, sig, p = f[0] >> 4, f[1] & 0x3F, f[2:]

        def answer(body=b'', kind=2):
            self.radio.acl(self.link, self.sig, bytes([label << 4 | kind, sig]) + body)
        if sig == 0x01:                                         # DISCOVER: SEID 1, audio, sink
            answer(bytes([1 << 2, 0 << 4 | 1 << 3]))
        elif sig in (0x02, 0x0C):                               # GET_(ALL_)CAPABILITIES
            answer(bytes([0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x3F, 0xFF, 0x02, 53]))
        elif sig == 0x03:                                       # SET_CONFIGURATION
            i, c = 2, None
            while i + 2 <= len(p):
                if p[i] == 0x07 and p[i + 1] >= 6:
                    c = p[i + 4:i + 8]
                i += 2 + p[i + 1]
            if not c:
                answer(bytes([0x07, 0x29]), 3)                  # (unsupported configuration)
                return
            rate = {0x80: 16000, 0x40: 32000, 0x20: 44100, 0x10: 48000}.get(c[0] & 0xF0)
            mode = {8: 'mono', 4: 'dual', 2: 'stereo', 1: 'joint'}.get(c[0] & 0x0F)
            log(f'a2dp configured: SBC {rate} Hz {mode}, blocks {c[1] >> 4:x}, subbands {c[1] >> 2 & 3:x}, '
                f'allocation {c[1] & 3:x}, bitpool {c[2]}-{c[3]}')
            answer()
        elif sig == 0x07:
            log('a2dp start')
            answer()
        elif sig == 0x09:
            log(f'a2dp suspend ({self.packets} packets)')
            answer()
        else:                                                   # OPEN, CLOSE, ABORT, DELAY_REPORT, ...
            answer()

    def media_in(self, f):
        if len(f) < 13:
            return
        self.packets += 1
        if AUDIO_DIR:
            if not self.sbc:
                self.sbc = open(os.path.join(AUDIO_DIR, 'bt-a2dp.sbc'), 'wb')
            self.sbc.write(f[12 + 4 * (f[0] & 0x0F) + 1:])     # (the frames, after RTP's header and SBC's)
            self.sbc.flush()


def decode(src, dst):
    """An SBC stream (frames back to back) into a 16-bit WAV file"""
    data, dec, pcm = open(src, 'rb').read(), SbcDecoder(), []
    while data:
        n, out = dec.frame(data)
        if not n:
            break
        data = data[n:]
        pcm += out
    Wav(dst, dec.rate or 48000, dec.channels or 2).add(struct.pack('<%dh' % len(pcm), *pcm))


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
        self.hf = self.sink = None
        self.sco = None                                   # its voice link's handle


class Radio:
    """The controller and the remotes' hosts, behind one usbredir connection"""

    def __init__(self, conn):
        self.conn = conn
        self.links = {}                                   # handle -> Link
        self.next_handle = 0x0001
        self.scan = 0
        self.lock = threading.RLock()
        self.sco = {}                                     # voice link handle -> Link
        self.sco_rx, self.mic_buf, self.mic_t = b'', b'', 0
        self.hfp_wav = None

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
                0x0C05: 'set event filter', 0x0C33: 'host buffer size', 0x0C26: 'write voice setting',
                0x0428: 'setup synchronous connection'}.get(opcode, f'command {opcode:04x}')
        log(name)
        if opcode == 0x0C03:                                       # Reset
            self.links.clear()
            self.sco.clear()
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
        elif opcode == 0x0428:                                     # Setup Synchronous Connection: eSCO, CVSD
            link = self.links.get(struct.unpack('<H', p[:2])[0] & 0x0FFF)
            self.status(opcode, 0 if link else 0x02)
            if link:
                h = self.next_handle
                self.next_handle += 1
                self.sco[h] = link
                link.sco = h
                log(f'voice link {h:03x} open')
                self.event(0x2C, b'\x00' + struct.pack('<H', h) + link.remote.addr + bytes([2, 0x0C, 0x02]) +
                           struct.pack('<HH', 60, 60) + b'\x02')
        elif opcode == 0x0406 and (struct.unpack('<H', p[:2])[0] & 0x0FFF) in self.sco:   # Disconnect: a voice link
            h = struct.unpack('<H', p[:2])[0] & 0x0FFF
            self.sco.pop(h).sco = None
            self.status(opcode)
            log(f'voice link {h:03x} closed')
            self.event(0x05, b'\x00' + struct.pack('<HB', h, 0x16))
        elif opcode == 0x0406:                                     # Disconnect
            h, reason = struct.unpack('<HB', p[:3])
            link = self.links.pop(h & 0x0FFF, None)
            if link and link.sco is not None:
                self.sco.pop(link.sco, None)
                self.event(0x05, b'\x00' + struct.pack('<HB', link.sco, 0x16))
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
        elif ch[0] == 0x03 and link.hf:
            link.hf.frame(frame)
        elif ch[0] == 0x19 and link.sink and ch[3] == 'sig':
            link.sink.signal(frame)
        elif ch[0] == 0x19 and link.sink:
            link.sink.media_in(frame)
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
            role = None
            if psm == 0x03:
                link.hf = HandsFree(self, link, scid)
            elif psm == 0x19:                                       # (AVDTP: signalling first, then the media channel)
                if not link.sink or link.sink.sig is None:
                    link.sink = link.sink or Sink(self, link)
                    link.sink.sig, role = scid, 'sig'
                else:
                    link.sink.media, role = scid, 'media'
            link.chans[dcid] = [psm, scid, False, role]
            self.sig(link, 0x03, ident, struct.pack('<HHHH', dcid, scid, 0, 0))
            link.ident = (link.ident + 1) & 0xFF or 1
            mtu = 895 if role == 'media' else 672                  # (a media channel takes bigger packets, as headsets' do)
            self.sig(link, 0x04, link.ident, struct.pack('<HH', scid, 0) + bytes([0x01, 2]) + struct.pack('<H', mtu))
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
                if ch[0] == 0x19 and link.sink and ch[3] == 'sig':
                    link.sink = None
                elif ch[0] == 0x19 and link.sink:
                    link.sink.media = None
                elif ch[0] == 0x03:
                    link.hf = None
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

    # -- SCO --
    def sco_out(self, data):
        """Interface 1's OUT packets: SCO packets put back together; the
        voice link's samples into bt-hfp.wav"""
        self.sco_rx += data
        while len(self.sco_rx) >= 3 and len(self.sco_rx) >= 3 + self.sco_rx[2]:
            n = 3 + self.sco_rx[2]
            pkt, self.sco_rx = self.sco_rx[:n], self.sco_rx[n:]
            if (struct.unpack('<H', pkt[:2])[0] & 0x0FFF) in self.sco and AUDIO_DIR:
                if not self.hfp_wav:
                    self.hfp_wav = Wav(os.path.join(AUDIO_DIR, 'bt-hfp.wav'), 8000, 1)
                self.hfp_wav.add(pkt[3:])

    def mic_bytes(self, n):
        """The next @n bytes of the IN endpoint's SCO packets (the
        microphone's tone, 24 samples a packet), or None while no voice link
        is up"""
        h = next(iter(self.sco), None)
        if h is None:
            self.mic_buf = b''
            return None
        while len(self.mic_buf) < n:
            s = [int(12000 * math.sin(2 * math.pi * MIC_HZ * (self.mic_t + i) / 8000)) for i in range(24)]
            self.mic_t = (self.mic_t + 24) % 8000
            self.mic_buf += struct.pack('<HB', h, 48) + struct.pack('<24h', *s)
        out, self.mic_buf = self.mic_buf[:n], self.mic_buf[n:]
        return out

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
        self.alt1 = 0                          # interface 1's setting
        self.mic_on = False
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
        for alt, mps in enumerate(SCO_MPS):
            d += bytes([9, 4, 1, alt, 2, 0xE0, 0x01, 0x01, 0])
            d += bytes([7, 5, EP_SCO_IN, 1]) + struct.pack('<H', mps) + bytes([1])
            d += bytes([7, 5, EP_SCO_OUT, 1]) + struct.pack('<H', mps) + bytes([1])
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
                    (EP_SCO_IN, 1, 1, 1, SCO_MPS[self.alt1]), (EP_SCO_OUT, 1, 1, 1, SCO_MPS[self.alt1])]
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

    def mic_loop(self):
        """Interface 1's IN endpoint while its stream runs: a packet every
        millisecond (the voice link's, while one is up)"""
        t0, sent = time.monotonic(), 0
        while self.mic_on:
            due = int((time.monotonic() - t0) * 1000)
            if due - sent > 200:                        # (stalled: don't flood)
                sent = due - 20
            try:
                for _ in range(due - sent):
                    with self.radio.lock:
                        data = self.radio.mic_bytes(SCO_MPS[self.alt1]) if self.alt1 else None
                    if data is not None:
                        self.send(ISO_PACKET, struct.pack('<BBH', EP_SCO_IN, OK, len(data)), data)
            except OSError:
                return
            sent = due
            time.sleep(0.002)

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
                iface, alt = body[0], body[1]
                ok = alt == 0 or (iface == 1 and alt < len(SCO_MPS))
                if ok and iface == 1:
                    self.alt1 = alt
                    log(f'interface 1 setting {alt}')
                    self.send_info()                    # (before the status: QEMU takes the endpoints' sizes from it)
                self.send(ALT_SETTING_STATUS, bytes([OK if ok else STALL, iface, alt]), pid=pid)
            elif ptype == GET_ALT_SETTING:
                self.send(ALT_SETTING_STATUS, bytes([OK, body[0], self.alt1 if body[0] == 1 else 0]), pid=pid)
            elif ptype == START_ISO_STREAM:
                self.send(ISO_STREAM_STATUS, bytes([OK, body[0]]), pid=pid)
                if body[0] == EP_SCO_IN and not self.mic_on:
                    self.mic_on = True
                    threading.Thread(target=self.mic_loop, daemon=True).start()
            elif ptype == STOP_ISO_STREAM:
                if body[0] == EP_SCO_IN:
                    self.mic_on = False
                self.send(ISO_STREAM_STATUS, bytes([OK, body[0]]), pid=pid)
            elif ptype == ISO_PACKET:
                ep, st, n = struct.unpack('<BBH', body[:4])
                if ep == EP_SCO_OUT and n:
                    with self.radio.lock:
                        self.radio.sco_out(body[4:4 + n])
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
                self.alt1 = 0
                self.mic_on = False
                with self.radio.lock:
                    self.radio.links.clear()
                    self.radio.sco.clear()
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
    if sys.argv[1:2] == ['--decode']:
        decode(sys.argv[2], sys.argv[3])
        return
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--ctl', type=int)
    ap.add_argument('--audio', help='where the headset writes what it hears (bt-hfp.wav, bt-a2dp.sbc)')
    a = ap.parse_args()
    global AUDIO_DIR
    AUDIO_DIR = a.audio
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
