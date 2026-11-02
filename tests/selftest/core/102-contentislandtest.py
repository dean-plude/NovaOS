# contentislandtest: the Windows App SDK's lifted content islands, input and
# windowing classes (Microsoft.UI.Input.dll, roadmap 32.4): a
# DesktopChildSiteBridge and a ContentIsland sized together, the island's
# pointer, key and focus sources raising events from the bridge window's
# messages, AppWindow's Changed for a move, and a CoreWindowSiteBridge
# filling a (stand-in) CoreWindow and following its size, and that island's
# input site and input sources (what XAML's InputSiteAdapter asks for)
DOC = ('`contentislandtest` (the lifted content islands a XAML Window sits on: a site bridge and an '
       'island sized together, the island\'s input events, AppWindow, the bridge a UWP app\'s CoreWindow shows its island through and that '
       'island\'s input site)')
TESTS = [
    Test('contentislandtest', 'contentislandtest', [r'contentislandtest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
]
