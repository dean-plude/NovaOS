# Program compatibility
<!-- The regions between "BEGIN generated" and "END generated" markers are built from fragment files by tools/docgen.py: edit those files, not the regions (CONTRIBUTING.md). -->

Which Windows programs and games run on NovaOS, how well, and how that
was checked: NovaOS's answer to Wine's AppDB and ProtonDB.

- Every program is an unmodified release from its publisher.  The one
  exception is PuTTY, which the corpus builds from PuTTY's source release
  with MinGW because PuTTY's download site is not reachable from every
  network.  When a program fails, NovaOS is what gets fixed, never the
  program.
- Everything was checked in QEMU.  Nothing has been run on a real PC yet
  ([hardware.md](hardware.md)).
- The version in the "Program" column is the one that was checked.  The
  App Store may offer a different version.
- The App Store shows each rating on a badge for the programs the
  nightly corpus tests with the Store's own download, with whether they
  passed the latest nightly run
  ([app-store-catalog.md](app-store-catalog.md#nightly-badges)).

## Ratings

Each program gets one rating, the way ProtonDB rates games:

| Rating | Meaning |
|---|---|
| **Platinum** | Works out of the box.  The "What works" column covers what most people use it for, and nothing known is broken. |
| **Gold** | Works as well as Platinum after a step the row names, such as installing "Mesa 3D" and "DXVK" from the App Store first. |
| **Silver** | Works, with a minor known problem (named in "Problems and gaps"). |
| **Bronze** | Starts and does some of its job, but a major part fails or has not been checked, so it is not yet usable for what it is for. |
| **Borked** | Does not start, or stops before it is usable. |
| **Untested** | In the App Store, but nobody has checked it yet.  Expect it to fail. |

The other columns:

- **Problems and gaps**: what is known not to work, and what has not been
  checked.  A rating counts only what was checked; an unchecked feature
  lowers it only when it is the program's main use.
- **Checked by**:
  - *nightly corpus*: a script in `tests/appcorpus/` (linked) that
    downloads the program and runs it in QEMU, one program per virtual
    machine, checking its output or a screenshot.  `tools/appcorpus.py`
    runs them in the "Nightly app corpus" workflow, which posts each
    night's pass/fail table on issue
    [#210](https://github.com/dean-plude/os/issues/210).
  - *CI graphics tests* and *core self-tests*: run on every pull request.
  - *by hand*: someone ran it in QEMU.  It is not re-checked
    automatically, so it can break unnoticed.

## Updating a rating

Each row is a file in `docs/compatibility/` with one `field: value` line
per column: `program`, `kind`, `rating`, `works`, `problems` and
`checked`.  `kind` is `program`, `game`, `tool` or `runtime`.  Change a
program's file when its rating changes; `tools/docgen.py` builds the
tables below from these files and refuses an unknown rating or kind
(CONTRIBUTING.md).  Ratings and the App Store's compatibility notes are
kept up to date by hand.  Having CI set them from the nightly results is
planned (roadmap item 27.5).

## Ratings by program

<!-- BEGIN generated:compat-table -->

| Rating | Programs | Games and game stores | Command-line and developer tools | Runtimes, graphics drivers and installers | All |
|---|---:|---:|---:|---:|---:|
| Platinum | 7 | 6 | 9 | 6 | 28 |
| Gold | 0 | 3 | 0 | 1 | 4 |
| Silver | 5 | 1 | 0 | 1 | 7 |
| Bronze | 17 | 2 | 1 | 1 | 21 |
| Borked | 0 | 1 | 0 | 0 | 1 |
| Untested | 2 | 0 | 1 | 3 | 6 |
| **All** | 31 | 13 | 11 | 12 | 67 |

### Programs

| Program | Rating | What works | Problems and gaps | Checked by |
|---|---|---|---|---|
| **7-Zip 26.03** (x64) | Platinum | Installs from its own setup; the file manager, the add and extract dialogs, dragging files out of archives; `7z.exe` packs and tests archives | — | nightly corpus ([`040-7zip.py`](../tests/appcorpus/040-7zip.py)) |
| **Notepad++ 8.8.3** (x64 portable) | Platinum | Opens, edits and saves files through the Open and Save As dialogs | — | nightly corpus ([`900-notepad++.py`](../tests/appcorpus/900-notepad++.py)) |
| **WinMerge 2.16.50** | Platinum | Compares two files side by side | Folder compare and plugins not checked | nightly corpus ([`860-winmerge.py`](../tests/appcorpus/860-winmerge.py)) |
| **VLC 3.0.21** (Qt 5) | Platinum | Plays an H.264 and AAC MP4 with sound in its window and closes cleanly | Other formats and streaming not checked | nightly corpus ([`870-vlc.py`](../tests/appcorpus/870-vlc.py)) |
| **Audacity 3.7.4** (wxWidgets) | Platinum | Records from the microphone without dropouts, draws the waveform and saves an `.aup3` project | Effects and export not checked | nightly corpus ([`880-audacity.py`](../tests/appcorpus/880-audacity.py)) |
| **Inkscape 0.91** (GTK 2) | Platinum | Opens, edits and saves an SVG | Inkscape 1.x not checked (its downloads are out of the test network's reach) | nightly corpus ([`880-inkscape.py`](../tests/appcorpus/880-inkscape.py)) |
| **Firefox 157** and **Floorp 12.19** | Platinum | Built into NovaOS as its default browser and also in the App Store; installs from the App Store, loads pages over HTTP and HTTPS, scrolls and takes typing in forms | A publicly trusted HTTPS site not checked (the test network is offline) | nightly corpus (Firefox, [`890-firefox.py`](../tests/appcorpus/890-firefox.py)), by hand in QEMU (Floorp) |
| **SumatraPDF 3.4.6** (x86 portable) | Silver | Opens a PDF and renders its pages | Printing finds no printer (NovaOS has no printers yet) | nightly corpus ([`850-sumatrapdf.py`](../tests/appcorpus/850-sumatrapdf.py)) |
| **KeePassXC 2.7.12** (Qt 5) | Silver | Opens and unlocks a KDBX 4 database and shows its entries | Windows Hello unlock reports "not supported" | nightly corpus ([`870-keepassxc.py`](../tests/appcorpus/870-keepassxc.py)) |
| **Krita 5.3.4** (App Store; Qt 5, portable zip) | Silver | Installs from the App Store (the Store unpacks its zip), opens a new image on the built-in OpenGL (Mesa's llvmpipe; nothing else to install), paints a stroke with the default brush and saves it as a `.kra` through its Save dialog; the saved file holds the document, its layers and the merged image ([history](history/2026-10-14-krita-saves.md)) | Under QEMU without KVM it takes about ten minutes from start to a saved image; exporting to other formats (PNG, JPEG) and opening a saved file again not checked | nightly corpus ([`885-krita.py`](../tests/appcorpus/885-krita.py)) |
| **PhotoCraft 0.7.0** (ArtCraft; Rust, egui on wgpu) | Silver | Installs from the App Store, opens its editor window on the built-in OpenGL (llvmpipe) and makes a new document | On software graphics (llvmpipe) it composites on the CPU by its own design and says so in a box at start; its GPU compositor ran on NovaOS's OpenGL in a test, but no hardware adapter has been tried. Editing not checked | nightly corpus ([`886-photocraft.py`](../tests/appcorpus/886-photocraft.py)) |
| **LibreOffice 24.8** (App Store; 64-bit, Windows Installer package) | Silver | Installs silently from the App Store's download (`msiexec /qn`, its DLL custom actions run out of process); Writer opens a `.docx`, takes typing and saves it as Word, and Calc opens an `.xlsx`, takes a formula and saves it as Excel; the saved files keep the edits ([history](history/2026-10-14-libreoffice.md)) | No Insert > OLE Object dialog (oledlg answers Cancel); no font subsetting for PDF export (whole fonts are embedded); Skia falls back from Vulkan to its CPU renderer; Impress, Draw and Base not checked | nightly corpus ([`953-libreoffice.py`](../tests/appcorpus/953-libreoffice.py)) |
| **PuTTY 0.81** (built from the source release with MinGW) | Bronze | Raw connections | SSH not checked | nightly corpus ([`950-putty.py`](../tests/appcorpus/950-putty.py)) |
| **CADCraft 0.5.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens an empty drawing (Drafting tools, view cube, Layers and Properties), about two and a half minutes after start under QEMU without KVM | Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own | nightly corpus ([`891-cadcraft.py`](../tests/appcorpus/891-cadcraft.py)) |
| **DeckCraft 0.5.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens its new presentation page (eight themes), about two and a half minutes after start under QEMU without KVM | Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own | nightly corpus ([`891-deckcraft.py`](../tests/appcorpus/891-deckcraft.py)) |
| **DesignCraft 0.6.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens its welcome page (new document sizes, tools and Properties panel), about two and a half minutes after start under QEMU without KVM | Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own | nightly corpus ([`891-designcraft.py`](../tests/appcorpus/891-designcraft.py)) |
| **EffectCraft 0.8.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store and opens full screen on its welcome page with an untitled project, drawn with the built-in OpenGL (llvmpipe) | Compositions and rendering not checked; the desktop's title bar sits above the app's own (it draws its own buttons too) | nightly corpus ([`888-effectcraft.py`](../tests/appcorpus/888-effectcraft.py)) |
| **FilmCraft 0.6.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens its demo project (timeline, sunset preview), about two and a half minutes after start under QEMU without KVM | Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own | nightly corpus ([`891-filmcraft.py`](../tests/appcorpus/891-filmcraft.py)) |
| **GridCraft 0.5.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens an empty workbook under its ribbon, about two and a half minutes after start under QEMU without KVM | Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own | nightly corpus ([`891-gridcraft.py`](../tests/appcorpus/891-gridcraft.py)) |
| **LightCraft 0.6.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens its photo library with a sample photo, histogram and Light sliders, about two and a half minutes after start under QEMU without KVM | Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own; all 27 of its GPU preview's compute shaders compile (RWByteAddressBuffer's Load, GetDimensions and Interlocked methods since 2026-10-13; the denoise network's convolution, which stores to indexed groupshared arrays, since 2026-10-14; checked outside the app, the denoise itself not yet run in it) | nightly corpus ([`891-lightcraft.py`](../tests/appcorpus/891-lightcraft.py)) |
| **PdfCraft 0.6.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store and opens on its welcome page and tool list, drawn with the built-in OpenGL (llvmpipe) | Opening and editing PDFs not checked; its window starts wider than a 1280x800 screen | nightly corpus ([`889-pdfcraft.py`](../tests/appcorpus/889-pdfcraft.py)) |
| **SoundCraft 0.5.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store; with "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store it gets a Direct3D 12 device (VKD3D-Proton on lavapipe) and opens an empty session (tracks, timeline, transport clock), about two and a half minutes after start under QEMU without KVM | Only the first window checked; its window starts wider than a 1280x800 screen; without the three App Store runtimes it exits at start, as on Windows it asks only for Direct3D 12 at first and NovaOS has no Direct3D 12 adapter of its own | nightly corpus ([`891-soundcraft.py`](../tests/appcorpus/891-soundcraft.py)) |
| **VectorCraft 0.9.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store and opens full screen on its welcome page, drawn with the built-in OpenGL (llvmpipe) | Drawing and saving not checked; the desktop's title bar sits above the app's own (it draws its own buttons too) | nightly corpus ([`887-vectorcraft.py`](../tests/appcorpus/887-vectorcraft.py)) |
| **WordCraft 0.5.0** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store and opens a blank document under its ribbon, drawn with the built-in OpenGL (llvmpipe) | Typing and saving not checked; its window starts wider than a 1280x800 screen | nightly corpus ([`889-wordcraft.py`](../tests/appcorpus/889-wordcraft.py)) |
| **Thunderbird 158** (App Store; 64-bit, Mozilla's Gecko) | Bronze | Installs from the App Store (Mozilla's full installer, unpacked) and opens an e-mail message (`.eml`) in its own window: sender, recipient, date, subject and text ([history](history/2026-10-14-thunderbird.md)) | No mail account, sending or receiving checked (no mail server in the test); its main window stays empty without an account | nightly corpus ([`954-thunderbird.py`](../tests/appcorpus/954-thunderbird.py)) |
| **GIMP 2.10.38** (App Store; 64-bit, GTK 2, Inno Setup installer) | Bronze | Installs from the App Store (its Inno Setup installer, silently) and opens a PNG: on the first start it registers its plug-ins (each one a program it starts and talks to over pipes), then shows the picture in its single-window layout ([history](history/2026-10-14-gimp.md)) | The TWAIN plug-in (scanning; a 32-bit program) does not start; one Python plug-in (Spyrogimp) crashed once while registering (a fault in `GetProcAddress` called from `libintl`, not reproduced); no printing, editing or saving checked yet; the first start takes minutes without KVM | nightly corpus ([`957-gimp.py`](../tests/appcorpus/957-gimp.py)) |
| **OBS Studio 30.2.3** (App Store; 64-bit, Qt 6, Direct3D 11) | Bronze | Installs from the App Store (the official zip) and, with the Store's Mesa 3D and DXVK installed first, opens its main window drawn with Direct3D 11 through DXVK on llvmpipe; cancels the Auto-Configuration Wizard, adds a scene from the Scenes dock (the name it offers) and saves it in its scene collection ([history](history/2026-10-14-obs-studio.md)) | A name typed into the Add Scene box is lost (the box never takes the keyboard, while Enter still reaches the dialog; user32 activates and focuses the dialog as Windows does, so the cause is still open); without DXVK it cannot start ("Failed to initialize video"): NovaOS has no Direct3D 11 device of its own; nothing captured, recorded or streamed in the test; Windows Graphics Capture is not available (`libobs-winrt.dll` needs `CoreMessaging.dll`, so OBS keeps to its BitBlt capture) and the browser source is not checked | nightly corpus ([`958-obs-studio.py`](../tests/appcorpus/958-obs-studio.py)) |
| **qBittorrent 5.0.2** (App Store; 64-bit, Qt 6, NSIS installer) | Bronze | Installs from the App Store (its NSIS installer, silently, which elevates itself through UAC) and opens a `.torrent`: the Legal Notice on first start, then the Add torrent dialog over the main window with the torrent's name, size, date, info hash and file list ([history](history/2026-10-14-qbittorrent.md)) | Nothing downloaded or seeded (the test has no peers or tracker); its crash handler's stack trace has no symbols (NovaOS has no debugger engine behind `dbgeng.dll`) | nightly corpus ([`956-qbittorrent.py`](../tests/appcorpus/956-qbittorrent.py)) |
| **WinSCP 6.3.5** (App Store; 32-bit, C++Builder VCL, Inno Setup installer) | Bronze | Installs from the App Store (its Inno Setup installer, silently, registering its 64-bit shell extension through `regsvr32`) and opens its Login dialog over its main window, whose two panels open on local folders ([history](history/2026-10-14-winscp.md)) | The Login dialog's site tree and the file panels list their items but draw none of them, and the port number box is blank; no connection or transfer checked | nightly corpus ([`952-winscp.py`](../tests/appcorpus/952-winscp.py)) |
| **HandBrake** (App Store) | Untested | — | Not checked yet; needs the .NET Desktop Runtime 8, whose desktop programs are not checked either | — |
| **ShareX** (App Store) | Untested | — | Not checked yet; needs the .NET Desktop Runtime 8, whose desktop programs are not checked either | — |

### Games and game stores

| Program | Rating | What works | Problems and gaps | Checked by |
|---|---|---|---|---|
| **OpenTTD 15.3** (App Store; free on GOG; the corpus installs OpenTTD's own Windows installer, as GOG's copy needs an account to download) | Platinum | Its setup program installs it silently (run as administrator, as its manifest asks); with the OpenGFX graphics it reaches its main menu, and its animated title game plays | Drawn in software (no hardware acceleration yet) | nightly corpus ([`910-gog-openttd.py`](../tests/appcorpus/910-gog-openttd.py)) |
| **Chocolate Doom 3.1.0** (32-bit, SDL2) with **Freedoom** | Platinum | Plays in a window on the built-in OpenGL (Mesa's llvmpipe) in the right colours; an Xbox or HID game pad walks, turns and fires | — | by hand in QEMU; graphics self-test `gltest colors` |
| **Beneath a Steel Sky** on **ScummVM 2026.3** (App Store; free on GOG, which ships it with ScummVM; the corpus takes the freeware floppy release Revolution and ScummVM publish, as GOG's copy needs an account to download) | Platinum | ScummVM's setup program (Inno Setup, 32-bit, installing the 64-bit ScummVM) installs it silently or through its wizard; the game starts, Esc skips the intro and a click walks Robert Foster along the first scene's gantry. ScummVM draws with OpenGL 1.1, NovaOS's own when no OpenGL driver is installed | Sound not checked (the corpus runs without a sound card) | nightly corpus ([`920-gog-beneath-a-steel-sky.py`](../tests/appcorpus/920-gog-beneath-a-steel-sky.py)) |
| **LBreakout2 2.6.5** (App Store; free and open source, GPL; 64-bit, SDL 1.2) | Platinum | Installs from the App Store (the official zip) and plays in a 640x480 window, drawn with GDI (SDL 1.2's `windib` driver: a 16-bit DIB section blitted with `BitBlt`). Its full screen ('f' anywhere) switches the display to 640x480 with `ChangeDisplaySettings` and takes the frame off its window (`WS_POPUP`); 'f' again, or ending the game, puts the display and the frame back | — | nightly corpus ([`940-lbreakout2.py`](../tests/appcorpus/940-lbreakout2.py)) |
| **Cave Story 1.0.0.6** (App Store; freeware, Aeon Genesis' English translation; 32-bit, DirectDraw) | Platinum | Installs from the App Store (the translation's zip) and plays in full screen: the game asks DirectDraw for 640x480 at 16 bits, and NovaOS's `ddraw.dll` (cnc-ddraw) switches the display to 640x480 and draws its surfaces with Direct3D 9 (with DXVK installed), OpenGL or GDI. Keyboard (Z, X, arrows), a game pad (an Xbox 360 controller through DirectInput 7: B jumps and says OK, the stick walks) and its text (GDI on DirectDraw surfaces) work; the display goes back to its own mode when the game ends. Its DoConfig.exe (32-bit) sets windowed modes and keys | — | nightly corpus ([`945-cave-story.py`](../tests/appcorpus/945-cave-story.py), [`946-cave-story-pad.py`](../tests/appcorpus/946-cave-story-pad.py)) |
| **SuperTux 0.1.3** (App Store; GPL; 32-bit, SDL 1.2, Inno Setup 4 installer) | Platinum | Installs from the App Store (its Inno Setup installer, silently or through its wizard) and plays in full screen at 640x480 with the keyboard or a game pad: SDL 1.2 reads the pad through winmm's joystick functions (`joyGetPosEx`), which NovaOS answers from its game controllers (Xbox 360 and other XInput pads, HID pads) | Sound not checked (the test machine has no sound card) | nightly corpus ([`947-supertux.py`](../tests/appcorpus/947-supertux.py)) |
| **OpenTyrian 2.1.20260913** (App Store; Tyrian 2.1, freeware since 2004, on the free and open-source OpenTyrian engine; SDL2) | Gold | Installs from the App Store (the official 64-bit zip, which carries the freeware game data) and draws with Direct3D 9, SDL's first choice on Windows, through DXVK on Mesa 3D's Vulkan (install "Mesa 3D" and "DXVK" first). Its demo plays in a window, Alt+Enter switches it to full screen (the Direct3D 9 device is reset at the display's size), its music plays through the sound card, and the keyboard takes it through its menus into a new game. Full screen is SDL's desktop full screen, so the display mode does not change | Needs "Mesa 3D" and "DXVK" from the App Store first. Full screen is SDL's desktop full screen, so the display mode does not change | nightly corpus ([`930-opentyrian.py`](../tests/appcorpus/930-opentyrian.py)) |
| **Blobby Volley 2 1.1.1** (App Store; free and open source, GPL; 32-bit, SDL2) | Gold | Installs from the App Store (the official zip) and plays in an 800x600 window. Its Fullscreen option is a real display-mode switch: SDL changes the display to 800x600 (`ChangeDisplaySettingsEx`) and draws with Direct3D 9 in exclusive full screen through DXVK on Mesa 3D's Vulkan (install "Mesa 3D" and "DXVK" first; without them it draws with SDL's software renderer); the display goes back to its own mode when the game ends | Full screen needs "Mesa 3D" and "DXVK" from the App Store first (without them it draws with SDL's software renderer) | nightly corpus ([`935-blobby-volley.py`](../tests/appcorpus/935-blobby-volley.py)) |
| **Squash the Creeps** on **Godot 4.7** (MIT; the official 64-bit Windows build running the game from Godot's demo projects) | Gold | Godot imports the game's assets (`--headless --import`) and plays it with its default Forward+ renderer on Direct3D 12 (`--rendering-driver d3d12`, through VKD3D-Proton on lavapipe from the App Store's Mesa 3D 26.2.0), every pipeline it asks for built, its SDFGI compute shaders included (VKD3D-Proton 3.0.1 as NovaOS builds it); its music plays through WASAPI (Godot's `IAudioClient3` path) | Needs "Mesa 3D", "DXVK" and "VKD3D-Proton" from the App Store first | nightly corpus ([`955-squash-the-creeps.py`](../tests/appcorpus/955-squash-the-creeps.py)) |
| **Teeworlds 0.7.5** (App Store; free and open source, SDL2) | Silver | Installs from the App Store and starts in full screen on the built-in OpenGL (Mesa's llvmpipe; it needs OpenGL 1.2), plays its menu music through the sound card and goes through its first-start questions to its start menu with the keyboard, and its menus follow the mouse with its default settings (SDL reads the pointer's moves and recentres it with `SetCursorPos`; with `inp_grab 1` it reads Raw Input instead); joins a game on its own server and stays in it | Joining a game times out under QEMU without KVM, while Mesa compiles its shaders; internet servers not checked (the test network is offline) | nightly corpus ([`925-teeworlds.py`](../tests/appcorpus/925-teeworlds.py), [`926-teeworlds-join.py`](../tests/appcorpus/926-teeworlds-join.py)) |
| **GOG GALAXY 2.1** (offline installer, Inno Setup 6) | Bronze | Its setup program runs elevated, through its wizard or silently (`/VERYSILENT`), installs the Visual C++ runtimes it carries (x86 and x64), copies GOG GALAXY's files and makes its shortcuts; the client starts (`GalaxyClient.exe`, 64-bit, Qt 6 WebEngine: every import resolves, 89 modules, and the Windows Runtime `UISettings` its Qt plugin reads the theme from), installs and starts its service (`GalaxyClientService.exe`, which trusts the client and answers its requests over 127.0.0.1:9978), opens its window and warns that drive C: is not NTFS; past that warning it finishes initialising, opens its sign-in window and starts its first Chromium renderer (`QtWebEngineProcess.exe`), which needs Mesa's OpenGL (built in; Qt then draws with it; without it Qt ends the client: "Could not get handle for shared context") and DXVK (Chromium's Direct3D 11, through ANGLE); with both, Chromium shares its Direct3D 11 textures and fences with Qt (on lavapipe NovaOS's Vulkan loader provides the sharing), ANGLE compiles its shaders with NovaOS's HLSL compiler, and the sign-in window loads GOG's sign-in page (`login.gog.com`) over HTTPS, its certificate checked against the Windows root stores as Chromium reads them; the page draws, then shows "Oops! Something went wrong" with a Log in button.  The client's own requests through Qt's network (its configuration, metrics and problem reports, over Schannel's TLS 1.2) get their answers; past the drive warning the client keeps running (Qt's `sscanf_s` calls get their buffer sizes honoured) and its sign-in window waits on its Log in button, and pressing it loads GOG's sign-in form again (email, password, Google, Steam, Discord and Xbox); its service runs elevated, as `LocalSystem` services do, and writes the client's Vulkan registry keys | Under QEMU without KVM the sign-in page does not always finish loading in time and shows the "Oops" page again; signing in with an account is not yet checked. Setup warns that drive C: is not NTFS (answer Yes) | by hand |
| **Steam** (App Store, the current client) | Bronze | Its installer runs and installs the bootstrapper; on first start Steam downloads its client over HTTPS, updates itself to the 64-bit client, verifies it and starts it with `SteamService` (which runs as a real service) and its browser, `steamwebhelper.exe` (Chromium); the browser starts its GPU, network, storage and page processes and Steam accepts its UI connection; the "Sign in to Steam" window opens and draws its form (account name, password, Remember me, Sign in) ([Steam](#steam)). Signing in and games are unchecked | Signing in and games not checked | nightly corpus (install and update, [`092-steam.py`](../tests/appcorpus/092-steam.py)) |
| **Roblox** (App Store, the current client) | Borked | Its installer runs: it fetches Roblox's settings and packages over HTTPS and installs the client; its Hyperion anti-cheat passes the system-call, alignment-fixup, 32-bit-code and thread-context checks ([Roblox and anti-cheat](#roblox-and-anti-cheat)) | Hyperion then reports "Virtual Machine detected" under QEMU, as Roblox does in any virtual machine, so no game starts; untested on a real PC | nightly corpus (the installer, [`090-roblox.py`](../tests/appcorpus/090-roblox.py)) |

### Command-line and developer tools

| Program | Rating | What works | Problems and gaps | Checked by |
|---|---|---|---|---|
| **MinGit 2.51** (Git for Windows; 2.47 by hand) | Platinum | `clone`, `commit`, `log`, `diff`, `merge`, `fetch` and `push` between local repositories, paged by `less` | — | nightly corpus (`clone`, `log` and `status` on 2.51, [`050-mingit.py`](../tests/appcorpus/050-mingit.py)), by hand in QEMU (the rest, on 2.47) |
| **ripgrep 14.1, fd 10.2, jq 1.7** | Platinum | Searching, walking folders and filtering JSON from the Terminal | — | nightly corpus ([`010-ripgrep.py`](../tests/appcorpus/010-ripgrep.py), [`020-fd.py`](../tests/appcorpus/020-fd.py), [`030-jq.py`](../tests/appcorpus/030-jq.py)) |
| **bat, fzf** | Platinum | Printing files with highlighting, fuzzy finding | — | by hand in QEMU |
| **Python 3.14** (NuGet package) and **NumPy 2.5.3** | Platinum | Scripts with hashlib, JSON, regular expressions, threads and subprocesses; NumPy gives the same results as on Linux | — | nightly corpus ([`060-python.py`](../tests/appcorpus/060-python.py)), by hand (NumPy) |
| **Node.js 24** (`.msi` and `.zip`) | Platinum | `node`, `npm`, crypto, files and timers; the `.msi` installs and uninstalls | — | nightly corpus ([`070-nodejs.py`](../tests/appcorpus/070-nodejs.py)) |
| **.NET 10** | Platinum | Console programs and the C# compiler; German and Japanese dates and numbers format as on Windows | — | nightly corpus ([`075-dotnet.py`](../tests/appcorpus/075-dotnet.py)) |
| **Eclipse Temurin 21** (`.msi` JRE, `.zip` JDK) | Platinum | `java`, `javac`, threads, exceptions and files | — | by hand in QEMU |
| **ffmpeg 7.1.1** | Platinum | Converts an H.264 MP4 to WebM | Only conversion to WebM is checked | nightly corpus ([`080-ffmpeg.py`](../tests/appcorpus/080-ffmpeg.py)) |
| **Neovim 0.10 and 0.11**, **MSYS2 `sh`** (from MinGit) | Platinum | Neovim opens, edits and saves a file; interactive `sh` sessions with pipes, subshells and globbing | — | by hand in QEMU |
| **ArtCraft Launcher 0.2.1** (ArtCraft; Rust, egui on wgpu) | Bronze | Installs from the App Store and opens its window on the built-in OpenGL (llvmpipe), listing the twelve ArtCraft apps | Installing and updating apps needs the internet, which the test machine lacks (it shows them as offline); closing the window keeps it running in the tray, its default; the desktop's title bar sits above the app's own (it draws its own buttons too) | by hand in QEMU (2026-10-12) |
| **Microsoft Build of OpenJDK 21** (App Store) | Untested | — | Not checked yet; Eclipse Temurin 21 works | — |

### Runtimes, graphics drivers and installers

| Program | Rating | What works | Problems and gaps | Checked by |
|---|---|---|---|---|
| **Windows Installer (`.msi`) and NSIS setup programs** | Platinum | 7-Zip, CMake, Node.js, Temurin and KeePassXC packages install, make shortcuts and uninstall, custom actions included; 32-bit packages put their system files in `SysWOW64`; bootstrappers such as WiX Burn query products, patches and source lists; NSIS installers and their uninstallers run | — | core self-tests (`msitest`, `msiscript`, `msiqtest`), by hand |
| **Mesa 3D** (OpenGL 24.2.4 built in; Vulkan 26.2.0 from the App Store, Runtimes) | Platinum | OpenGL 4.5 on the CPU (llvmpipe, on drive C: from the start) and Vulkan (lavapipe), 64- and 32-bit | CPU only (llvmpipe, lavapipe) unless Venus is installed | core self-test `gl2test`, CI graphics tests (`gltest`) |
| **DXVK 2.5.3** (App Store, Runtimes) | Platinum | Direct3D 8 to 11 on Vulkan, 64- and 32-bit; ANGLE's Direct3D 11 back end (Chromium's GPU process: Steam's browser, WebView2, Qt WebEngine) starts on it | Needs Mesa 3D's Vulkan (App Store) first | CI graphics tests (`d3dtest`, `d3dtest angle`, `d3dtest present`: frames shown straight from the host's GPU on Venus) |
| **Venus** (App Store, Runtimes) | Platinum | Vulkan, and Direct3D through DXVK, on the host GPU when QEMU gives NovaOS a 3D virtio-gpu | Only in QEMU with a 3D virtio-gpu; real GPUs need their own driver | CI graphics tests |
| **cnc-ddraw** (built in as NovaOS's `ddraw.dll`; also in the App Store, Runtimes) | Platinum | DirectDraw games in full screen or a window, drawing with Direct3D 9 (with DXVK installed), OpenGL or GDI; display-mode switches and back | — | nightly corpus ([`945-cave-story.py`](../tests/appcorpus/945-cave-story.py)) |
| **Visual C++ Redistributable** (App Store) | Platinum | NovaOS has its own `vcruntime140` and `msvcp140`, so most programs do not need it, but GOG GALAXY needs its MFC.  Its installer (WiX Burn) installs the Minimum and Additional Runtimes, the 32-bit one into `SysWOW64` and the 64-bit one into `System32` (14.44 x86 and 14.51 x64 checked), replacing NovaOS's own copies there | — | by hand |
| **VKD3D-Proton 3.0.1** (App Store, Runtimes; built by `tools/build_vkd3d_proton.py` with a newer dxil-spirv) | Gold | Direct3D 12 on Vulkan (install "Mesa 3D" and "DXVK" first): devices, root signatures, pipelines, drawing, read-back and fences, 64- and 32-bit programs, both presenting through DXVK's swap chains; Godot 4.7's Forward+ renderer plays Squash the Creeps on it with every pipeline built, and a compute pipeline from Godot's SDFGI shader builds (`d3d12test compute`) | — | CI graphics tests (`d3d12test`), nightly corpus ([`955-squash-the-creeps.py`](../tests/appcorpus/955-squash-the-creeps.py)) |
| **Microsoft Edge WebView2 runtime** (the evergreen offline installer) | Silver | Its installer, Microsoft Edge Update, installs itself, accepts Microsoft's signature on the runtime's package and starts the runtime's own setup, which installs the runtime; a WebView2 host starts the runtime's browser process (`msedgewebview2.exe`) and its GPU, network and storage processes and gets a WebView2 environment; the host puts the browser's window inside its own, a page loads, a script runs in it and the page is drawn (Chromium's software compositor, through the Basic Render Driver, a WARP device and DirectComposition), also with DXVK installed and Venus the GPU, where the GPU process keeps ANGLE's Direct3D 11 device on the GPU and composites the page through DirectComposition on it ([WebView2](#webview2)) | Programs that host it (Roblox's and Steam's sign-in) not checked end to end | nightly corpus (the installer, `wv2host`, [`095-webview2.py`](../tests/appcorpus/095-webview2.py)) |
| **.NET Desktop Runtime 8** (App Store) | Bronze | Console programs run | WinForms and WPF programs not checked | by hand |
| **Wine Mono 9.4** (App Store, Runtimes) | Untested | — | Not checked yet (it is for: The .NET Framework 2.0 to 4.8 programs it is for) | — |
| **DirectX End-User Runtimes (June 2010)** (App Store, Runtimes) | Untested | — | Not checked yet (it is for: Its D3DX, XInput and XAudio2 libraries for older games) | — |
| **OpenAL Soft 1.23.1** (App Store, Runtimes) | Untested | — | Not checked yet (it is for: Sound for games that use OpenAL) | — |

<!-- END generated:compat-table -->

## Roblox and anti-cheat

Roblox's installer works on NovaOS (it needed the firmware tables
`GetSystemFirmwareTable` reads and Windows' answer for a registry key
below a missing one).  The client, `RobloxPlayerBeta.exe`, starts with
Hyperion, Roblox's anti-cheat (`RobloxPlayerBeta.dll`), which decrypts the
game at run time and makes its own `syscall` instructions instead of
calling ntdll.  It does not read ntdll's stubs for the service numbers;
it works them out the way the kernel assigns them, by ranking ntdll's `Zw`
exports by address.  So ntdll now has a stub for every Windows 10 1903
service, laid out in service-number order (one per line in
`userland/ntdll/ntdll.c`, from `nt1903_services.h`), the way Windows'
ntdll is; the services that used to be ntdll C code are now system calls
the kernel answers (`kernel/um/um_services.c`), and the kernel reads a
system-call number the way Windows does, from the low bits of `EAX`,
ignoring the noise Hyperion leaves in the top bits.

With that, every raw system call Hyperion makes lands on the service it
meant, and it gets past the point where it used to crash on service 0: it
initialises, reads the kernel's loaded-module list
(`NtQuerySystemInformation(SystemModuleInformation)`), its own token
(`NtQueryInformationToken`) and ntdll's version resource.

Hyperion then checks a run-time behaviour of the kernel: it turns on
alignment-fault fixup and runs a deliberately misaligned `MOVDQA` in a
page it has just allocated, expecting the move to go through rather than
the process to die.  Windows' kernel, when fixup is on, rewrites a
misaligned 16-byte SSE move to its unaligned form in the program's code
and retries it; NovaOS now does the same
([the history note](history/2026-10-04-alignment-fault-fixup.md),
`kernel/um/um_gpfault.c`), along with the smaller things the same code
path checks: `ThreadHideFromDebugger` validating its length and ntdll's
extended-context (XSAVE) functions.  Past that, Hyperion runs further into
its start-up — hundreds more system calls, reading registry keys and the
system's time-zone information.

Next it runs 32-bit code inside the 64-bit client: it maps memory just
below 4 GiB, puts a `CPUID` in its last two bytes and far-jumps there
through selector 0x23, Windows' 32-bit user code segment, so the
instruction pointer wraps to 0 and faults, and its handler looks at what
the fault says.  NovaOS used its own selector numbers, where 0x23 was the
64-bit code segment, so the "32-bit" code ran as 64-bit code and the
client crashed.  NovaOS now has Windows' x64 segment layout (0x23 32-bit
code, 0x2B data, 0x33 64-bit code, 0x53 the 32-bit TEB;
[the history note](history/2026-10-04-windows-segment-layout.md)), a fault
in such code reaches the program's handlers with `SegCs` 0x23 and the
handler resumes in 64-bit code.  Hyperion then reads its own registers
with `NtGetContextThread` on itself, which NovaOS now answers as Windows
does (`SetThreadContext` on the calling thread works too, and
`__fastfail` ends a program the Windows way).

Past those checks Hyperion reads the firmware's SMBIOS table and the
display devices and stops with **"Virtual Machine detected. Roblox can't
be used in a Virtual Machine or Virtual Desktop."**  That is the right
answer under QEMU, where NovaOS's tests run: Roblox refuses virtual
machines on Windows too, and NovaOS reports the machine it runs on
truthfully.  The next Roblox steps need a real PC (the reference
ThinkPad T14, [hardware.md](hardware.md)); on one, this check should
pass, but it is untested (roadmap item 24.5).  NovaOS does not, and will not, work around anti-cheat checks,
hide the virtual machine or change Roblox itself.  `syscalltest` checks the
table the way Hyperion reads it, `aligntest` the alignment-fault fixup and
`gatetest` the segment layout, the 32-bit code, the calling thread's
context and `__fastfail`.

## WebView2

Roblox's login page, and many other programs, show web content with
Microsoft Edge WebView2, a runtime Windows installs once for every
program.  Its installer is Microsoft Edge Update, a 32-bit program that
installs itself, registers its update tasks with the Task Scheduler and
its COM servers, and then runs the runtime's own setup.  On NovaOS, Edge
Update now starts and reaches its install step (NovaOS gained the
functions it calls: Task Scheduler 2.0, the Data Protection API,
`UrlCombine`, the package-name functions, the MDM enrolment check and
others), reads its manifests with MSXML 6 and installs itself.  Its
`/regserver` step registers its proxy/stub DLL (`psmachine.dll`) through
rpcrt4's NDR engine, and COM calls between processes work (ole32's
standard marshaler over named pipes, `LocalServer32` servers started on
demand, oleaut32's `IDispatch` proxy and `BSTR`/`VARIANT` marshaling).
Its silent install unpacks the runtime's package and checks that
Microsoft signed it: `WinVerifyTrust`, then crypt32's Microsoft root
chain policy (`CERT_CHAIN_POLICY_MICROSOFT_ROOT`, with the application
root flag for Microsoft's 2011 root), which NovaOS now answers as
Windows does, so the package is accepted and cached.  Edge Update's own
background update pass (`/ua`), which runs alongside the install, finds
the install worker by listing Edge Update's processes and reading each
one's image path (`GetProcessImageFileName`), owner and command line
(from its PEB); NovaOS now answers those questions about another
process, so the pass leaves the install alone, and the install starts
the runtime's own setup (`MicrosoftEdgeWebview_X64_*.exe --msedgewebview
--user-level`, Chromium's `mini_installer` with `setup.exe`).  That setup
unpacks its 728 MB archive (`MSEDGE.7z`) through a file mapping, which
NovaOS used to refuse over 256 MB, reports to Windows Error Reporting
through `wer.dll` (NovaOS answers as a machine with reporting turned
off) and checks that it runs on a desktop (`RtlGetDeviceFamilyInfoEnum`).
It then copies the runtime into
`C:\AppData\Local\Microsoft\EdgeWebView\Application` and gives the
runtime's sandboxed processes read access to that folder: it reads the
folder's permissions, adds one entry and sets them back.  Drive C: used
to have no permissions list anywhere (as on FAT), so that one entry
became the folder's whole list and shut the user out of it ("Access is
denied" on `SetupMetrics`, and the install rolled back).  C:'s root now
has the permissions Windows gives `C:\` (and that a new NTFS volume's
root already had), which every folder inherits, so the added entry joins
them and the setup finishes: the runtime is installed and Edge Update
records it.  Its installer then cleans up its temporary files, marking
each for deletion on close; NovaOS deletes such a file when its last
handle closes, as Windows does.

The runtime itself now starts.  `wv2host`, a small WebView2 host NovaOS
builds, loads `WebView2Loader.dll` from Microsoft's WebView2 SDK out of
its own folder (NovaOS now searches the program's current folder for
DLLs after the system folders, as Windows' safe search order does),
finds the installed runtime and creates a WebView2 environment: the
browser process (`msedgewebview2.exe`) starts with its GPU, network and
storage processes.  For that NovaOS publishes COM's apartment in the
thread's TEB (`ReservedForOle`, which Chromium reads instead of asking
COM), answers the functions the browser calls (`GetAddrInfoExW`,
`RtlIpv4StringToAddressEx` and its IPv6 form, `LdrLockLoaderLock`,
`CryptFindOIDInfo`, the performance counter provider API, power
notifications, the packaged-app queries, `GetDllDirectory`), gives each
process up to 4080 fiber-local storage slots, ends a process that calls
`TerminateProcess` on itself without running DLL detach (Chromium's
DllMain deliberately crashes on a detach it does not expect), and its
`__C_specific_handler` and `RtlUnwindEx` no longer run a `__finally`
twice when an exception is caught by an `__except` inside it (the
Microsoft C++ runtime re-raises a rethrow from such a block, and
`oneauth.dll` aborted the browser on it).  The browser then connects
to the host's pipe and checks that the pipe's server is the host
(`GetNamedPipeServerProcessId`); NovaOS's pipes now keep both ends'
process ids, so it keeps the connection and makes the WebView's window
(it also needed shlwapi's ordinal 14, `GetAcceptLanguagesA`).  The host
then moves that window into its own with `SetParent` and `SetWindowPos`;
NovaOS's window handles now work across processes (messages, the calls
on a window, `SetParent` of another process's window and drawing into
it, checked by `xpwin`), so the controller is made, the page loads and
a script runs in it (`wv2host: script "NovaOS WebView2 / 42"`), with
the event log functions the browser calls afterwards (`EvtSubscribe`,
bookmarks, logs).  The page is drawn: the GPU process finds no
Direct3D 11 renderer for ANGLE (none without DXVK; NovaOS's WARP device
does not rasterize, so ANGLE refuses it) and falls back to Chromium's
software compositor, which draws through a DXGI 1.2 swap chain on a WARP
device shown by DirectComposition in a window the GPU process makes and
the browser parents in its own.  NovaOS's `dxgi.dll` now lists Windows'
Basic Render Driver, `d3d11.dll` gives that WARP device, `dcomp.dll`
draws a visual's swap chain into its window, and a child window parented
by another process becomes a window of its own kept inside the new
parent (`dcomptest`).
The machine-wide `WOW6432Node` registry view is implemented: 32-bit
machine `SOFTWARE` keys are isolated from the 64-bit view, and both
`KEY_WOW64_32KEY`/`KEY_WOW64_64KEY` and shared keys such as `Classes` are
covered by `regtest`.  This supplies the registry view used by 64-bit
programs to discover machine-wide WebView2 installs; per-user installs
remain registered under `HKEY_CURRENT_USER`.

With DXVK installed and Venus as the GPU, the GPU process keeps ANGLE's
Direct3D 11 device on the GPU and composites the page through
DirectComposition on it, across resizing, cross-process parenting and
teardown, for 64-bit and 32-bit hosts (`dcomptest dxvk`,
`dcomptest life`; roadmap item 25.1).  Programs that host WebView2, such
as Roblox's and Steam's sign-in pages, have not been checked end to end.

## Steam

Steam's installer (`SteamSetup.exe` from steampowered.com, a 32-bit NSIS
installer) installs the bootstrapper in `C:\Programs\Steam`.  On its first
start the bootstrapper downloads the client from Valve's servers, first the
32-bit one and then the 64-bit one it updates itself to, unpacks the
packages, checks every file and starts the 64-bit client, which starts
`SteamService.exe` and its browser, `steamwebhelper.exe` (Chromium 126 in
`bin\cef\cef.win64`).  Steam picks the proxy from the user's Internet
Settings, as on Windows.

The login window opens and draws its sign-in form (see the end of the
list below).  The browser starts its child
processes as on Windows: the GPU process, the network and storage
services and a page (renderer) process for Steam's first page.  The GPU
process draws with ANGLE on Direct3D 11 when Mesa 3D and DXVK are
installed; without them it gives up after three tries and Chromium draws
in software, as it does on Windows without a usable GPU.  The browser
used to close a few minutes in because `steam.exe` itself stopped: its UI
imports `AcceptEx` from `wsock32.dll` (ordinal 1141), which NovaOS's
Winsock 1.1 library did not export, so the first connection it accepted
ended the client with `STATUS_ENTRYPOINT_NOT_FOUND` and the browser shut
down with it.  `wsock32` now exports `TransmitFile`, `AcceptEx` and
`GetAcceptExSockaddrs` (1140 to 1142) from `mswsock`, as Windows does,
and Steam keeps running.  The browser then crashed in DirectWrite:
Chromium asks the DirectWrite factory for `IDWriteFactory2` and
`IDWriteFactory3` (Windows 10's font sets, face references and font
fallback) without checking the answer.  NovaOS's DirectWrite now has
them, and the browser keeps running: under emulation it starts its GPU,
network and storage processes, creates Steam's first browser ("SP Shared
JS Context") and launches its page process about eight minutes in, which
is as far as the corpus test's fifteen minutes reach.  `steam.exe` also
asks for `IDWriteFactory5` and goes on without it.  Run longer, the
browser restarted every few minutes: its network process got Winsock
error 10038 from `connect` on a UDP socket (Chromium's DNS client), which
NovaOS's kernel did not support, it hands sockets between processes with
`WSADuplicateSocket`, which was missing, and the browser process stopped
on a check that a file handle it passes to a child is read-only
(`NtQueryObject` reported every handle as holding all rights).  All three
are fixed; the browser no longer stops, and about half an hour in Steam
opened its first window of its own, an "Unexpected Transport Error"
dialog.  Its text now draws (DirectWrite's GDI interop,
`CreateBitmapRenderTarget`), the page's websocket to `steam.exe` falls
back from `::1` to `127.0.0.1` the way it does on Windows, and Steam
accepts the connection: it checks that the connecting program is its
browser or one of the browser's child processes through the Tool Help
snapshot's parent process ids, which NovaOS used to report as 0.  The UI
now logs "WebUITransportStore: Connection status: connected" and starts
its login stores within about two minutes of the browser starting, and
the "Sign in to Steam" window opens (see the list below).  Signing in and
games are not checked yet.  What is known to be missing or was fixed on
the way, each a NovaOS gap and none a reason to change Steam:

- Media Foundation's playback (`mf.dll` has camera capture only; Chromium uses the rest for video).
- DirectWrite's `IDWriteFactory4` and later (`steam.exe` asks for
  `IDWriteFactory5` and goes on without it).
- WMI (`WbemLocator`, `{4590F811-1D3A-11D0-891F-00AA004B2E24}`), which
  Steam and its browser ask for and get on without (Chromium reads the
  board and BIOS names through it), and the COM classes
  `{33C53A50-F456-4884-B049-85FD643ECFED}`,
  `{E77CC89B-7401-4C04-8CED-149DB35ADD04}` and
  `{E2B3C97F-6AE1-41AC-817A-F6F92166D7DD}`, which it gets on without.
- `SteamService.exe` checks Steam's files with `WinVerifyTrust` and
  accepts them: NovaOS checks Authenticode signatures (the digest, Valve's
  chain to DigiCert's root and the timestamp), offline, so a revocation
  check it is asked for fails rather than passes.  The service then
  updates itself once, then runs as a real service under the control
  manager (`StartService` no longer fails with
  `ERROR_SERVICE_REQUEST_TIMEOUT`, 1053, now that `CopyFile` keeps a
  file's last-write time the way Windows does: the service compares it to
  tell whether its copy is current), installs its helper files and keeps
  running.  It asks for an event-tracing (ETW) session to watch process
  starts; NovaOS runs no trace sessions, so `StartTrace` fails as it does
  on Windows when no session can start, and the service watches processes
  without one.
- Steam's service pipe ("Failed to create Service pipe") and its
  security descriptors in SDDL form (`advapi32`'s SDDL functions are
  incomplete).
- `GetAdaptersAddresses` lists the network adapter now (Steam asserted
  "CalcUnIPThisBox - GetAdaptersAddresses returned 232" while it listed
  none; see `adaptertest`).  (The browser's sockets and Steam's downloads
  no longer fail with `WSAENOBUFS`: NovaOS's socket tables were sized for
  a small device; see `loadtest`.)
- The login window opens: Steam's interface (the hidden "SP Shared JS
  Context" page) connects to `steam.exe`, starts its stores, gets Steam
  Input's controller strings and list (a 28 KB answer that a short
  non-blocking send used to cut off) and opens "Sign in to Steam".  Its
  contents were black: Steam's browser composer puts Chromium's window
  inside an SDL window of its own as a child window, and Chromium's GPU
  process, drawing in software, draws into that child with `GetDC` and
  `BitBlt` from another process, which NovaOS allowed only on top-level
  windows.  It now works on child windows too, and the form draws (QR
  code area aside, see the next point).
- Steam's connection to its servers (its CM servers, over WebSockets on
  ports 443 and 27017 to 27023) fails in the test sandbox, whose only way
  out is an HTTPS proxy; the sign-in page logs "Failed to start auth
  session" and Steam retries.  On a real network this is expected to
  connect; signing in is a hand check.
  Chromium's network change and DNS watchers do not start
  (`WSALookupServiceBegin` has no providers, error 10108).
- The "Thread creation failed" messages `steam.exe` logged once under
  load did not come back in about three hours of runs; NovaOS logs a
  refused thread (`no new thread`) and never did.

How to check the rest by hand, which needs a Steam account and so is not
in the corpus: start Steam from the App Store, sign in (or pick "Go
offline" once signed in), install a small free game and
start it to its main menu.

## Programs that come with NovaOS

Built in: the Terminal, File Explorer, Notepad, Settings, Calendar,
Photos, the App Store, Install NovaOS, Welcome to NovaOS, NovaOS's own
`cmd.exe` and command-line tools, Firefox 157 (Mozilla's unmodified
Windows build, the default browser) and the NetSurf web browser.  Mesa's
OpenGL (llvmpipe) and cnc-ddraw (as `ddraw.dll`) are built in too.  The
[user guide](user-guide.md) describes the apps.

## Reporting a program

If a program fails, open an issue on
[GitHub](https://github.com/dean-plude/os/issues) with its name, version
and download link, what you did and what happened, the boot log (the
serial output in QEMU, or `EFI\NOVA\bootlog.txt` on a USB stick) and,
if it crashed, its crash report from `C:\NovaOS\Crashes` (`crashes last`
in the Terminal shows the newest).
