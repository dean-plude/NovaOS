#!/usr/bin/env python3
"""A USB Video Class camera for testing NovaOS, as a QEMU usb-redir device.

    tools/campeer.py --port 10714
    tools/campeer.py --port 10716 --iso

QEMU has no camera of its own, so this program is one, behind QEMU's
usb-redir device (the usbredir protocol, as tools/padpeer.py speaks it for
its game controllers):

    -chardev socket,id=cam,host=127.0.0.1,port=10714
    -device usb-redir,chardev=cam,bus=xhci.0

It listens on --port for QEMU and is a high-speed UVC 1.1 camera
(1209:000a, "NovaOS Test Camera"), streaming on bulk endpoint 0x81:
an interface association of a VideoControl interface (a camera terminal
wired to a streaming output terminal) and a VideoStreaming interface with
two formats:

  1  YUY2, frames 1 640x480 and 2 320x240, 30 frames a second (333333)
     or 15 (666666);
  2  NV12, frame 1 640x480, 30 frames a second.

Each frame is a test card: eight colour bars (white, yellow, cyan, green,
magenta, red, blue, black) over the top seven eighths, and under them
the frame's number as sixteen blocks, lowest bit on the left, white for a
one and black for a zero (BT.601 studio-range YUV).  The host negotiates
with VS_PROBE_CONTROL and VS_COMMIT_CONTROL; bulk IN transfers then get
payloads of up to dwMaxPayloadTransferSize (32 KiB) bytes, each with a
two-byte payload header (FID toggling per frame, EOF on its last).
Clearing the endpoint's halt (CLEAR_FEATURE) stops the stream; a frame
starts again with the next transfer.

With --iso it is instead a camera as most built into laptops are
(1209:000b, "NovaOS Isochronous Camera"): its streaming interface's
first setting has no endpoint, setting 1 an isochronous IN endpoint 0x81
of 1024 bytes a packet and setting 2 one of three 1024-byte transactions
a microframe (3072 bytes), each serviced every millisecond, and its
formats are

  1  MJPEG, frames 1 640x480 (30 or 15 frames a second) and 2 320x240
     (30), payloads up to 3072 bytes (the host must select setting 2);
  2  YUY2, frame 1 160x120, 15 frames a second, payloads up to 1024
     bytes (setting 1 is enough).

The host commits a format and then selects the setting whose packets
hold the payload; QEMU starts the endpoint's stream (start_iso_stream)
and this program sends a packet every millisecond, each a payload of the
frame being sent or an empty one between frames, until the host selects
setting 0 again ("stopped").  The MJPEG frames are the same test card as
baseline JPEG images (4:4:4, every quantizer 2) without Huffman tables,
as most cameras send them: the host must use the JPEG standard's typical
ones (Annex K.3).

What the host does is printed: "commit format F frame N interval I",
"streaming", "frame N" every 30 frames, "stopped".
"""
import argparse, math, socket, struct, sys, threading, time

HELLO, DEVICE_CONNECT, RESET, INTERFACE_INFO, EP_INFO = 0, 1, 3, 4, 5
SET_CONFIGURATION, GET_CONFIGURATION, CONFIGURATION_STATUS = 6, 7, 8
SET_ALT_SETTING, GET_ALT_SETTING, ALT_SETTING_STATUS = 9, 10, 11
START_ISO_STREAM, STOP_ISO_STREAM, ISO_STREAM_STATUS = 12, 13, 14
CANCEL_DATA_PACKET = 21
CONTROL_PACKET, BULK_PACKET, ISO_PACKET = 100, 101, 102
CAPS = (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6)   # connect_device_version, ep_info_max_packet_size, 64bits_ids, 32bits_bulk_length
CAP_64BIT_IDS, CAP_32BIT_BULK = 1 << 5, 1 << 6
SPEED_HIGH = 2
OK, CANCELLED, STALL = 0, 1, 4
EP_IN = 0x81
VID, PID = 0x1209, 0x000A                           # (--iso: 0x000b)
PAYLOAD = 32768
ISO = False                                         # --iso
UVC = 0x0110
PROBE_LEN = 34

