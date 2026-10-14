# LibreOffice 24.8 (Writer and Calc, 64-bit) installed the way a user
# gets it: the App Store's download (its Windows Installer package, put in
# C:\Downloads) installed silently with msiexec into C:\Programs\LibreOffice.
# Writer then opens C:\Apps\data\letter.docx (a Word document the corpus
# makes), a line is typed at its start and Ctrl+S saves it as Word
# ("Use Word 2007-365!" in the Keep format question); Calc opens
# C:\Apps\data\sums.xlsx, =SUM(A1:A2) goes in A3 and it is saved as Excel
# the same way.  7-Zip then reads the saved files back: the typed line
# must be in the document's text and the formula in the sheet.  The first
# start sets up the user profile and restarts soffice.bin (exit code 81)
# under the same soffice.exe.  Windowed, takes the keyboard; the screen
# shows the document, so only the saved files decide (the screenshot is
# kept in the output).
import os, tempfile, time

DOC = 'LibreOffice (Writer and Calc open, edit and save a .docx and an .xlsx)'
MSI = r'C:\Downloads\LibreOffice_24.8.4.2_Win_x86-64.msi'
SOFFICE = r'C:\Programs\LibreOffice\program\soffice.exe'
SEVENZIP = r'C:\Programs\7-Zip\7z.exe'


def white(nova, timeout):
    """Until most of the screen's lower middle is white (a document page or
    a sheet's cells, over the dark desktop and Terminal): why it did not
    happen, or None"""
    from PIL import Image
    f = os.path.join(tempfile.gettempdir(), 'libreoffice-look.png')
    for _ in range(timeout // 10):
        nova.shot(f)
        time.sleep(1)
        im = Image.open(f).convert('RGB')
        sx, sy = im.width // 1280, im.height // 800
        light = sum(sum(im.getpixel((x * sx, y * sy))) > 690 for x in (400, 650, 900) for y in (560, 620, 680))
        if light >= 6:
            return None
        time.sleep(9)
    return 'the document did not appear'


def edit(nova, echo):
    """Writer's turn first, then Calc's: type, save in the file's own format"""
    calc = getattr(edit, 'done', False)
    edit.done = True
    w = white(nova, 900)
    if w:
        return w
    time.sleep(10)                                  # (the Tip of the Day, on a first start)
    nova.keys('esc')
    time.sleep(3)
    nova.keys('ctrl-home')
    time.sleep(2)
    if calc:
        nova.keys('down')
        time.sleep(1)
        nova.keys('down')
        time.sleep(1)
        nova.qmp.type('=SUM(A1:A2)\n')
    else:
        nova.qmp.type('Edited on NovaOS. ')
    time.sleep(5)
    nova.keys('ctrl-s')
    time.sleep(30)                                  # the Keep format question
    nova.keys('ret')                                # its default: keep Word's / Excel's format
    time.sleep(30)                                  # the file is written
    return None


APP = App('LibreOffice', '24.8.4.2',
          'https://downloadarchive.documentfoundation.org/libreoffice/old/24.8.4.2/win/x86_64/LibreOffice_24.8.4.2_Win_x86-64.msi',
          'LibreOffice', [Test('install', rf'msiexec.exe /i {MSI} /qn', timeout=1200),
                          Test('Writer', rf'start {SOFFICE} {A}\data\letter.docx', timeout=120),
                          Test('Calc', rf'start {SOFFICE} {A}\data\sums.xlsx', timeout=120),
                          Test('saved .docx', rf'{SEVENZIP} e -so {A}\data\letter.docx word/document.xml',
                               [r'Edited on NovaOS\.', r'a needle in a haystack'], timeout=120),
                          Test('saved .xlsx', rf'{SEVENZIP} e -so {A}\data\sums.xlsx xl/worksheets/sheet1.xml',
                               [r'SUM\(A1:A2\)', r'<v>42</v>'], timeout=120)],
          store='LibreOffice', gui=True, interact=edit, reference=False, close_wait=300)
