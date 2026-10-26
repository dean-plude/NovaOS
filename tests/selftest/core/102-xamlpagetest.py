# xamlpagetest: a XAML page in a Window (roadmap step 32.4, first part): an app
# started by Application.Start makes a Window in OnLaunched (DesktopWindowImpl,
# its window chrome, DesktopWindowXamlSource and XAML island root, on NovaOS's
# lifted content islands in Microsoft.UI.Input.dll), sets a page loaded with
# XamlReader.Load as its content and activates it; the page is laid out and
# drawn in the window; the Border's colour is read back from the window's
# pixels, the island is sent a mouse click, the characters "Nova" and Space
# (the Button's Click fires twice, the page's CharacterReceived writes the
# characters into a TextBlock: the step's input part), the page is
# screenshotted, then the window closes and the app exits
DOC = ('`xamlpagetest` (a XAML page in a `Window`: the framework\'s desktop window, built from its sources, on the '
       'Windows App SDK\'s content islands as NovaOS\'s `Microsoft.UI.Input.dll` serves them (`ContentIsland`, '
       '`DesktopChildSiteBridge`, the island\'s input sources, `AppWindow`), showing a `Grid` with a `TextBlock` and a '
       '`Button` loaded by `XamlReader.Load`, laid out to the window\'s size and drawn through the framework\'s composition '
       'tree on `dcomp.dll`\'s compositor, its `Border`\'s colour read back from the window, clicked with the mouse '
       'and with Space and typed into, then closed)')
TESTS = [
    Test('xamlpagetest', 'xamlpagetest', [r'xamlpagetest: \d+ passed, 0 failed \(64-bit\)'], timeout=200,
         shot=r'xamlpagetest: page on screen'),
]
