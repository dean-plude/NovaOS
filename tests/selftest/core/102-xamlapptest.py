# xamlapptest: Microsoft's XAML framework started as a desktop app starts it
# (roadmap step 32.3, the end of it): Application.Start built from dxaml/lib
# takes the desktop path, the app's initialization callback composes its
# Application over the framework's, WindowsXamlManager starts it on the thread
# and raises OnLaunched, which loads a page with XamlReader.Load; work queued on
# the DispatcherQueue runs from the message loop and Application.Exit returns
# from Application.Start; there the framework's theme resources and control
# styles (Microsoft.UI.Xaml.Resources.19H1.dll, roadmap step 32.4) are found and
# the core's resource manager is made
DOC = ('`xamlapptest` (Microsoft\'s XAML framework started as a desktop app starts it, built from its sources: '
       '`Application.Start` on the desktop path with a `DispatcherQueue`, the app\'s initialization callback composing its '
       '`Application` over the framework\'s, `WindowsXamlManager` starting it on the thread and raising `OnLaunched`, '
       '`XamlReader.Load` of a `Grid` holding a `TextBlock` there, the framework\'s theme resources and control styles '
       'found in `Microsoft.UI.Xaml.Resources.19H1.dll` and the core\'s resource manager made, queued work run from the desktop message loop, and '
       '`Application.Exit` returning from `Application.Start`)')
TESTS = [
    Test('xamlapptest', 'xamlapptest', [r'xamlapptest: \d+ passed, 0 failed \(64-bit\)'], timeout=150),
]
