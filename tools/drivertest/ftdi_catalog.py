#!/usr/bin/env python3
"""ftdi_catalog.py — the Microsoft Update Catalog's answers for FTDI's USB
serial driver, for the usb-serial self-test (tests/selftest/devices/usbserial)

    ftdi_catalog.py PORT WORKDIR

Like catalog_fixture.py, it serves on 127.0.0.1:PORT (10.0.2.2:PORT from
QEMU's user network) the Catalog's three requests, from pages recorded
from it (tests/selftest/devices/usbserial/catalog):

  GET  /Search.aspx?q=ID          search-usb.html for the FT232's hardware
                                  ID USB\\VID_0403&PID_6001, search-ports.html
                                  for the COM port ftdibus.sys reports,
                                  FTDIBUS\\COMPORT&VID_0403&PID_6001, and
                                  "no results" for any other
  POST /DownloadDialog.aspx       the update's file: every FTDI 2.12.36.20
                                  update in those pages has the same one
  GET  /d/msdownload/update/driver/drvs/2025/10/NAME.cab
                                  FTDI's own signed cabinet (ftdibus.inf,
                                  ftdiport.inf, their catalogs, ftdibus.sys,
                                  ftser2k.sys), as the Catalog serves it

The cabinet is the real one: FTDI's package, signed by Microsoft (WHQL).
It is not in the repository; it is downloaded once from the Catalog's
address into WORKDIR (or NOVA_DRIVER_CACHE, when set, so CI can keep it),
checked against the SHA-256 the Catalog gives, and served from there.
Requests go to WORKDIR/catalog.log.
"""
import base64, hashlib, http.server, json, os, sys, time, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PAGES = os.path.join(ROOT, 'tests', 'selftest', 'devices', 'usbserial', 'catalog')
TEMPLATES = os.path.join(ROOT, 'tests', 'selftest', 'devices', 'driversearch', 'catalog')

SEARCHES = {'usb\\vid_0403&pid_6001': 'search-usb.html', 'ftdibus\\comport&vid_0403&pid_6001': 'search-ports.html'}
CAB_PATH = '/d/msdownload/update/driver/drvs/2025/10/'
CAB_NAME = '3c6ba1b9-6ca5-4e0a-903e-d60cfcf0c09c_04780a77d96c6214457450540118639a2b542f31.cab'
CAB_URL = 'https://catalog.s.download.windowsupdate.com' + CAB_PATH + CAB_NAME
SHA256 = 'udno06DeOg0piGFB2z7oeFbgqO277q2q9Sw31aLsUfg='      # (base64, as the Catalog gives it)
SHA1 = 'BHgKd9lsYhRFdFBUARhjmitULzE='
TITLE = 'FTDI - USB - 2.12.36.20'
PAUSES = (10, 30, 60, 120)                                  # seconds between attempts, as tools/download_cache.py


def cabinet(work):
    """FTDI's cabinet, downloaded once and checked"""
    cache = os.environ.get('NOVA_DRIVER_CACHE') or work
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, CAB_NAME)
    for pause in PAUSES + (None,):
        if os.path.exists(path):
            data = open(path, 'rb').read()
            if base64.b64encode(hashlib.sha256(data).digest()).decode() == SHA256:
                return data
            os.unlink(path)
        try:
            with urllib.request.urlopen(CAB_URL, timeout=120) as r:
                data = r.read()
            with open(path + '.part', 'wb') as f:
                f.write(data)
            os.replace(path + '.part', path)
            continue
        except OSError as e:
            sys.stderr.write('download of %s failed: %s\n' % (CAB_URL, e))
        if pause is None:
            break
        time.sleep(pause)
    raise SystemExit('FTDI driver cabinet unavailable: ' + CAB_URL)


class Handler(http.server.BaseHTTPRequestHandler):
    cab = b''

    def send(self, body, ctype='text/html; charset=utf-8'):
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        if url.path.lower() == '/search.aspx':
            q = urllib.parse.parse_qs(url.query).get('q', [''])[0]
            page = SEARCHES.get(q.lower())
            if page:
                return self.send(open(os.path.join(PAGES, page), 'rb').read())
            none = open(os.path.join(TEMPLATES, 'none.html'), encoding='utf-8').read()
            return self.send(none.replace('{query}', q).encode())
        if url.path == CAB_PATH + CAB_NAME:
            return self.send(self.cab, 'application/vnd.ms-cab-compressed')
        self.send_error(404)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
        if urllib.parse.urlsplit(self.path).path.lower() != '/downloaddialog.aspx':
            return self.send_error(404)
        try:
            uid = json.loads(urllib.parse.parse_qs(body)['updateIDs'][0])[0]['updateID']
        except (KeyError, IndexError, ValueError):
            return self.send_error(400)
        host = self.headers.get('Host', '10.0.2.2')
        page = open(os.path.join(TEMPLATES, 'download.html'), encoding='utf-8').read()
        self.send(page.format(update_id=uid, title=TITLE, url=f'http://{host}{CAB_PATH}{CAB_NAME}', sha1=SHA1,
                              sha256=SHA256, file_name=CAB_NAME, name_length=len(CAB_NAME)).encode())

    def log_message(self, fmt, *args):
        sys.stderr.write('%s\n' % (fmt % args))
        sys.stderr.flush()


def main():
    port, work = int(sys.argv[1]), sys.argv[2]
    os.makedirs(work, exist_ok=True)
    sys.stderr = open(os.path.join(work, 'catalog.log'), 'w')
    Handler.cab = cabinet(work)
    http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()


if __name__ == '__main__':
    main()
