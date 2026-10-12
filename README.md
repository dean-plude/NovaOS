# NovaOS — Windows-Compatible Operating System
<!-- The regions between "BEGIN generated" and "END generated" markers are built from fragment files by tools/docgen.py: edit those files, not the regions (CONTRIBUTING.md). -->

A clean-room, from-scratch x86-64 operating system that runs native Windows
executables without emulation.  The programs' own machine code runs directly
on the CPU; NovaOS supplies what they expect from Windows: the NT system-call
ABI, the Win32 API, the loader, a GUI, and the drivers underneath.  64-bit
(x64, PE32+) programs run natively, and 32-bit (x86, PE32) ones run through
NovaOS's own WoW64 layer, as on 64-bit Windows.

**Status:** working towards the first release, 0.1 (Phase 22 of the
roadmap).  NovaOS boots on UEFI machines (tested in QEMU with OVMF; no
real PC has been checked yet, see [docs/hardware.md](docs/hardware.md)),
uses every CPU core, keeps its files on a SATA or NVMe disk, and runs
unmodified Windows programs: 7-Zip, Git, Notepad++, Firefox, VLC,
Audacity, KeePassXC, Inkscape, NSIS installers, `.msi` packages (with
their own dialogs, custom actions, shortcuts and services), the Java,
.NET, Node.js and Python runtimes, and OpenGL, Vulkan and Direct3D 8–11
programs through Mesa and DXVK.  It can install itself on a disk from its
live ISO.  New to NovaOS?  Start with the
[user guide](docs/user-guide.md); [docs/compatibility.md](docs/compatibility.md)
rates each program and game that has been tried, ProtonDB-style.

