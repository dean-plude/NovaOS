# textservtest: the windowless Rich Edit (userland/textserv/textserv.c) that XAML's TextBox,
# PasswordBox and RichEditBox host: WinUIEdit.dll's CreateTextServices and exported IIDs, and
# the text services driven as a host does (typing, keys, selection, clipboard, undo, limits,
# read-only, lines and word wrap, the mouse, notifications, drawing, a password box), the Text Object
# Model (ITextDocument2, ranges, the selection) and TxDrawD2D; rich text's character formats
# (EM_SETCHARFORMAT/EM_GETCHARFORMAT SCF_SELECTION, Ctrl+B/I/U, TOM fonts, ITextCharacterFormat);
# the context menu asked of the host's IRichEditOleCallback (right button, menu key);
# paragraph alignment (EM_SETPARAFORMAT, Ctrl+E/R/L, TOM ITextPara, ITextParagraphFormat, SPF_SETDEFAULT);
# paragraph indents (start, offset, right: EM_SETPARAFORMAT, TOM ITextPara, ITextParagraphFormat.SetIndents);
# font sizes and faces per character with lines of different heights (EM_SETCHARFORMAT CFM_SIZE/CFM_FACE,
# TOM ITextFont Set/GetSize and Name, ITextCharacterFormat.Size and Name);
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
       'and the menu key, not with `RTO_DISABLECONTEXTMENU`); paragraph alignment (`EM_SETPARAFORMAT` and '
       '`EM_GETPARAFORMAT`, drawn and checked by pixels with the caret following, Enter keeping it on both '
       'sides, Ctrl+E/R/L and undo, the TOM range\'s live `ITextPara` with `GetDuplicate` and `SetPara`, '
       '`ITextParagraphFormat.Alignment`, and plain text\'s default from `SPF_SETDEFAULT` and the host\'s '
       '`TxGetParaFormat`); paragraph indents (`EM_SETPARAFORMAT`/`EM_GETPARAFORMAT` start, offset '
       'and right indents, placed and drawn by pixels, Enter keeping them, undo, hanging and right indents '
       'with word wrap, the TOM range\'s `ITextPara` indents and `ITextParagraphFormat.SetIndents`); '
       'font sizes and faces per character (`EM_SETCHARFORMAT`/`EM_GETCHARFORMAT` `CFM_SIZE` and `CFM_FACE`, '
       'a line as tall as its biggest text with the next line, the natural size, the caret and mouse hits '
       'following, typing taking the size, undo, the TOM range\'s `ITextFont` `SetSize`, `GetSize`, `SetName` '
       'and `GetName`, `ITextCharacterFormat.Size` and `Name`, plain text sized as one); '
       '`msftedit.dll` and `riched20.dll` too)')
TESTS = [
    Test('textservtest', 'textservtest', [r'textservtest: \d+ passed, 0 failed'], timeout=120),
    Test('textservtest x86', r'C:\Programs\x86\textservtest.exe', [r'textservtest: \d+ passed, 0 failed'], timeout=120),
]
