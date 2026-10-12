# chkstktest (tools/selftest.py's core suite)
DOC = '`chkstktest` (ntdll\'s stack probe, `__chkstk` on x64 and `_chkstk` on x86, and its half-precision conversions, which LLVM\'s JIT in Mesa looks up)'
TESTS = [
    Test('chkstktest x64', 'chkstktest', [r'chkstktest: \d+ passed, 0 failed']),
    Test('chkstktest x86', r'C:\Programs\x86\chkstktest.exe', [r'chkstktest: \d+ passed, 0 failed']),
]
