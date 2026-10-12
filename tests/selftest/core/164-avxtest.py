# avxtest (tools/selftest.py's core suite)
DOC = '`avxtest` (the AVX state: CPUID\'s OSXSAVE, AVX and F16C and XCR0 as the kernel sets them, YMM0-15 kept across context switches, in an exception\'s `CONTEXT_XSTATE` and through `NtContinue`, and read from a suspended thread by `GetThreadContext`)'
TESTS = [
    Test('avxtest', 'avxtest', [r'avxtest: \d+ passed, 0 failed']),
]