YUY2_GUID = bytes.fromhex('5955593200001000800000aa00389b71')
NV12_GUID = bytes.fromhex('4e56313200001000800000aa00389b71')
MJPG = b'MJPG'                                      # (an MJPEG format: no GUID)
# (format index, GUID, bits per pixel, [(frame index, width, height, [intervals])])
FORMATS = [
    (1, YUY2_GUID, 16, [(1, 640, 480, [333333, 666666]), (2, 320, 240, [333333, 666666])]),
    (2, NV12_GUID, 12, [(1, 640, 480, [333333])]),
]
ISO_FORMATS = [
    (1, MJPG, 16, [(1, 640, 480, [333333, 666666]), (2, 320, 240, [333333])]),
    (2, YUY2_GUID, 16, [(1, 160, 120, [666666])]),
]
ISO_ALTS = [(1, 0x0400), (2, 0x1400)]               # (setting, wMaxPacketSize: 1024, 3 x 1024)
ISO_INTERVAL = 4                                    # bInterval: 2^3 microframes, a millisecond
BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]


def le16(v):
    return struct.pack('<H', v)


def le32(v):
    return struct.pack('<I', v)


def yuv(r, g, b):
    """BT.601 studio range"""
    y = 16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255
    u = 128 + (-37.797 * r - 74.203 * g + 112.0 * b) / 255
    v = 128 + (112.0 * r - 93.786 * g - 18.214 * b) / 255
    return round(y), round(u), round(v)


def frame_size(fourcc_bpp, w, h):
    return w * h * fourcc_bpp // 8


def packet_bytes(mps):
    return (mps & 0x7FF) * (1 + (mps >> 11 & 3))


# ---- a baseline JPEG encoder, for the MJPEG frames ----

ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14,
          21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60,
          61, 54, 47, 55, 62, 63]
Q = 2                                               # every quantizer
# JPEG Annex K.3: the typical Huffman tables (bits per code length, values)
DC_LUM = ([0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0], list(range(12)))
DC_CHR = ([0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0], list(range(12)))
AC_LUM = ([0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d], bytes.fromhex(
    '01020300041105122131410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a25262728292a3435363738'
    '393a434445464748494a535455565758595a636465666768696a737475767778797a838485868788898a92939495969798999aa2a3a4a5'
    'a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7e8e9eaf1f2f3f4f5f6f7f8f9fa'))
AC_CHR = ([0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77], bytes.fromhex(
    '000102031104052131061241510761711322328108144291a1b1c109233352f0156272d10a162434e125f11718191a262728292a3536'
    '3738393a434445464748494a535455565758595a636465666768696a737475767778797a82838485868788898a92939495969798999aa2'
    'a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae2e3e4e5e6e7e8e9eaf2f3f4f5f6f7f8f9fa'))


def huff_codes(table):
    """{value: (code, length)} of a table (JPEG Annex C)"""
    bits, vals = table
    codes, code, k = {}, 0, 0
    for length in range(1, 17):
        for _ in range(bits[length - 1]):
            codes[vals[k]] = (code, length)
            code += 1
            k += 1
        code <<= 1
    return codes


HUFF = [(huff_codes(DC_LUM), huff_codes(AC_LUM)), (huff_codes(DC_CHR), huff_codes(AC_CHR))]
COS = [[(math.sqrt(0.5) if u == 0 else 1.0) * 0.5 * math.cos((2 * x + 1) * u * math.pi / 16) for x in range(8)]
       for u in range(8)]


def fdct(block):
    """The 8x8 block's (64 level-shifted samples, rows) quantized coefficients, in zigzag order"""
    rows = [[sum(COS[u][x] * block[y * 8 + x] for x in range(8)) for u in range(8)] for y in range(8)]
    out = [0.0] * 64
    for v in range(8):
        for u in range(8):
            out[v * 8 + u] = sum(COS[v][y] * rows[y][u] for y in range(8))
    return [int(round(out[z] / Q)) for z in ZIGZAG]


