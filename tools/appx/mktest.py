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
  Framework.msix    NovaOS.TestFramework 1.0.0.0 (x64), a framework package
                    signed (tools/appx/signappx.py) by "NovaOS Package
                    Signer" under the Authenticode tests' "NovaOS Test
                    Root" (testroot.cer, which appxtest trusts for its
                    checks): novafw.dll (tools/appx/test/novafw.c) and
                    fwgreeter.dll (greeter.dll as the framework's class
                    NovaOS.TestFramework.Greeter)
  FrameworkTampered.msix  the same with a byte of its records changed after
                    it was signed (its files still match the block map):
                    must be refused (the signature's AXPC digest)
  FrameworkWrongPublisher.msix  signed by the same signer, its publisher
                    another's: must be refused
  FrameworkTest.msixbundle  a bundle signed by the same signer: NovaOS.
                    FrameworkTest 1.0.0.0 for x64 (appxtest.exe, depending
                    on NovaOS.TestFramework), the same for x86 (which must
                    not be picked) and a scale-200 resource package (not
                    installed yet)
  testroot.cer      the test root
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import makeappx  # noqa: E402
import makepri   # noqa: E402
import signappx  # noqa: E402
from pki import Cert, Key  # noqa: E402

TEST = os.path.join(HERE, 'test')
OID = ', OID.2.25.311729368913984317654407730594956997722=1'


def run(cmd):
    r = subprocess.run(cmd)
    if r.returncode:
        raise SystemExit(f'mktest: failed: {" ".join(cmd)}')


SIGNER = 'CN=NovaOS Package Signer, O=NovaOS, C=US'
LONG = ((2000, 1, 1, 0, 0, 0), (2049, 12, 31, 23, 59, 59))

FRAMEWORK = '''<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">
  <Identity Name="NovaOS.TestFramework" Publisher="%s" Version="1.0.0.0" ProcessorArchitecture="x64" />
  <Properties>
    <Framework>true</Framework>
    <DisplayName>NovaOS Test Framework</DisplayName>
    <PublisherDisplayName>NovaOS</PublisherDisplayName>
    <Logo>novafw.dll</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.17763.0" MaxVersionTested="10.0.22621.0" />
  </Dependencies>
  <Resources>
    <Resource Language="en-us" />
  </Resources>
  <Extensions>
    <Extension Category="windows.activatableClass.inProcessServer">
      <InProcessServer>
        <Path>fwgreeter.dll</Path>
        <ActivatableClass ActivatableClassId="NovaOS.TestFramework.Greeter" ThreadingModel="both" />
      </InProcessServer>
    </Extension>
  </Extensions>
</Package>
'''

APP = '''<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
         xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
         IgnorableNamespaces="uap rescap">
  <Identity Name="NovaOS.FrameworkTest" Publisher="%s" Version="1.0.0.0" ProcessorArchitecture="%s" %s/>
  <Properties>
    <DisplayName>NovaOS Framework Test</DisplayName>
    <PublisherDisplayName>NovaOS</PublisherDisplayName>
    <Logo>appxtest.exe</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.17763.0" MaxVersionTested="10.0.22621.0" />
    <PackageDependency Name="NovaOS.TestFramework" Publisher="%s" MinVersion="1.0.0.0" />
  </Dependencies>
  <Resources>
    <Resource Language="en-us" />
  </Resources>
  <Applications>
    <Application Id="App" Executable="appxtest.exe" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="NovaOS Framework Test" Description="The appx self-test's framework user"
                          BackgroundColor="transparent" Square150x150Logo="appxtest.exe" Square44x44Logo="appxtest.exe"
                          AppListEntry="none" />
    </Application>
  </Applications>
  <Capabilities>
    <rescap:Capability Name="runFullTrust" />
  </Capabilities>
</Package>
'''


def freestanding(out, name):
    """tools/appx/test/@name.c as @name.dll: clang and lld-link, freestanding"""
    obj, dll = os.path.join(out, name + '.obj'), os.path.join(out, name + '.dll')
    src = os.path.join(TEST, name + '.c')
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
    dll = freestanding(work, 'greeter')
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

    # the signed framework, its damaged copies, and the bundle that uses it
    root = Cert('NovaOS Test Root', Key('novaos-test-root'), 1, LONG, ca=True)
    signer = Cert('NovaOS Package Signer', Key('novaos-test-signer'), 5, ((2020, 1, 1, 0, 0, 0), LONG[1]), root,
                  root.key, usage='codesign')
    fw = [('novafw.dll', freestanding(work, 'novafw')), ('fwgreeter.dll', dll),
          ('AppxManifest.xml', (FRAMEWORK % SIGNER).encode())]
    signed = signappx.sign(makeappx.pack(fw), signer, [root])
    makeappx.write(os.path.join(out, 'Framework.msix'), signed)
    t = bytearray(signed)
    t[4] ^= 0x01                                    # (the first record's "version needed")
    makeappx.write(os.path.join(out, 'FrameworkTampered.msix'), bytes(t))
    other = fw[:-1] + [('AppxManifest.xml', (FRAMEWORK % 'CN=Someone Else, O=NovaOS, C=US').encode())]
    makeappx.write(os.path.join(out, 'FrameworkWrongPublisher.msix'), signappx.sign(makeappx.pack(other), signer, [root]))
    exe_ = files[0][1]
    app = [makeappx.pack([('appxtest.exe', exe_), ('AppxManifest.xml', (APP % (SIGNER, a, '', SIGNER)).encode())])
           for a in ('x64', 'x86')]
    res = makeappx.pack([('resources.pri', pri), ('AppxManifest.xml', (APP % (
        SIGNER, 'x64', 'ResourceId="split.scale-200" ', SIGNER)).encode())])
    bundle = makeappx.pack_bundle('NovaOS.FrameworkTest', SIGNER, '1.0.0.0', [
        ('application', 'x86', 'FrameworkTest_x86.msix', app[1], None),
        ('application', 'x64', 'FrameworkTest_x64.msix', app[0], None),
        ('resource', None, 'FrameworkTest_scale-200.msix', res, 'split.scale-200')])
    makeappx.write(os.path.join(out, 'FrameworkTest.msixbundle'), signappx.sign(bundle, signer, [root], bundle=True))
    makeappx.write(os.path.join(out, 'testroot.cer'), root.der)


if __name__ == '__main__':
    main(sys.argv[1:])
