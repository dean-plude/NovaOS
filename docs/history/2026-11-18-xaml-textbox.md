## A typable XAML TextBox on a page

Third part of XAML text input, item 1 of 0.2.3 "Nebula"
([plan](releases/0.2.3.md)), after the windowless Rich Edit
([first part](2026-11-17-windowless-rich-edit.md)) and the Text Object
Model ([second part](2026-11-17-text-object-model.md)).  A `TextBox` in
a XAML page now loads NovaOS's `WinUIEdit.dll`, shows in its default
style, takes the focus from a click, takes typing and Backspace, raises
`TextChanged` and draws its text.

- **XAML's text box controls built** from microsoft-ui-xaml 866ba81
  (MIT): the core's `TextBoxBase`, `TextBox`, `TextBoxView` (the view
  that draws through `TxDrawD2D`), `TextServicesHost` (its
  `ITextHost2`, with `IProvideFontInfo` for the fonts), its Direct2D
  render target, the context menu's OLE callback and the private input
  settings (`core/native/text/Controls`), the text core's caret helpers
  and selection settings, and the framework's `TextBox`, `TextBoxView`,
  their event args and the text control flyout helper.  The closed
  headers they include are NovaOS's in `userland/windows_ui_xaml/inc`:
  `WinUIEdit.h` (WinUIEdit's own messages, with NovaOS's values, and the
  pointer input message `EM_MOUSEINPUT` with its `RE_MOUSEINPUT`), the
  second versions of the text services, host and TOM interfaces
  (`textserv.h`, `tom.h`), `fontinfo.h`, `xamltexthost.h`,
  `TextInput.h`, `ctffunc.h`, and `richedit.h` with Rich Edit 8's
  `GETCONTEXTMENUEX`.  `d2d1.h` declares Direct2D's methods that return
  a struct as Windows does, so XAML's render target implements them.
- **Default styles:** the resource split
  (`userland/microsoft_ui_xaml_resources_19h1`) dropped every style that
  named a resource it leaves out; a style now keeps itself and loses only
  those setters, so `TextBox`, `PasswordBox` and `RichEditBox` get their
  templates (without the text command bar flyouts, which are WinUI's).
- **`WinUIEdit.dll`** (`userland/textserv`): draws XAML's text with
  DirectWrite glyph runs on XAML's render target (`dwtext.c`: the font
  face from the host's `IProvideFontInfo`, as XAML ignores a character
  format's face name; XAML's render target has no bitmaps), measures with
  the same face, answers `EM_MOUSEINPUT` as the mouse message it carries
  and `EM_SETDPI`, gives `EN_CHANGE` a `CHANGENOTIFY`, takes a host's
  `INFINITE` text limit as no limit, and leaves a plain-text control's
  final paragraph mark out of `GetText2(tomAllowFinalEOP)` (a
  `TextBox`'s `Text` has none).  Character and paragraph format objects
  (`ITextFont2`, `ITextPara2`, `tomfmt.c`) answer what `TextBoxBase`
  asks (right-to-left reading order, the cookie); the rest is
  `E_NOTIMPL`.

`xamlpagetest` now puts a `TextBox` on its page, clicks it (focused),
types "Hi!" (its `Text`; the page's `CharacterReceived` sees the
characters too, as XAML leaves them unhandled), presses Backspace
("Hi"), counts `TextChanged` (four) and finds the text's pixels in the
box.  `textservtest` checks the new `EN_CHANGE` payload, the unlimited
host and the final mark.  Next: `PasswordBox` and `RichEditBox`, and
selection, cut and paste in a CI check.
