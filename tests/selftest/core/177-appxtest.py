# appxtest: app packages (step 32.6): appx.exe installs C:\Tests\Appx's
# packages (tools/appx/mktest.py), refusing the damaged, the signed and the
# unsigned without the OID; ApplicationActivationManager starts the
# installed one by its AUMID, which checks its package identity, its
# resources.pri strings (MRM.dll) and its own Windows Runtime class; then
# appx remove
DOC = ('`appxtest` (app packages: `appx` install, block map and signature checks, start by AUMID, '
       'package identity, `resources.pri`, a packaged Windows Runtime class, remove)')
TESTS = [
    Test('appxtest', 'appxtest', [r'appxtest: \d+ passed, 0 failed'], timeout=180, elevated=True),
]
