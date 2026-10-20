# dispatchqtest: the DispatcherQueue (CoreMessaging.dll), Microsoft.UI.Dispatching's
# through the registry's Windows Runtime class keys and Windows.System's through
# CreateDispatcherQueueController: a queue per thread, work by priority, timers,
# the event loop, a dedicated thread and shutdown
DOC = ('`dispatchqtest` (the DispatcherQueue: work by priority, timers, the event loop, '
       'a dedicated thread, shutdown; 64- and 32-bit)')
TESTS = [
    Test('dispatchqtest', 'dispatchqtest', [r'dispatchqtest: \d+ passed, 0 failed \(64-bit\)'], timeout=120),
    Test('dispatchqtest x86', r'C:\Programs\x86\dispatchqtest.exe', [r'dispatchqtest: \d+ passed, 0 failed \(32-bit\)'], timeout=120),
]
