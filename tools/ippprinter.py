#!/usr/bin/env python3
"""A fake IPP Everywhere printer for the network tests (tools/selftest.py).

    tools/ippprinter.py [--port 18631]

Two printers on one HTTP server (QEMU's user-mode network: 10.0.2.2 from
the guest), as a driverless network printer answers IPP/2.0 over HTTP:

  /ipp/print   "NovaOS Test Printer": takes application/pdf, image/pwg-raster
               and application/octet-stream
  /ipp/raster  "NovaOS Raster Printer": takes only image/pwg-raster (the
               spooler must turn its PDF into PWG Raster)

Operations: Get-Printer-Attributes, Validate-Job, Print-Job, Get-Jobs and
Get-Job-Attributes.  What each printer last printed can be fetched back
(plain HTTP GET), so the test in the guest compares it with what it sent:

  /received/print.bin    the last document, as it came
  /received/raster.ppm   the last PWG Raster document's first page,
                         decoded to a binary PPM (P6)
  /received/NAME.txt     the job's operation attributes and, for PWG
                         Raster, each page's header (one "key=value" per
                         line), or "error=..." when the request was bad
"""
import argparse
import http.server
import struct
import sys
import threading

OP_NAMES = {0x0002: 'Print-Job', 0x0004: 'Validate-Job', 0x0009: 'Get-Job-Attributes', 0x000A: 'Get-Jobs',
            0x000B: 'Get-Printer-Attributes'}
TAG_INT, TAG_BOOL, TAG_ENUM, TAG_RES, TAG_RANGE = 0x21, 0x22, 0x23, 0x32, 0x33
TAG_TEXT, TAG_NAME, TAG_KEYWORD, TAG_URI, TAG_CHARSET, TAG_LANG, TAG_MIME = 0x41, 0x42, 0x44, 0x45, 0x47, 0x48, 0x49

PRINTERS = {
    'print': dict(name='NovaOS Test Printer', formats=['application/pdf', 'image/pwg-raster', 'application/octet-stream'],
                  location='CI host, desk 1'),
    'raster': dict(name='NovaOS Raster Printer', formats=['image/pwg-raster'], location='CI host, desk 2'),
}
LOCK = threading.Lock()
RECEIVED = {}          # 'print.bin', 'raster.ppm', 'NAME.txt' -> bytes
JOBS = {}              # printer -> [(id, name)]
NEXT_JOB = [1]


def parse(body):
    """An IPP message: (version, op/status, request id, {group tag: [(name, tag, [values])]}, data)"""
    if len(body) < 9:
        raise ValueError('shorter than an IPP header')
    ver, op, rid = struct.unpack('>HHI', body[:8])
    groups, at, group, last = {}, 8, None, None
    while True:
        if at >= len(body):
            raise ValueError('no end-of-attributes tag')
        tag = body[at]
        at += 1
        if tag == 0x03:
            break
        if tag < 0x10:
            group = groups.setdefault(tag, [])
            last = None
            continue
        if group is None:
            raise ValueError('an attribute before its group')
        nlen = struct.unpack('>H', body[at:at + 2])[0]
        name = body[at + 2:at + 2 + nlen].decode('utf-8')
        at += 2 + nlen
        vlen = struct.unpack('>H', body[at:at + 2])[0]
        value = body[at + 2:at + 2 + vlen]
        at += 2 + vlen
        if tag in (TAG_INT, TAG_ENUM) and vlen == 4:
            value = struct.unpack('>i', value)[0]
        elif tag == TAG_BOOL and vlen == 1:
            value = bool(value[0])
        elif tag >= 0x40:
            value = value.decode('utf-8')
        if nlen:
            last = (name, tag, [value])
            group.append(last)
        elif last:
            last[2].append(value)
    return ver, op, rid, groups, body[at:]


def attr(name, tag, *values):
    out = b''
    for i, v in enumerate(values):
        if isinstance(v, str):
            v = v.encode('utf-8')
        elif tag in (TAG_INT, TAG_ENUM):
            v = struct.pack('>i', v)
        elif tag == TAG_BOOL:
            v = bytes([1 if v else 0])
        elif tag == TAG_RES:
            v = struct.pack('>iib', v[0], v[1], 3)
        elif tag == TAG_RANGE:
            v = struct.pack('>ii', *v)
        n = name.encode() if i == 0 else b''
        out += bytes([tag]) + struct.pack('>H', len(n)) + n + struct.pack('>H', len(v)) + v
    return out


def reply(status, rid, groups=()):
    out = struct.pack('>HHI', 0x0200, status, rid)
    out += b'\x01' + attr('attributes-charset', TAG_CHARSET, 'utf-8') + \
        attr('attributes-natural-language', TAG_LANG, 'en')
    for tag, data in groups:
        out += bytes([tag]) + data
    return out + b'\x03'


