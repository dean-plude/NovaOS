# comtest: ole32/oleaut32, IShellLink, TaskbarList (tools/selftest.py's core suite)
DOC = '`comtest`'
TESTS = [
    Test('comtest', 'comtest', [r'comtest: \d+ passed, 0 failed']),
    Test('comtest x86', r'C:\Programs\x86\comtest.exe', [r'comtest: \d+ passed, 0 failed']),
]
