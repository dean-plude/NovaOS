# installing a Windows driver package (tools/drivertest/mkdriver.py's, in
# C:\Tests\Driver): from a setup program (setupapi and newdev), from an
# unpacked .zip with pnputil, and from an MSI whose custom action runs pnputil
DOC = ('driver packages staged in the driver store and published as oemN.inf from a program '
       '(`DiInstallDriver`, `SetupCopyOEMInf`), with `pnputil` and from an MSI (`drvtest exe`, `zip`, `msi`)')
TESTS = [
    Test('driver package from a program', 'drvtest exe', [r'drvtest exe: \d+ passed, 0 failed']),
    Test('driver package with pnputil', 'drvtest zip', [r'drvtest zip: \d+ passed, 0 failed']),
    Test('driver package from an MSI', 'drvtest msi', [r'drvtest msi: \d+ passed, 0 failed']),
]
