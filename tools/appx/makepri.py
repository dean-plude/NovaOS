#!/usr/bin/env python3
"""makepri.py: write a resources.pri (the Package Resource Index of an app
package) from .resw string tables, as the Windows SDK's MakePri.exe does for
the strings a manifest names with "ms-resource:".

    tools/appx/makepri.py PACKAGE_NAME OUT.pri TABLE.resw...

Each TABLE.resw (the XML resource format: <data name="Key"><value>Text
</value></data>) becomes the scope named after its file ("Resources.resw"
gives ms-resource://PACKAGE_NAME/Resources/Key).  Every string is one
neutral candidate (no language, scale or contrast qualifiers): enough for a
package's own display strings; MakePri's qualified candidates, files and
embedded data are not written.

The file is the format Microsoft's MRT Core reads (third_party/mrtcore,
MIT; userland/mrm builds it as NovaOS's MRM.dll), laid out as MakePri lays
out a "mrm_pri2" file, each section from the structure MRT Core declares:
  [mrm_decn_info]  the decisions: here only MRT's two fixed ones (empty, and
                   "neutral only": one unconditional candidate)
  [mrm_pridescex]  which sections are the schema, decisions, map, data
  [mrm_hschemaex]  the hierarchical schema: the package's names (scopes and
                   items) as [def_hnamesx] nodes, with its version checksum
  [mrm_res_map2_]  each item's decision and candidate values
  [mrm_dataitem]   the strings (UTF-16, NUL-terminated)
(mrm/include/mrm/common/file/MrmFiles.h, FileBase.h, FileBaseSections.h,
HNamesSection.h; the readers in mrm/mrmmin).
"""
import os
import struct
import sys
import xml.etree.ElementTree as ET
import zlib

SECTION_END = 0xDEF5FADE
FILE_END = 0xDEFFFADE


def pad(b, n=8):
    return b + b'\0' * (-len(b) % n)


def section(kind, body):
    """a section: its header, @body padded to 8 bytes, its trailer"""
    body = pad(body)
    total = 32 + len(body) + 8
    return struct.pack('<16sIHHII', kind, 0, 0, 0, total, 0) + body + struct.pack('<II', SECTION_END, total)


# ---- checksums (mrmmin/Checksums.cpp: CRC-32 over sizes and lower-cased UTF-16 strings)

def ck_data(crc, data):
    return zlib.crc32(data, zlib.crc32(struct.pack('<I', len(data)), crc))


def ck_string(crc, s):
    if s is None:
        return zlib.crc32(b'\0' * 8, crc)
    return ck_data(crc, (s.lower() + '\0').encode('utf-16le'))


def ck_u32(crc, v):
    return zlib.crc32(struct.pack('<I', v), crc)


def ck_pool(crc, names):
    """an atom pool's: its (absent) description, case-insensitive, its size, its names"""
    crc = ck_u32(ck_u32(ck_string(crc, None), 1), len(names))
    for n in names:
        crc = ck_string(crc, n)
    return crc


# ---- the hierarchical names ([def_hnamesx])

class Node:
    def __init__(self, name, parent, scope):
        self.name, self.parent, self.scope, self.children = name, parent, scope, []
        self.index = self.payload = 0

    def path(self):
        parts, n = [], self
        while n.parent is not None:
            parts.append(n.name)
            n = n.parent
        return '/'.join(reversed(parts))


def hnames(root):
    """the nodes breadth first (a scope's children together, sorted as MRT
    compares them: upper-cased, a name before the longer names it begins),
    the scopes and the items in the order their nodes come"""
    nodes, scopes, items, queue = [root], [root], [], [root]
    while queue:
        s = queue.pop(0)
        s.children.sort(key=lambda c: c.name.upper())
        s.first = len(nodes)
        for c in s.children:
            nodes.append(c)
            (scopes if c.scope else items).append(c)
            if c.scope:
                queue.append(c)
    for i, n in enumerate(nodes):
        n.index = i
    for i, s in enumerate(scopes):
        s.payload = i
    for i, it in enumerate(items):
        it.payload = i
    pool = b'\0'                                  # names, ASCII, NUL-terminated; offset 0 is ""
    out = b''
    for n in nodes:
        flags, off = 0x10 if n.scope else 0, 0
        if n.parent is not None:
            if not n.name.isascii():
                raise SystemExit(f'makepri: {n.name}: only ASCII names are written')
            off = len(pool)
            pool += n.name.encode('ascii') + b'\0'
            flags |= 0x20 | (off >> 16 & 0xF)
        if off >= 0x100000:
            raise SystemExit('makepri: names pool too large')
        out += struct.pack('<HHHBBHH', n.parent.index if n.parent else 0, len(n.path()),
                           ord(n.name[0].upper()) if n.name else 0, len(n.name), flags, off & 0xFFFF, n.payload)
    for s in scopes:
        out += struct.pack('<4H', s.index, len(s.children), s.first if s.children else 0, 0)
    out += struct.pack('<%dH' % len(items), *[it.index for it in items])
    out += pool
    total = 28 + len(out)
    total += -total % 8
    head = struct.pack('<HHIIIIII', max(len(n.path()) for n in nodes), 0, len(nodes), len(scopes), len(items),
                       0, total, len(pool))
    return pad(head + out), scopes, items


