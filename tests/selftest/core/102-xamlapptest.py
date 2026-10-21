# xamlapptest: Microsoft's XAML framework started as a desktop app starts it
# (roadmap step 32.3, the end of it): Application.Start built from dxaml/lib
# takes the desktop path, the app's initialization callback composes its
# Application over the framework's, WindowsXamlManager starts it on the thread
# and raises OnLaunched, which loads a page with XamlReader.Load; work queued on
# the DispatcherQueue runs from the message loop and Application.Exit returns
# from Application.Start
DOC = ('`xamlapptest` (Microsoft\'s XAML framework started as a desktop app starts it, built from its sources: '
       '`Application.Start` on the desktop path with a `DispatcherQueue`, the app\'s initialization callback composing its '
       '`Application` over the framework\'s, `WindowsXamlManager` starting it on the thread and raising `OnLaunched`, '
       '`XamlReader.Load` of a `Grid` holding a `TextBlock` there, queued work run from the desktop message loop, and '
       '`Application.Exit` returning from `Application.Start`)')
TESTS = [
    Test('xamlapptest', 'xamlapptest', [r'xamlapptest: \d+ passed, 0 failed \(64-bit\)'], timeout=150),
]