def printer_attrs(key, host):
    p = PRINTERS[key]
    uri = f'ipp://{host}/ipp/{key}'
    return (attr('printer-uri-supported', TAG_URI, uri) + attr('uri-security-supported', TAG_KEYWORD, 'none') +
            attr('uri-authentication-supported', TAG_KEYWORD, 'none') +
            attr('printer-name', TAG_NAME, p['name']) + attr('printer-info', TAG_TEXT, p['name']) +
            attr('printer-make-and-model', TAG_TEXT, p['name']) + attr('printer-location', TAG_TEXT, p['location']) +
            attr('printer-state', TAG_ENUM, 3) + attr('printer-state-reasons', TAG_KEYWORD, 'none') +
            attr('printer-is-accepting-jobs', TAG_BOOL, True) +
            attr('ipp-versions-supported', TAG_KEYWORD, '1.1', '2.0') +
            attr('operations-supported', TAG_ENUM, *OP_NAMES) +
            attr('document-format-supported', TAG_MIME, *p['formats']) +
            attr('document-format-default', TAG_MIME, p['formats'][-1]) +
            attr('pwg-raster-document-resolution-supported', TAG_RES, (300, 300), (600, 600)) +
            attr('pwg-raster-document-type-supported', TAG_KEYWORD, 'sgray_8', 'srgb_8') +
            attr('media-supported', TAG_KEYWORD, 'na_letter_8.5x11in', 'iso_a4_210x297mm') +
            attr('media-default', TAG_KEYWORD, 'na_letter_8.5x11in') +
            attr('color-supported', TAG_BOOL, True) + attr('printer-up-time', TAG_INT, 1000) +
            attr('queued-job-count', TAG_INT, 0))


