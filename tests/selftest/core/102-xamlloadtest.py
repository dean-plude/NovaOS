# xamlloadtest: Microsoft's XAML resource dictionaries and object writer
# built for NovaOS (roadmap step 32.3, parts 7 to 9): XAML text read by XmlLite,
# parsed and written by the parser's object writer runtime into the core's
# objects, a live CResourceDictionary with merged and theme dictionaries,
# loaded through the core's own CCoreServices
DOC = ('`xamlloadtest` (Microsoft\'s XAML object writer and `CResourceDictionary`, built from its sources: '
       'XAML text loaded into a live resource dictionary with merged and theme dictionaries, through the core\'s own '
       '`CCoreServices`)')
TESTS = [
    Test('xamlloadtest', 'xamlloadtest', [r'xamlloadtest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
]
