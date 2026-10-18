# svctest: a real Windows service through the service control manager,
# installed the way Steam's is (a name with spaces, an ImagePath of the
# form "path" /service): StartService with arguments, the
# StartServiceCtrlDispatcher / ServiceMain / SetServiceStatus handshake,
# controls through the handler, stop, delete; a program the service starts
# before it is running cannot connect as the service; CopyFile keeps the
# last-write time and attributes (Steam's service compares them); a
# LocalSystem service runs elevated though a limited program starts it.
DOC = '`svctest` (a real service: start with arguments, status handshake, controls, stop and delete; a LocalSystem service runs elevated; CopyFile keeps file times; 64- and 32-bit)'

TESTS = [
    Test('service x64', 'svctest', [r'svctest: \d+ passed, 0 failed'], elevated=True),
    Test('service x86', r'C:\Programs\x86\svctest.exe', [r'svctest: \d+ passed, 0 failed'], elevated=True),
]
