# textservtest: the windowless Rich Edit (userland/textserv/textserv.c) that XAML's TextBox,
# PasswordBox and RichEditBox host: WinUIEdit.dll's CreateTextServices and exported IIDs, and
# the text services driven as a host does (typing, keys, selection, clipboard, undo, limits,
# read-only, lines and word wrap, the mouse, notifications, drawing, a password box); 32-bit too,
# where the interfaces' methods are thiscall
DOC = ('`textservtest` (the windowless Rich Edit XAML\'s text boxes host: `WinUIEdit.dll`\'s '
       '`CreateTextServices` and IIDs, typing, editing keys, selection, clipboard, undo and redo, '
       'limits, read-only, lines and word wrap, the mouse, `EN_CHANGE`/`EN_SELCHANGE`/`EN_MAXTEXT`, '
       '`TxDraw` checked by pixels, a password box; `msftedit.dll` and `riched20.dll` too)')
TESTS = [
    Test('textservtest', 'textservtest', [r'textservtest: \d+ passed, 0 failed'], timeout=120),
    Test('textservtest x86', r'C:\Programs\x86\textservtest.exe', [r'textservtest: \d+ passed, 0 failed'], timeout=120),
]
