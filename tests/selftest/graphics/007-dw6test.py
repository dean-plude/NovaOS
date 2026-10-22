# tools/dw6test: DirectWrite's Windows 10 font model as XAML's text core
# uses it: IDWriteFactory4-7, the typographic family model (font collections
# and families 2, font lists 2), matching and filtering by axis values, font
# sets 1/2, font faces 4-6 and resources, set builders 2, the in-memory
# loader, text formats 1-3 and layouts 1-4, font fallback 1 and text
# analyzers 1/2; 64- and 32-bit (C:\Tests)
TESTS = [
    Test('dw6test x64', r'C:\Tests\dw6test.exe', [r'dw6test: \d+ passed, 0 failed']),
    Test('dw6test x86', r'C:\Tests\dw6test32.exe', [r'dw6test: \d+ passed, 0 failed']),
]
