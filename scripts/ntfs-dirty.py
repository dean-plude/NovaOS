#!/usr/bin/env python3
"""Whether an NTFS volume is marked dirty (chkdsk pending, or NovaOS was
cut off while writing to it): scripts/ntfs-dirty.py image [offset_bytes]
Prints "dirty" or "clean"; exits 1 when dirty, 0 when clean."""
import struct, sys

f = open(sys.argv[1], 'rb')
base = int(sys.argv[2]) if len(sys.argv) > 2 else 0
f.seek(base)
bs = f.read(512)
if bs[3:11] != b'NTFS    ':
    sys.exit('not an NTFS volume')
bps, spc = struct.unpack_from('<HB', bs, 11)
cl = bps * spc
mft_lcn = struct.unpack_from('<Q', bs, 0x30)[0]
cpr = struct.unpack_from('<b', bs, 0x40)[0]
rec_size = cl * cpr if cpr > 0 else 1 << -cpr
f.seek(base + mft_lcn * cl + 3 * rec_size)              # $Volume (in the MFT's first extent)
rec = bytearray(f.read(rec_size))
if rec[:4] != b'FILE':
    sys.exit('$Volume: not a FILE record')
uo, uc = struct.unpack_from('<HH', rec, 4)
for i in range(1, uc):                                    # the update sequence
    rec[i * 512 - 2:i * 512] = rec[uo + 2 * i:uo + 2 * i + 2]
o = struct.unpack_from('<H', rec, 0x14)[0]
while o + 8 <= len(rec):
    t, ln = struct.unpack_from('<II', rec, o)
    if t == 0xFFFFFFFF or ln < 0x18:
        break
    if t == 0x70 and rec[o + 8] == 0:                      # $VOLUME_INFORMATION: flags at 10
        vo = struct.unpack_from('<H', rec, o + 0x14)[0]
        dirty = struct.unpack_from('<H', rec, o + vo + 10)[0] & 1
        print('dirty' if dirty else 'clean')
        sys.exit(1 if dirty else 0)
    o += ln
sys.exit('$Volume has no $VOLUME_INFORMATION')