- [Screenshots](#screenshots)
- [What is inside](#what-is-inside)
- [Quick start](#quick-start)
- [Testing](#testing)
- [Roadmap](#roadmap)
- [Repository layout](#repository-layout)
- [Key design decisions](#key-design-decisions)
- [Documentation](#documentation)
- [License](#license)

## Screenshots

Captured from NovaOS running in QEMU at 2560×1600 (200% scale) and stored at
1280×800; [docs/screenshots/README.md](docs/screenshots/README.md) says how
each one was taken.

| | |
|---|---|
| ![Terminal and File Explorer snapped side by side](docs/screenshots/desktop.png) | ![Notepad++ editing NovaOS's own scheduler source](docs/screenshots/notepad++.png) |
| The desktop: Terminal and File Explorer snapped side by side | Notepad++ 8.8 editing NovaOS's own kernel scheduler |
| ![The App Store](docs/screenshots/app-store.png) | ![Firefox showing a page fetched over HTTPS](docs/screenshots/firefox.png) |
| The App Store, which installs real Windows programs | Firefox loading a page over HTTPS |
| ![VLC playing a video](docs/screenshots/vlc.png) | ![KeePassXC with an open database](docs/screenshots/keepassxc.png) |
| VLC playing an H.264 video with sound | KeePassXC (Qt) with an open password database |
| ![Inkscape with a new document](docs/screenshots/inkscape.png) | ![Beneath a Steel Sky on ScummVM, Robert Foster on the gantry](docs/screenshots/beneath-a-steel-sky.png) |
| Inkscape (GTK) with a new document | Beneath a Steel Sky (free on GOG) on ScummVM, installed with its installer |
| ![Teeworlds' start menu in full screen](docs/screenshots/teeworlds.png) | ![Tyrian's first level in OpenTyrian, in full screen](docs/screenshots/opentyrian.png) |
| Teeworlds in full screen at its start menu, its music playing | Tyrian 2.1 (freeware) on OpenTyrian in full screen, drawn with Direct3D 9 through DXVK |
| ![Blobby Volley 2's main menu, the display switched to 800x600](docs/screenshots/blobby-volley-2.png) | ![LBreakout2's first level in full screen at 640x480](docs/screenshots/lbreakout2.png) |
| Blobby Volley 2 in full screen: the display switched to 800x600, drawn with Direct3D 9 in exclusive full screen through DXVK | LBreakout2 (SDL 1.2) in full screen: the display switched to 640x480, drawn with GDI |
| ![Cave Story's First Cave in full screen at 640x480](docs/screenshots/cave-story.png) | ![SuperTux's first level, played with a game pad](docs/screenshots/supertux.png) |
| Cave Story (freeware) in full screen: DirectDraw through cnc-ddraw, the display switched to 640x480 | SuperTux (SDL 1.2) played with an Xbox 360 game pad through winmm's joystick functions |

## What is inside

A short tour; [docs/inside.md](docs/inside.md) has the full one and
[docs/HISTORY.md](docs/HISTORY.md) the details of every part, phase by phase.

<!-- BEGIN generated:inside -->

- **Boot**: a UEFI bootloader loads the ELF kernel from the EFI System Partition or the ISO; the same ISO runs live and installs to a disk.
- **Kernel** (`kernel/`): an NT-style executive with objects and handles, processes and threads, virtual memory, an SMP scheduler and a registry.
- **Drivers**: SATA and NVMe disks, FAT, GPT and NTFS, USB (xHCI to UHCI, HID, mass storage), Intel and virtio networking, audio, ACPI and power.
- **Networking**: lwIP over IPv4 and IPv6, DNS, DHCP and SLAAC, TLS 1.3/1.2 on Mbed TLS, HTTP/2 and Winsock.
- **Windows userland** (`userland/`): about 35 system DLLs written from scratch, x64 and x86, with `ntdll`, `kernel32`, `user32`, `gdi32`, COM, the Windows Installer and a C runtime; text, 2D drawing, ICU and media support are part of it.
- **Program support**: the PE loader, x64 and x86 structured exceptions, WoW64, services, and the compatibility shims real programs need.
- **Updates and apps**: the App Store installs real Windows programs and, from a signed update channel, newer NovaOS builds; Firefox is bundled and Hyper-V is supported.

<!-- END generated:inside -->

## Quick start

Linux (Ubuntu/Debian) is the build host; on Windows use WSL2.

Install the prerequisites, build the bootloader, the kernel with the whole
userland inside, and `nova.img`, then run it in QEMU:

```bash
sudo apt install cmake nasm clang lld llvm libc++-dev gcc-mingw-w64-x86-64 \
                 python3 qemu-system-x86 ovmf mtools dosfstools xorriso
mkdir build && cd build
cmake ..
make -j$(nproc)
cmake --build . --target run
```

The first build compiles the NetSurf browser (about 800 files; later builds
reuse the objects).  Set `NOVA_NO_NETSURF=1` to leave the browser out, and
`NOVA_NO_WOW64=1` to skip the 32-bit userland.  `run` attaches a second
disk, `build/nova-data.img`, where drive C: is kept across restarts and
rebuilds.  [docs/building.md](docs/building.md) covers the build, QEMU
options, putting your own programs on the disk, and debugging.

### Bootable ISO and installing on a disk

A ready-to-boot UEFI ISO, `nova.iso`, is built by CI rather than committed.
Download it from the [newest release](https://github.com/dean-plude/os/releases/latest)
with its checksum (`sha256sum -c nova.iso.sha256`), take the one built
from `main` from the [`latest` build](https://github.com/dean-plude/os/releases/download/latest/nova.iso),
or the `nova-iso` artifact of any pull request's CI run (the
**Artifacts** list on the run's Summary page).  It is also the installation disc: booted from it, NovaOS runs live and
opens **Install NovaOS**, which writes a GPT disk with an EFI System
Partition and a data partition for drive C:.

```bash
truncate -s 1G disk.img
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/OVMF_VARS.fd
qemu-system-x86_64 -machine q35 -m 2G -smp 4 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS.fd \
  -drive file=disk.img,format=raw -cdrom nova.iso
```

After installing, the same command without `-cdrom nova.iso` starts from
`disk.img`.  The first start from it opens **Welcome to NovaOS**, which
asks for your name, your time zone, your keyboard layout and a display
resolution before the desktop (`start welcome` in the Terminal goes through it again).

Give the machine 2 GB so downloaded installers fit in drive C: (which lives
in memory and is saved to disk).  On macOS with Homebrew QEMU, the firmware
ships with QEMU:

```bash
FW="$(brew --prefix qemu)/share/qemu/edk2-x86_64-code.fd"
qemu-system-x86_64 -machine q35 -m 2G -smp 4 \
  -drive if=pflash,format=raw,readonly=on,file="$FW" \
  -cdrom nova.iso -serial stdio
```

The same ISO starts a real PC from a USB stick (UEFI, Secure Boot off;
the display is the firmware's framebuffer).  Writing it erases the stick:
on Linux, find the stick with `lsblk` (here `/dev/sdX`) and run the
commands below; on Windows use Rufus in "DD image" mode or balenaEtcher,
and on a Mac see [docs/macos.md](docs/macos.md).  Started from the stick,
NovaOS runs live as from the disc and writes its log into
`EFI\NOVA\bootlog.txt` on the stick's EFI partition (partition 2,
labelled `NOVA_EFI`), so a PC without a serial port still leaves a log to
read on another computer.  Linux mounts that partition as it is; macOS
with `diskutil mount` (`disk4s2` for a stick at `disk4`); Windows gives
an EFI partition no drive letter by itself, so assign one in `diskpart`
(`list volume`, `select volume N`, `assign letter=Z`).

```bash
sudo dd if=nova.iso of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

To make the ISO yourself from a fresh build, run
`scripts/create-iso.sh nova.iso build/bootx64.efi build/kernel.elf`
(needs `xorriso`).  `*.iso` is in `.gitignore`: the ISO is never committed.

## Testing

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
  64- and 32-bit), `sectest`, `acltest` (64- and 32-bit),
  `consolecursortest` (classic console screen buffers: cursor, cell I/O,
  fills, scrolling, attributes and independent buffers; 64- and 32-bit),
  `guitest auto`, `inputtest` (side buttons, horizontal wheel, volume
  keys), `usbcheck` (media keys, AC Pan, pen pressure, tilt and twist,
  virtio pens and tablets), `anitest` (animated cursors and program
  pointers), `cursortest` (system pointers, SetSystemCursor, cursors at
  the display scale), `bmpcurtest` (1-, 4-, 8- and 16-bit DIB sections,
  cursors from bitmaps with alpha or monochrome masks), `pentest` (a pen
  as WM_POINTER* messages: enter, down, update, up, leave;
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
  of 15/16 of the RAM), `crash kernel`, a deliberate kernel fault whose
  serial log must show a backtrace with function names, and last the
  kernel crash report: after `crash kernel` and a reset, `crashes last`
  shows the fault's backtrace.<!-- END generated:core-tests -->
- **Network** (in the boot-test job): two boots with a virtio-net card: IPv4,
  IPv6 (SLAAC, RDNSS), HTTP/2 and loopback tests, then an Intel e1000e.
- **Graphics tests**: Direct2D, then OpenGL, Vulkan and Direct3D 9/11 on Mesa,
  DXVK and Venus, each 64- and 32-bit with screenshots, and the frame-rate
  checks for direct presentation.  What each one runs:
  [docs/ci.md](docs/ci.md#what-the-network-and-graphics-suites-run).

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
files and notes from the history ([docs/releasing.md](docs/releasing.md)).  Run the same
gates locally with `python3 tools/selftest.py` (and `--suite network`, `--suite graphics`)
after a build.  The core suite compiles its ACPI test tables with `iasl`; install
it with `sudo apt install acpica-tools` first.

**Every night, real programs.**  `.github/workflows/nightly.yml` builds
main and runs `tools/appcorpus.py`: the official Windows x64 releases of
<!-- BEGIN generated:corpus -->ripgrep, fd, jq, 7-Zip, MinGit (cloning a repository), Python, Node.js, .NET (German and Japanese formatting through ICU), ffmpeg (an MP4 converted to WebM), Roblox (installs; the client stops in its anti-cheat), Steam (installs and updates itself; the login window opens after the test's time), Microsoft Edge WebView2 runtime (installs; a host creates a controller, loads a page and executes JavaScript), SumatraPDF, WinMerge, KeePassXC, VLC (plays an H.264 and AAC MP4 with sound), Audacity (records 10 s from the microphone and saves the project), Inkscape, Krita, PhotoCraft, Firefox, Notepad++, OpenTTD (free on GOG; installs with its installer and reaches its main menu), Beneath a Steel Sky on ScummVM (free on GOG; installs with its installer, skips the intro and walks), Teeworlds (full screen, through its first-start questions to its start menu, with music; Settings opened with the mouse, SDL recentring the pointer with SetCursorPos), Teeworlds joining a game on its own server (teeworlds_srv.exe on the same machine, over the loopback interface) and staying in it, OpenTyrian (Tyrian 2.1 on Direct3D 9 through DXVK, its demo, menus and full screen, with music), Blobby Volley 2 (its full screen switches the display to 800x600 on Direct3D 9 through DXVK, and back), LBreakout2 (an SDL 1.2 game drawn with GDI; its full screen switches the display to 640x480, and back), Cave Story (a DirectDraw game, on cnc-ddraw: its title screen in full screen at 640x480, then a new game), Cave Story played with a game pad (DirectInput 7 joystick): the opening talk, then Quote walks in the Start Point cave, SuperTux (an SDL 1.2 game played with a game pad, through winmm's joystick functions: its first level), PuTTY and Squash the Creeps (Godot 4.7 drawing with Direct3D 12 through VKD3D-Proton)<!-- END generated:corpus -->.  The
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
  Terminal; each prints "N passed, 0 failed": <!-- BEGIN generated:selftest-programs -->`abitest`, `acltest`, `adaptertest`, `afdtest`, `aligntest`, `anitest`, `apitest`, `authtest`, `bmpcurtest`, `boosttest`, `cabtest`, `cachetest`, `chrometest`, `cliptest`, `cmdlinetest`, `comoop`, `comtest`, `consolecursortest`, `cppeh`, `crashtest`, `crttest`, `crtthreads`, `cursortest`, `d3d9test`, `dcomptest`, `delaytest`, `devenumtest`, `devnotifytest`, `disptest`, `dlgtest`, `dlltest`, `dltest`, `dpitest`, `drvtest`, `edgeupdtest`, `errnotest`, `etwtest`, `filetest`, `fpstate`, `gatetest`, `gl2test`, `glgeneric`, `httptest`, `icutest`, `imgsharetest`, `inputtest`, `kbdtest`, `libmtest`, `linktest`, `loadtest`, `looptest`, `mmcsstest`, `montest`, `msiqtest`, `msitest`, `msxmltest`, `ndrtest`, `nlstest`, `nstest`, `ntfstest`, `overlaptest`, `padtest`, `pentest`, `pipetest`, `pnptest`, `posixtest`, `powertest`, `prioritytest`, `proclisttest`, `proctest`, `qttest`, `qtthemetest`, `qtwebtest`, `ramdisktest`, `rawpadtest`, `rawtest`, `regtest`, `rttest`, `samplertest`, `savetest`, `schanneltest`, `sectest`, `setuptest`, `shmtest`, `smftest`, `smpstress`, `stltest`, `svctest`, `syscalltest`, `tcptabletest`, `threads`, `tlsslots`, `touchtest`, `tpsimpletest`, `tztest`, `unwindtest`, `usptest`, `waitmigrationtest`, `warptest`, `wintabtest`, `wndthreads`, `wvsetuptest`, `wvstarttest`, `xpwin`<!-- END generated:selftest-programs -->.  `soundtest`
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

See [docs/building.md#tests](docs/building.md#tests) for how to run them.

## Roadmap

**Current status: Phase 22, preparing the first release (0.1).** UEFI boot,
the x64/x86 Windows environment, desktop, persistent storage, tested apps,
software graphics and QEMU virtio-GPU acceleration are in place. The app
corpus and QEMU checks provide evidence for these features; they do not imply
broad Windows compatibility or native physical-GPU support.

The 0.1 release is not complete until its ISO boots on the reference ThinkPad
T14 Gen 4 and the release is tagged. Real-hardware validation remains
outstanding.

| Next phase | Focus |
|---|---|
| 23 | Finish 0.1: check the T14, fix its first missing driver, achieve a clean nightly week, check Hyper-V input and verify update signing. |
| 24 | Gaming: Steam/GOG sign-in and a DRM-free game; Direct3D 12 support is already demonstrated. |
| 25 | GPU: design and implement native Intel GPU support, then test 3D and an external monitor; retain the CPU fallback. |
| 26 | Kernel, console and storage: complete console cells and ConPTY, verify NTFS after power loss, and track performance regressions. |
| 27 | Everyday apps: grow the nightly App Store corpus, add office-suite coverage and verify public HTTPS browsing. |
| 28 | Laptop hardware: Wi-Fi, Bluetooth, camera, battery life and a second reference PC. |
| 29 | NovaOS 0.2 release on both reference PCs. |

See the [full roadmap](docs/ROADMAP.md) for current evidence, remaining
milestones and completion gates.

## Repository layout

```
os/
├── bootloader/           # UEFI bootloader (PE32+ EFI application)
├── include/              # Headers shared by bootloader and kernel (boot_protocol.h)
├── kernel/               # NT-style kernel (freestanding ELF64)
│   ├── arch/x86_64/      # GDT, IDT, APIC, paging, SMP trampoline, syscall entry
│   ├── ke/               # Startup, scheduler, SMP, wait queues, KPCR, syscall table, time zones
│   ├── mm/               # Physical pages, kernel heap, VMAs, sections
│   ├── ob/ ps/ se/ cm/ io/  # Object, process, security, configuration, I/O managers
│   ├── um/               # Windows programs: processes, threads, loader, NT services,
│   │                     #   WoW64, pipes, registry, sockets, windows, consoles
│   ├── fs/               # VFS, RAM disk (drive C:), FAT16/32, saving C:, Setup engine
│   ├── drivers/          # AHCI (SATA), NVMe, e1000/e1000e/I219, virtio-net, USB core, xHCI/EHCI/OHCI/UHCI, hubs, HID, mass storage
│   ├── hal/              # Serial, framebuffer, display (VBE), PCI, PS/2, CMOS clock, HPET, I/O APIC, ACPI (uACPI host)
│   ├── net/              # lwIP port, HTTP client, TLS (Mbed TLS)
│   ├── gdi/              # Software renderer, fonts, ICO and PNG decoding
│   ├── wm/               # Window manager, desktop shell, input, keyboard layouts, clipboard
│   ├── apps/             # Built-in apps: Terminal, Explorer, Notepad, Settings,
│   │                     #   Calendar, Photos, App Store, Setup
│   ├── ldr/              # Early PE loader and syscall thunk pages
│   └── lib/              # Freestanding string and memory library
├── userland/             # The Windows userland, built with clang + lld-link:
│   ├── ntdll/ kernel32/ user32/ gdi32/ ...   # one directory per system DLL (dll.json)
│   ├── msvcrt/ crt/      # C runtime (msvcrt.dll and ucrtbase.dll), program startup
│   ├── msi/              # Windows Installer (msi.dll)
│   ├── programs/         # cmd.exe, msiexec, reg, find..., samples and self-tests
│   ├── netsurf/          # NetSurf port: fetcher, window surface, fonts
│   └── include/          # The Windows SDK headers NovaOS provides
├── third_party/          # lwIP, Mbed TLS, nghttp2, libxml2, uACPI, musl (libm), HarfBuzz, FreeType, NetSurf, stb, fonts, ICU (icu.dll + data), 7-Zip installer
├── tools/                # Host tools: build_userland.py, build_netsurf.py, mkfont,
│                         #   make_icons.py, mkani.py, pe_imports.py, msitest/, docgen.py,
│                         #   mkupdate.py and release_notes.py (releases)
├── tests/                # CI self-tests and app corpus (one file per test), ACPI
│                         #   tables, reference screenshots
├── scripts/              # build.sh, run-qemu.sh, create-disk.sh, create-iso.sh
└── docs/                 # Building, releasing, roadmap, feature history, Phase 1 architecture,
                          #   release notes (releases/), and the fragments README and docs/*.md are built from
```

## Key design decisions

The full list, with the reasons, is in
[docs/design/architecture.md](docs/design/architecture.md).

- **Clean-room and ABI-faithful**: every DLL and service is written from
  scratch, but syscall numbers, `NTSTATUS` codes and structure layouts
  follow Windows 10 1903 x64, so unmodified binaries find what they expect.
- **Native execution, both bitnesses**: x64 in long mode, x86 in compatibility
  mode through a 32-bit `ntdll`, the way WoW64 does.
- **The window system lives in the program**: `user32` keeps the window tree;
  the kernel composites top-level windows from bitmaps the programs own.
- **Software rendering**: GDI is a CPU rasterizer at integer HiDPI scale;
  the QEMU 3D virtio-gpu (Venus, virgl) is the only GPU path so far.
- **Drive C: in memory, saved to FAT**, with its files read back on demand.
- **SMP with fine-grained locks**, and a big lock for the rest of the kernel.
- **Kernel-helper syscalls (0x01F0–0x01FF)** are private to NovaOS's own DLLs.

## Documentation

- [docs/user-guide.md](docs/user-guide.md): using NovaOS: trying it in a
  virtual machine, installing it, the desktop and its apps, installing
  programs.
- [docs/compatibility.md](docs/compatibility.md): which Windows programs
  run, rated Platinum to Borked as ProtonDB does, and how each was checked.
- [CONTRIBUTING.md](CONTRIBUTING.md): where a change goes (one file per
  DLL, test and doc item, so parallel pull requests do not conflict) and
  how to merge main into a branch.
- [docs/building.md](docs/building.md): building, running, the data disk,
  tests, debugging.
- [docs/ci.md](docs/ci.md): how CI is organised, and what its network and
  graphics suites run.
- [docs/design/architecture.md](docs/design/architecture.md): the key design
  decisions in full.
- [docs/macos.md](docs/macos.md): running and building on a Mac (Apple
  Silicon and Intel).
- [docs/ROADMAP.md](docs/ROADMAP.md): the compatibility strategy and what
  comes next.
- [docs/hardware.md](docs/hardware.md): the reference PC for real
  hardware, which of its devices NovaOS drives, and every driver NovaOS
  has.
- [docs/hyperv.md](docs/hyperv.md): running NovaOS in a Hyper-V VM (the
  keyboard and mouse over VMBus) and what does not work there yet.
- [docs/ethernet.md](docs/ethernet.md): the Intel Ethernet driver, the
  I219 IDs it takes and how it brings one up.
- [docs/install-and-power.md](docs/install-and-power.md): installing on a
  laptop's NVMe disk, its embedded controller, sleep without S3, and the
  checks to run on the reference ThinkPad.
- [docs/HISTORY.md](docs/HISTORY.md): what every phase added, in detail.
- [docs/phase1-architecture.md](docs/phase1-architecture.md): the boot flow,
  address-space layout and early kernel design.

## License

NovaOS is MIT licensed. The operating system (kernel, bootloader, system
DLLs, C runtime, desktop and apps) contains no GPL code (the one LGPL
library, the HLSL compiler in `d3dcompiler_47.dll`, is below); bundled third-party
code keeps its own permissive licence (<!-- BEGIN generated:licenses -->lwIP: BSD 3-clause; Mbed TLS: Apache-2.0; Monocypher: BSD-2-Clause (or CC0); nghttp2: MIT; libxml2: MIT; mujs: ISC; uACPI: MIT; Intel's e1000 shared code (FreeBSD's, for the I219 bring-up in `kernel/drivers/e1000.c`): BSD 3-clause; OpenBSD's `pchgpio(4)` (Intel GPIO controllers' register layout and pad groups, in `kernel/hal/gpio.c`): ISC; musl's libm and complex functions: MIT; ICU: Unicode License v3 (`third_party/icu/LICENSE`); kernel32's locale table, from .NET: MIT; the time zone table (`kernel/ke/tzdata.inc`): zone names from Unicode CLDR's windowsZones, Unicode License v3; rules from the IANA tz database, public domain; HarfBuzz: MIT; the keyboard layouts (`userland/include/kbdlayouts.h`): xkeyboard-config, MIT/X11 licence, compiled by libxkbcommon (MIT); FreeType: the FreeType License (BSD-style; portions of this software are copyright © 2024 The FreeType Project (www.freetype.org), all rights reserved); Boost.Math (the C++17 special math functions in `msvcp140_2.dll`): Boost Software License 1.0; Microsoft's C++ standard library (STL): Apache-2.0 WITH LLVM-exception; plutovg: MIT (with FreeType-licensed rasteriser and stroker files); Mesa's llvmpipe (mesa-dist-win 24.2.4, the built-in OpenGL): MIT; Mesa's Venus and virgl (`third_party/mesa-venus`, the App Store's Venus): MIT; the SPIR-V headers (`third_party/spirv-headers`, for the HLSL compiler in `d3dcompiler_47.dll`): MIT; Inter and Cascadia Mono: SIL OFL 1.1; Noto Sans Arabic and Devanagari: SIL OFL 1.1; DejaVu Sans Mono: Bitstream Vera licence; stb_truetype/stb_image: public domain or MIT; FAudio: zlib; TinySoundFont: MIT; Sound Open Firmware (Intel's signed audio DSP firmware, built in only when tools/fetch_sof_firmware.py fetched it): BSD 3-clause with Intel's firmware licence; cnc-ddraw (`third_party/cnc-ddraw`, `ddraw.dll`): MIT, with lodepng: zlib; Firefox: Mozilla Public License 2.0 and bundled third-party notices (unmodified official Mozilla payload).<!-- END generated:licenses -->).  All Win32 API implementations are clean-room, based on public
Microsoft documentation, the ReactOS reference and study of Wine's source,
but independently written.

**NetSurf** (`third_party/netsurf/netsurf`) is licensed under the **GNU GPL
version 2**; its libraries are MIT, zlib and libpng licensed (see
`third_party/netsurf/NOVA-VENDOR.txt`).  `netsurf.exe`, NetSurf linked with
the NovaOS glue in `userland/netsurf` (MIT, GPL-compatible), is a separate
program distributed under the GPL-2.0, with its complete source in this
repository; the kernel image merely carries it as a file for drive C:.
Build with `NOVA_NO_NETSURF=1` for an image without it.

**vkd3d-shader** (`third_party/vkd3d-shader`), Wine's HLSL compiler, is
licensed under the **GNU LGPL version 2.1** (or later).  It is compiled
into `d3dcompiler_47.dll` alone, a separate DLL whose own sources
(`userland/d3dcompiler_47`, MIT) call it only through its public API; its
complete source is in this repository, and a changed copy can be rebuilt
into that DLL and put in its place (see
`third_party/vkd3d-shader/NOVA-VENDOR.txt`).

**7-Zip** (`third_party/7z2603-x64.exe`) is Igor Pavlov's unmodified
installer, kept as a test case under 7-Zip's own licence
(<https://www.7-zip.org/license.txt>); it is not part of NovaOS.
