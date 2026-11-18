# textservtest: the windowless Rich Edit (userland/textserv/textserv.c) that XAML's TextBox,
# PasswordBox and RichEditBox host: WinUIEdit.dll's CreateTextServices and exported IIDs, and
# the text services driven as a host does (typing, keys, selection, clipboard, undo, limits,
# read-only, lines and word wrap, the mouse, notifications, drawing, a password box), the Text Object
# Model (ITextDocument2, ranges, the selection) and TxDrawD2D; 32-bit too, where the text services'
# methods are thiscall
DOC = ('`textservtest` (the windowless Rich Edit XAML\'s text boxes host: `WinUIEdit.dll`\'s '
       '`CreateTextServices` and IIDs, typing, editing keys, selection, clipboard, undo and redo, '
       'limits, read-only, lines and word wrap, the mouse, `EN_CHANGE`/`EN_SELCHANGE`/`EN_MAXTEXT`, '
       '`TxDraw` checked by pixels, a password box; the Text Object Model as XAML\'s `TextBoxBase` '
       'uses it: `ITextDocument2`, ranges that follow edits, the selection, units, points, find, '
       'clipboard, undo with `tomSuspend`; `TxDrawD2D` on a Direct2D render target, opaque and '
       'transparent; the cut, copy and paste keys and the TOM selection\'s offered to the host first '
       '(`EN_CUTREQUESTED`, `EN_COPYREQUESTED`, `EN_PASTEREQUESTED`); the Windows Runtime document a XAML '
       '`RichEditBox` hands out (`Microsoft.UI.Text`\'s `ITextDocument`: text, ranges, the selection, undo); '
       '`msftedit.dll` and `riched20.dll` too)')
TESTS = [
    Test('textservtest', 'textservtest', [r'textservtest: \d+ passed, 0 failed'], timeout=120),
    Test('textservtest x86', r'C:\Programs\x86\textservtest.exe', [r'textservtest: \d+ passed, 0 failed'], timeout=120),
]
