DOC = '`consolecursortest` (classic console screen buffers: cursor, cell I/O, fills, scrolling, attributes and independent buffers; 64- and 32-bit)'
TESTS = [
    Test('console cursor x64', 'consolecursortest', [r'consolecursortest: \d+ passed, 0 failed']),
    Test('console cursor x86', r'C:\Programs\x86\consolecursortest.exe', [r'consolecursortest: \d+ passed, 0 failed']),
]
