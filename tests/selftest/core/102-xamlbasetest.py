# xamlbasetest: the first of Microsoft's XAML framework built for NovaOS
# (roadmap step 32.3): the XAML core's lowest layer (strings, numbers,
# containers, resource qualifiers, runtime-enabled features, the dependency
# locator) from Microsoft's sources, and NovaOS's WRL they are written on
DOC = ('`xamlbasetest` (Microsoft\'s XAML core, lowest layer, built from its sources, and NovaOS\'s WRL: '
       'ComPtr, RuntimeClass, weak references, Callback, EventSource, HString)')
TESTS = [
    Test('xamlbasetest', 'xamlbasetest', [r'xamlbasetest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
]
