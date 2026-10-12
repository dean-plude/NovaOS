# Squash the Creeps (MIT; the finished game of Godot's own "Your first 3D
# game" tutorial, from godotengine/godot-demo-projects) on Godot 4.7 (MIT,
# the official 64-bit Windows build) drawing with Direct3D 12: Godot's
# Forward+ renderer on its D3D12 driver (--rendering-driver d3d12), which
# NovaOS's d3d12.dll hands to VKD3D-Proton on Mesa 3D's Vulkan (lavapipe),
# presenting through DXVK's dxgi, so the App Store's Mesa 3D, DXVK and
# VKD3D-Proton are installed first, as a user without a GPU driver would.
# Godot turns the game's shaders into DXIL itself (no dxcompiler.dll).
# The demo comes as the project's source, so Godot imports its assets
# first (--headless --import, no window); then the game starts in a window
# and, once Godot has built its pipelines behind its boot splash, draws the
# field (its light ground fills the window) and plays its first seconds
# (creeps walk across it): the screenshot is kept but not compared (the
# creeps are random); lavapipe refuses two of Forward+'s compute
# pipelines, which the game draws without (see docs/compatibility).  Godot's
# log (--log-file) must then name the Direct3D 12 driver, so a fall back
# to Vulkan (rendering/rendering_device/fallback_to_vulkan) fails the
# test.  The game's music (an autoloaded player) must be heard: NovaOS
# boots with a sound card, which Godot's WASAPI driver opens through
# IAudioClient3 (without one it finds no endpoint and plays nothing, as on
# Windows).  Takes the keyboard.
import os
import shutil
import zipfile

DIR = r'C:\Apps\Squash the Creeps'
GODOT = 'Godot_v4.7-stable_win64'
LOG = DIR + r'\godot.log'

DOC = 'Squash the Creeps (Godot 4.7 drawing with Direct3D 12 through VKD3D-Proton)'


def unpack(app, files, dest):
    """Godot's two executables and the game's project folder (game/)"""
    os.makedirs(dest)
    with zipfile.ZipFile(files[0]) as z:
        z.extractall(dest)
    with zipfile.ZipFile(files[1]) as z:
        z.extractall(dest)
    shutil.move(os.path.join(dest, 'squash_the_creeps'), os.path.join(dest, 'game'))


def drawn(nova, echo):
    """Godot shows its dark boot splash while it builds the game's
    pipelines (minutes on llvmpipe without KVM), then the game: its light
    field fills the window (the centre's mean brightness is about 50 on the
    splash, 180-220 while playing, and about 100 once a creep has caught
    the player and the field is dimmed under "Press Space or Enter to
    retry", which with KVM can happen before the first look).  Waits up to
    20 minutes for it"""
    import time
    from PIL import Image, ImageStat
    f = os.path.join(nova.work, 'creeps.png')
    for _ in range(120):
        time.sleep(10)
        nova.shot(f)
        g = Image.open(f).convert('L')
        w, h = g.size
        if ImageStat.Stat(g.crop((w // 4, h // 4, 3 * w // 4, 3 * h // 4))).mean[0] > 80:
            time.sleep(10)                          # (creeps walk on)
            return None
    return 'the game did not draw its field in 20 minutes (still on the boot splash)'


APP = App('Squash the Creeps', '4.7',
          'https://github.com/godotengine/godot/releases/download/4.7-stable/Godot_v4.7-stable_win64.exe.zip',
          'Squash the Creeps',
          [Test('install Mesa 3D', 'store install Mesa 3D', store='Mesa 3D', timeout=1200),
           Test('install DXVK', 'store install DXVK', store='DXVK', timeout=600),
           Test('install VKD3D-Proton', 'store install VKD3D-Proton', store='VKD3D-Proton', timeout=600),
           Test('its folder', rf'cd "{DIR}"', builtin=True),
           Test('import its assets', f'{GODOT}_console.exe --headless --path game --import', timeout=1800),
           Test('plays on Direct3D 12', f'start {GODOT}.exe --path game --rendering-driver d3d12 '
                                        f'--log-file "{LOG}"', timeout=60),
           Test('Godot used Direct3D 12', f'type "{LOG}"', expect=[r'D3D12|Direct3D 12'], builtin=True),
           Test('back to Documents', r'cd C:\Documents', builtin=True)],
          unpack=unpack, gui=True, runtimes=['Mesa 3D', 'DXVK', 'VKD3D-Proton'], interact=drawn,
          extra=['https://github.com/godotengine/godot-demo-projects/releases/download/4.7-6ad6167/3d_squash_the_creeps.zip'],
          sound=(None, 5000), reference=False, close_wait=600)
