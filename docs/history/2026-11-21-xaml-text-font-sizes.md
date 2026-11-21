## XAML text font sizes and faces

Ninth part of XAML text input, item 1 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)), after paragraph indents
([eighth part](2026-11-20-xaml-paragraph-indents.md)).  Characters in a
XAML `RichEditBox` can have a size and a typeface of their own
(`CharacterFormat.Size` and `Name` on its `Document`, or Rich Edit's own
messages), and each line is as tall as its tallest text.

- **Sizes and faces per character** (`userland/textserv/runs.c`): a
  character's format now also keeps a size in twips and a face name,
  each either its own or the default's.  Each distinct face, size, bold
  and italic in use is a font key with its GDI font or the host's
  DirectWrite face (`dwtext.c`, asked of XAML's `IProvideFontInfo` by
  the key's face name) and its height and ascent; key 0 stays the
  default's.  Plain text keeps one format, so a `TextBox`'s selection
  size is the whole text's.
- **Lines of different heights** (`textserv.c`): layout gives each line
  a top, a height and a baseline from the tallest ascent and deepest
  descent of its characters and its paragraph mark, and every run of a
  line is drawn on that one baseline (GDI and Direct2D).  The caret is
  as tall as its line, and mouse hits, scrolling, `EM_POSFROMCHAR`,
  TOM's `GetPoint`, `TxGetNaturalSize` and the baseline follow the
  line tops instead of one line height.
- **Every way in**: `EM_SETCHARFORMAT` and `EM_GETCHARFORMAT`
  (`CFM_SIZE`, `CFM_FACE`, undefined over text that differs), the Text
  Object Model's `ITextFont` (`SetSize` in points, `SetName`, `GetSize`
  and `GetName` `tomUndefined` or empty over mixed text, duplicates and
  `IsEqual` carry them), and `Microsoft.UI.Text.ITextCharacterFormat`
  (`Size`, `Name`).

`textservtest` sets 18 points on the first of two lines and finds the
second line's top, the natural size and the caret follow the taller
line, the bigger text drawn bigger, a mouse hit below the first line's
height land on the second, typing take the size before it, undo, a face
set and read back and undefined over mixed text, `ITextFont`'s
`SetSize`, `GetSize`, `SetName` and `GetName` live over a range,
`ITextCharacterFormat`'s `Size` and `Name`, and plain text sized by a
selection.  Left for later parts: WinUI's text command bar flyout
(`TextCommandBarFlyout`, on `CommandBarFlyout` and `CommandBar`),
paragraph spacing and line spacing (now simple with per-line heights),
tabs and lists, RTF, links and the touch keyboard's input scope.
