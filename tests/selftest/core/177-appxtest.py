# appxtest: app packages (step 32.6): appx.exe installs C:\Tests\Appx's
# packages (tools/appx/mktest.py), refusing the damaged, the badly signed
# and the unsigned without the OID; ApplicationActivationManager starts the
# installed one by its AUMID, which checks its package identity, its
# resources.pri strings (MRM.dll) and its own Windows Runtime class; then
# appx remove.  Then signed packages under a test root: a framework
# package and a signed bundle whose program finds the framework's DLL and
# Windows Runtime class through its package graph
DOC = ('`appxtest` (app packages: `appx` install, block map and signature checks (APPX SIP, publisher), '
       'start by AUMID, package identity, `resources.pri`, a packaged Windows Runtime class, framework packages '
       'and the package graph, a signed bundle, a Store-layout bundle whose packages are signed by themselves, remove)')
TESTS = [
    Test('appxtest', 'appxtest', [r'appxtest: \d+ passed, 0 failed'], timeout=300, elevated=True),
]
