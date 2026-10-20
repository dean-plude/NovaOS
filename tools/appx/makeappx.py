#!/usr/bin/env python3
"""makeappx.py: pack a folder into an app package (.msix, .appx) as the
Windows SDK's MakeAppx.exe "pack" does:

    tools/appx/makeappx.py FOLDER OUT.msix

The folder holds the package's files, its AppxManifest.xml among them.  The
package is a zip whose every file is deflated 64 KiB block by block (each
block flushed, so a block can be checked alone), with
  AppxBlockMap.xml     each block's SHA-256 and compressed size
  [Content_Types].xml  the files' MIME types
written after the files, as MakeAppx writes them.  The package is not
signed (no AppxSignature.p7x); NovaOS's appx.exe, as Windows, installs an
unsigned package only when asked to (-AllowUnsigned) and only if its
publisher carries Microsoft's "unsigned" OID.

pack() is what tools/appx/mktest.py packs the appx self-test's packages with.
"""
import base64
import hashlib
import os
import struct
import sys
import urllib.parse
import zlib

BLOCK = 65536
STAMP = (0, 0x21)                       # (DOS time, date: 1980-01-01, so packages are reproducible)
TYPES = {'exe': 'application/x-msdownload', 'dll': 'application/x-msdownload', 'pri': 'application/octet-stream',
         'png': 'image/png', 'xml': 'text/xml', 'txt': 'text/plain', 'winmd': 'application/octet-stream'}


def deflate_blocks(data):
    """@data deflated 64 KiB at a time, each block ending on a flush: the
    stream and each block's compressed size"""
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    out, sizes = b'', []
    for i in range(0, len(data), BLOCK):
        last = i + BLOCK >= len(data)
        part = c.compress(data[i:i + BLOCK]) + c.flush(zlib.Z_FINISH if last else zlib.Z_FULL_FLUSH)
        sizes.append(len(part))
        out += part
    if not data:
        out = c.flush(zlib.Z_FINISH)
    return out, sizes


def zip_name(name):
    """the zip's name for a file: '/', its reserved characters %-escaped"""
    return urllib.parse.quote(name.replace('\\', '/'), safe="/!$&'()*+,;=-._~@[]")


def pack(files, change=None, signature=None):
    """the package's bytes for @files [(name, bytes)]; @change, when given,
    maps a name to the bytes written for it in place of the bytes the
    block map hashes (a damaged package, for tests); @signature, the bytes
    of an AppxSignature.p7x (outside the block map, as a signature is)"""
    zip_, central, entries = b'', b'', []

    def add(name, data, method, packed):
        nonlocal zip_, central
        n = zip_name(name).encode('utf-8')
        crc = zlib.crc32(data)
        head = struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 0, method, STAMP[0], STAMP[1], crc, len(packed), len(data),
                           len(n), 0) + n
        central += struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 20, 20, 0, method, STAMP[0], STAMP[1], crc,
                               len(packed), len(data), len(n), 0, 0, 0, 0, 0, len(zip_)) + n
        zip_ += head + packed
        entries.append(name)
        return len(head)

    blockmap = ['<?xml version="1.0" encoding="UTF-8" standalone="no"?>',
                '<BlockMap xmlns="http://schemas.microsoft.com/appx/2010/blockmap" '
                'HashMethod="http://www.w3.org/2001/04/xmlenc#sha256">']
    for name, data in files:
        packed, sizes = deflate_blocks(data)
        written = (change or {}).get(name)
        if written is not None:
            packed = deflate_blocks(written)[0]
        lfh = add(name, written if written is not None else data, 8, packed)
        blocks = ''.join('<Block Hash="%s" Size="%d"/>' % (
            base64.b64encode(hashlib.sha256(data[i:i + BLOCK]).digest()).decode(), sizes[k])
            for k, i in enumerate(range(0, len(data), BLOCK)))
        blockmap.append('<File Name="%s" Size="%d" LfhSize="%d">%s</File>' % (
            name.replace('/', '\\').replace('&', '&amp;').replace('"', '&quot;'), len(data), lfh, blocks))
    blockmap = ('\r\n'.join(blockmap) + '</BlockMap>').encode('utf-8')
    add('AppxBlockMap.xml', blockmap, 8, deflate_blocks(blockmap)[0])
    exts = sorted({n.rsplit('.', 1)[1].lower() for n, _ in files if '.' in n and n != 'AppxManifest.xml'})
    types = ('<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/'
             'content-types">' + ''.join('<Default Extension="%s" ContentType="%s"/>' % (
                 e, TYPES.get(e, 'application/octet-stream')) for e in exts) +
             '<Override PartName="/AppxManifest.xml" ContentType="application/vnd.ms-appx.manifest+xml"/>'
             '<Override PartName="/AppxBlockMap.xml" ContentType="application/vnd.ms-appx.blockmap+xml"/>'
             '</Types>').encode('utf-8')
    add('[Content_Types].xml', types, 8, deflate_blocks(types)[0])
    if signature is not None:
        add('AppxSignature.p7x', signature, 0, signature)
    end = struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(entries), len(entries), len(central), len(zip_), 0)
    return zip_ + central + end


def folder_files(folder):
    """[(name, bytes)] of the files under @folder, AppxManifest.xml last"""
    out = []
    for root, dirs, names in os.walk(folder):
        dirs.sort()
        for n in sorted(names):
            p = os.path.join(root, n)
            out.append((os.path.relpath(p, folder).replace(os.sep, '\\'), open(p, 'rb').read()))
    if not any(n == 'AppxManifest.xml' for n, _ in out):
        raise SystemExit(f'makeappx: {folder}: no AppxManifest.xml')
    return sorted(out, key=lambda f: f[0] == 'AppxManifest.xml')


def write(path, data):
    if not os.path.exists(path) or open(path, 'rb').read() != data:
        with open(path, 'wb') as f:
            f.write(data)


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    write(argv[1], pack(folder_files(argv[0])))


if __name__ == '__main__':
    main(sys.argv[1:])
