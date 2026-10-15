# wictest: windowscodecs.dll, the Windows Imaging Component (tools/selftest.py's core suite)
DOC = '`wictest`'
TESTS = [
    Test('wictest', 'wictest', [r'wictest: \d+ passed, 0 failed']),
    Test('wictest x86', r'C:\Programs\x86\wictest.exe', [r'wictest: \d+ passed, 0 failed']),
]
