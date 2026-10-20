#!/usr/bin/env python3
"""mktest.py: the app packages the appx self-test installs

    tools/appx/mktest.py OUTDIR APPXTEST.EXE

tools/build_userland.py runs it and puts what it writes in C:\\Tests\\Appx:

  PackageTest.msix  NovaOS.PackageTest 1.0.0.0 (x64), unsigned with the
                    "unsigned" publisher OID: appxtest.exe (a copy of the
                    self-test, run packaged), greeter.dll (its Windows
                    Runtime class, tools/appx/test/greeter.c) and
                    resources.pri (tools/appx/test/Resources.resw, through
                    makepri.py), the manifest tools/appx/test/AppxManifest.xml
  Tampered.msix     the same, greeter.dll changed after its block map was
                    made: must be refused
  Signed.msix       the same with an AppxSignature.p7x: a signature NovaOS
                    cannot verify yet, so it must be refused, never
                    installed unchecked
  NoOid.msix        the same, its publisher without the "unsigned" OID:
                    must be refused as Windows refuses it
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import makeappx  # noqa: E402
import makepri   # noqa: E402

TEST = os.path.join(HERE, 'test')
OID = ', OID.2.25.311729368913984317654407730594956997722=1'


def run(cmd):
    r = subprocess.run(cmd)
    if r.returncode:
        raise SystemExit(f'mktest: failed: {" ".join(cmd)}')


def greeter(out):
    """greeter.dll: clang and lld-link, freestanding"""
    obj, dll = os.path.join(out, 'greeter.obj'), os.path.join(out, 'greeter.dll')
    src = os.path.join(TEST, 'greeter.c')
    if os.path.exists(dll) and os.path.getmtime(dll) >= max(os.path.getmtime(src), os.path.getmtime(__file__)):
        return open(dll, 'rb').read()
    run(['clang', '--target=x86_64-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-stack-protector', '-std=c11',
         '-Wall', '-Werror', '-c', src, '-o', obj])
    run(['lld-link', '/dll', '/noentry', '/nodefaultlib', '/machine:x64', f'/out:{dll}', obj])
    return open(dll, 'rb').read()


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    out, exe = argv
    work = os.path.join(out, 'build')
    os.makedirs(work, exist_ok=True)
    manifest = open(os.path.join(TEST, 'AppxManifest.xml'), 'rb').read()
    pri = makepri.build('NovaOS.PackageTest', [('Resources', makepri.read_resw(os.path.join(TEST, 'Resources.resw')))])
    dll = greeter(work)
    files = [('appxtest.exe', open(exe, 'rb').read()), ('greeter.dll', dll), ('resources.pri', pri),
             ('AppxManifest.xml', manifest)]
    makeappx.write(os.path.join(out, 'PackageTest.msix'), makeappx.pack(files))
    makeappx.write(os.path.join(out, 'Tampered.msix'),
                   makeappx.pack(files, {'greeter.dll': dll[:-1] + bytes([dll[-1] ^ 0xFF])}))
    makeappx.write(os.path.join(out, 'Signed.msix'),
                   makeappx.pack(files, signature=b'PKCX' + b'\0' * 60))
    noid = manifest.replace(OID.encode(), b'')
    assert noid != manifest
    makeappx.write(os.path.join(out, 'NoOid.msix'), makeappx.pack(files[:-1] + [('AppxManifest.xml', noid)]))


if __name__ == '__main__':
    main(sys.argv[1:])
