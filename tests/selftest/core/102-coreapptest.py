# coreapptest: the UWP app model's core (twinapi.appcore.dll): CoreApplication
# found through the registry's Windows Runtime class keys, Run driving an
# IFrameworkView, the CoreWindow's events, its dispatcher and RunAsync, and
# in 64 bits a Direct3D 11 swap chain made for the CoreWindow
DOC = ('`coreapptest` (CoreApplication.Run, CoreWindow events and dispatcher, RunAsync, '
       'a swap chain for a CoreWindow; 64- and 32-bit)')
TESTS = [
    Test('coreapptest', 'coreapptest hello world', [r'coreapptest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
    Test('coreapptest x86', r'C:\Programs\x86\coreapptest.exe hello world', [r'coreapptest: \d+ passed, 0 failed \(32-bit\)'], timeout=120),
]
