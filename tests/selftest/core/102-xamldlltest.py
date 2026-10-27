# xamldlltest: the system Windows.UI.Xaml.dll (roadmap step 32.6, 0.2.2
# "Pulsar" item 1): a program with nothing of the XAML framework in it and
# no classes registered by it gets Windows.UI.Xaml.Application from
# RoGetActivationFactory (the registry's ActivatableClassId key loads
# Windows.UI.Xaml.dll), starts it with Application.Start, makes its
# Application in the callback, shows a page loaded by
# Windows.UI.Xaml.Markup.XamlReader in a Windows.UI.Xaml.Window, checks it
# is laid out, is screenshotted, then closes the window and exits
DOC = ('`xamldlltest` (the system `Windows.UI.Xaml.dll`: `Windows.UI.Xaml.Application`, `XamlReader` and `Window` '
       'from `RoGetActivationFactory` alone, through the registry\'s `ActivatableClassId` keys, `Application.Start`, '
       'a page laid out in a window, then closed)')
TESTS = [
    Test('xamldlltest', 'xamldlltest', [r'xamldlltest: \d+ passed, 0 failed \(64-bit\)'], timeout=200,
         shot=r'xamldlltest: page on screen'),
]
