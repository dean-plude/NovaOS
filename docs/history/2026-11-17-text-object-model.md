## The Text Object Model and Direct2D drawing for XAML's text boxes

Second part of XAML text input, item 1 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)), after the windowless Rich Edit
([first part](2026-11-17-windowless-rich-edit.md)).  XAML's
`TextBoxBase` asks the text services it hosts for `ITextDocument2`
(`QueryInterface` with the IID it reads from `WinUIEdit.dll`) and works
through it: the selection (`GetSelection2`) for select all, cut, copy,
paste and the caret's rectangle, ranges (`Range2`, `RangeFromPoint2`)
for the text, the paragraph under the caret and the lines in view,
`Undo` and `Redo`, and `Undo(tomSuspend)`/`Undo(tomResume)` around its
own changes.  It draws the text with `TxDrawD2D` on its Direct2D render
target.  Both answered `E_NOTIMPL`.

- **The Text Object Model** (`userland/textserv/tom.c`; the interfaces in
  C in `userland/include/novatextserv.h`): `ITextDocument2` (also as
  `ITextDocument` and `IDispatch`) on the text services, and
  `IRichEditOle` from `EM_GETOLEINTERFACE`, whose `QueryInterface` gives
  the same document.  Ranges (`ITextRange2`) and the selection
  (`ITextSelection2`, which moves the text services' own selection, so
  the host gets `EN_SELCHANGE` and the caret moves) take text in and out
  (`GetText`, `GetText2` with `tomAllowFinalEOP` and `tomUseCRLF`,
  `SetText`, `SetChar`, `TypeText`), move by characters, words, lines,
  paragraphs and the story (`Move`, `MoveStart`, `MoveEnd`, `Expand`,
  `StartOf`, `EndOf`, `GetIndex`, `SetIndex`, `Delete`, the selection's
  `MoveLeft`/`Right`/`Up`/`Down`, `HomeKey` and `EndKey`), find text,
  change case, cut, copy and paste, and give and take points
  (`GetPoint` with the `TA_*` alignments, `SetPoint`, `GetRect`,
  `RangeFromPoint`).  As in Rich Edit the story ends in a paragraph mark
  ranges may take in but no edit removes.  Ranges follow every edit,
  however it is made, and a range kept after the text services are gone
  answers `CO_E_RELEASED`.  The document also has `Freeze`/`Unfreeze`,
  the saved flag, the text limit check, the client rectangle and the
  input context.  Character and paragraph format objects (`ITextFont2`,
  `ITextPara2`), tables, math and embedded objects answer `E_NOTIMPL`.
- **`TxDrawD2D`** draws what `TxDraw` draws into a 32-bit bitmap and puts
  it on the render target with `DrawBitmap`.  A host that leaves the
  background alone (`TXTBACK_TRANSPARENT`, as XAML's text boxes) gets a
  premultiplied bitmap: the text is drawn on black in its colours and
  again all in white, whose brightness is each pixel's coverage.

The core suite's `textservtest` (64- and 32-bit) now also drives the
Text Object Model the way `TextBoxBase` and `TextBoxView` do and draws
with `TxDrawD2D` on a Direct2D DC render target, opaque and transparent,
checking the pixels.  Next: XAML's `TextBox` on a page (its
`TextBoxBase`, `TextBoxView` and `TextServicesHost` sources), with the
format objects it needs.
