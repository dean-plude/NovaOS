# apiinfotest: Windows.Foundation.Metadata.ApiInformation (ole32), which API
# contracts and types this Windows has (Godot 4 asks before it uses them)
DOC = ('`apiinfotest` (Windows.Foundation.Metadata.ApiInformation: API contracts, '
       'types, members; 64- and 32-bit)')
TESTS = [
    Test('apiinfotest', 'apiinfotest', [r'apiinfotest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
    Test('apiinfotest x86', r'C:\Programs\x86\apiinfotest.exe', [r'apiinfotest: \d+ passed, 0 failed \(32-bit\)'], timeout=120),
]
