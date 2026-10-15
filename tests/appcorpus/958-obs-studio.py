# OBS Studio 30.2.3 (screen recording and live streaming, 64-bit, Qt 6)
# installed the way a user gets it: the App Store's download (the official
# zip, put in C:\Downloads) unpacked by the Store into C:\Programs\obs-studio.
# OBS draws with Direct3D 11, which NovaOS hands to DXVK on Mesa 3D's
# Vulkan (llvmpipe), so the App Store's Mesa 3D and DXVK are installed
# first, as a user without a GPU driver would.  OBS finds its data folder
# from the folder it runs in (../../data), so the Terminal goes there first,
# as the Store and the Start menu start it.  Its first start opens the
# Auto-Configuration Wizard over the main window; Esc cancels it.  The
# Scenes dock's + button then asks for a new scene's name: Enter takes the
# one it offers, "Scene 2", which goes under the default "Scene", and the
# main window (preview, docks, controls) must match
# tests/reference/obs studio.png.  (A name typed into that box is lost on
# NovaOS: docs/compatibility/440-obs.md.)  Alt+F4 closes OBS, which saves
# its scene collection: the file must hold the new scene.
# Nothing is captured, recorded or streamed (no network in the test).
# Windowed; it takes a few minutes to load its plugins without KVM.
import time

DOC = 'OBS Studio (Direct3D 11 through DXVK: adds a scene and saves it)'
OBS = r'C:\Programs\obs-studio\bin\64bit'
SCENES = r'C:\AppData\Roaming\obs-studio\basic\scenes\Untitled.json'
ADD_SCENE = (18, 710)       # the Scenes dock's + button (logical screen points)


def add_scene(nova, echo):
    """Esc on the Auto-Configuration Wizard, then + in the Scenes dock and
    Enter on the name it offers"""
    time.sleep(240)                     # the main window and the wizard over it
    nova.keys('esc')
    time.sleep(40)
    nova.click(*ADD_SCENE)
    time.sleep(40)                      # the Add Scene dialog
    nova.keys('ret')
    time.sleep(40)
    return None


APP = App('OBS Studio', '30.2.3',
          'https://github.com/obsproject/obs-studio/releases/download/30.2.3/OBS-Studio-30.2.3-Windows.zip',
          'obs-studio', [Test('install Mesa 3D', 'store install Mesa 3D', store='Mesa 3D', timeout=1200),
                         Test('install DXVK', 'store install DXVK', store='DXVK', timeout=600),
                         Test('install from the App Store', 'store install OBS Studio', store='OBS Studio', timeout=1200),
                         Test('its folder', rf'cd {OBS}', builtin=True),
                         Test('add a scene', rf'start {OBS}\obs64.exe', timeout=400),
                         Test('saved scene', f'type {SCENES}', [r'"scene_order":\[\{"name":"Scene"\},\{"name":"Scene 2"\}\]'], builtin=True)],
          store='OBS Studio', gui=True, runtimes=['Mesa 3D', 'DXVK'], interact=add_scene)
