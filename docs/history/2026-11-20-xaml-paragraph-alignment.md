## XAML text paragraph alignment

Seventh part of XAML text input, item 1 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)), after the text boxes' context menu
([sixth part](2026-11-20-xaml-text-context-menu.md)).  Text in a XAML
text box can be centred, right-aligned or justified: a `TextBox`'s
`TextAlignment` now shows, and a `RichEditBox` aligns each paragraph on
its own, from Ctrl+L, E, R and J or from its `Document`'s
`ParagraphFormat`.

- **Paragraph formats in the windowless Rich Edit**
  (`userland/textserv/runs.c`): a paragraph's format is its mark's, as in
  Rich Edit, so the `'\r'` ending it carries the alignment in its
  character format; Enter keeps it on both sides of the split, and two
  paragraphs joined take the later one's.  The final paragraph's is kept
  apart, plain text has one for every paragraph, and both fall back to the
  default the host gives (`TxGetParaFormat` when it signals
  `TXTBIT_PARAFORMATCHANGE`, or `EM_SETPARAFORMAT` with `SPF_SETDEFAULT`,
  which is how XAML passes on `TextAlignment`).  Undo restores an
  alignment change.
- **Drawing and the caret** (`textserv.c`, `dwtext.c`): layout moves each
  line of a centred or right-aligned paragraph right in the width it is
  drawn in; the caret, mouse hits, Up and Down, GDI's drawing and
  DirectWrite's all follow.  Justified paragraphs are drawn left-aligned
  for now.
- **Every way in**: `EM_SETPARAFORMAT` and `EM_GETPARAFORMAT`
  (`PFM_ALIGNMENT`, undefined over paragraphs that differ), the alignment
  keys in rich text, the Text Object Model's `ITextPara` (now live over its
  range: `GetAlignment`, `SetAlignment`, `GetDuplicate`, `SetDuplicate`,
  `IsEqual`, and `ITextRange::SetPara`), and
  `Microsoft.UI.Text.ITextParagraphFormat` on a Windows Runtime range
  (`winrtfmt.c`: `Alignment`, `GetClone`, `SetClone`, `IsEqual`; indents,
  spacing, lists and tabs read 0 and setting them does nothing).

`textservtest` centres a paragraph and finds it drawn centred with the
caret following, splits it with Enter, right-aligns and left-aligns the
last paragraph with Ctrl+R and Ctrl+L and undoes one, aligns through
`ITextPara` and its duplicate and through `ITextParagraphFormat`, and
aligns plain text by `SPF_SETDEFAULT` and the host's `TxGetParaFormat`.
`xamlpagetest` presses Ctrl+E in its `RichEditBox` and reads
`Document.Selection.ParagraphFormat.Alignment` back as `Center`.  Left
for later parts: WinUI's text command bar flyout (`TextCommandBarFlyout`,
on `CommandBarFlyout` and `CommandBar`), font sizes and faces per run,
indents and spacing, RTF, links and the touch keyboard's input scope.