class Bits:
    """The entropy-coded segment being written (0xFF bytes stuffed)"""

    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def copy(self):
        b = Bits()
        b.out, b.acc, b.n = bytearray(self.out), self.acc, self.n
        return b

    def put(self, code, length):
        self.acc = (self.acc << length) | (code & ((1 << length) - 1))
        self.n += length
        while self.n >= 8:
            self.n -= 8
            byte = self.acc >> self.n & 0xFF
            self.out.append(byte)
            if byte == 0xFF:
                self.out.append(0)
        self.acc &= (1 << self.n) - 1

    def flush(self):
        if self.n:
            self.put((1 << (8 - self.n)) - 1, 8 - self.n)
        return bytes(self.out)


def put_value(bits, codes, sym_low, v):
    """A coefficient: its size category's code (with @sym_low's run) and its bits"""
    size = abs(v).bit_length()
    bits.put(*codes[sym_low | size])
    if size:
        bits.put(v if v > 0 else v + (1 << size) - 1, size)


class Jpeg:
    """Encodes w x h images of three full-size planes (Y, Cb, Cr: 4:4:4).
    Blocks of one value (most of the test card) skip the transform, and
    the coded rows of blocks above @fixed_rows are kept from the first
    image (the bars do not change)"""

    def __init__(self, w, h, fixed_rows):
        self.w, self.h, self.fixed = w, h, fixed_rows
        self.prefix = None
        self.cache = {}
        dqt = b'\xff\xdb' + struct.pack('>HB', 67, 0) + bytes([Q] * 64)
        sof = b'\xff\xc0' + struct.pack('>HBHHB', 17, 8, h, w, 3) + bytes([1, 0x11, 0, 2, 0x11, 0, 3, 0x11, 0])
        sos = b'\xff\xda' + struct.pack('>HB', 12, 3) + bytes([1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0])
        self.head = b'\xff\xd8' + dqt + sof + sos         # (no DHT: the typical tables)

    def block(self, bits, plane, bx, by, comp, pred):
        w = self.w
        o = by * 8 * w + bx * 8
        first = plane[o]
        rows = [bytes(plane[o + r * w:o + r * w + 8]) for r in range(8)]
        dc_codes, ac_codes = HUFF[1 if comp else 0]
        if all(r == bytes([first]) * 8 for r in rows):
            coefs = None
            dc = int(round(8 * (first - 128) / Q))
        else:
            key = b''.join(rows)
            coefs = self.cache.get(key)
            if coefs is None:
                coefs = self.cache[key] = fdct([b - 128 for b in key])
            dc = coefs[0]
        put_value(bits, dc_codes, 0, dc - pred[comp])
        pred[comp] = dc
        if coefs:
            run = 0
            for c in coefs[1:]:
                if not c:
                    run += 1
                    continue
                while run > 15:
                    bits.put(*ac_codes[0xF0])
                    run -= 16
                put_value(bits, ac_codes, run << 4, c)
                run = 0
            if run:
                bits.put(*ac_codes[0x00])
        else:
            bits.put(*ac_codes[0x00])

    def encode(self, planes):
        bw, bh = self.w // 8, self.h // 8
        if self.prefix is None:
            bits, pred = Bits(), [0, 0, 0]
            for by in range(self.fixed):
                for bx in range(bw):
                    for c in range(3):
                        self.block(bits, planes[c], bx, by, c, pred)
            self.prefix = (bits, pred)
        bits, pred = self.prefix[0].copy(), list(self.prefix[1])
        for by in range(self.fixed, bh):
            for bx in range(bw):
                for c in range(3):
                    self.block(bits, planes[c], bx, by, c, pred)
        return self.head + bits.flush() + b'\xff\xd9'


