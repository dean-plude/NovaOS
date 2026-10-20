# xamlloadtest: Microsoft's XAML resource dictionaries and object writer
# built for NovaOS (roadmap step 32.3, parts 7 to 12): XAML text read by XmlLite,
# parsed and written by the parser's object writer runtime into the core's
# objects, a live CResourceDictionary with merged and theme dictionaries,
# loaded through the core's own CCoreServices, made by CCoreServices::Create
# on the platform layer's services, with the framework's XamlReader factory
# and the control layer (CXcpDispatcher on a DispatcherQueue, CJupiterControl::Create
# up to the rendering seam)
DOC = ('`xamlloadtest` (Microsoft\'s XAML object writer and `CResourceDictionary`, built from its sources: '
       'XAML text loaded into a live resource dictionary with merged and theme dictionaries, through the core\'s own '
       '`CCoreServices`, made by `CCoreServices::Create` on the platform layer\'s services, the framework\'s `XamlReader` factory, and the control layer: `CXcpDispatcher` on a `DispatcherQueue` and `CJupiterControl::Create` up to the rendering seam; and the framework\'s dispatcher and default styles)')
TESTS = [
    Test('xamlloadtest', 'xamlloadtest', [r'xamlloadtest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
]
