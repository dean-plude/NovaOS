# SoundCraft, ArtCraft's open-source audio editor (Rust, egui on wgpu), installed
# from the App Store as PhotoCraft is (886-photocraft.py).  Its Windows build
# draws only with Direct3D 12, so the App Store's Mesa 3D, DXVK and
# VKD3D-Proton are installed first, as a user without a GPU driver would:
# NovaOS's d3d12.dll hands the device to VKD3D-Proton on lavapipe, and wgpu
# compiles its shaders with NovaOS's d3dcompiler_47 (shader model 5.1, its
# sampler heap).  Its window must show its user interface (not stay black,
# as it did while egui's vertex shader was miscompiled: hlsltest's const
# copies) within ten minutes without KVM; the screenshot is kept but not
# compared (the window starts beside the Terminal, whose text shows
# through differently from run to run).  Windowed.
import os
import time

DOC = 'SoundCraft'
EXE = r'C:\Programs\SoundCraft\soundcraft-0.5.0-windows-x64-portable\soundcraft.exe'


def drawn(nova, echo):
    """The window (below its title bar, above the taskbar) stops being one flat
    colour (black) within ten minutes"""
    from PIL import Image, ImageStat
    f = os.path.join(nova.work, 'soundcraft-drawn.png')
    for _ in range(60):
        time.sleep(10)
        nova.shot(f)
        g = Image.open(f).convert('L')
        w, h = g.size
        if ImageStat.Stat(g.crop((w // 6, h // 8, w, 7 * h // 8))).stddev[0] > 12:
            return None
    return 'its window stayed one flat colour for ten minutes (nothing drawn)'


APP = App('SoundCraft', '0.5.0',
          'https://github.com/storytold/soundcraft/releases/download/v0.5.0/soundcraft-0.5.0-windows-x64-portable.zip',
          'SoundCraft',
          [Test('install Mesa 3D', 'store install Mesa 3D', store='Mesa 3D', timeout=1200),
           Test('install DXVK', 'store install DXVK', store='DXVK', timeout=600),
           Test('install VKD3D-Proton', 'store install VKD3D-Proton', store='VKD3D-Proton', timeout=600),
           Test('install from the App Store', 'store install SoundCraft', store='SoundCraft', timeout=900),
           Test('main window', rf'start "{EXE}"', timeout=120)],
          gui=True, store='SoundCraft', runtimes=['Mesa 3D', 'DXVK', 'VKD3D-Proton'], interact=drawn, reference=False)
