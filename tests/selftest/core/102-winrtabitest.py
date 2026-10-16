# winrtabitest: the Windows Runtime C++ ABI headers NovaOS generates without
# MIDL (tools/winrt/abigen.py): published IIDs (metadata, generic instances,
# the XAML framework's IDL) and the Compositor driven only through the
# generated interfaces, so their method order matches dcomp.dll's
DOC = ('`winrtabitest` (the generated Windows Runtime ABI headers: IIDs, and the Compositor '
       'driven through them; 64- and 32-bit)')
TESTS = [
    Test('winrtabitest', 'winrtabitest', [r'winrtabitest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
    Test('winrtabitest x86', r'C:\Programs\x86\winrtabitest.exe', [r'winrtabitest: \d+ passed, 0 failed \(32-bit\)'], timeout=120),
]
