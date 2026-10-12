"""Signed driver catalogs (.cat) in plain Python, as Inf2Cat and signtool
make them (tools/drivertest/mksys.py signs NovaOS's test driver packages
with it).

catalog(files, signer, chain) returns a catalog listing each file by its
SHA-256 hash: a program's Authenticode digest, any other file's hash of
all its bytes, as Windows computes them.  It is a PKCS #7 SignedData over
a certificate trust list (szOID_CTL) whose members are tagged with the
hash and carry it again in an SpcIndirectDataContent, the layout of the
catalogs Microsoft's hardware signing writes.
"""
import hashlib
import struct

from pki import alg, digest, explicit, null, octets, oid, seq, set_of, split, time, tlv, bits
from sign import SPC_INDIRECT_DATA, SPC_PE_IMAGE_DATA, attr, pe_digest, signed_data, signer_info

CTL = '1.3.6.1.4.1.311.10.1'
CATALOG_LIST = '1.3.6.1.4.1.311.12.1.1'
CATALOG_MEMBER2 = '1.3.6.1.4.1.311.12.1.3'
NAME_VALUE = '1.3.6.1.4.1.311.12.2.1'
SPC_CAB_DATA = '1.3.6.1.4.1.311.2.1.25'


def is_pe(data):
    if len(data) < 64 or data[:2] != b'MZ':
        return False
    e = struct.unpack_from('<I', data, 60)[0]
    return data[e:e + 4] == b'PE\0\0'


def file_hash(data):
    """the hash a catalog lists @data by"""
    return pe_digest(data, 'sha256') if is_pe(data) else hashlib.sha256(data).digest()


def member(name, data):
    h = file_hash(data)
    kind = seq(oid(SPC_PE_IMAGE_DATA), seq(bits(b''), explicit(0, explicit(2, tlv(0x80, b''))))) if is_pe(data) \
        else seq(oid(SPC_CAB_DATA), explicit(2, tlv(0x80, b'')))
    spc = seq(kind, seq(alg('sha256'), octets(h)))
    label = seq(tlv(0x1E, 'File'.encode('utf-16-be')), tlv(0x02, b'\x10\x01\x00\x01'),
                octets((name + '\0').encode('utf-16-le')))
    return seq(octets(h), set_of(attr(NAME_VALUE, label), attr(SPC_INDIRECT_DATA, spc)))


def catalog(files, signer, chain, when=(2026, 10, 12, 0, 0, 0)):
    """@files: (name, bytes) pairs; @signer a Cert with its key"""
    ctl = seq(seq(oid(CATALOG_LIST)), octets(hashlib.md5(b''.join(n.encode() for n, _ in files)).digest()),
              time(when), seq(oid(CATALOG_MEMBER2), null()), seq(*[member(n, d) for n, d in files]))
    body = split(ctl)[2]                                    # PKCS #7: only the contents octets are hashed
    auth = [attr('1.2.840.113549.1.9.3', oid(CTL)), attr('1.2.840.113549.1.9.4', octets(digest('sha256', body)))]
    return signed_data(CTL, ctl, [signer] + list(chain), [signer_info(signer, signer.key, 'sha256', auth)], 'sha256')
