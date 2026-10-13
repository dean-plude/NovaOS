#!/usr/bin/env python3
"""sys_imports.py — what a Windows driver imports that NovaOS does not export yet

    sys_imports.py DRIVER.sys [DRIVER.sys ...] [--all]

Reads each driver's import directory (PE32+, any subsystem) and compares
it with the exports NovaOS's kernel gives drivers (kernel/nt/exports.h).
Prints, per driver, the imports missing by module (with --all, every
import, the missing ones marked), and a last line counting them; the
exit status is the number of drivers with something missing.  A driver
with nothing missing loads as far as its imports go.
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def exported():
    text = open(os.path.join(ROOT, 'kernel', 'nt', 'exports.h')).read()
    have = {}
    for m in re.finditer(r'^\s*NT_EXPORT(?:_DATA)?\(\s*([\w.-]+)\s*,\s*(\w+)\s*\)', text, re.M):
        have.setdefault(m.group(1).lower(), set()).add(m.group(2))
    return have


def imports(path):
    data = open(path, 'rb').read()
    if data[:2] != b'MZ':
        raise ValueError('not a PE file')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    nsec, opt_size = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', data, opt)[0]
    if magic != 0x20B:
        raise ValueError('not a 64-bit image')
    imp_rva = struct.unpack_from('<I', data, opt + 112 + 8)[0]   # DataDirectory[1]
    secs = []
    for i in range(nsec):
        s = pe + 24 + opt_size + 40 * i
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, s + 8)
        secs.append((va, max(vsize, rsize), raw))

    def off(rva):
        for va, size, raw in secs:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError(f'RVA {rva:#x} outside the sections')

    def cstr(rva):
        o = off(rva)
        return data[o:data.index(b'\0', o)].decode('ascii', 'replace')

    out = {}
    at = imp_rva
    while imp_rva:
        olt, _, _, name, iat = struct.unpack_from('<IIIII', data, off(at))
        if not name and not iat:
            break
        mod = cstr(name)
        lookup, names = olt or iat, []
        while True:
            t = struct.unpack_from('<Q', data, off(lookup))[0]
            if not t:
                break
            names.append(f'#{t & 0xFFFF}' if t >> 63 else cstr((t & 0x7FFFFFFF) + 2))
            lookup += 8
        out.setdefault(mod, []).extend(names)
        at += 20
    return out


def main():
    args = [a for a in sys.argv[1:] if a != '--all']
    if not args:
        sys.exit(__doc__)
    show_all = '--all' in sys.argv
    have = exported()
    lacking_drivers = 0
    for path in args:
        try:
            imp = imports(path)
        except (OSError, ValueError, struct.error) as e:
            print(f'{path}: {e}')
            lacking_drivers += 1
            continue
        total = missing = 0
        print(f'{os.path.basename(path)}:')
        for mod in sorted(imp, key=str.lower):
            names = sorted(set(imp[mod]))
            lack = [n for n in names if n not in have.get(mod.lower(), set())]
            total += len(names)
            missing += len(lack)
            shown = names if show_all else lack
            if shown:
                print(f'  {mod}: ' + ', '.join(n + (' (missing)' if show_all and n in lack else '') for n in shown))
        print(f'  {missing} of {total} imports missing')
        lacking_drivers += missing > 0
    return lacking_drivers


if __name__ == '__main__':
    sys.exit(main())
