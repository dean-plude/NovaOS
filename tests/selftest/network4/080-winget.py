# winget (userland/programs/winget.c) against the WinGet REST source
# tools/h2server.js serves at http://10.0.2.2:18080/winget: the source is
# added (its /information read), searched, a package installed at an older
# version (download, SHA-256, a portable program in C:\Programs\wgtest),
# listed with its upgrade, upgraded, run from its new place and uninstalled;
# a download whose hash differs from the manifest's is refused; and the
# App Store's catalog is searched as the "novaos" source (no network)
SRC = 'http://10.0.2.2:18080/winget'
TESTS = [
    Test('winget source add', f'winget source add --name test {SRC} -t Microsoft.Rest',
         [r'Adding source:', r'test -> ' + SRC, r'\nDone']),
    Test('winget search', 'winget search wgtest -s test',
         [r'WinGet Test\s+NovaOS\.WinGetTest\s+2\.0\.0\s+.*test']),
    Test('winget install', 'winget install --id NovaOS.WinGetTest -v 1.0.0 -s test --accept-package-agreements',
         [r'Found WinGet Test \[NovaOS\.WinGetTest\] Version 1\.0\.0', r'Successfully verified installer hash',
          r'Command line alias added: "wgtest"', r'Successfully installed NovaOS\.WinGetTest 1\.0\.0']),
    Test('winget list', 'winget list wgtest',
         [r'WinGet Test\s+NovaOS\.WinGetTest\s+1\.0\.0\s+2\.0\.0\s+test']),
    Test('winget upgrade', 'winget upgrade --all',
         [r'Found WinGet Test \[NovaOS\.WinGetTest\] Version 2\.0\.0', r'Successfully installed NovaOS\.WinGetTest 2\.0\.0']),
    Test('winget upgraded', r'type C:\Programs\wgtest\wgtest.exe', [r'NovaOS winget test 2\.0\.0'], builtin=True),
    Test('winget uninstall', 'winget uninstall NovaOS.WinGetTest',
         [r'Found WinGet Test \[NovaOS\.WinGetTest\]', r'Successfully uninstalled']),
    Test('winget hash mismatch', 'cmd /c "winget install NovaOS.WinGetBad -s test || (echo winget refused it & exit 0)"',
         [r'Installer hash does not match', r'winget refused it']),
    Test('winget store source', 'winget search 7-Zip -s novaos', [r'7-Zip\s+7zip\.7zip\s+24\.08\s+.*novaos']),
]
