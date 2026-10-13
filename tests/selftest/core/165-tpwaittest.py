# tpwaittest (tools/selftest.py's core suite)
DOC = '`tpwaittest` (thread-pool waits: a wait calls back once per signal or on its timeout, a callback sets its own wait again, setting a wait never waits for a running callback, cancelling and closing from a callback; 64- and 32-bit)'
TESTS = [
    Test('tpwaittest x64', 'tpwaittest', [r'tpwaittest: \d+ passed, 0 failed']),
    Test('tpwaittest x86', r'C:\Programs\x86\tpwaittest.exe', [r'tpwaittest: \d+ passed, 0 failed']),
]
