#!/usr/bin/env python3
"""catalog_fixture.py — a stand-in for the Microsoft Update Catalog that the
driver-search self-test (tests/selftest/devices/driversearch) points
pnputil at, so CI downloads a driver by hardware ID without the network

    catalog_fixture.py PORT WORKDIR

It serves, on 127.0.0.1:PORT (10.0.2.2:PORT from QEMU's user network),
the Catalog's three requests from the pages recorded in
tests/selftest/devices/driversearch/catalog:

  GET  /Search.aspx?q=ID          the results page (search.html) for the
                                  QEMU virtio-serial port's second hardware
                                  ID, PCI\\VEN_1AF4&DEV_1003&SUBSYS_00031AF4,
                                  and "no results" (none.html) for any other
  POST /DownloadDialog.aspx       the update's files (download.html), for
                                  updateIDs=[{..."updateID":"ID"}]
  GET  /d/msdownload/update/driver/drvs/2026/10/NAME.cab
                                  the package's cabinet

The package is mkdriver.py's test package made over for the virtio-serial
port (novaser.inf, a System-class driver for its hardware ID, with
placeholder .sys and .cat files), packed with MSZIP like the Catalog's
cabinets and followed by bytes past the cabinet's end, where a signed
cabinet keeps its signature.  Every update ID in search.html gets that
cabinet, except that the dialog of 958a8300-... gives a SHA-256 that does
not match it: pnputil must refuse that download and take the next result.
Requests go to WORKDIR/catalog.log.
"""
import base64, hashlib, http.server, json, os, sys, urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PAGES = os.path.join(ROOT, 'tests', 'selftest', 'devices', 'driversearch', 'catalog')
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import mkdriver                      # noqa: E402  (tools/drivertest)
import make_cabtest_data as cab      # noqa: E402  (tools)

FOUND = 'PCI\\VEN_1AF4&DEV_1003&SUBSYS_00031AF4'
BAD_HASH = '958a8300-effe-4113-98d8-f7ded68166c2'
TITLES = {'d4966e96-b0ee-4361-8867-d53fb5656d5c': 'NovaOS Test System Driver Update (2.0.0.0)',
          '958a8300-effe-4113-98d8-f7ded68166c2': 'NovaOS Test - System - 2.0.0.0',
          '18ae1212': 'NovaOS Test - System - 1.0.0.0'}
CAB_NAME = '5e2d7a10-6f1b-4c8e-9a0d-2b7f3c9e4d61_novaser.cab'


def package():
    inf = mkdriver.INF.decode('ascii').replace('novadrv', 'novaser').replace('NovaDrv', 'NovaSer') \
        .replace('ROOT\\NOVADRV', FOUND + '&REV_00') \
        .replace('"NovaOS Test Device"', '"NovaOS Test VirtIO Serial Driver"') \
        .replace('DriverVer   = 10/10/2026,1.0.0.0', 'DriverVer   = 01/12/2025,1.0.0.0').encode('ascii')
    files = [('novaser.inf', inf), ('novaser.sys', mkdriver.SYS), ('novaser.cat', mkdriver.CAT)]
    data = cab.cabinet(cab.entries(files), 1, cab.mszip(b''.join(d for _, d in files)))
    return data + b'\0' * 8 + b'(where a signed cabinet keeps its signature)\0'


CAB = package()
SHA1 = base64.b64encode(hashlib.sha1(CAB).digest()).decode()
SHA256 = base64.b64encode(hashlib.sha256(CAB).digest()).decode()


class Handler(http.server.BaseHTTPRequestHandler):
    def send(self, body, ctype='text/html; charset=utf-8'):
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def page(self, name):
        return open(os.path.join(PAGES, name), encoding='utf-8').read()

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        if url.path.lower() == '/search.aspx':
            q = urllib.parse.parse_qs(url.query).get('q', [''])[0]
            if q.lower() == FOUND.lower():
                return self.send(self.page('search.html').encode())
            return self.send(self.page('none.html').replace('{query}', q).encode())
        if url.path == '/d/msdownload/update/driver/drvs/2026/10/' + CAB_NAME:
            return self.send(CAB, 'application/vnd.ms-cab-compressed')
        self.send_error(404)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode()
        if urllib.parse.urlsplit(self.path).path.lower() != '/downloaddialog.aspx':
            return self.send_error(404)
        try:
            uid = json.loads(urllib.parse.parse_qs(body)['updateIDs'][0])[0]['updateID']
        except (KeyError, IndexError, ValueError):
            return self.send_error(400)
        title = next((t for k, t in TITLES.items() if uid.startswith(k)), 'NovaOS Test - System')
        host = self.headers.get('Host', '10.0.2.2')
        wrong = base64.b64encode(hashlib.sha256(b'changed on the way').digest()).decode()
        self.send(self.page('download.html').format(
            update_id=uid, title=title, url=f'http://{host}/d/msdownload/update/driver/drvs/2026/10/{CAB_NAME}',
            sha1=SHA1, sha256=wrong if uid == BAD_HASH else SHA256, file_name=CAB_NAME,
            name_length=len(CAB_NAME)).encode())

    def log_message(self, fmt, *args):
        sys.stderr.write('%s\n' % (fmt % args))
        sys.stderr.flush()


def main():
    port, work = int(sys.argv[1]), sys.argv[2]
    os.makedirs(work, exist_ok=True)
    sys.stderr = open(os.path.join(work, 'catalog.log'), 'w')
    http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()


if __name__ == '__main__':
    main()
