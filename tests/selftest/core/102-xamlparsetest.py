# xamlparsetest: Microsoft's XAML markup parser built for NovaOS (roadmap
# step 32.3, part 6): XAML text through XmlLite (NovaOS's xmllite.dll, on
# libxml2's reader) into XAML's node stream, and the resources layer's keys,
# maps and theme-walk cache
DOC = ('`xamlparsetest` (Microsoft\'s XAML parser, built from its sources: XAML text to XAML\'s node stream '
       'through NovaOS\'s xmllite.dll, markup extensions, resource keys and maps)')
TESTS = [
    Test('xamlparsetest', 'xamlparsetest', [r'xamlparsetest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
]
