DOC = '`conptytest` (a terminal host types into, resizes and exits an interactive `cmd.exe` through `CreatePseudoConsole`)'
TESTS = [
    Test('cmd.exe through a pseudo console', 'conptytest', [r'conptytest: \d+ passed, 0 failed']),
]