def build(name, tables):
    """the .pri bytes: @tables is [(scope name, {key: string})]"""
    root = Node('', None, True)
    for scope, strings in tables:
        s = Node(scope, root, True)
        root.children.append(s)
        for key in strings:
            s.children.append(Node(key, s, False))
    names, scopes, items = hnames(root)
    uid, sid = 'ms-appx://%s/' % name, name

    # the schema, with its version (1.0) checksummed as HSchema.cpp does
    crc = ck_string(ck_string(0, uid), sid)
    crc = ck_u32(crc, 1)
    crc = ck_pool(crc, [s.path() for s in scopes])
    crc = ck_pool(crc, [it.path() for it in items])
    body = struct.pack('<4H16s', 1, len(uid) + 1, len(sid) + 1, 0, b'[def_hnamesx]  \0')
    body += struct.pack('<HHIIII', 1, 0, 0, crc, len(scopes), len(items))
    body += pad((uid + '\0' + sid + '\0').encode('utf-16le'), 4)
    schema = section(b'[mrm_hschemaex] ', body + names)

    # MRT's fixed decisions: 0 empty, 1 "neutral only" (qualifier set 0: no
    # qualifiers, its always-true base qualifier), as MakePri writes them
    body = struct.pack('<6H', 1, 1, 1, 2, 1, 1)
    body += struct.pack('<4H', 0, 0, 0, 1)              # decisions
    body += struct.pack('<2H', 0, 0)                    # qualifier sets
    body += struct.pack('<4H', 0, 0, 0, 0)              # qualifiers
    body += struct.pack('<HHHHI', 0, 0, 0, 1, 0)        # base qualifiers
    body += struct.pack('<H', 0)                        # references
    body += '\0'.encode('utf-16le')                     # literals
    decisions = section(b'[mrm_decn_info]\0', pad(body, 4))

    # the strings, one data item each
    data, offsets = b'', []
    for it in items:
        s = (dict(tables)[it.parent.name][it.name] + '\0').encode('utf-16le')
        if len(s) > 0x7FFF:
            raise SystemExit(f'makepri: {it.path()}: string too long')
        offsets.append((len(data), len(s)))
        data += s
    if len(data) > 0xFFFF or len(items) > 0xFFFF:
        raise SystemExit('makepri: too many strings')
    body = struct.pack('<IHHI', 0, len(items), 0, len(data))
    body += b''.join(struct.pack('<HH', o, n) for o, n in offsets) + data
    dataitems = section(b'[mrm_dataitem] \0', body)

    # the map: every item's one neutral candidate, a UTF-16 string (value
    # type 0 of the environment's seven) in data item section 4
    SCHEMA, DECISIONS, MAP, DATA = 2, 0, 3, 4
    body = struct.pack('<HHhHhHHHHHIII', 0, 0, SCHEMA, 0, DECISIONS, 7, 1, 1, len(items), 0, len(items), 0, 0)
    body += b''.join(struct.pack('<ii', 4, k) for k in range(7))
    body += struct.pack('<HH', 0, 0)                    # one directory entry: range 0
    body += struct.pack('<HH', len(items), 0)           # the range: every item
    body += b''.join(struct.pack('<HH', 1, i) for i in range(len(items)))
    body += b''.join(struct.pack('<BBHI', 1, 0, 0, DATA << 16 | i) for i in range(len(items)))
    resmap = section(b'[mrm_res_map2_]\0', pad(body, 4))

    body = struct.pack('<HhHHHHhHHH', 2, -1, 0, 1, 1, 1, MAP, 0, 1, 0)
    body += struct.pack('<4h', SCHEMA, DECISIONS, MAP, DATA)  # (no referenced-file sections)
    descriptor = section(b'[mrm_pridescex]\0', body)

    sections = [decisions, descriptor, schema, resmap, dataitems]
    toc, offset = b'', 0
    for s in sections:
        toc += struct.pack('<16sHHIII', s[:16], 0, 0, 0, offset, len(s))
        offset += len(s)
    start = 32 + len(toc)
    total = start + offset + 16
    head = struct.pack('<8sHHIIIhhI', b'mrm_pri2', 0, 1, total, 32, start, len(sections), -1, 0)
    return head + toc + b''.join(sections) + struct.pack('<II8s', FILE_END, total, b'mrm_pri2')


def read_resw(path):
    """{name: value} from a .resw (ResX) string table, in file order"""
    out = {}
    for d in ET.parse(path).getroot().iter('data'):
        v = d.find('value')
        out[d.get('name')] = (v.text or '') if v is not None else ''
    return out


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    name, out = argv[0], argv[1]
    tables = [(os.path.splitext(os.path.basename(p))[0], read_resw(p)) for p in argv[2:]]
    data = build(name, tables)
    if not os.path.exists(out) or open(out, 'rb').read() != data:
        with open(out, 'wb') as f:
            f.write(data)


if __name__ == '__main__':
    main(sys.argv[1:])
