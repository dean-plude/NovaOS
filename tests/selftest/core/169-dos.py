# The DOS host (roadmap step 31.2): ntvdm.exe boots MS-DOS 4.00, built from
# Microsoft's MIT-licensed source, on its real-mode CPU and BIOS in the
# Terminal's console window; dostest types VER and DIR at COMMAND.COM's
# prompt, reads the answers off the screen and leaves with F12.
DOC = ('`dostest` (MS-DOS 4.00 boots in a console window on ntvdm.exe\'s real-mode CPU and BIOS: '
       'COMMAND.COM\'s prompt, VER and DIR, F12 gives the screen back)')
TESTS = [
    Test('MS-DOS in a console window', 'dostest', [r'dostest: \d+ passed, 0 failed'], timeout=600),
]
