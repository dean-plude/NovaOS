# WinSCP 6.3.5 (SFTP, FTP and SCP client; 32-bit, Delphi VCL) installed
# the way a user gets it: the App Store's download (its Inno Setup
# installer, put in C:\Downloads) run silently for all users, as the
# Store's Install would run it with its defaults, into C:\Programs\WinSCP.
# WinSCP then opens its Login dialog (the session's protocol, host, port
# and user fields) over its main window, whose two panels open on local
# folders (its drive tree starts from the shell's desktop folder); the screenshot must
# match tests/reference/winscp.png.  Alt+F4 closes the Login dialog, a
# second one WinSCP.  Windowed, takes the keyboard.
SETUP = r'C:\Downloads\WinSCP-6.3.5-Setup.exe'
PROGRAM = r'C:\Programs\WinSCP'

DOC = 'WinSCP (a Delphi VCL program: its Login dialog)'

APP = App('WinSCP', '6.3.5',
          'https://sourceforge.net/projects/winscp/files/WinSCP/6.3.5/WinSCP-6.3.5-Setup.exe/download',
          'WinSCP', [Test('install', rf'{SETUP} /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /ALLUSERS', timeout=600),
                     Test('its folder', rf'cd "{PROGRAM}"', builtin=True),
                     Test('Login dialog', 'start WinSCP.exe', timeout=120),
                     Test('back to Documents', r'cd C:\Documents', builtin=True)],
          store='WinSCP', gui=True, closes=2)
