#!/usr/bin/env python3
"""Verify NovaOS provides every import of the real Windows drivers we host.

    tools/ci/check-driver-imports.py            # check the committed fixtures
    tools/ci/check-driver-imports.py --dump X.sys [...]   # regenerate a fixture

NovaOS loads real signed Windows driver packages unchanged (step 30, the
Windows driver model), so every symbol they import from ntoskrnl.exe and
hal.dll must be an export kernel/nt/exports.h lists (step 30.2 completes the
kernel API for exactly this reason).  This compares the import lists of the
drivers we target first — the VirtIO serial driver (vioser.sys), the
KMDF runtime and loader (Wdf01000.sys, wdfldr.sys) and the VirtIO block
miniport (viostor.sys, whose storport.sys imports NovaOS's own Storport
host provides) — against exports.h and fails naming any import that is
neither provided nor allow-listed.

The import lists live beside the wdm self-test
(tests/selftest/devices/wdm/imports/*.txt), extracted from the signed
binaries with `--dump` (maintainers run it; the binaries are not in the
repo).  Imports from wdfldr.sys are the KMDF loader's own exports, provided
by the KMDF step (30.4/30.5), so they are allow-listed here rather than in
exports.h.  CI runs the check on every pull request.
"""
import os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXPORTS_H = os.path.join(ROOT, "kernel", "nt", "exports.h")
IMPORTS_DIR = os.path.join(ROOT, "tests", "selftest", "devices", "wdm", "imports")

# Modules NovaOS itself provides (its export tables); every import from one
# of these must appear in exports.h.
PROVIDED_MODULES = {"ntoskrnl.exe", "hal.dll", "storport.sys"}
# Modules a driver package brings with it (provided by a later driver step),
# so their imports are not NovaOS exports.
ALLOWLISTED_MODULES = {"wdfldr.sys"}


def exports_from_header(path):
    """The (module, name) pairs exports.h declares (NT_EXPORT/NT_EXPORT_DATA)."""
    pat = re.compile(r"^\s*NT_EXPORT(?:_DATA)?\(\s*([^,]+?)\s*,\s*([^)]+?)\s*\)")
    out = set()
    with open(path) as f:
        for line in f:
            m = pat.match(line)
            if m:
                out.add((m.group(1), m.group(2)))
    return out


def imports_from_fixture(path):
    """The (module, name) import pairs listed in a fixture file."""
    out = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) == 2:
                out.append((parts[0].lower(), parts[1]))
    return out


def check():
    if not os.path.isdir(IMPORTS_DIR):
        print("no import fixtures at %s" % IMPORTS_DIR, file=sys.stderr)
        return 1
    provided = {(m.lower(), n) for m, n in exports_from_header(EXPORTS_H)}
    missing = []
    files = sorted(f for f in os.listdir(IMPORTS_DIR) if f.endswith(".txt"))
    total = 0
    for fn in files:
        for mod, name in imports_from_fixture(os.path.join(IMPORTS_DIR, fn)):
            total += 1
            if mod in ALLOWLISTED_MODULES:
                continue
            if mod in PROVIDED_MODULES and (mod, name) not in provided:
                missing.append((fn, mod, name))
    if missing:
        print("Driver imports NovaOS does not provide (add to kernel/nt/exports.h):")
        for fn, mod, name in missing:
            print("  %-24s %s!%s" % (fn, mod, name))
        return 1
    print("driver imports: %d imports across %d driver(s) all provided or allow-listed"
          % (total, len(files)))
    return 0


# --- PE import extraction (maintainers: regenerate a fixture from a .sys) ---
def pe_imports(path):
    with open(path, "rb") as f:
        b = f.read()
    u16 = lambda o: struct.unpack_from("<H", b, o)[0]
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    u64 = lambda o: struct.unpack_from("<Q", b, o)[0]
    pe = u32(0x3C)
    assert b[pe:pe + 4] == b"PE\x00\x00", "not a PE image"
    nsec = u16(pe + 6)
    opt = pe + 24
    assert u16(opt) == 0x20B, "not PE32+"
    imp_rva = u32(opt + 112 + 8)
    sec = opt + u16(pe + 20)
    sections = []
    for i in range(nsec):
        o = sec + i * 40
        sections.append((u32(o + 12), max(u32(o + 8), u32(o + 16)), u32(o + 20)))

    def off(rva):
        for va, sz, praw in sections:
            if va <= rva < va + sz:
                return praw + (rva - va)
        return None

    out = []
    o = off(imp_rva)
    while True:
        oft, name_rva, iat = u32(o), u32(o + 12), u32(o + 16)
        if not (oft or name_rva or iat):
            break
        no = off(name_rva)
        dll = b[no:b.index(b"\x00", no)].decode("ascii").lower()
        to = off(oft or iat)
        while True:
            ent = u64(to)
            to += 8
            if ent == 0:
                break
            if not (ent & (1 << 63)):
                hno = off(ent & 0x7FFFFFFF)
                out.append((dll, b[hno + 2:b.index(b"\x00", hno + 2)].decode("ascii")))
        o += 20
    return out


def dump(paths):
    hdr = ("# Import list of %s, extracted from the real signed driver with\n"
           "# tools/ci/check-driver-imports.py --dump (do not edit by hand).  The\n"
           "# import-coverage check (the same tool) verifies every ntoskrnl.exe and\n"
           "# hal.dll import below is provided by kernel/nt/exports.h; wdfldr.sys\n"
           "# imports are the KMDF loader's own exports, provided by a later driver\n"
           "# step (30.4/30.5), and are allow-listed there.\n")
    os.makedirs(IMPORTS_DIR, exist_ok=True)
    for p in paths:
        name = os.path.basename(p).lower()
        base = name[:-4] if name.endswith(".sys") else name
        lines = sorted({"%s\t%s" % (m, n) for m, n in pe_imports(p)})
        dest = os.path.join(IMPORTS_DIR, base + ".imports.txt")
        with open(dest, "w") as f:
            f.write(hdr % name + "\n".join(lines) + "\n")
        print("wrote %s (%d imports)" % (dest, len(lines)))
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--dump":
        sys.exit(dump(sys.argv[2:]))
    sys.exit(check())
