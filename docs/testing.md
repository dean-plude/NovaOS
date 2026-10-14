# Testing NovaOS

The README has the short version; [building.md](building.md#tests) says how to run the tests.

**Every pull request is boot-tested.**  GitHub Actions
(`.github/workflows/ci.yml`) builds the kernel, bootloader, userland and
`build/nova.img`, boots it in QEMU with OVMF and runs three suites with
`tools/selftest.py`:

- **Build and boot-test** (core): <!-- BEGIN generated:core-tests -->`apitest`, `abitest` (the PEB, TEB, `KUSER_SHARED_DATA`, `CONTEXT` and
  loader layouts, ntdll's stubs and the system-call numbers, against
  Windows 10 1903 x64), `syscalltest` (the system-call table reached
  without ntdll: ntdll's Zw exports ranked by address give each service
  its Windows 10 1903 number, and the kernel answers that number, as
  anti-cheat code expects), `aligntest` (a misaligned SSE access is
  fixed up to the unaligned move when alignment-fault fixup is on, as
  anti-cheat code expects; ThreadHideFromDebugger and the
  extended-context functions), `gatetest` (Windows' segment selectors,
  32-bit code in a 64-bit program through a far jump to 0x23,
  GetThreadContext on the calling thread and `__fastfail`, as anti-cheat
  code expects), `filetest`, `linktest`, `pipetest`, `proctest`,
  `dlltest` (DllMain, static TLS, and a DllMain returning FALSE stopping
  the program with 0xC0000142, module file names past the 64th module,
  exports by ordinal alone, image lists in streams and the shell's
  desktop folder, 64- and 32-bit), `sectest`, `acltest` (64- and
  32-bit), `consolecursortest` (classic console screen buffers: cursor,
  cell I/O, fills, scrolling, attributes and independent buffers; 64-
  and 32-bit), `ptyscreentest` (a full-screen program drawn in a console
  window and through a pseudo console gives the same cells),
  `conptytest` (a terminal host types into, resizes and exits an
  interactive `cmd.exe` through `CreatePseudoConsole`), `guitest auto`,
  `inputtest` (side buttons, horizontal wheel, volume keys), `usbcheck`
  (media keys, AC Pan, pen pressure, tilt and twist, virtio pens and
  tablets), `anitest` (animated cursors and program pointers),
  `cursortest` (system pointers, SetSystemCursor, cursors at the display
  scale), `bmpcurtest` (1-, 4-, 8- and 16-bit DIB sections, cursors from
  bitmaps with alpha or monochrome masks), `pentest` (a pen as
  WM_POINTER* messages: enter, down, update, up, leave;
  GetPointerPenInfo pressure, tilt, rotation, barrel and eraser;
  DefWindowProc promotion to mouse with the pen signature in
  GetMessageExtraInfo; WM_NCPOINTER* over a title bar and
  WM_POINTERACTIVATE; the mouse as a pointer, entering and leaving
  windows; 64- and 32-bit), `wintabtest` (wintab32 with no pen and with
  a synthetic one: contexts, packets, pressure, tilt and rotation),
  `dpitest` (per-monitor DPI: the manifest's `dpiAwareness`,
  `GetDpiForMonitor`, `GetDpiForWindow`, `WM_DPICHANGED` and its
  suggested rectangle when the monitor goes to 192 DPI and back, and the
  window and monitor coordinates an aware, an unaware and a system-aware
  process see; user32's controls, menus, scroll bars, dialogs, metrics
  and fonts at 192 DPI; per-thread awareness contexts; controls and
  dialogs rescaled when a window's DPI changes, comctl32 at 192 DPI, and
  coordinates converted between awareness contexts), `disptest`,
  `icutest` (ICU and locales, 64- and 32-bit), `comtest`, `nlstest`
  (date, time, number and currency formats in German and Japanese, 64-
  and 32-bit; `nlstest user`), `nlstest calendars` (Japanese eras,
  Buddhist, Taiwan, Tangun, Hebrew, Hijri, Um Al Qura and Persian dates;
  `GetDurationFormat`) and `nlstest override` (`SetLocaleInfo`), 64- and
  32-bit, `tlbtest` (type libraries, 64- and 32-bit), `usptest` (Arabic
  and Devanagari shaped through Uniscribe and drawn by `ExtTextOut`, 64-
  and 32-bit), `cppeh`, `dlgtest` (the common file dialogs:
  `OPENFILENAME` settings, `IFileDialog` objects), `unwindtest`,
  `stltest` (the C++ standard library, 64- and 32-bit), `delaytest` (the
  DLLs Firefox delay-loads), `rttest` (complex math, conio, DLL
  directories, sscanf_s, 64- and 32-bit), `qttest` (what Qt programs
  need: `GetGlyphOutline`, C++/WinRT `HSTRING`s, Windows Hello, the
  process DACL), `battery` (against the battery in
  `tests/acpi/battery.asl`), `soundtest` (the recorded WAV must hold the
  tones played), `soundtest godot` (WASAPI through IAudioClient3 as
  Godot plays, 64-bit and 32-bit), `soundtest record`, `capture` and
  `volume` (`waveIn` and WASAPI capture must record the tone the
  microphone hears, and a quarter of the endpoint volume must sound 12
  dB quieter), `soundtest dsound` and `dscapture` (DirectSound playback
  with notifications, frequency and volume, and DirectSoundCapture
  recording the microphone), `sleeptest timer` (`Sleep(1)`, 1 ms wait
  timeouts and waitable timers (periodic ones, their completion routines
  and timer queue timers too) end within a millisecond with every CPU
  busy), `soundtest mme` (`waveIn` recorded as PortAudio's MME host
  does, the program held up 250 ms a second: no input overflow, and no
  frame missing from the recorded tone), `savetest` (while NovaOS saves
  32 MiB of drive C: to its disk, calls behind the kernel, desktop and
  file-system locks keep answering, the file-system one within 100 ms;
  the save holds the file-system lock under 20 ms), `xa2test` (XAudio2
  2.9 and 2.7 voices, callbacks and effects, and X3DAudio panning),
  `powertest` (closing the lid in `tests/acpi/lid-thermal.asl` sleeps, a
  USB key and the lid wake it, the thermal zone's readings), `disptest
  1024 768` (saves the mode the restart must keep), `tpsimpletest` (5120
  one-shot thread-pool callbacks: callback context and calling
  convention, no leaked handles; 64- and 32-bit), `miditest` (MIDI
  through `midiOut`, `midiStream` and the MCI sequencer), `nlstest set
  ja-JP` (the user locale the restart must keep), `waitmigrationtest`
  (yielding workers repeatedly wait-any and wait-all on unsignaled
  events, exercising current-thread lookup and timeout registration on
  SMP; x64 and x86), an installer that replaces a running program and
  finishes after a restart (`filetest install`, `shutdown /r`, `filetest
  installed`), `nlstest after-restart ja-JP` (the restart kept the user
  locale), `disptest saved 1024 768` (the restart kept the saved display
  mode), hard links kept across a restart (`linktest restarted`),
  Windows Installer transforms, patches and rollback, and an installed
  service started at the next boot (`msitest transform`, `patch`,
  `rollback`, `service`, `shutdown /r`, `msitest service-boot`), Windows
  Installer JScript and VBScript custom actions setting and reading
  properties and writing files, and a failing script rolling its install
  back (`msitest script`), `smftest` (the C++17 special math functions
  in `msvcp140_2.dll`, 64- and 32-bit), `errnotest` (errno and the other
  C runtime per-thread state belong to each thread, 64- and 32-bit), a
  power cut in the middle of saving drive C: (the FAT holds chains no
  file reaches; at the next boot NovaOS frees them and has as much free
  space as `fsck.fat` finds), `boosttest` (a woken thread is boosted
  above its base priority and runs within 2 ms while threads of the same
  priority spin on every processor; the boost decays back to base, 64-
  and 32-bit), `crtthreads` (FLS callbacks, per-thread locale,
  thread-safe `getenv`, `WaitOnAddress` by API set, 64- and 32-bit),
  `mmcsstest` (MMCSS through avrt.dll: "Pro Audio" runs at 18 and
  "Games" at 16 without the privilege and reverts to the thread's own
  priority; a registered thread woken by an event runs within 2 ms while
  TIME_CRITICAL threads spin on every processor; registered threads
  spinning on every processor still leave a NORMAL thread room, 64- and
  32-bit), `prioritytest` (every priority class and thread level gives
  NT's base priority and reads back; REALTIME without the privilege is
  HIGH; with boosts off a HIGHEST thread woken by an event runs within 2
  ms while NORMAL threads spin on every processor; the Terminal's
  console program is the foreground process, gets the foreground boost
  and three times a background process's time slice, 64- and 32-bit),
  `tlsslots` (1088 TLS indexes and FLS callbacks at process exit, 64-
  and 32-bit), `cmdlinetest` (a 1,200-character command typed into the
  Terminal and into cmd.exe, CreateProcess with up to 32,766 characters
  and from an unquoted path with spaces, 64- and 32-bit), `devices`
  (every PCI function and the driver that claimed it: AHCI, the VGA
  card, HD Audio, xHCI; bridges marked), `gl2test` (OpenGL 3+ with
  shaders on the built-in Mesa llvmpipe, 64- and 32-bit), `glgeneric`
  (OpenGL 1.1 with no OpenGL driver installed, drawing in 2D, 64- and
  32-bit), `hwcheck` audio DSP: NHLT digital microphones, the SOF
  firmware manifest, the DSP boot (ROM, code loader, FW_READY, IPC4),
  the IPC4 capture pipeline into a host ring, paused and run again, and
  the boot again after sleep, on a modelled DSP, `hwcheck` (HD Audio
  controller matching, a modelled ALC257 codec with its headphone jack,
  a modelled I2C-HID touchpad); the touchpad in the boot's ACPI table is
  found and its missing controller reported, `crashtest` (crash.exe's
  report in `C:\NovaOS\Crashes`: exception, module+offset, stack,
  modules; 64- and 32-bit), and the Terminal's `Crash report:` line and
  `crashes last`, `hwcheck` touchpad gestures on a modelled precision
  touchpad: tap to click, two-finger tap for the right button,
  two-finger scrolling (vertical and horizontal) as wheel notches, palms
  ignored, `wndthreads` (child windows of other threads, sends to an
  ended thread's window, 64- and 32-bit), `hwcheck mic`: a modelled
  audio DSP as the "Microphone Array" recording device (`waveIn`, WASAPI
  and DirectSound list it, `soundtest record` and `capture` hear its
  tone, before and after a modelled sleep), `fpstate` (x87 control word
  and MXCSR on new threads, across switches, `RtlRestoreContext` and SEH
  unwinds, 64- and 32-bit), `ramdisktest` (files on drive C: take the
  memory their contents need: appended files, SetEndOfFile, deleting;
  the memory programs are told), the first-boot setup, `start welcome`
  (name, time zone, keyboard layout and display pages from the keyboard;
  German typed in the layout's test field), `kbdtest` with German in
  effect and US put back, then `whoami` as the name it gave (the program
  and the Terminal command), `tztest` in the zone it chose (Tokyo: UTC+9
  in GetLocalTime and localtime; Berlin, Sydney and Los Angeles rules)
  and `tzutil /s UTC`, `edgeupdtest` (Task Scheduler 2.0, DPAPI,
  UrlCombine and the other APIs Microsoft Edge Update needs, 64- and
  32-bit), `setuptest` (run as administrator, Rich Edit with RTF,
  variant arithmetic, a 300 MB file on C:, 64- and 32-bit), `msxmltest`
  (MSXML 6 DOM, XPath, XSL Patterns, SAX2 and registration, 64- and
  32-bit), `cabtest` (cabinet.dll: MSZIP and LZX cabinets, an attached
  container, a cabinet set; 64- and 32-bit), `authtest` (Authenticode:
  WinVerifyTrust, signer chains and timestamps, CryptQueryObject; 64-
  and 32-bit), `ndrtest` (rpcrt4's NDR engine: stubless proxies,
  NdrStubCall2, CStdStubBuffer and the NdrDll* entry points, 64- and
  32-bit), `regtest` (isolated 32- and 64-bit machine registry views,
  shared keys, and registry keys and values changed concurrently),
  `msiqtest` (Windows Installer: 32-bit packages into SysWOW64, product
  information by context, source lists, patch applicability; 64- and
  32-bit), `chrometest` (Chromium start-up: read-only shared memory,
  handle lists, ordinal exports; 64- and 32-bit), `svctest` (a real
  service: start with arguments, status handshake, controls, stop and
  delete; a LocalSystem service runs elevated; CopyFile keeps file
  times; 64- and 32-bit), `comoop` (cross-process COM: LocalServer32
  activation, the standard marshaler and RPC channel, the IDispatch
  proxy/stub, BSTR/VARIANT marshalling; 64- and 32-bit clients and
  servers), `etwtest` (event-tracing controllers with no sessions:
  StartTrace, StopTrace, ControlTrace, EnableTrace, QueryAllTraces,
  OpenTrace, ProcessTrace, CloseTrace; 64- and 32-bit), `qtwebtest` (the
  calls Qt WebEngine imports: Bluetooth and SDP records, Direct3D 12,
  WinUSB, AppContainer profiles, proxy resolver, security zones and
  more; 64- and 32-bit), `cachetest` (saved files of drive C: let go of
  and read back from the data disk: memory, ReadFile, a mapped view,
  renamed, hard-linked and emptied files, `shutdown /r`, `cachetest
  after`), `qtthemetest` (Windows.UI.ViewManagement UISettings and
  UIViewSettings as Qt reads them, ColorValuesChanged, Schannel's cipher
  suites; 64- and 32-bit), `samplertest` (a sampling profiler:
  GetThreadContext of a waiting thread, unwound with RtlVirtualUnwind),
  `hwcheck` touchpad interrupt: a modelled Intel GPIO controller from
  the ACPI tables, its interrupt routed, the touchpad's GpioInt pin set
  up and its reports read on the interrupt instead of polling,
  `proclisttest` (another program's image name, user and command line,
  as Edge Update looks for its install worker; 64- and 32-bit),
  `rawtest` (the mouse and the keyboard through Raw Input: device list,
  names and info; WM_INPUT with relative motion, button flags and the
  wheel; keys through GetRawInputBuffer with RIDEV_NOLEGACY; 64- and
  32-bit), `libmtest` (the 32-bit C runtime's `_libm_sse2_*` and `_CI*`
  math, which MSVC-built SDL2 imports), `warptest` (SetCursorPos and
  ClipCursor: the pointer moved and confined by the window in front,
  WM_MOUSEMOVE but no WM_INPUT for a warp, the mouse kept in the
  rectangle, a background process refused; 64- and 32-bit),
  `wvsetuptest` (a 300 MB file mapped whole, wer.dll's report API and
  FlsGetValue2, as the WebView2 runtime's setup needs; 64- and 32-bit),
  `wvstarttest` (COM's apartment in the TEB, TerminateProcess on itself,
  FLS slots and the functions the WebView2 runtime's browser process
  calls), `xpwin` (windows of different processes: SetParent,
  SetWindowPos, ShowWindow and messages across processes, GetDC on
  another process's window; 64- and 32-bit), `dcomptest` (Direct3D
  without a GPU as Chromium's software compositor uses it: the Basic
  Render Driver adapter, a WARP device, DirectComposition showing a swap
  chain, dirty-rectangle presents, a GPU process's window parented in
  another process's; 64- and 32-bit), `schanneltest stores` (a
  certificate found through a collection of collections, a root's
  extended key usages, and `InternetGetConnectedState` offline without a
  network; 64- and 32-bit), driver packages staged in the driver store
  and published as oemN.inf from a program (`DiInstallDriver`,
  `SetupCopyOEMInf`), with `pnputil` and from an MSI (`drvtest exe`,
  `zip`, `msi`), `imgsharetest` (a second process of a running program
  shares its image pages from the image cache; 64- and 32-bit; a commit
  of 15/16 of the RAM), `chkstktest` (ntdll's stack probe, `__chkstk` on
  x64 and `_chkstk` on x86, and its half-precision conversions, which
  LLVM's JIT in Mesa looks up), `avxtest` (the AVX state: CPUID's
  OSXSAVE, AVX and F16C and XCR0 as the kernel sets them, YMM0-15 kept
  across context switches, in an exception's `CONTEXT_XSTATE` and
  through `NtContinue`, and read from a suspended thread by
  `GetThreadContext`), `tpwaittest` (thread-pool waits: a wait calls
  back once per signal or on its timeout, a callback sets its own wait
  again, setting a wait never waits for a running callback, cancelling
  and closing from a callback; 64- and 32-bit), programs started from
  the Start menu (Win+S, the name, Enter): `winhello`, a GUI program,
  with no Terminal window, and `hello`, a console program, in a
  Terminal, `crash kernel`, a deliberate kernel fault whose serial log
  must show a backtrace with function names, and last the kernel crash
  report: after `crash kernel` and a reset, `crashes last` shows the
  fault's backtrace.<!-- END generated:core-tests -->
- **Network** (in the boot-test job): two boots with a virtio-net card: IPv4,
  IPv6 (SLAAC, RDNSS), HTTP/2 and loopback tests, then an Intel e1000e.
- **Graphics tests**: Direct2D, then OpenGL, Vulkan and Direct3D 9/11 on Mesa,
  DXVK and Venus, each 64- and 32-bit with screenshots, and the frame-rate
  checks for direct presentation.  What each one runs:
  [docs/ci.md](ci.md#what-the-network-and-graphics-suites-run).

The build compiles through ccache, and the boot-test job saves the cache
after each build, so a pull request recompiles only what it changed.  A pull
request that changes only Markdown, `docs/` or `LICENSE` skips the build and
boot jobs (GitHub counts a skipped job as passing a required check).

A failing test fails its check; each run's summary has a table of results,
and the serial logs and screenshots are kept as artifacts, along with the
bootable ISO (`nova-iso`).  When a push to `main` passes both suites, the
**Publish nova.iso** job puts that ISO on the `latest` build.  Pushing a
version tag (`v0.1.0`) runs the same suites on the tagged commit and
publishes a release with that ISO, its checksums, the update channel's
files and notes from the history ([docs/releasing.md](releasing.md)).  Run the same
gates locally with `python3 tools/selftest.py` (and `--suite network`, `--suite graphics`)
after a build.  The core suite compiles its ACPI test tables with `iasl`; install
it with `sudo apt install acpica-tools` first.

**Every night, real programs.**  `.github/workflows/nightly.yml` builds
main and runs `tools/appcorpus.py`: the official Windows x64 releases of
<!-- BEGIN generated:corpus -->ripgrep, fd, jq, 7-Zip, MinGit (cloning a repository), Python, Node.js, .NET (German and Japanese formatting through ICU), ffmpeg (an MP4 converted to WebM), Roblox (installs; the client stops in its anti-cheat), Steam (installs and updates itself; the login window opens after the test's time), Microsoft Edge WebView2 runtime (installs; a host creates a controller, loads a page and executes JavaScript), SumatraPDF, WinMerge, KeePassXC, VLC (plays an H.264 and AAC MP4 with sound), Audacity (records 10 s from the microphone and saves the project), Inkscape, Krita, PhotoCraft, VectorCraft, EffectCraft, PdfCraft, WordCraft, Firefox, CADCraft, DeckCraft, DesignCraft, FilmCraft, GridCraft, LightCraft, SoundCraft, Notepad++, OpenTTD (free on GOG; installs with its installer and reaches its main menu), Beneath a Steel Sky on ScummVM (free on GOG; installs with its installer, skips the intro and walks), Teeworlds (full screen, through its first-start questions to its start menu, with music; Settings opened with the mouse, SDL recentring the pointer with SetCursorPos), Teeworlds joining a game on its own server (teeworlds_srv.exe on the same machine, over the loopback interface) and staying in it, OpenTyrian (Tyrian 2.1 on Direct3D 9 through DXVK, its demo, menus and full screen, with music), Blobby Volley 2 (its full screen switches the display to 800x600 on Direct3D 9 through DXVK, and back), LBreakout2 (an SDL 1.2 game drawn with GDI; its full screen switches the display to 640x480, and back), Cave Story (a DirectDraw game, on cnc-ddraw: its title screen in full screen at 640x480, then a new game), Cave Story played with a game pad (DirectInput 7 joystick): the opening talk, then Quote walks in the Start Point cave, SuperTux (an SDL 1.2 game played with a game pad, through winmm's joystick functions: its first level), PuTTY, WinSCP (a Delphi VCL program: its Login dialog) and Squash the Creeps (Godot 4.7 drawing with Direct3D 12 through VKD3D-Proton)<!-- END generated:corpus -->.  The
windowed programs run last, one at a time: SumatraPDF opens a PDF,
WinMerge compares two files, Firefox installs from the App Store and
loads a page from an HTTPS server on the host, Notepad++ opens a file and PuTTY makes a raw
connection to an echo server on the host and types a line; each one's
screenshot must match its `tests/reference/NAME.png`.
It also checks NovaOS's own screens: `dir` on C: and on an NTFS drive D:
(each with its own free space) and File Explorer's This PC listing both.
It posts a pass/fail table per program to the "Nightly app corpus" issue.

- **Self-test programs** in `userland/programs/`, installed in
  `C:\Programs` (and 32-bit builds in `C:\Programs\x86`).  Run them from the
  Terminal; each prints "N passed, 0 failed": <!-- BEGIN generated:selftest-programs -->`abitest`, `acltest`, `adaptertest`, `afdtest`, `aligntest`, `anitest`, `apitest`, `authtest`, `avxtest`, `bmpcurtest`, `boosttest`, `cabtest`, `cachetest`, `chkstktest`, `chrometest`, `cliptest`, `cmdlinetest`, `comoop`, `comtest`, `conptytest`, `consolecursortest`, `cppeh`, `crashtest`, `crttest`, `crtthreads`, `cursortest`, `d3d9test`, `dcomptest`, `delaytest`, `devenumtest`, `devnotifytest`, `disptest`, `dlgtest`, `dlltest`, `dltest`, `dpitest`, `drvtest`, `edgeupdtest`, `edutest`, `errnotest`, `etwtest`, `filetest`, `fpstate`, `gatetest`, `gl2test`, `glgeneric`, `httptest`, `icutest`, `imgsharetest`, `inputtest`, `kbdtest`, `kmdftest`, `libmtest`, `linktest`, `loadtest`, `looptest`, `mmcsstest`, `montest`, `msiqtest`, `msitest`, `msxmltest`, `ndrtest`, `nlstest`, `nstest`, `ntfstest`, `overlaptest`, `padtest`, `pentest`, `pipetest`, `pnptest`, `posixtest`, `powertest`, `prioritytest`, `proclisttest`, `proctest`, `ptyscreentest`, `qttest`, `qtthemetest`, `qtwebtest`, `ramdisktest`, `rawpadtest`, `rawtest`, `regtest`, `rttest`, `samplertest`, `savetest`, `schanneltest`, `sectest`, `serialtest`, `setuptest`, `shmtest`, `smftest`, `smpstress`, `stltest`, `svctest`, `syscalltest`, `tcptabletest`, `threads`, `tlsslots`, `touchtest`, `tpsimpletest`, `tpwaittest`, `tztest`, `umdftest`, `unwindtest`, `usptest`, `viosertest`, `waitmigrationtest`, `warptest`, `wdmtest`, `wintabtest`, `wndthreads`, `wvsetuptest`, `wvstarttest`, `xpwin`<!-- END generated:selftest-programs -->.  `soundtest`
  plays tones through `waveOut`, WASAPI, `PlaySound` and `Beep`, records
  through `waveIn` and WASAPI capture, and lists the sound devices,
  chooses the default and sets each device's own volume;
  `tools/novarun.py --wav out.wav` records what NovaOS plays, `--rec in.wav`
  feeds a WAV to its microphone, and
  `tools/wavcheck.py out.wav` lists each tone's length and pitch.  `disktest
  write`, a restart and `disktest verify` check that drive C: survives a
  reboot.
- **GUI and interactive checks**: `winhello`, `guitest` (`guitest auto`
  drives its own menus, dialog, message box and property sheet and
  reports), `droptest`, `cpus` (SMP speed-up).
- **Real programs** are tested from a second disk image holding them
  (not in this repository), driven by a QEMU harness that types, clicks and takes screenshots.
- **On the host**: `tools/pe_imports.py PROGRAM.exe` lists the imports a
  Windows program needs that NovaOS's DLLs lack; `tools/msitest/` exercises
  the Windows Installer's package readers and SQL, and builds test
  packages (one with a service).  `tools/gltest/`,
  `tools/d3dtest/` and `tools/d2dtest/` are OpenGL, Direct3D 9/11 and
  Direct2D test programs, built with MinGW, for checking Mesa, DXVK and
  `d2d1.dll` on NovaOS.
- **Debugging**: the serial log (COM1) has every kernel message; the
  Terminal's `dmesg` shows it, and `trace NAME` logs a program's failing
  system calls.  A kernel fault, panic or failed assertion prints a
  backtrace with function names and offsets (the kernel carries its own
  symbol table); `crash kernel` shows one on purpose.  A crashed program
  or kernel leaves a text report in `C:\NovaOS\Crashes` to attach to an
  issue (a kernel fault's at the next start); the Terminal's `crashes`
  lists them and `crashes last` shows the newest.
