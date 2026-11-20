# xamlpagetest: a XAML page in a Window (roadmap step 32.4, first part): an app
# started by Application.Start makes a Window in OnLaunched (DesktopWindowImpl,
# its window chrome, DesktopWindowXamlSource and XAML island root, on NovaOS's
# lifted content islands in Microsoft.UI.Input.dll), sets a page loaded with
# XamlReader.Load as its content and activates it; the page is laid out and
# drawn in the window; the Border's colour is read back from the window's
# pixels, the island is sent a mouse click, the characters "Nova" and Space
# (the Button's Click fires twice, the page's CharacterReceived writes the
# characters into a TextBlock: the step's input part), the page is
# screenshotted, then the window closes and the app exits; and a TextBox
# (XAML text input part 3, on NovaOS's WinUIEdit.dll): clicked, typed into,
# Backspace, TextChanged and its text's pixels; then (part 4) Ctrl+A, Ctrl+C, Ctrl+V and Ctrl+X
# in it through the clipboard with its Copying/Paste/Cutting events, a PasswordBox typed into
# (its Password, PasswordChanged, its characters drawn, Ctrl+C copying nothing) and a
# RichEditBox typed into (its Document's text, TextChanged, Ctrl+A and Ctrl+C); then (part 5)
# Ctrl+B bolds its selection, read back from Document.Selection.CharacterFormat.Bold, and (part 7)
# Ctrl+E centres its paragraph, read back from Document.Selection.ParagraphFormat.Alignment; then (part 6)
# the TextBox's context menu (a PopupMenu): opened with the right button, Select All chosen from it,
# opened with the menu key and closed with Escape
DOC = ('`xamlpagetest` (a XAML page in a `Window`: the framework\'s desktop window, built from its sources, on the '
       'Windows App SDK\'s content islands as NovaOS\'s `Microsoft.UI.Input.dll` serves them (`ContentIsland`, '
       '`DesktopChildSiteBridge`, the island\'s input sources, `AppWindow`), showing a `Grid` with a `TextBlock` and a '
       '`Button` loaded by `XamlReader.Load`, laid out to the window\'s size and drawn through the framework\'s composition '
       'tree on `dcomp.dll`\'s compositor, its `Border`\'s colour read back from the window, clicked with the mouse '
       'and with Space and typed into, with a `TextBox` clicked, typed into ("Hi!", then Backspace), its `TextChanged` '
       'counted and its text found in its pixels, selected with Ctrl+A, copied, pasted and cut through the clipboard '
       '(its `CopyingToClipboard`, `Paste` and `CuttingToClipboard` events), a `PasswordBox` typed into (its `Password`, '
       '`PasswordChanged`, its password characters drawn, Ctrl+C copying nothing) and a `RichEditBox` typed into (its '
       '`Document` text, `TextChanged`, Ctrl+A and Ctrl+C, Ctrl+B bolding its selection as '
       '`Document.Selection.CharacterFormat.Bold` reads back, Ctrl+E centring its paragraph as '
       '`Document.Selection.ParagraphFormat.Alignment` reads back), the `TextBox`\'s context menu opened with the right '
       'button (Select All chosen from it) and with the menu key, then closed)')
TESTS = [
    Test('xamlpagetest', 'xamlpagetest', [r'xamlpagetest: \d+ passed, 0 failed \(64-bit\)'], timeout=200,
         shot=r'xamlpagetest: page on screen'),
]
