## XAML text paragraph indents

Eighth part of XAML text input, item 1 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)), after paragraph alignment
([seventh part](2026-11-20-xaml-paragraph-alignment.md)).  Paragraphs in
a XAML text box can be indented: the first line, the lines after it
(hanging indents too) and the right edge, from a `RichEditBox`'s
`Document` (`ParagraphFormat.SetIndents`, `RightIndent`) or Rich Edit's
own messages.

- **Indents in the windowless Rich Edit** (`userland/textserv/runs.c`):
  kept as the alignment is, on the paragraph's mark (Enter keeps them on
  both sides, undo restores them), the final paragraph's apart and plain
  text's as one; in twips as `PARAFORMAT`'s start indent, offset and right
  indent, each either the paragraph's own or the default's (the host's
  `TxGetParaFormat`, or `EM_SETPARAFORMAT` with `SPF_SETDEFAULT`).
- **Layout** (`textserv.c`): each line wraps in the width inside its
  paragraph's indents and starts at the first line's or the others'
  indent, before any centring or right alignment, so the caret, mouse
  hits and GDI and DirectWrite drawing follow; `TxGetNaturalSize` counts
  the indents.
- **Every way in**: `EM_SETPARAFORMAT` and `EM_GETPARAFORMAT`
  (`PFM_STARTINDENT`, `PFM_OFFSETINDENT`, `PFM_OFFSET`, `PFM_RIGHTINDENT`,
  undefined over paragraphs that differ), the Text Object Model's
  `ITextPara` (`GetFirstLineIndent`, `GetLeftIndent`, `GetRightIndent`,
  `SetRightIndent`, `SetIndents`, in points, and duplicates carry them),
  and `Microsoft.UI.Text.ITextParagraphFormat` (`FirstLineIndent`,
  `LeftIndent`, `RightIndent`, `SetIndents`).

`textservtest` indents a paragraph half an inch and finds it drawn and its
characters placed 48 pixels right, the next paragraph not, splits it with
Enter and undoes an indent, wraps a paragraph with a hanging indent and
with a right indent, sets and reads indents through `ITextPara` and a
duplicate and through `ITextParagraphFormat`, and indents plain text by
`SPF_SETDEFAULT`.  Left for later parts: WinUI's text command bar flyout
(`TextCommandBarFlyout`, on `CommandBarFlyout` and `CommandBar`), font
sizes and faces per run and paragraph spacing (both need lines of
different heights), tabs and lists, RTF, links and the touch keyboard's
input scope.
