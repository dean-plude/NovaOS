# propvaltest: Windows.Foundation.PropertyValue (wintypes.dll), the Windows
# Runtime's boxes for values, through the registry's Windows Runtime class key:
# scalars, numeric conversions, IReference<T>, arrays
DOC = ('`propvaltest` (Windows.Foundation.PropertyValue: boxed values, numeric conversions, '
       'IReference<T>, arrays; 64- and 32-bit)')
TESTS = [
    Test('propvaltest', 'propvaltest', [r'propvaltest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
    Test('propvaltest x86', r'C:\Programs\x86\propvaltest.exe', [r'propvaltest: \d+ passed, 0 failed \(32-bit\)'], timeout=120),
]
