# textservtest: the windowless Rich Edit (userland/textserv/textserv.c) that XAML's TextBox,
# PasswordBox and RichEditBox host: WinUIEdit.dll's CreateTextServices and exported IIDs, and
# the text services driven as a host does (typing, keys, selection, clipboard, undo, limits,
# read-only, lines and word wrap, the mouse, notifications, drawing, a password box), the Text Object
# Model (ITextDocument2, ranges, the selection) and TxDrawD2D; rich text's character formats
# (EM_SETCHARFORMAT/EM_GETCHARFORMAT SCF_SELECTION, Ctrl+B/I/U, TOM fonts, ITextCharacterFormat);
# the context menu asked of the host's IRichEditOleCallback (right button, menu key);
# 32-bit too, where the text services' methods are thiscall
DOC = ('`textservtest` (the windowless Rich Edit XAML\'s text boxes host: `WinUIEdit.dll`\'s '
       '`CreateTextServices` and IIDs, typing, editing keys, selection, clipboard, undo and redo, '
       'limits, read-only, lines and word wrap, the mouse, `EN_CHANGE`/`EN_SELCHANGE`/`EN_MAXTEXT`, '
       '`TxDraw` checked by pixels, a password box; the Text Object Model as XAML\'s `TextBoxBase` '
       'uses it: `ITextDocument2`, ranges that follow edits, the selection, units, points, find, '
       'clipboard, undo with `tomSuspend`; `TxDrawD2D` on a Direct2D render target, opaque and '
       'transparent; the cut, copy and paste keys and the TOM selection\'s offered to the host first '
       '(`EN_CUTREQUESTED`, `EN_COPYREQUESTED`, `EN_PASTEREQUESTED`); the Windows Runtime document a XAML '
       '`RichEditBox` hands out (`Microsoft.UI.Text`\'s `ITextDocument`: text, ranges, the selection, undo); '
       'rich text\'s character formats (bold, italic, underline, strikethrough and colour per character by '
       '`EM_SETCHARFORMAT`, read back by `EM_GETCHARFORMAT`, drawn and checked by pixels, typing taking the '
       'format before it or the caret\'s, Ctrl+B/I/U and `EM_SETFORMATACCELERATORS`, undo; the TOM range\'s live '
       '`ITextFont`, its duplicate and `SetFont`; `ITextCharacterFormat` on a Windows Runtime range); '
       'the context menu asked of the host\'s `IRichEditOleCallback` (`GetContextMenu` for the right button '
       'and the menu key, not with `RTO_DISABLECONTEXTMENU`); '
       '`msftedit.dll` and `riched20.dll` too)')
TESTS = [
    Test('textservtest', 'textservtest', [r'textservtest: \d+ passed, 0 failed'], timeout=120),
    Test('textservtest x86', r'C:\Programs\x86\textservtest.exe', [r'textservtest: \d+ passed, 0 failed'], timeout=120),
]
