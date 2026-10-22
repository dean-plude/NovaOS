# contentislandtest: the Windows App SDK's lifted content islands, input and
# windowing classes (Microsoft.UI.Input.dll, roadmap 32.4): a
# DesktopChildSiteBridge and a ContentIsland sized together, the island's
# pointer, key and focus sources raising events from the bridge window's
# messages, and AppWindow's Changed for a move
DOC = ('`contentislandtest` (the lifted content islands a XAML Window sits on: a site bridge and an '
       'island sized together, the island\'s input events, AppWindow)')
TESTS = [
    Test('contentislandtest', 'contentislandtest', [r'contentislandtest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
]
