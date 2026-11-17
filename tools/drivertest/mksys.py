#!/usr/bin/env python3
"""mksys.py — builds a Windows kernel-mode test driver: wdmtest.sys,
edutest.sys (step 30.3's Plug and Play driver for QEMU's edu device),
wddmdod.sys (Phase 25.3's WDDM display-only driver for QEMU's standard VGA),
wddmtest.sys (the Intel GPU design's step 3: a WDDM render miniport whose
GPU is software, on a root-enumerated device) wditest.sys (step 28.1's WDI Wi-Fi miniport for QEMU's edu device) or
btfilter.sys (step 28.2's lower filter on the test Bluetooth radio)

    mksys.py OUTDIR [NAME]

tools/build_userland.py runs it and puts what it writes in C:\\Tests\\Wdm,
C:\\Tests\\Edu, C:\\Tests\\Wddm, C:\\Tests\\WddmTest, C:\\Tests\\Wifi and C:\\Tests\\BtFilter.  The driver (tools/drivertest/NAME.c) is built as a Windows driver is:
clang for x86_64-pc-windows-msvc, linked by lld-link /driver against an
import library for ntoskrnl.exe and hal.dll.  The import libraries are made
here from the functions NovaOS's kernel exports to drivers
(kernel/nt/exports.h), so the WDK is not needed.

Then it is signed as a driver developer signs one for test signing, by a
test certificate ("NovaOS Test Driver Signer" under "NovaOS Driver Test
Root", made here from fixed seeds and trusted by nobody), and the signing
policy's other cases are made from it:

    wdmtest.sys         an embedded test signature: loads only with test signing on
    wdmunsigned.sys     no signature: never loads
    wdmtampered.sys     wdmtest.sys with a code byte changed after signing: never loads
    wdmcat\\            a package signed by its catalog: wdmcat.inf, wdmcat.sys
                        (unsigned itself), wdmcat.cat (test-signed, listing both)
    wdmbad\\            the same package with wdmbad.sys changed after the catalog
                        was made: pnputil refuses to stage it

edutest.sys's, wddmdod.sys's, wddmtest.sys's, wditest.sys's and btfilter.sys's packages (NAME.inf, from tools/drivertest)
are signed as wdmcat's is: a test-signed catalog, NAME.cat, lists the INF
and the .sys.
"""
import concurrent.futures, glob, hmac, os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'authenticode'))
from pki import Cert, Key                                       # noqa: E402
from sign import sign                                           # noqa: E402
from catalog import catalog                                     # noqa: E402
from pki import integer, seq, oid, null, octets, explicit       # noqa: E402
import hashlib                                                  # noqa: E402

LONG = ((2020, 1, 1, 0, 0, 0), (2049, 12, 31, 23, 59, 59))

INF = r'''; {name}.inf - a test-signed kernel driver package (tools/drivertest/mksys.py)
[Version]
Signature   = "$WINDOWS NT$"
Class       = System
ClassGuid   = {{4d36e97d-e325-11ce-bfc1-08002be10318}}
Provider    = %ProviderName%
DriverVer   = 10/12/2026,1.0.0.0
CatalogFile = {name}.cat
PnpLockdown = 1

[DestinationDirs]
DefaultDestDir = 13

[SourceDisksNames]
1 = %DiskName%,,,""

[SourceDisksFiles]
{name}.sys = 1,,

[Manufacturer]
%ProviderName% = NovaTest,NTamd64

[NovaTest.NTamd64]
%DeviceDesc% = Wdm_Install, ROOT\{upper}

[Wdm_Install.NT]
CopyFiles = Wdm_Files

[Wdm_Files]
{name}.sys

[Wdm_Install.NT.Services]
AddService = {upper}, 0x00000002, Wdm_Service

[Wdm_Service]
ServiceType   = 1               ; SERVICE_KERNEL_DRIVER
StartType     = 3               ; SERVICE_DEMAND_START
ErrorControl  = 1
ServiceBinary = %13%\{name}.sys

[Strings]
ProviderName = "NovaOS Test"
DiskName     = "NovaOS driver test disk"
DeviceDesc   = "NovaOS catalog-signed test driver"
'''