def pwg_decode(data):
    """A PWG Raster document: ([page header dicts], the first page as (w, h, rgb bytes))"""
    if data[:4] != b'RaS2':
        raise ValueError('not PWG Raster (no RaS2)')
    at, pages, first = 4, [], None
    while at < len(data):
        h = data[at:at + 1796]
        if len(h) < 1796:
            raise ValueError('a page header is cut short')
        at += 1796
        u = lambda off: struct.unpack('>I', h[off:off + 4])[0]
        hdr = dict(MediaClass=h[0:64].split(b'\0')[0].decode(), xdpi=u(276), ydpi=u(280), PageSize=(u(352), u(356)),
                   Width=u(372), Height=u(376), BitsPerColor=u(384), BitsPerPixel=u(388), BytesPerLine=u(392),
                   ColorOrder=u(396), ColorSpace=u(400), NumColors=u(420), TotalPageCount=u(452),
                   PageSizeName=h[1732:1796].split(b'\0')[0].decode())
        if hdr['MediaClass'] != 'PwgRaster':
            raise ValueError(f"page {len(pages) + 1}: MediaClass {hdr['MediaClass']!r}, not 'PwgRaster'")
        bpp = hdr['BitsPerPixel'] // 8
        w, hgt = hdr['Width'], hdr['Height']
        if hdr['BitsPerColor'] != 8 or bpp not in (1, 3) or hdr['BytesPerLine'] != w * bpp or not w or not hgt:
            raise ValueError(f'page {len(pages) + 1}: an unexpected format {hdr}')
        lines, y = [], 0
        while y < hgt:
            rep = data[at] + 1
            at += 1
            line = bytearray()
            while len(line) < w * bpp:
                n = data[at]
                at += 1
                if n == 128:
                    line += b'\xff' * (w * bpp - len(line))
                elif n < 128:
                    line += data[at:at + bpp] * (n + 1)
                    at += bpp
                else:
                    k = (257 - n) * bpp
                    line += data[at:at + k]
                    at += k
            if len(line) != w * bpp:
                raise ValueError(f'page {len(pages) + 1}, line {y}: {len(line)} bytes, not {w * bpp}')
            if not pages:
                lines += [bytes(line)] * rep
            y += rep
        if y != hgt:
            raise ValueError(f'page {len(pages) + 1}: {y} lines, not {hgt}')
        if not pages:
            rgb = b''.join(lines)
            if bpp == 1:
                rgb = bytes(b for g in rgb for b in (g, g, g))
            first = (w, hgt, rgb)
        pages.append(hdr)
    return pages, first


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, fmt, *args):
        print('ippprinter: ' + fmt % args, flush=True)

    def send(self, code, body, ctype):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        name = self.path.rsplit('/', 1)[-1]
        with LOCK:
            body = RECEIVED.get(name) if self.path.startswith('/received/') else None
        if body is None:
            self.send(404, b'nothing printed\n', 'text/plain')
        else:
            self.send(200, body, 'application/octet-stream')

    def body(self):
        if self.headers.get('Transfer-Encoding', '').lower() == 'chunked':
            out = b''
            while True:
                n = int(self.rfile.readline().split(b';')[0], 16)
                if not n:
                    self.rfile.readline()
                    return out
                out += self.rfile.read(n)
                self.rfile.readline()
        return self.rfile.read(int(self.headers.get('Content-Length', 0)))

    def do_POST(self):
        key = self.path.rstrip('/').rsplit('/', 1)[-1]
        data = self.body()
        if key not in PRINTERS or not self.path.startswith('/ipp/'):
            return self.send(404, b'no such printer\n', 'text/plain')
        if self.headers.get('Content-Type', '').split(';')[0].strip() != 'application/ipp':
            return self.send(400, b'not application/ipp\n', 'text/plain')
        try:
            ver, op, rid, groups, doc = parse(data)
        except (ValueError, struct.error, UnicodeDecodeError) as e:
            print(f'ippprinter: a bad request: {e}', flush=True)
            return self.send(400, b'bad IPP\n', 'text/plain')
        opattrs = {n: v[0] if len(v) == 1 else v for n, _, v in groups.get(0x01, [])}
        print(f'ippprinter: {key}: {OP_NAMES.get(op, hex(op))} {ver >> 8}.{ver & 0xff} {opattrs}', flush=True)
        host = self.headers.get('Host', 'localhost')
        if ver >> 8 not in (1, 2):
            out = reply(0x0503, rid)                       # server-error-version-not-supported
        elif 'attributes-charset' not in opattrs or 'printer-uri' not in opattrs:
            out = reply(0x0400, rid)                       # client-error-bad-request
        elif op == 0x000B:
            out = reply(0x0000, rid, [(0x04, printer_attrs(key, host))])
        elif op in (0x0002, 0x0004):
            fmt = opattrs.get('document-format', 'application/octet-stream')
            if fmt not in PRINTERS[key]['formats']:
                out = reply(0x040A, rid)                   # client-error-document-format-not-supported
            elif op == 0x0004:
                out = reply(0x0000, rid)
            else:
                out = self.print_job(key, rid, opattrs, fmt, doc, host)
        elif op in (0x0009, 0x000A):
            with LOCK:
                jobs = JOBS.get(key, [])
            if op == 0x0009:
                jobs = [j for j in jobs if j[0] == opattrs.get('job-id')]
            groups = [(0x02, attr('job-id', TAG_INT, j[0]) + attr('job-state', TAG_ENUM, 9) +
                       attr('job-name', TAG_NAME, j[1])) for j in jobs]
            out = reply(0x0000 if jobs or op == 0x000A else 0x0406, rid, groups)
        else:
            out = reply(0x0501, rid)                       # server-error-operation-not-supported
        self.send(200, out, 'application/ipp')

    def print_job(self, key, rid, opattrs, fmt, doc, host):
        lines = [f'{n}={v}' for n, v in opattrs.items()] + [f'bytes={len(doc)}']
        status = 0x0000
        ppm = None
        try:
            if fmt == 'image/pwg-raster':
                pages, (w, h, rgb) = pwg_decode(doc)
                lines.append(f'pages={len(pages)}')
                for i, p in enumerate(pages):
                    lines.append(f'page{i + 1}=' + ' '.join(f'{k}:{v}' for k, v in p.items()))
                ppm = b'P6\n%d %d\n255\n' % (w, h) + rgb
            elif fmt == 'application/pdf' and not doc.startswith(b'%PDF-'):
                raise ValueError('application/pdf that is not a PDF')
        except (ValueError, IndexError, struct.error) as e:
            lines.append(f'error={e}')
            status = 0x0400
        with LOCK:
            job = NEXT_JOB[0]
            NEXT_JOB[0] += 1
            JOBS.setdefault(key, []).append((job, str(opattrs.get('job-name', ''))))
            RECEIVED[f'{key}.bin'] = doc
            RECEIVED[f'{key}.txt'] = ('\n'.join(lines) + '\n').encode()
            if ppm:
                RECEIVED[f'{key}.ppm'] = ppm
        print(f'ippprinter: {key}: job {job}: ' + '; '.join(lines), flush=True)
        if status:
            return reply(status, rid)
        return reply(0x0000, rid, [(0x02, attr('job-id', TAG_INT, job) +
                                    attr('job-uri', TAG_URI, f'ipp://{host}/ipp/{key}/{job}') +
                                    attr('job-state', TAG_ENUM, 9) + attr('job-state-reasons', TAG_KEYWORD, 'none'))])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--port', type=int, default=18631)
    a = ap.parse_args()
    srv = http.server.ThreadingHTTPServer(('0.0.0.0', a.port), Handler)
    print(f'ippprinter: listening on {a.port}', flush=True)
    srv.serve_forever()


if __name__ == '__main__':
    sys.exit(main())
