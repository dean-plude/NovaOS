# stacktest: a thread's stack has a guard page (PAGE_GUARD) below its
# committed part, which moves down as the stack grows with StackLimit on
# top of it, as on Windows (the .NET runtime looks for it before it
# prints an unhandled exception, and said "Stack overflow." without it).
DOC = ('`stacktest` (the first thread\'s and a new thread\'s stack have one guard page, found by `VirtualQuery`, '
       'with the TEB\'s StackLimit on top; it moves down as the stack grows; 64- and 32-bit)')
TESTS = [
    Test('stacktest x64', 'stacktest', [r'stacktest: \d+ passed, 0 failed']),
    Test('stacktest x86', r'C:\Programs\x86\stacktest.exe', [r'stacktest: \d+ passed, 0 failed']),
]