def restamp(pe, delta):
    """@pe with its TimeDateStamp moved: a different image (another hash)"""
    b = bytearray(pe)
    e = struct.unpack_from('<I', b, 60)[0]
    struct.pack_into('<I', b, e + 8, (struct.unpack_from('<I', b, e + 8)[0] + delta) & 0xFFFFFFFF)
    return bytes(b)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def test_signer():
    """The test certificate NovaOS's test drivers are signed with, and its root"""
    root_key, signer_key = Key('novaos-driver-test-root'), Key('novaos-driver-test-signer')
    root = Cert('NovaOS Driver Test Root', root_key, 1, LONG, ca=True)
    signer = Cert('NovaOS Test Driver Signer', signer_key, 2, LONG, root, root_key, usage='codesign')
    return signer, root


def signed_package(out, name):
    """@name.sys with its INF beside it and a test-signed catalog of both (the
    .sys signed by the catalog, as wdmcat's is)"""
    signer, root = test_signer()
    pe = open(os.path.join(out, name + '.sys'), 'rb').read()
    inf = open(os.path.join(HERE, name + '.inf'), 'rb').read()
    write(os.path.join(out, name + '.inf'), inf)
    write(os.path.join(out, name + '.cat'), catalog([(name + '.inf', inf), (name + '.sys', pe)], signer, [root]))


def signed_variants(out):
    signer, root = test_signer()
    path = os.path.join(out, 'wdmtest.sys')
    pe = open(path, 'rb').read()
    good = sign(pe, signer, [root], 'sha256')
    write(path, good)
    write(os.path.join(out, 'wdmunsigned.sys'), restamp(pe, 1))
    t = bytearray(good)
    e = struct.unpack_from('<I', t, 60)[0]
    sec = e + 24 + struct.unpack_from('<H', t, e + 20)[0]         # (the first section's raw data)
    text = struct.unpack_from('<I', t, sec + 20)[0]
    t[text + 16] ^= 0x01
    write(os.path.join(out, 'wdmtampered.sys'), bytes(t))
    for name, changed in (('wdmcat', False), ('wdmbad', True)):
        inf = INF.format(name=name, upper=name.upper()).replace('\n', '\r\n').encode()
        sys_ = restamp(pe, 2)
        cat = catalog([(name + '.inf', inf), (name + '.sys', sys_)], signer, [root])
        write(os.path.join(out, name, name + '.inf'), inf)
        write(os.path.join(out, name, name + '.sys'), restamp(sys_, 1) if changed else sys_)
        write(os.path.join(out, name, name + '.cat'), cat)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(' '.join(cmd) + '\n' + r.stdout + r.stderr)
        sys.exit(1)


def import_libs(work):
    """Import libraries for ntoskrnl.exe and hal.dll in @work, made from the
    export list (kernel/nt/exports.h); returns their paths"""
    mods = {}
    src = open(os.path.join(ROOT, 'kernel', 'nt', 'exports.h')).read()
    for m in re.finditer(r'^\s*NT_EXPORT(_DATA)?\(\s*([\w.]+)\s*,\s*(\w+)\s*\)', src, re.M):
        # data exports are imported by address: the .def marks them DATA
        mods.setdefault(m.group(2), []).append((m.group(3), bool(m.group(1))))
    libs = []
    for mod, names in sorted(mods.items()):
        stem = os.path.splitext(mod)[0]
        d = os.path.join(work, stem + '.def')
        with open(d, 'w') as f:
            f.write(f'LIBRARY {mod}\nEXPORTS\n' + ''.join(f'    {n}{" DATA" if data else ""}\n' for n, data in names))
        lib = os.path.join(work, stem + '.lib')
        run(['llvm-lib', f'/def:{d}', '/machine:x64', f'/out:{lib}'])
        libs.append(lib)
    return libs


