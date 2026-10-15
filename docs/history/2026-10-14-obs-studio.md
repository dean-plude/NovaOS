## OBS Studio adds a scene

Roadmap step 27.1 (grow the corpus), its fifth app: OBS Studio 30.2.3,
the screen recording and streaming program the App Store lists, a 64-bit
Qt 6 program that draws with Direct3D 11.  The nightly corpus now installs
the Store's Mesa 3D and DXVK, as a user without a GPU driver would, then
OBS from the Store (the official zip), and starts it from its own folder,
as the Store does (OBS finds its data from there).  Esc cancels the
Auto-Configuration Wizard of its first start, the Scenes dock's + button
adds a scene under the name OBS offers ("Scene 2"), and the main window
must match
`tests/reference/obs studio.png`; once OBS is closed its scene collection
must hold the new scene (`tests/appcorpus/958-obs-studio.py`).  Nothing is
captured, recorded or streamed.  It needed these NovaOS fixes:

- **The Store's download link.**  The catalog named
  `OBS-Studio-30.2.3-Windows-x64.zip`, which the release does not have
  (404); the download is `OBS-Studio-30.2.3-Windows.zip`, and the program
  is `obs-studio\bin\64bit\obs64.exe` in it.
- **`msvcp140.dll`'s version.**  OBS reads the version of the Visual C++
  runtime's `msvcp140.dll` before anything else and quits (exit code 1)
  when it is older than 14.40 or has none; NovaOS's had no version
  resource.  It now carries 14.42.34433, the runtime its STL sources come
  from (`stltest` checks it).
- **`wldap32.dll` by ordinal.**  OBS's `libcurl.dll`, which `obs64.exe`
  imports, imports eight LDAP functions by number, so OBS could not load.
  `wldap32.dll` now exports its functions on Windows' ordinals.
- **Missing imports.**  `gdi32`'s `D3DKMTSetProcessSchedulingPriorityClass`
  (and `Get...`), which `libobs-d3d11.dll` imports to raise its GPU
  priority; the UCRT's `_mbsnbcpy`, `_mbsnbcmp` (libcurl) and
  `__stdio_common_vfwscanf` (LuaJIT, for scripts); `setupapi`'s
  `CM_Get_Device_ID_Size` and `SetupDiDeleteDeviceInfo` (the AJA and
  DirectShow capture plug-ins); `iphlpapi`'s `GetBestRoute` (the RTMP
  output).

Still open: a name typed into the Add Scene box is lost.  The dialog
gets Enter (so it takes the offered name), but not the letters: Qt's
line edit never has the keyboard.  A trace of user32 shows the dialog
shown, activated and given the focus (`WM_ACTIVATE`, `WM_SETFOCUS`) in
Windows' order, so the cause is somewhere else; the test does not type.

Without DXVK OBS stops with "Failed to initialize video": NovaOS has no
Direct3D 11 device of its own (`d3d11.dll` hands hardware devices to
DXVK).  Under QEMU without KVM OBS shows its main window about four
minutes after it starts.
