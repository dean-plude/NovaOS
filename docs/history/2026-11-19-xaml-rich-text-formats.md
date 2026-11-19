## XAML rich text formats

Fifth part of XAML text input, item 1 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)), after the `PasswordBox`, the `RichEditBox`
and the clipboard ([fourth part](2026-11-18-xaml-passwordbox-richeditbox.md)).
A `RichEditBox`'s text now has character formats: bold, italic,
underline, strikethrough and colour, per character, from the keyboard
(Ctrl+B, Ctrl+I, Ctrl+U) and from an app's
`Document.Selection.CharacterFormat`.

- **Character formats in `WinUIEdit.dll`** (`userland/textserv/runs.c`):
  rich text (`TXTBIT_RICHTEXT`, a `RichEditBox`'s) keeps a format for
  each character, an index into a small table of what each format sets
  over the default one, so a change of the default (the host's font)
  shows through wherever a format leaves it alone.  Text put in takes
  the format of the text it replaces or of the character before it, or
  the caret's own when `EM_SETCHARFORMAT` set one with nothing selected,
  as Windows' Rich Edit does; undo and redo keep formats.  Plain text (a
  `TextBox`'s) still has one format.  `EM_SETCHARFORMAT` and
  `EM_GETCHARFORMAT` take `SCF_SELECTION` and `SCF_ALL` (a mixed
  selection leaves the property's `CFM_` bit clear), and the formatting
  keys honour `EM_SETFORMATACCELERATORS`, which a `RichEditBox`'s
  `DisabledFormattingAccelerators` sends.
- **Drawn in runs.**  Each run of one format is measured and drawn in its
  own font (GDI's bold and italic fonts, or the host's DirectWrite faces
  in that weight and style through `IProvideFontInfo`) and colour;
  underline and strikethrough are bars under and through the run.
- **The Text Object Model's font** (`tomfmt.c`): a range's `ITextFont` is
  live, as on Windows: `GetBold`, `GetItalic`, `GetUnderline`,
  `GetStrikeThrough`, `GetForeColor` and `GetWeight` read the range's
  characters now (`tomUndefined` where they differ) and the setters
  format them; `GetDuplicate` gives a font of its own, which
  `SetDuplicate` or the range's `SetFont` applies.  Name and size are
  the default's.
- **`Microsoft.UI.Text.ITextCharacterFormat`** (`winrtfmt.c`): a
  Windows Runtime range's `CharacterFormat` is that TOM font with
  `FormatEffect`, `UnderlineType` and `Windows.UI.Color` values;
  `GetClone`, `SetClone` and `IsEqual` too.  The effects NovaOS does not
  draw (all caps, hidden, outline, small caps, subscript, superscript)
  read Off.

`textservtest` checks the formats on a rich text: set and read back by
selection, a mixed selection, bold drawn heavier and a red run found in
the pixels, underline and strikethrough, typing after a bold word and at
a caret given italic, Ctrl+B twice, Ctrl+U, a key turned off, undo, the
live TOM font, its duplicate applied with `SetFont`, and
`ITextCharacterFormat`'s `Bold`, `ForegroundColor` and `Underline`;
64- and 32-bit.  `xamlpagetest` presses Ctrl+B in its `RichEditBox` and
reads `Document.Selection.CharacterFormat.Bold` back as On.  Left for the
next part: font sizes and faces per run (lines of different heights),
paragraph formats, RTF, links, the text boxes' context menus (WinUI's
text command bar flyouts) and the touch keyboard's input scope.