def jfif_ycc(r, g, b):
    """JFIF's full-range YCbCr"""
    y = 0.299 * r + 0.587 * g + 0.114 * b
    return (max(0, min(255, round(y))), max(0, min(255, round(128 + (b - y) / 1.772))),
            max(0, min(255, round(128 + (r - y) / 1.402))))


class MjpegCard:
    """The test card as MJPEG frames: the same bars and frame-number blocks"""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.band = h * 7 // 8 & ~1
        bar_w = w // 8
        self.planes = [bytearray(w * h) for _ in range(3)]
        row = [jfif_ycc(*BARS[min(x // bar_w, 7)]) for x in range(w)]
        for c in range(3):
            line = bytes(p[c] for p in row)
            for y in range(self.band):
                self.planes[c][y * w:(y + 1) * w] = line
        self.white, self.black = jfif_ycc(255, 255, 255), jfif_ycc(0, 0, 0)
        self.jpeg = Jpeg(w, h, self.band // 8)

    def frame(self, n):
        w, h, bw = self.w, self.h, self.w // 16
        for k in range(16):
            col = self.white if n >> k & 1 else self.black
            for c in range(3):
                blk = bytes([col[c]]) * bw
                for y in range(self.band, h):
                    self.planes[c][y * w + k * bw:y * w + (k + 1) * bw] = blk
        return self.jpeg.encode(self.planes)


def find(fmt, frm):
    for fi, guid, bpp, frames in FORMATS:
        if fi == fmt:
            for fr in frames:
                if fr[0] == frm:
                    return guid, bpp, fr
    return None


class Card:
    """The test card of one format and frame size; frame(n) has n's blocks"""

    def __init__(self, guid, w, h):
        self.nv12, self.w, self.h = guid == NV12_GUID, w, h
        self.band = h * 7 // 8 & ~1
        bar_w = w // 8
        if self.nv12:
            y_row = bytearray()
            uv_row = bytearray()
            for x in range(0, w, 2):
                yy, u, v = yuv(*BARS[min(x // bar_w, 7)])
                y_row += bytes([yy, yy])
                uv_row += bytes([u, v])
            black_y, bu, bv = yuv(0, 0, 0)
            self.base_y = bytearray(bytes(y_row) * self.band + bytes([black_y]) * (w * (h - self.band)))
            self.base_uv = bytearray(bytes(uv_row) * (self.band // 2) + bytes([bu, bv]) * (w // 2 * ((h - self.band) // 2)))
        else:
            row = bytearray()
            for x in range(0, w, 2):
                yy, u, v = yuv(*BARS[min(x // bar_w, 7)])
                row += bytes([yy, u, yy, v])
            by, bu, bv = yuv(0, 0, 0)
            self.base = bytearray(bytes(row) * self.band + bytes([by, bu, by, bv]) * (w // 2 * (h - self.band)))
        wy, wu, wv = yuv(255, 255, 255)
        by, bu, bv = yuv(0, 0, 0)
        bw = w // 16
        if self.nv12:
            self.ones = bytes([wy]) * bw, bytes([wu, wv]) * (bw // 2)
            self.zeros = bytes([by]) * bw, bytes([bu, bv]) * (bw // 2)
        else:
            self.ones = bytes([wy, wu, wy, wv]) * (bw // 2)
            self.zeros = bytes([by, bu, by, bv]) * (bw // 2)

    def frame(self, n):
        w, h, bw = self.w, self.h, self.w // 16
        if self.nv12:
            ys, uvs = self.base_y, self.base_uv
            for k in range(16):
                ones = n >> k & 1
                yb, uvb = self.ones if ones else self.zeros
                for y in range(self.band, h):
                    ys[y * w + k * bw: y * w + (k + 1) * bw] = yb
                for y in range(self.band // 2, h // 2):
                    uvs[y * w + k * bw: y * w + (k + 1) * bw] = uvb
            return bytes(ys + uvs)
        b = self.base
        for k in range(16):
            blk = self.ones if n >> k & 1 else self.zeros
            for y in range(self.band, h):
                o = (y * w + k * bw) * 2
                b[o:o + bw * 2] = blk
        return bytes(b)


class Camera:
    def __init__(self):
        self.config = 0
        self.probe = self.default_probe()
        self.committed = None              # (guid, bpp, frame, interval)
        self.cards = {}
        self.data = b''                    # the frame being sent
        self.offset = 0
        self.fid = 0
        self.count = 0
        self.streaming = False
        self.alt = 0                       # (--iso: the streaming interface's setting)
        self.next_frame = 0.0              # (--iso: when the next frame is due)

    def default_probe(self, fmt=1, frm=1, interval=0):
        guid, bpp, fr = find(fmt, frm) or find(1, 1)
        interval = interval or fr[3][0]
        payload = (packet_bytes(ISO_ALTS[1][1]) if guid == MJPG else packet_bytes(ISO_ALTS[0][1])) if ISO else PAYLOAD
        p = struct.pack('<HBBIHHHHHII', 1, fmt, frm, interval, 0, 0, 0, 0, 0, frame_size(bpp, fr[1], fr[2]), payload)
        return p + le32(48000000) + bytes([3, 1, 1, 1])

    def device(self):
        return bytes([18, 1]) + le16(0x0200) + bytes([0xEF, 0x02, 0x01, 64]) + le16(VID) + le16(PID) + \
            le16(0x0100) + bytes([1, 2, 3, 1])

    def config_desc(self):
        vc = bytes([18, 0x24, 0x02, 1]) + le16(0x0201) + bytes([0, 0]) + le16(0) + le16(0) + le16(0) + bytes([3, 0, 0, 0])
        vc += bytes([9, 0x24, 0x03, 2]) + le16(0x0101) + bytes([0, 1, 0])
        vc_hdr_len = 13
        vc = bytes([vc_hdr_len, 0x24, 0x01]) + le16(UVC) + le16(vc_hdr_len + len(vc)) + le32(48000000) + bytes([1, 1]) + vc
        vs = b''
        for fi, guid, bpp, frames in FORMATS:
            if guid == MJPG:                                            # VS_FORMAT_MJPEG
                vs += bytes([11, 0x24, 0x06, fi, len(frames), 1, 1, 0, 0, 0, 0])
            else:
                vs += bytes([27, 0x24, 0x04, fi, len(frames)]) + guid + bytes([bpp, 1, 0, 0, 0, 0])
            for idx, w, h, ivs in frames:
                size = frame_size(bpp, w, h)
                fr = bytes([0x24, 0x07 if guid == MJPG else 0x05, idx, 0]) + le16(w) + le16(h) + \
                    le32(size * 8 * 15) + le32(size * 8 * 30) + \
                    le32(size) + le32(ivs[0]) + bytes([len(ivs)]) + b''.join(le32(i) for i in ivs)
                vs += bytes([len(fr) + 1]) + fr
        hdr_len = 13 + len(FORMATS)
        vs = bytes([hdr_len, 0x24, 0x01, len(FORMATS)]) + le16(hdr_len + len(vs)) + bytes([EP_IN, 0, 2, 0, 0, 0, 1]) + \
            bytes(len(FORMATS)) + vs
        d = bytes([8, 0x0B, 0, 2, 0x0E, 0x03, 0x00, 2])                 # interface association
        d += bytes([9, 4, 0, 0, 0, 0x0E, 0x01, 0x00, 2]) + vc
        if ISO:                       # setting 0 without an endpoint, then one isochronous endpoint a setting
            d += bytes([9, 4, 1, 0, 0, 0x0E, 0x02, 0x00, 0]) + vs
            for alt, mps in ISO_ALTS:
                d += bytes([9, 4, 1, alt, 1, 0x0E, 0x02, 0x00, 0])
                d += bytes([7, 5, EP_IN, 0x05]) + le16(mps) + bytes([ISO_INTERVAL])
        else:
            d += bytes([9, 4, 1, 0, 1, 0x0E, 0x02, 0x00, 0]) + vs
            d += bytes([7, 5, EP_IN, 2]) + le16(512) + bytes([0])
        return bytes([9, 2]) + le16(9 + len(d)) + bytes([2, 1, 0, 0x80, 250]) + d

    def string(self, i):
        if i == 0:
            return bytes([4, 3]) + le16(0x0409)
        s = {1: 'NovaOS', 2: 'NovaOS Isochronous Camera' if ISO else 'NovaOS Test Camera',
             3: 'NOVA0004' if ISO else 'NOVA0003'}.get(i)
        if s is None:
            return None
        u = s.encode('utf-16-le')
        return bytes([2 + len(u), 3]) + u

    def endpoints(self):
        eps = [(0x00, 0, 0, 0, 64), (0x80, 0, 0, 0, 64)]
        if self.config and not ISO:
            eps.append((EP_IN, 2, 0, 1, 512))
        elif self.config and self.alt:
            eps.append((EP_IN, 1, 1 << (ISO_INTERVAL - 1), 1, dict(ISO_ALTS)[self.alt]))
        return eps

    def iso_packet(self, now):
        """--iso: the packet for this millisecond (b'': none, between frames)"""
        if not (self.committed and self.streaming and self.alt):
            return b''
        if not self.offset:
            if now < self.next_frame:
                return b''
            interval = self.committed[3] / 1e7
            self.next_frame = max(self.next_frame + interval, now - interval)
        return self.payload(packet_bytes(dict(ISO_ALTS)[self.alt]))

    def payload(self, want):
        """The next payload, at most @want bytes"""
        guid, bpp, fr, interval = self.committed
        if not self.offset:
            key = (guid, fr[1], fr[2])
            if key not in self.cards:
                self.cards[key] = MjpegCard(fr[1], fr[2]) if guid == MJPG else Card(guid, fr[1], fr[2])
            self.count += 1
            self.data = self.cards[key].frame(self.count)
            if self.count % 30 == 0 or self.count == 1:
                print(f'campeer: frame {self.count}', flush=True)
        n = min(len(self.data) - self.offset, min(want, PAYLOAD) - 2)
        chunk = self.data[self.offset:self.offset + n]
        self.offset += n
        eof = self.offset >= len(self.data)
        head = bytes([2, 0x80 | self.fid | (2 if eof else 0)])
        if eof:
            self.offset = 0
            self.fid ^= 1
        return head + chunk


class Conn:
    """One QEMU usb-redir connection"""

    def __init__(self, sock, cam):
        self.sock, self.cam = sock, cam
        self.wlock = threading.Lock()
        self.ids64 = False
        self.bulk32 = False
        self.held = []                     # bulk IN requests before the stream starts: (id, length)
        self.iso_stop = None               # --iso: the running stream's stop event

    def iso_loop(self, stop):
        """--iso: a packet every millisecond in real time until @stop is set
        (an empty one between frames: QEMU hands the guest what it has, and
        refills its buffer before giving more after running dry)"""
        t0, sent = time.monotonic(), 0
        tail = struct.pack('<Q' if self.ids64 else '<I', 0)
        while not stop.is_set():
            now = time.monotonic()
            due = int((now - t0) * 1000)
            if due - sent > 200:                         # (stalled: don't flood)
                sent = due - 20
            try:
                while sent < due:
                    if not self.cam.alt:                 # (the endpoint is gone until a setting brings it back)
                        sent = due
                        break
                    pk = self.cam.iso_packet(t0 + sent / 1000)
                    with self.wlock:
                        self.sock.sendall(struct.pack('<II', ISO_PACKET, 4 + len(pk)) + tail +
                                          struct.pack('<BBH', EP_IN, OK, len(pk)) + pk)
                    sent += 1
            except OSError:
                return
            time.sleep(0.001)

    def iso_end(self):
        if self.iso_stop:
            self.iso_stop.set()
            self.iso_stop = None

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
        ifaces = [(0, 0x0E, 1, 0), (1, 0x0E, 2, 0)]
        cols = [[f[k] for f in ifaces] + [0] * (32 - len(ifaces)) for k in range(4)]
        self.send(INTERFACE_INFO, struct.pack('<I', len(ifaces)) + b''.join(bytes(c) for c in cols))
        types, intervals, ifs, mps = [255] * 32, [0] * 32, [0] * 32, [0] * 32
        for addr, t, iv, iface, m in self.cam.endpoints():
            i = ((addr & 0x80) >> 3) | (addr & 0x0F)
            types[i], intervals[i], ifs[i], mps[i] = t, iv, iface, m
        self.send(EP_INFO, bytes(types) + bytes(intervals) + bytes(ifs) + struct.pack('<32H', *mps))

    def bulk_reply(self, pid, ep, status, data=b''):
        n = len(data)
        hdr = struct.pack('<BBHI', ep, status, n & 0xFFFF, 0)
        if self.bulk32:
            hdr += le16(n >> 16)
        self.send(BULK_PACKET, hdr, data, pid)

    def serve(self, pid, want):
        cam = self.cam
        if not cam.committed or not cam.streaming:
            self.held.append((pid, want))
            return
        self.bulk_reply(pid, EP_IN, OK, cam.payload(want))

    def control(self, pid, hdr, data):
        ep, req, rtype, _, value, index, length = struct.unpack('<BBBBHHH', hdr)
        cam = self.cam
        reply, status = b'', OK
        if rtype in (0x80, 0x81) and req == 6:                        # GET_DESCRIPTOR
            kind, i = value >> 8, value & 0xFF
            reply = {1: cam.device, 2: cam.config_desc}.get(kind, lambda: None)() if kind != 3 else cam.string(i)
            if reply is None:
                reply, status = b'', STALL
        elif rtype & 0x80 and req == 0:                               # GET_STATUS
            reply = b'\0\0'
        elif rtype == 0x02 and req == 1 and value == 0 and (index & 0xFF) == EP_IN:   # CLEAR_FEATURE(ENDPOINT_HALT)
            if cam.streaming:
                print('campeer: stopped', flush=True)
            cam.streaming, cam.offset = False, 0
        elif rtype == 0x21 and (index & 0xFF) == 1 and req == 0x01:    # SET_CUR on the streaming interface
            sel = value >> 8
            p = (data + bytes(PROBE_LEN))[:PROBE_LEN]
            fmt, frm, interval = p[2], p[3], struct.unpack('<I', p[4:8])[0]
            if not find(fmt, frm):
                status = STALL
            elif sel == 1:
                fr = find(fmt, frm)[2]
                cam.probe = cam.default_probe(fmt, frm, interval if interval in fr[3] else fr[3][0])
            elif sel == 2:
                guid, bpp, fr = find(fmt, frm)
                interval = interval if interval in fr[3] else fr[3][0]
                cam.committed = (guid, bpp, fr, interval)
                cam.offset, cam.streaming = 0, True
                print(f'campeer: commit format {fmt} frame {frm} interval {interval}', flush=True)
                print('campeer: streaming', flush=True)
                held, self.held = self.held, []
                for hp, hw in held:
                    self.serve(hp, hw)
            else:
                status = STALL
        elif rtype == 0xA1 and (index & 0xFF) == 1:                    # GET_* on the streaming interface
            sel = value >> 8
            if req == 0x85:                                           # GET_LEN
                reply = le16(PROBE_LEN)
            elif req == 0x86:                                         # GET_INFO
                reply = bytes([3])
            elif sel in (1, 2):
                reply = cam.probe if req == 0x81 else cam.default_probe()
            else:
                status = STALL
        elif rtype & 0x80:
            status = STALL
        if rtype & 0x80:
            reply = reply[:length]
        self.send(CONTROL_PACKET, struct.pack('<BBBBHHH', ep, req, rtype, status, value, index,
                                              len(reply) if rtype & 0x80 else length), reply, pid)

    def run(self):
        self.send(HELLO, b'novaos campeer'.ljust(64, b'\0') + struct.pack('<I', CAPS))
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
                    d = self.cam.device()
                    self.send(DEVICE_CONNECT, struct.pack('<BBBBHHH', SPEED_HIGH, d[4], d[5], d[6], VID, PID, 0x0100))
            elif ptype == CONTROL_PACKET:
                self.control(pid, body[:10], body[10:])
            elif ptype == SET_CONFIGURATION:
                self.cam.config = body[0]
                self.send_info()
                self.send(CONFIGURATION_STATUS, bytes([OK, self.cam.config]), pid=pid)
            elif ptype == GET_CONFIGURATION:
                self.send(CONFIGURATION_STATUS, bytes([OK, self.cam.config]), pid=pid)
            elif ptype == SET_ALT_SETTING:
                iface, alt = body[0], body[1]
                ok = alt == 0 or (ISO and iface == 1 and alt in dict(ISO_ALTS))
                if ok and ISO and iface == 1 and alt != self.cam.alt:
                    if self.cam.alt and self.cam.streaming:
                        print('campeer: stopped', flush=True)
                    self.cam.alt, self.cam.offset = alt, 0
                    if alt:
                        print(f'campeer: setting {alt}', flush=True)
                    else:
                        self.cam.streaming = False
                    self.send_info()
                self.send(ALT_SETTING_STATUS, bytes([OK if ok else STALL, iface, alt]), pid=pid)
            elif ptype == START_ISO_STREAM:
                self.send(ISO_STREAM_STATUS, bytes([OK, body[0]]), pid=pid)
                if not self.iso_stop:
                    self.iso_stop = threading.Event()
                    threading.Thread(target=self.iso_loop, args=(self.iso_stop,), daemon=True).start()
            elif ptype == STOP_ISO_STREAM:
                self.iso_end()
                self.send(ISO_STREAM_STATUS, bytes([OK, body[0]]), pid=pid)
            elif ptype == GET_ALT_SETTING:
                self.send(ALT_SETTING_STATUS, bytes([OK, body[0], self.cam.alt if body[0] == 1 else 0]), pid=pid)
            elif ptype == BULK_PACKET:
                hl = 10 if self.bulk32 else 8
                ep, st, n, _ = struct.unpack('<BBHI', body[:8])
                if self.bulk32:
                    n |= struct.unpack('<H', body[8:10])[0] << 16
                if ep & 0x80:
                    self.serve(pid, n)
                else:
                    self.bulk_reply(pid, ep, OK, b'')
                _ = hl
            elif ptype == CANCEL_DATA_PACKET:
                for i, (hp, hw) in enumerate(self.held):
                    if hp == pid:
                        del self.held[i]
                        self.bulk_reply(pid, EP_IN, CANCELLED)
                        break
            elif ptype == RESET:
                self.iso_end()
                self.cam.streaming, self.cam.offset, self.cam.alt = False, 0, 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--iso', action='store_true', help='an isochronous camera with MJPEG frames (1209:000b)')
    a = ap.parse_args()
    global ISO, PID, FORMATS
    if a.iso:
        ISO, PID, FORMATS = True, 0x000B, ISO_FORMATS
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', a.port))
    srv.listen(1)
    print(f'campeer: listening on {a.port}', flush=True)
    while True:
        s, _ = srv.accept()
        print('campeer: QEMU connected', flush=True)
        cam = Camera()
        conn = Conn(s, cam)
        try:
            conn.run()
        except (EOFError, OSError):
            pass
        conn.iso_end()
        print('campeer: QEMU went away', flush=True)
        s.close()


if __name__ == '__main__':
    sys.exit(main())
