#!/usr/bin/env python3
"""signappx.py: sign an app package (.msix, .appx) or a bundle
(.msixbundle, .appxbundle) as the Windows SDK's SignTool does through the
APPX SIP, in plain Python (tools/authenticode's PKI):

    sign(package, signer, chain, hash_name='sha256', bundle=False) -> bytes

The package's digest is "APPX" and four named hashes (five with a code
integrity catalog), each of the unsigned package:
  AXPC  its local file records: every byte before its central directory
  AXCD  its central directory and end records
  AXCT  [Content_Types].xml, inflated
  AXBM  AppxBlockMap.xml, inflated
in an SpcIndirectDataContent whose SpcSipInfo names the APPX SIP (or the
bundle SIP), signed as Authenticode signs (tools/authenticode/sign.py);
the SignedData, after "PKCX", is the package's last file,
AppxSignature.p7x (stored), and the central directory is written again
after it.  wintrust.dll's verify_package checks exactly these (and the
same algorithm on Microsoft's own packages: checked on VCLibs and
Microsoft.UI.Xaml).
"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'authenticode'))
from pki import alg, digest, integer, octets, oid, seq   # noqa: E402
from sign import sign_content                            # noqa: E402

SIPINFO = '1.3.6.1.4.1.311.2.1.30'
APPX_SIP = bytes.fromhex('4BDFC50A07CEE24DB76E23C839A09FD1')      # {0AC5DF4B-CE07-4DE2-B76E-23C839A09FD1}
BUNDLE_SIP = bytes.fromhex('B3585F0FDEAA9A4BA43495742D92ECEB')    # {0F5F58B3-AADE-4B9A-A434-95742D92ECEB}


def entries(z):
    """the zip's (end of central directory offset, cd offset, cd size,
    [(name, method, csize, lho, record)])"""
    eocd = z.rindex(b'PK\x05\x06')
    count, size, off = struct.unpack_from('<HII', z, eocd + 10)
    out, p = [], off
    for _ in range(count):
        method, csize = struct.unpack_from('<H', z, p + 10)[0], struct.unpack_from('<I', z, p + 20)[0]
        nl, xl, cl = struct.unpack_from('<HHH', z, p + 28)
        lho = struct.unpack_from('<I', z, p + 42)[0]
        out.append((z[p + 46:p + 46 + nl].decode(), method, csize, lho, z[p:p + 46 + nl + xl + cl]))
        p += 46 + nl + xl + cl
    return eocd, off, size, out


def contents(z, e):
    name, method, csize, lho, _ = e
    nl, xl = struct.unpack_from('<HH', z, lho + 26)
    raw = z[lho + 30 + nl + xl:lho + 30 + nl + xl + csize]
    return raw if method == 0 else zlib.decompress(raw, -15)


def sign(package, signer, chain, hash_name='sha256', bundle=False):
    z = bytes(package)
    eocd, cdoff, cdsize, ents = entries(z)
    if any(e[0] == 'AppxSignature.p7x' for e in ents):
        raise SystemExit('signappx: the package is signed already')
    files = {e[0]: e for e in ents}
    digests = b'APPX' + b'AXPC' + digest(hash_name, z[:cdoff]) + b'AXCD' + digest(hash_name, z[cdoff:])
    digests += b'AXCT' + digest(hash_name, contents(z, files['[Content_Types].xml']))
    digests += b'AXBM' + digest(hash_name, contents(z, files['AppxBlockMap.xml']))
    if 'AppxMetadata/CodeIntegrity.cat' in files:
        digests += b'AXCI' + digest(hash_name, contents(z, files['AppxMetadata/CodeIntegrity.cat']))
    sip = seq(integer(0x01010000), octets(BUNDLE_SIP if bundle else APPX_SIP), *[integer(0)] * 5)
    spc = seq(seq(oid(SIPINFO), sip), seq(alg(hash_name), octets(digests)))
    p7x = b'PKCX' + sign_content(spc, signer, chain, hash_name)
    name = b'AppxSignature.p7x'
    crc = zlib.crc32(p7x)
    head = struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 0, 0, 0, 0x21, crc, len(p7x), len(p7x), len(name), 0) + name
    rec = struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 20, 20, 0, 0, 0, 0x21, crc, len(p7x), len(p7x), len(name),
                      0, 0, 0, 0, 0, cdoff) + name
    cd = z[cdoff:cdoff + cdsize] + rec
    end = struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(ents) + 1, len(ents) + 1, len(cd), cdoff + len(head) + len(p7x),
                      0)
    return z[:cdoff] + head + p7x + cd + end


if __name__ == '__main__':
    raise SystemExit(__doc__)
