# wrtpooltest: the Windows.System classes a UWP app's start-up asks for
# (Windows Calculator's; 0.2.2 "Pulsar" item 2): MemoryManager's usage,
# limit, level, events and reports, ThreadPool's work items behind
# IAsyncActions (completed, High priority, failing, cancelled while they
# run) and ThreadPoolTimer's one-shot, periodic and cancelled timers, all
# from twinapi.appcore.dll through the registry's ActivatableClassId keys
DOC = ('`wrtpooltest` (`Windows.System.MemoryManager` and `Windows.System.Threading`\'s `ThreadPool` and '
       '`ThreadPoolTimer` from `RoGetActivationFactory`: memory use and reports, work items as `IAsyncAction`s, '
       'one-shot, periodic and cancelled timers; 64- and 32-bit)')
TESTS = [
    Test('wrtpooltest', 'wrtpooltest', [r'wrtpooltest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
    Test('wrtpooltest x86', r'C:\Programs\x86\wrtpooltest.exe', [r'wrtpooltest: \d+ passed, 0 failed \(32-bit\)'], timeout=120),
]
