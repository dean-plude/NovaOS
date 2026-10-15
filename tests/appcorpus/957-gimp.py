# GIMP 2.10.38 (image editor, 64-bit, GTK 2) installed the way a user gets
# it: the App Store's download (GIMP's Inno Setup installer, put in
# C:\Downloads) run silently, as the Store's Install would run it with its
# defaults, into C:\Programs\GIMP 2.  GIMP then opens
# C:\Apps\data\picture.png (colour bars over a grey ramp, made by the
# corpus).  Its first start registers its plug-ins (each one a program it
# runs and asks what it offers) before the image window comes up with the
# picture; the screenshot must match tests/reference/gimp.png.  Alt+F4
# closes GIMP.  Windowed, takes the keyboard.  The window gets 360 s (under
# KVM the picture is up in about two minutes; the screenshot is taken when
# the wait ends, so a longer wait would only run past the job's hour).
SETUP = r'C:\Downloads\gimp-2.10.38-setup.exe'
GIMP = r'C:\Programs\GIMP 2\bin\gimp-2.10.exe'

DOC = 'GIMP (a GTK 2 program: opens a picture)'

APP = App('GIMP', '2.10.38', 'https://download.gimp.org/gimp/v2.10/windows/gimp-2.10.38-setup.exe',
          'GIMP', [Test('install', rf'{SETUP} /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /ALLUSERS', timeout=1800),
                   Test('open a picture', rf'start "{GIMP}" {A}\data\picture.png', timeout=360)],
          store='GIMP', gui=True)
