## XAML text box context menu

Sixth part of XAML text input, item 1 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)), after rich text's character formats
([fifth part](2026-11-19-xaml-rich-text-formats.md)).  A right click in
a `TextBox`, `PasswordBox` or `RichEditBox`, or the menu key, opens the
text box's context menu: Cut, Copy, Paste, Undo, Redo and Select All, as
the text and the clipboard allow (a password box offers neither Cut nor
Copy).

- **Rich Edit asks its host** (`userland/textserv/textserv.c`):
  `WinUIEdit.dll` holds the OLE callback `EM_SETOLECALLBACK` gives and
  calls its `IRichEditOleCallback::GetContextMenu` with the selection's
  type and range, and for the mouse `GCM_MOUSEMENU` and the point
  (Rich Edit 8's `GETCONTEXTMENUEX`).  It asks when the right button is
  let go (a XAML text box hands Rich Edit the mouse's release, and a
  pen's `RightTapped` as one) and when the menu key is; never while
  `EM_SETTOUCHOPTIONS` has `RTO_DISABLECONTEXTMENU` (a text box with a
  `ContextFlyout` of its own).  The right button's press moves the caret
  there unless it is in the selection.  The Text Object Model's document
  answers the properties the menu asks (`tomCanCopy`, `tomCanUndo`,
  `tomCanRedo`, `tomUndoLimit`).
- **XAML's menu** (`TextContextMenu`, vendored from Microsoft's MIT
  sources with its command handlers, and the `ContextRequested` event's
  arguments a right click raises first): the callback raises the text box's
  `ContextMenuOpening` event, and when no handler takes it the menu is
  shown on a `Windows.UI.Popups.PopupMenu`; the command chosen runs on
  the text box (`TextBoxCommandHandler`).
- **`Windows.UI.Popups`' `PopupMenu` and `UICommand`**
  (`userland/twinapi_appcore/popups.c`, in `twinapi.appcore.dll`): a
  menu of up to six commands shown at a point or beside a rectangle in
  its window (`IInitializeWithWindow`; no window or a hidden one gives
  way to the thread's focus window, as for XAML islands), as a user32 popup menu
  opened once the thread is back in its message loop.  The command
  chosen has its `Invoked` handler called, then `ShowAsync`'s operation
  completes with it, or with none when the menu is dismissed.  UWP apps
  use the same class for menus of their own.

`textservtest` checks the callback: held by `EM_SETOLECALLBACK`, not
asked by the right button's press, asked by its release (for the mouse,
at its point) and by the menu key (with the selection), not asked with
`RTO_DISABLECONTEXTMENU` or once the callback is cleared, and the
document's `tomCanCopy`, `tomCanUndo` and `tomCanRedo`.
`xamlpagetest` right-clicks its `TextBox`, finds the menu open and
chooses Select All from it with the keyboard, then opens it with the
menu key and closes it with Escape.  Left for the next part: WinUI's text
command bar flyout (`TextCommandBarFlyout`, which Windows 10's text boxes
show instead, on `CommandBarFlyout` and `CommandBar`), font sizes and
faces per run, paragraph formats, RTF, links and the touch keyboard's
input scope.
