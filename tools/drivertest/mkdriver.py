#!/usr/bin/env python3
"""mkdriver.py — the driver package the drvtest self-test installs

    mkdriver.py OUTDIR [--pnp]

tools/build_userland.py runs it and puts what it writes in C:\\Tests\\Driver:
novadrv.inf (a root-enumerated "System" class device, ROOT\\NOVADRV, with a
kernel service), novadrv.sys and novadrv.cat.  The .sys and .cat are
placeholders: NovaOS stages a package in its driver store but does not yet
start a Windows driver on a device, and a placeholder catalog is no signed
catalog, so setupapi stages the package as unsigned (mksys.py's packages
carry real test-signed catalogs; the driver NovaOS does load, as a kernel
service, is its wdmtest.sys).
tools/msitest/mkpkg.py packs the same files into driver.msi.

With --pnp it writes the Plug and Play self-test's package instead (for
C:\\Tests\\PnpDriver): novakbd.inf, a Keyboard-class driver for QEMU's USB
keyboard (HID\\VID_0627&PID_0001), which pnputil /enum-devices /drivers
lists as matching the keyboard, outranked by NovaOS's own driver.
"""
import os, sys

INF = r'''; novadrv.inf - the NovaOS driver-install self-test's package (tools/drivertest/mkdriver.py)
[Version]
Signature   = "$WINDOWS NT$"
Class       = System
ClassGuid   = {4d36e97d-e325-11ce-bfc1-08002be10318}
Provider    = %ProviderName%
DriverVer   = 10/10/2026,1.0.0.0
CatalogFile = novadrv.cat
PnpLockdown = 1

[DestinationDirs]
DefaultDestDir = 13

[SourceDisksNames]
1 = %DiskName%,,,""

[SourceDisksFiles]
novadrv.sys = 1,,

[Manufacturer]
%ProviderName% = NovaTest,NTamd64,NTx86

[NovaTest.NTamd64]
%DeviceDesc% = NovaDrv_Install, ROOT\NOVADRV

[NovaTest.NTx86]
%DeviceDesc% = NovaDrv_Install, ROOT\NOVADRV

[NovaDrv_Install.NT]
CopyFiles = NovaDrv_Files

[NovaDrv_Files]
novadrv.sys

[NovaDrv_Install.NT.Services]
AddService = NovaDrv, 0x00000002, NovaDrv_Service

[NovaDrv_Service]
ServiceType   = 1               ; SERVICE_KERNEL_DRIVER
StartType     = 3               ; SERVICE_DEMAND_START
ErrorControl  = 1
ServiceBinary = %13%\novadrv.sys

[Strings]
ProviderName = "NovaOS Test"
DiskName     = "NovaOS driver test disk"
DeviceDesc   = "NovaOS Test Device"
'''.replace('\n', '\r\n').encode('ascii')

SYS = b'MZ' + b'\0' * 62 + b'NovaOS driver-install test placeholder: never loaded\r\n'
CAT = b'0\x82\0\0NovaOS driver-install test catalog placeholder\r\n'

FILES = [('novadrv.inf', INF), ('novadrv.sys', SYS), ('novadrv.cat', CAT)]

PNP_INF = INF.decode('ascii').replace('novadrv', 'novakbd').replace('NovaDrv', 'NovaKbd') \
    .replace('Class       = System', 'Class       = Keyboard') \
    .replace('{4d36e97d-e325-11ce-bfc1-08002be10318}', '{4d36e96b-e325-11ce-bfc1-08002be10318}') \
    .replace('ROOT\\NOVADRV', 'HID\\VID_0627&PID_0001') \
    .replace('"NovaOS Test Device"', '"NovaOS Test Keyboard"').encode('ascii')
PNP_FILES = [('novakbd.inf', PNP_INF), ('novakbd.sys', SYS), ('novakbd.cat', CAT)]


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, data in (PNP_FILES if '--pnp' in sys.argv[2:] else FILES):
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)


if __name__ == '__main__':
    main()