def p12_kdf(password, salt, purpose, iterations, n, hash_name='sha256'):
    """PKCS #12's key derivation (RFC 7292 B.2) from @password (a str, as a
    BMPString with its terminator): @n bytes for @purpose (3: the MAC key)"""
    u, v = hashlib.new(hash_name).digest_size, hashlib.new(hash_name).block_size

    def fill(b):
        return (b * (v // len(b) + 2))[:v * ((len(b) + v - 1) // v)] if b else b''
    pw = password.encode('utf-16-be') + b'\0\0'
    i = fill(salt) + fill(pw)
    out = b''
    while True:
        a = hashlib.new(hash_name, bytes([purpose]) * v + i).digest()
        for _ in range(iterations - 1):
            a = hashlib.new(hash_name, a).digest()
        out += a
        if len(out) >= n:
            return out[:n]
        b = int.from_bytes((a * (v // u + 1))[:v], 'big')
        i = b''.join(((int.from_bytes(i[j:j + v], 'big') + b + 1) % (1 << (8 * v))).to_bytes(v, 'big')
                     for j in range(0, len(i), v))


def pfx(cert, key, chain, password):
    """A PKCS #12 file (RFC 7292) as Windows' certutil -exportpfx and
    OpenSSL write one, but with its bags in the clear (OpenSSL's -keypbe
    NONE -certpbe NONE: Python has no AES to encrypt them with): @cert and
    @chain in certBags, @key in a keyBag (PKCS #8), and the HMAC-SHA-256
    integrity MAC from @password"""
    data = '1.2.840.113549.1.7.1'
    pkcs1 = seq(*[integer(v) for v in (0, key.n, key.e, key.d, key.p, key.q, key.dp, key.dq, key.qi)])
    pkcs8 = seq(integer(0), seq(oid('1.2.840.113549.1.1.1'), null()), octets(pkcs1))
    certs = seq(*[seq(oid('1.2.840.113549.1.12.10.1.3'),
                      explicit(0, seq(oid('1.2.840.113549.1.9.22.1'), explicit(0, octets(c.der)))))
                  for c in [cert] + chain])
    keys = seq(seq(oid('1.2.840.113549.1.12.10.1.1'), explicit(0, pkcs8)))
    auth = seq(*[seq(oid(data), explicit(0, octets(b))) for b in (certs, keys)])
    salt = hashlib.sha256(cert.der).digest()[:8]                 # (fixed: the same file every build)
    mac = hmac.new(p12_kdf(password, salt, 3, 2048, 32), auth, 'sha256').digest()
    return seq(integer(3), seq(oid(data), explicit(0, octets(auth))),
               seq(seq(seq(oid('2.16.840.1.101.3.4.2.1'), null()), octets(mac)), octets(salt), integer(2048)))


PFX_PASSWORD = 'nova-tls-password'


def radius_certs(work, out):
    """wditest.sys's WPA2-Enterprise authentication server's certificate
    (tools/drivertest/eapap.h): "radius.novacorp.test" (a TLS server
    certificate) under "NovaCorp Test Root", both from fixed seeds and
    trusted by nobody until the wifi self-test imports the root
    (novacorp-ca.cer beside the driver: certutil -addstore root).  The
    server's certificate and its RSA key (PKCS #1 DER) and the root (which
    EAP-TLS's client certificates must chain to) go into the driver as
    wditest_certs.h.  For EAP-TLS, two client certificates in PKCS #12
    files (password PFX_PASSWORD) for certutil -importpfx: alice.pfx,
    "alice" under NovaCorp Test Root (with the root, as an exported chain
    has it), and mallory.pfx, a self-signed "mallory" the server refuses"""
    root_key, server_key = Key('novacorp-test-root'), Key('novacorp-radius-server')
    root = Cert('NovaCorp Test Root', root_key, 1, LONG, ca=True)
    server = Cert('radius.novacorp.test', server_key, 2, LONG, root, root_key, usage='server')
    alice_key, mallory_key = Key('novacorp-eap-tls-alice'), Key('novacorp-eap-tls-mallory')
    alice = Cert('alice', alice_key, 3, LONG, root, root_key, usage='client')
    mallory = Cert('mallory', mallory_key, 1, LONG, usage='client')
    k = server_key
    der = seq(*[integer(v) for v in (0, k.n, k.e, k.d, k.p, k.q, k.dp, k.dq, k.qi)])

    def arr(name, b):
        rows = ',\n'.join('    ' + ', '.join(f'0x{x:02x}' for x in b[i:i + 16]) for i in range(0, len(b), 16))
        return f'static const unsigned char {name}[{len(b)}] = {{\n{rows}\n}};\n'
    write(os.path.join(work, 'wditest_certs.h'),
          ('/* made by tools/drivertest/mksys.py: NovaCorp\'s RADIUS server certificate and key, and its root */\n'
           '#pragma once\n' + arr('CERT_SERVER', server.der) + arr('KEY_SERVER', der) +
           arr('CERT_ROOT', root.der)).encode())
    write(os.path.join(out, 'novacorp-ca.cer'), root.der)
    write(os.path.join(out, 'alice.pfx'), pfx(alice, alice_key, [root], PFX_PASSWORD))
    write(os.path.join(out, 'mallory.pfx'), pfx(mallory, mallory_key, [], PFX_PASSWORD))


def mbedtls_objects(work):
    """Mbed TLS (third_party/mbedtls) compiled for wditest.sys: its own
    configuration and C library headers (tools/drivertest/mbedtls)"""
    lib = os.path.join(ROOT, 'third_party', 'mbedtls')
    srcs = sorted(glob.glob(os.path.join(lib, 'library', '*.c')))

    def cc(src):
        obj = os.path.join(work, 'mbedtls_' + os.path.basename(src)[:-2] + '.obj')
        run(['clang', '--target=x86_64-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-stack-protector',
             '-mno-stack-arg-probe', '-w', '-nostdlibinc', '-U_WIN32', '-U_WIN64',     # (no Win32 in a driver)
             '-I', os.path.join(HERE, 'mbedtls'),
             '-I', os.path.join(lib, 'include'), '-I', os.path.join(lib, 'library'),
             '-DMBEDTLS_CONFIG_FILE="mbedtls_wditest_config.h"', '-c', src, '-o', obj])
        return obj
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 2) as ex:
        return list(ex.map(cc, srcs))


def main():
    out = sys.argv[1]
    name = sys.argv[2] if len(sys.argv) > 2 else 'wdmtest'
    os.makedirs(out, exist_ok=True)
    work = os.path.join(out, 'obj')
    os.makedirs(work, exist_ok=True)
    libs = import_libs(work)
    obj = os.path.join(work, name + '.obj')
    extra, flags = [], []
    if name == 'wditest':           # (its RADIUS server: TLS by Mbed TLS, with a test certificate)
        radius_certs(work, out)
        extra = mbedtls_objects(work)
        lib = os.path.join(ROOT, 'third_party', 'mbedtls')
        flags = ['-I', work, '-I', os.path.join(HERE, 'mbedtls'), '-I', os.path.join(lib, 'include'),
                 '-DMBEDTLS_CONFIG_FILE="mbedtls_wditest_config.h"']
    # -fasync-exceptions (MSVC's /EHa for the kernel): without it the
    # optimizer assumes a memory access never faults and deletes a
    # __try/__except around one, dropping the .pdata/.xdata and the
    # __C_specific_handler import the kernel's SEH (kernel/nt/seh.c) walks.
    run(['clang', '--target=x86_64-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-stack-protector',
         '-mno-stack-arg-probe', '-fno-builtin', '-fasync-exceptions', '-Wall'] + flags + ['-c',
         os.path.join(HERE, name + '.c'), '-o', obj])
    run(['lld-link', '/driver', '/subsystem:native', '/entry:DriverEntry', '/nodefaultlib', '/machine:x64',
         '/base:0x140000000', obj] + extra + libs + [f'/out:{os.path.join(out, name + ".sys")}'])
    for n in os.listdir(work):
        os.remove(os.path.join(work, n))
    os.rmdir(work)
    if name == 'wdmtest':
        signed_variants(out)
    else:
        signed_package(out, name)


if __name__ == '__main__':
    main()
