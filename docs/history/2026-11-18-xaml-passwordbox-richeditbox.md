## XAML PasswordBox, RichEditBox and the clipboard

Fourth part of XAML text input, item 1 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)), after the typable `TextBox`
([third part](2026-11-18-xaml-textbox.md)).  A XAML page's `PasswordBox`
and `RichEditBox` now take typing, and the text boxes select, copy, cut
and paste through the clipboard with the keyboard.

- **Built from microsoft-ui-xaml 866ba81** (MIT): the core's
  `PasswordBox.cpp` and `RichEditBox.cpp` (in the text controls
  library), the framework's `PasswordBox_Partial`, `RichEditBox_Partial`
  and the generated classes of both and of their event args.  NovaOS's
  closed-header stand-ins gain what they name: `TextInput.h`'s touch
  keyboard skin (nothing to set on NovaOS) and `WinUIEdit.h`'s
  `EM_SETFORMATACCELERATORS`, `EM_HANDLELINKNOTIFICATION`,
  `EN_LINK_DO_DEFAULT` and `IMF_COPYPLAINTEXTONLY`.
  `Windows.UI.Xaml.dll` links `crypt32` (a `PasswordBox` keeps its
  password with `CryptProtectMemory`), and `shlwapi.dll` gains
  `SHRegGetIntW` (its `DisablePasswordReveal` policy).
- **`WinUIEdit.dll`'s Windows Runtime document**
  (`userland/textserv/winrtdoc.c`): a `RichEditBox`'s `Document` is its
  text services, asked for `Microsoft.UI.Text.ITextDocument`.  The
  document, its ranges (`ITextRange`) and its selection
  (`ITextSelection`) are thin wrappers over the Text Object Model: text
  in and out, positions, units, find, case, the clipboard, undo and
  redo, the selection's key moves and `TypeText`.  Formats, links,
  images, streams and math answer `E_NOTIMPL`.
- **Cut, copy and paste ask the control first.**  XAML takes Ctrl+C, X
  and V itself and cuts, copies or pastes through the TOM selection;
  `WinUIEdit.dll` now offers each to the host first (`EN_CUTREQUESTED`,
  `EN_COPYREQUESTED`, `EN_PASTEREQUESTED`, as do its own Ctrl and
  Shift+Insert/Delete keys), which is where a text box raises
  `CuttingToClipboard`, `CopyingToClipboard` and `Paste`; an app that
  handles one stops the Rich Edit's own.

`xamlpagetest` (82 checks) now selects all of its `TextBox` with Ctrl+A,
copies ("Hi" on the clipboard, `CopyingToClipboard`), pastes after it
("HiHi", `Paste`), cuts it all (`CuttingToClipboard`) and pastes it back;
types "pw1" into a `PasswordBox` (its `Password`, `PasswordChanged`
three times, three password characters drawn, Ctrl+C copying nothing);
and types "Rich" into a `RichEditBox` (its `Document.GetText`,
`TextChanged`, Ctrl+A and Ctrl+C).  `textservtest` checks the requests
and the Windows Runtime document, 64- and 32-bit.  This is the item's CI
check; left for later are the text boxes' context menus (WinUI's text
command bar flyouts), rich text formats and the touch keyboard's input
scope.
