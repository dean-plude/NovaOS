# The DOS host (roadmap steps 31.2 and 31.3): ntvdm.exe boots MS-DOS 4.00,
# built from Microsoft's MIT-licensed source, on its real-mode CPU and BIOS
# in the Terminal's console window; dostest types VER and DIR at
# COMMAND.COM's prompt, reads the answers off the screen and leaves with
# F12.  Then a DOS .COM runs from cmd by name and a DOS .EXE from
# CreateProcess, each reading and writing a file on drive C: and ending
# with its exit code.
DOC = ('`dostest` (MS-DOS 4.00 boots in a console window on ntvdm.exe\'s real-mode CPU and BIOS: '
       'COMMAND.COM\'s prompt, VER and DIR, F12 gives the screen back; a DOS .COM run from cmd by name '
       'and a DOS .EXE from CreateProcess read and write files on drive C: and return their exit code)')
TESTS = [
    Test('MS-DOS in a console window', 'dostest', [r'dostest: \d+ passed, 0 failed'], timeout=600),
]
