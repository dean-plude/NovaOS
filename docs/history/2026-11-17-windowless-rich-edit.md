## The windowless Rich Edit: WinUIEdit.dll's text services

First part of XAML text input, item 1 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)).  XAML's `TextBox`, `PasswordBox` and
`RichEditBox` keep their text in a windowless Rich Edit: they load
`WinUIEdit.dll` by name, call its `CreateTextServices` with their own
`ITextHost2`, and pass their keyboard, mouse and focus messages to the
`ITextServices2` it returns.  `WinUIEdit.dll` is closed, and NovaOS's
`riched20.dll` answered `CreateTextServices` with `E_NOTIMPL`.

- **The text services** (`userland/textserv/textserv.c`, the interfaces
  in C in `userland/include/novatextserv.h`) are built into a new
  `WinUIEdit.dll` and into `msftedit.dll` and `riched20.dll`.  The DLLs
  export `CreateTextServices`, `ShutdownTextServices` and the IIDs XAML
  reads from them (`IID_ITextServices2`, `IID_ITextDocument2`,
  `IID_IRicheditWindowlessAccessibility`, the host's).  As on Windows the
  host is kept without a reference, and in 32-bit programs the
  interfaces' methods are `thiscall`.
- **Editing.** Typing (`WM_CHAR`; consecutive characters undo as one
  step), Backspace, the arrows, Home, End and Delete with Shift
  extending the selection and Ctrl moving by words, Ctrl+A, C, X, V, Z
  and Y; the mouse (click, Shift+click, drag, double-click for a word);
  the clipboard (`WM_COPY`, `WM_CUT`, `WM_PASTE`, `WM_CLEAR`); undo and
  redo; the text limit (`EN_MAXTEXT`), read-only and the modify flag;
  one line (Enter goes to the host, pasted text keeps its first
  paragraph) or several, word-wrapped to the host's width.
- **The host is told** what changed, as its event mask asks
  (`EN_CHANGE`, `EN_SELCHANGE` with the range), and where the caret is
  (`TxCreateCaret`, `TxSetCaretPos`, `TxShowCaret`), and asked to redraw.
- **Drawing.** `TxDraw` draws in the host's character format and colours:
  the text, the selection on the highlight colour (hidden without focus
  when the host asks), a single line scrolled to keep the caret in view.
  A password box draws its password character for each character and
  never copies or cuts.
- **Messages**: `WM_SETTEXT`/`WM_GETTEXT` (with `\r\n`), `EM_GETTEXTEX`,
  `EM_SETTEXTEX`, `EM_EXSETSEL`/`EM_EXGETSEL`, `EM_REPLACESEL`,
  `EM_GETSELTEXT`, `EM_GETTEXTRANGE`, `EM_POSFROMCHAR`,
  `EM_CHARFROMPOS`, the line messages, `EM_EXLIMITTEXT`,
  `EM_SETREADONLY`, `EM_SETEVENTMASK`, `EM_SETCHARFORMAT` and the
  option and style messages XAML sends; `TxGetNaturalSize(2)` measures.

The core suite's `textservtest` (64- and 32-bit) is a host written the
way XAML's is and checks each of these, the drawing by the bitmap's
pixels.  Not yet: the Text Object Model (`ITextDocument2`,
`EM_GETOLEINTERFACE`) and `TxDrawD2D`, which XAML's `TextBox` uses, rich
text formatting, IME composition and the touch keyboard; then the
`TextBox` itself on a XAML page.
