# What is inside NovaOS

A tour of NovaOS's parts, from the bootloader to the Windows DLLs. The
[README](../README.md#what-is-inside) has a one-line summary of each, and
[HISTORY.md](HISTORY.md) records how and when each part was built.

For how the parts fit together and why, see
[design/architecture.md](design/architecture.md); for the boot path and
the kernel's memory layout, [phase1-architecture.md](phase1-architecture.md);
for which programs run, [compatibility.md](compatibility.md).

Each item below is its own file in `docs/inside/`, and the list is
rebuilt from those files by `tools/docgen.py`. To change an item, edit its
file, not this page ([CONTRIBUTING.md](../CONTRIBUTING.md)).

<!-- BEGIN generated:inside -->

- **Boot** (`bootloader/`): a UEFI bootloader (a PE32+ EFI application)
  loads the ELF kernel, `\EFI\NOVA\kernel.elf`, from the EFI System
  Partition of an installed disk, or from the ISO on a CD or written to a
  USB stick.
  - The ISO is both the live system and the installer. Started from a USB
    stick, NovaOS also writes its log to `\EFI\NOVA\bootlog.txt` on the
    stick.
  - On an installed disk the bootloader adds a "NovaOS" firmware boot
    entry, and starts a newly downloaded kernel once, falling back to the
    previous one if the new one never reaches the desktop
    ([updates.md](updates.md)).
  - It hands the kernel the memory map, the GOP framebuffer, the ACPI RSDP
    and a copy of the SMBIOS tables ([phase1-architecture.md](phase1-architecture.md)).
  - It also reads every file under `\EFI\NOVA\Files` (wallpapers, icon
    sets, sample pictures: content kept out of `kernel.elf`) and lists them
    for the kernel (boot protocol version 5), which installs
    `\EFI\NOVA\Files\X` at `C:\X` (`kernel/fs/bootfiles.c`) and gives
    the memory back; the installer copies them to the disk it installs on.
- **Kernel** (`kernel/`): an NT-style executive written from scratch.
  - Object manager and handles, processes and threads, an I/O manager, the
    registry (saved to disk), security tokens (restricted tokens,
    impersonation) and security descriptors, checked when named objects
    and files on drive C: are opened.
  - Virtual memory with sections and guard pages. The read-only pages of a
    loaded DLL are shared by every process that loads the same bytes, as
    Windows shares image sections.
  - The NT system-call table at Windows 10 1903 x64 numbers; NovaOS's own
    services are numbered from 0x200 up.
  - SMP: a run queue per CPU, fine-grained locks for the busy paths and a
    big kernel lock for the rest ([design/architecture.md](design/architecture.md)).
  - Timers are the local APIC's, one-shot or TSC-deadline, calibrated
    against the HPET (or CPUID leaf 0x15, or the PIT). `Sleep(1)` and wait
    timeouts end within a fraction of a millisecond even with every CPU
    busy.
  - NT's priorities: base and dynamic priorities, priority classes,
    wake-up boosts (+1 to +6), the foreground boost and a three times
    longer time slice (60 ms against 20 ms) for the active window's
    process, and a balance set that lifts threads starved for 3 s. Kernel
    threads run at 16 to 19, above anything a program can ask for.
  - The Multimedia Class Scheduler (`AvSetMmThreadCharacteristics`) runs
    audio threads at real-time priority 18, capped at 80% of a CPU as on
    Windows. NovaOS's own sound threads (waveOut, waveIn, DirectSound,
    WASAPI) use it too.
  - CPU time is measured with the TSC: each switch and each timer tick
    charges the running thread, its process and its CPU (idle, user
    programs or the kernel), which `NtQuerySystemInformation`
    (`SystemProcessInformation`, `SystemProcessorPerformanceInformation`),
    `GetSystemTimes`, `GetProcessTimes` and `GetThreadTimes` report and
    Task Manager graphs.
  - Drive C: is a RAM disk, saved to an NTFS or FAT32 volume once it has
    been quiet for a second and restored at boot.
- **Built-in drivers** (`kernel/drivers/`, `kernel/hal/`, `kernel/fs/`):
  NovaOS's own drivers, compiled into the kernel; they play the part of
  Windows' inbox drivers ([drivers.md](drivers.md), [hardware.md](hardware.md)).
  - **Storage**: AHCI SATA and NVMe disks (NovaOS installs to and boots
    from either); MBR and GPT partition tables, read and written; every partition a
    volume with a `\\?\Volume{GUID}\` name, as on Windows, its drive
    letter kept in `HKLM\SYSTEM\MountedDevices`; FAT16/FAT32 (writable on
    fixed disks); NTFS with read, write and format (drive C: with file
    ACLs and hard links, and other drives as D:, E:, ...), repaired by
    NovaOS itself after a power cut.
  - **Network**: Intel e1000/e1000e (82540EM, 82574L and the I219 built
    into Intel PCs) and virtio-net.
  - **Sound**: Intel High Definition Audio, playback and recording, with
    headphone detection; a laptop's digital microphones through Sound Open
    Firmware on Intel's audio DSP; a kernel mixer.
  - **USB**: xHCI, EHCI, OHCI and UHCI controllers, any number of each,
    with hubs; HID keyboards, mice, tablets, pens (pressure, tilt, barrel
    rotation and buttons, eraser) and multi-touch screens; mass storage
    (FAT and NTFS sticks, hot-plugged as the next drive letter); USB Audio
    Class 1 and 2 speakers, headsets and microphones; wired Xbox 360 and
    Xbox One controllers and HID game pads (for XInput, DirectInput 8, Raw
    Input and `hid.dll`).
  - **Other input**: PS/2 keyboards and mice; I2C-HID touchpads on Intel's
    LPSS I2C controllers, found through ACPI and read on their GPIO
    interrupt (tap to click, two-finger right click and scrolling);
    virtio multi-touch screens, pens and tablets.
  - **Display**: a VBE "DISPI" driver for QEMU's standard VGA, QXL,
    virtio-vga, VMware and bochs-display adapters and VirtualBox's VBoxVGA
    (modes switched at run time, page flipping, more adapters as more
    monitors); a Cirrus GD5446 driver; a virtio-gpu driver
    (see Graphics below); the UEFI framebuffer as the fallback.
  - **Platform**: ACPI through uACPI's AML interpreter (power-off, reset,
    power and sleep buttons, S3 sleep or low-power S0 idle, batteries and
    AC adapters, the lid, thermal zones, wake devices, PCI interrupt
    routing, the embedded controller), the I/O APIC, the HPET and the CMOS
    clock.
  - **Plug and Play**: a device tree of the PCI and USB devices with
    Windows instance, hardware and compatible IDs, setup classes and
    drivers, arrivals and removals as events, walked with cfgmgr32,
    setupapi and `pnputil /enum-devices`.
- **Windows drivers** (`kernel/nt/`): real Windows driver packages (an
  `.inf` with its `.sys`, `.cat` and firmware files) run unchanged on an
  NT kernel-mode driver interface ([drivers.md](drivers.md)).
  - A kernel PE loader and `ntoskrnl.exe`/`hal.dll` exports in the
    Microsoft x64 ABI, with Windows' own structure layouts: IRQL, DPCs,
    spin locks, dispatcher objects, the pool, MDLs and DMA, IRPs,
    kernel-mode SEH, line-based and message-signalled (MSI, MSI-X)
    interrupts.
  - KMDF from Microsoft's MIT-licensed Windows Driver Frameworks source
    (`Wdf01000.sys` with NovaOS's `WdfLdr.sys`), and UMDF 2 drivers in
    `WUDFHost.exe` behind the `WUDFRd.sys` reflector.
  - Class hosts that vendor miniports plug into: NDIS (virtio-win's
    `netkvm.sys`), Storport (virtio-win's `viostor.sys`), the USB bus
    interface (FTDI's USB serial driver) and a display-only WDDM host in
    place of `dxgkrnl.sys`.
  - Drivers are installed with `pnputil` or `sc`, matched to devices by
    hardware ID from the driver store, downloaded by hardware ID from the
    Microsoft Update Catalog when none is there, and loaded only under
    Windows' kernel-mode signing policy.
- **Networking** (`kernel/net/`): lwIP for TCP/IP over IPv4 and IPv6.
  - DHCP, SLAAC and DNS over either, or a static address and DNS servers
    set in Settings > Network or with the Terminal's `netsh`, kept across
    restarts. A loopback interface carries 127.0.0.1 and ::1, and
    `localhost` resolves to both without DNS.
  - An HTTP/1.1 client and Mbed TLS with the Mozilla root store in the
    kernel, which the App Store and updates download with.
  - Winsock (`ws2_32`, and `wsock32` with Winsock 1.1's ordinals): IPv6 and
    dual-stack sockets, `getaddrinfo`, socket options that reach the stack
    and read back (`TCP_NODELAY`, timeouts, `SO_LINGER`, `SO_REUSEADDR`,
    `SO_KEEPALIVE`, buffer sizes, `IPV6_V6ONLY` on by default as on
    Windows), hundreds of sockets per program, `select`, `WSAPoll` and
    `WSAEventSelect` on any number of them, connected UDP sockets and
    `WSADuplicateSocket`.
  - Overlapped `AcceptEx`, `ConnectEx` and `WSARecv` stay pending and
    complete on an I/O completion port, as proactor event loops such as
    Python's asyncio need.
  - `winhttp` is an HTTP client over Schannel TLS, with HTTP/2 through
    ALPN (nghttp2).
- **Desktop** (`kernel/wm/`, `kernel/apps/`): a window manager and shell
  drawn by the kernel's software GDI ([design/native-desktop.md](design/native-desktop.md)).
  - Windows 11-style frames, a dock with pinned and running apps, a Start
    menu that searches apps, programs, settings pages and files, Alt+Tab,
    snapping, desktop icons and context menus, the Aurora workspace or the
    classic wallpaper themes, and several monitors arranged in Settings.
  - Wallpapers and icon sets are files on drive C:, never part of the
    kernel image: PNG pictures in `C:\Windows\Web\Wallpaper`
    (`kernel/wm/wallpaper.c`: the Aurora theme draws the chosen one,
    filling the screen, decoded and thumbnailed on a thread of its own;
    `HKCU\Control Panel\Desktop\Wallpaper` keeps it) and folders of
    `.ico`/`.png` files in `C:\Windows\Web\Icons` named after the icons
    they replace (`kernel/apps/iconset.c`; the built-in drawings stand in
    for any it lacks).  Settings > Personalization picks them and adds
    more from the user's folders; `HKCU\Software\NovaOS\Personalization`
    keeps the theme and the icon set.
  - Built-in apps: File Explorer, Notepad, Terminal (a command shell over
    drive C:, which also runs Windows console programs), Settings,
    Calendar, Photos, the App Store (downloads open-source Windows
    programs and NovaOS updates) and, on the live media, Install NovaOS.
  - Task Manager (`taskmgr.exe`, a Windows program in System32;
    Ctrl+Shift+Esc, Start's right-click menu or search): Processes with
    End task, Performance (each logical processor, memory, disk and
    Ethernet over 60 seconds), Details and Services, read through the same
    APIs Windows' Task Manager uses.
  - Disk Management (`diskmgmt.exe`, a Windows program in System32; Start
    menu search): the volumes with their letters, file systems, capacity
    and free space above, each disk and its partitions drawn below;
    Initialize Disk (GPT or MBR), New Simple Volume, Format (NTFS or
    FAT32, through `fmifs.dll`'s `FormatEx`) and Change Drive Letter,
    done through the same APIs Windows' Disk Management uses
    (`IOCTL_DISK_*_DRIVE_LAYOUT_EX`, `IOCTL_STORAGE_QUERY_PROPERTY`,
    `FindFirstVolume`, `GetVolumeInformation`, `SetVolumeMountPoint`).
- **Windows userland** (`userland/`): about 115 system DLLs, NovaOS's own
  code apart from the third-party libraries named below, built with clang
  for `x86_64-pc-windows-msvc` into `System32` and again for `i686` into
  `SysWOW64`.
  - **Core**: `ntdll` (the user-mode loader, fiber-local storage, and
    64 + 1024 TLS slots as on Windows), `kernel32`/`kernelbase`, `advapi32` (registry, security,
    services), `msvcrt`, `ucrtbase` and the `api-ms-win-crt-*` API sets
    (per-thread `errno` and locales as on Windows), `vcruntime140` and
    `vcruntime140_1` (C++ exceptions, FH3 and FH4 tables), `msvcp140` and
    its satellites (Microsoft's own STL, with Boost.Math under
    `msvcp140_2`), and the NT 3 `crtdll`.
  - **Windows and controls**: `user32` (the window tree, controls, menus,
    dialogs, MDI, hooks, per-monitor and per-thread DPI awareness, touch,
    pens and the mouse as `WM_POINTER` messages), `gdi32`, `gdiplus` (on the
    plutovg rasteriser), `comctl32`, `comdlg32` (classic and `IFileDialog`
    Open and Save As dialogs), `riched20`/`msftedit` (Rich Edit with RTF),
    `shell32`, `oleacc`, `uxtheme`, `dwmapi`.
  - **COM**: `ole32`/`oleaut32` (in-process and local servers, type
    libraries, calls between processes with the standard marshaler, the
    Windows Runtime's strings and the few runtime classes Win32 programs
    ask for) and `rpcrt4` (the NDR engine proxy/stub DLLs run on).
  - **Sound and input**: `winmm` (waveOut, waveIn, MIDI, joysticks),
    `mmdevapi` (WASAPI playback and capture, one endpoint per device),
    `dsound`, `xaudio2_7`/`_8`/`_9` and `x3daudio1_7` (on FAudio),
    `dinput` and `dinput8`, XInput, `wintab32` (Wintab pen tablets) and
    `hid`.
  - **Installers and files**: `msi` (Windows Installer, with JScript and
    VBScript custom actions on mujs), `cabinet` (MSZIP and LZX), `msxml6`
    (on libxml2, also answering MSXML 3's classes), `shfolder`, `setupapi`,
    `cfgmgr32`, `taskschd`.
  - **Security and the web**: `secur32` with Schannel (TLS 1.3/1.2 on Mbed
    TLS), `crypt32` and `wintrust` (certificate stores and chains, PKCS #7,
    Authenticode with `WinVerifyTrust`), `bcrypt`/`ncrypt`, `ws2_32`,
    `winhttp`, `wininet`, `urlmon`, `normaliz` (IDN), `wldap32` (no
    directory servers yet).
  - **Stand-ins**: DLLs that programs import for hardware NovaOS does not
    have answer as Windows does on a PC without it, such as `bthprops.cpl`
    (no Bluetooth radio) and `winusb` (no WinUSB device).
- **Text**: `novatext.dll`, one shared text core with HarfBuzz (shaping)
  and FreeType (fonts).
  - Uniscribe (`usp10`) itemizes text by script and direction and shapes
    it with HarfBuzz, and GDI's `ExtTextOut` sends complex scripts through
    it, as Windows' LPK does, so Arabic, Hebrew and the Indic scripts join,
    reorder and run right to left. GDI falls back to Noto Sans for Arabic
    and Devanagari.
  - DirectWrite (`dwrite`) lays text out on the same core, with font
    fallback, and its factory is an `IDWriteFactory3` with Windows 10's
    font sets and `IDWriteFontFallback`, which Chromium-based browsers ask
    for (`tools/dwtest`, `tools/dw3test`).
- **2D drawing**: Direct2D (`d2d1.dll`) draws in software: geometries
  (rectangles, ellipses, paths with Béziers and arcs, groups, transforms,
  combining, widening, tessellation), strokes with caps, joins and dashes,
  solid, gradient and bitmap brushes, layers and clips, on HWND, DC and
  bitmap render targets, with text through DirectWrite (`tools/d2dtest`).
- **Graphics**: drawing runs on the CPU by default, and on the host's GPU
  under QEMU with a 3D virtio-gpu.
  - **OpenGL**: `opengl32` hands every call to Mesa: llvmpipe (OpenGL 4.5
    on the CPU) is on drive C: from the start, and virgl (the App Store's
    "Venus" package) runs OpenGL on the host GPU. With neither, NovaOS's own
    OpenGL 1.1 draws 2D, as ScummVM needs.
  - **Vulkan and Direct3D**: `vulkan-1` loads Mesa's drivers from the App
    Store: lavapipe on the CPU ("Mesa 3D") or Venus on the host GPU. DXVK
    runs Direct3D 8 to 11 and VKD3D-Proton runs Direct3D 12 on them, with
    `d3d9`, `d3d11` and `d3d12` handing their calls over once installed.
  - **Without them**: `d3d9` lists the adapters with no device, so programs
    take their software paths; `dxgi`, `d3d11` and `dcomp` give Windows'
    Microsoft Basic Render Driver adapter, a CPU device that Chromium's
    software compositor draws WebView2 with; `ddraw` is cnc-ddraw for
    32-bit DirectDraw games.
  - **Shaders**: `d3dcompiler_47` compiles HLSL to Direct3D 9 to 12
    bytecode (up to shader model 5.1) on Wine's vkd3d-shader, for ANGLE,
    wgpu and games, with `D3DReflect` and `D3DDisassemble`.
  - **virtio-gpu** (`kernel/drivers/virtio_gpu.c`): QEMU's virtio-vga and
    virtio-gpu-pci with several outputs plugged in and out at run time; on
    `virtio-vga-gl` it gives Venus and virgl their 3D contexts, blobs and
    fences, and a window alone on its monitor has its OpenGL or Vulkan
    frames written to the screen by the host (direct presentation).
  - Real GPUs: a display-only WDDM host runs Windows display drivers for
    the screen alone; 3D on Intel's GPU through Intel's own Windows driver
    is planned ([design/intel-gpu.md](design/intel-gpu.md)).
- **Media tools**: an unmodified Windows build of ffmpeg 7.1.1 makes an
  H.264 and AAC MP4 and converts it to VP9 and Opus with all its threads
  (`tests/appcorpus/080-ffmpeg.py`).
- **Media players and editors**: VLC plays an H.264 and AAC video with
  sound, and Audacity records from the microphone and saves the project,
  on the same WASAPI, GDI and message-loop paths real Windows programs
  take (`tests/appcorpus/870-vlc.py`, `880-audacity.py`).
- **ICU**, as Windows 10 ships it: `icu.dll` (ICU 77.1, 64- and 32-bit,
  built by `tools/build_icu.py`) with its data in
  `C:\Windows\Globalization\ICU`.
  - .NET does its globalization through it.
  - `kernel32` answers `GetLocaleInfoEx` from it for all 864 Windows
    locales, and formats dates, times, numbers, currency and durations in
    their calendars (Japanese eras, Thai Buddhist, Hebrew, Hijri, Um Al
    Qura, Persian and others).
  - The user's regional format is set with `intl NAME` or
    Settings > Time & language, and kept in the registry with the user's
    own changes (`SetLocaleInfo`).
- **Program support**: what unmodified Windows programs rely on beyond
  the DLLs themselves.
  - The PE loader: imports, forwarders, API sets, TLS callbacks and
    `DllMain` run in user mode by `ntdll`, in dependency order.
  - 64- and 32-bit programs side by side: 32-bit code runs in compatibility
    mode through a 32-bit `ntdll` that converts each system call, as WoW64
    does, and Windows' segment selectors let 64-bit code far-jump into
    32-bit code.
  - x64 and x86 structured exception handling, with `CONTEXT` and the AVX
    state as Windows lays them out, and Windows' fix-up of misaligned SSE
    moves.
  - Consoles: screen buffers on libvterm, and pseudo consoles (ConPTY) for
    terminal hosts running `cmd.exe`. GUI programs start without a console
    window.
  - COM in-process and local (`LocalServer32`) servers, proxy/stub DLLs
    and calls between processes; drag and drop; a shared clipboard; `.lnk`
    shortcuts; Windows Installer packages.
  - Services (`advapi32`'s service control manager), scheduled tasks (Task
    Scheduler 2.0, kept in `C:\Windows\System32\Tasks`), the Data
    Protection API, and event tracing that answers as Windows does with no
    logging session running.
  - The event log: an EventLog service keeps the Application, System and
    Security logs in `C:\Windows\System32\winevt\Logs` (classic `.evt`
    files), fed through a kernel queue, so the kernel's boot, driver-load
    and crash events and the service control manager's start and stop
    events are kept even before the service runs.  Programs use
    `advapi32`'s classic functions (`ReportEvent`, `ReadEventLog`,
    `ClearEventLog`...) or `wevtapi` (`EvtQuery` with XPath filters,
    `EvtRender`, `EvtSubscribe`, `EvtExportLog` writing `.evtx`); Event
    Viewer (`eventvwr.exe`) shows them.
- **Updates**: an installed NovaOS updates itself from an update channel
  ([updates.md](updates.md)). The App Store's Updates page (or the
  Terminal's `update`) downloads the new kernel and bootloader, the next
  restart starts them, and NovaOS goes back to the old ones if the new
  kernel does not reach the desktop. A NovaOS built with the release key
  installs only channels signed with it (Ed25519). The default channel is
  the newest tagged release; each green CI run on `main` also publishes a
  channel of its own with the rolling `latest` build.
- **Firefox**: Mozilla's unmodified Firefox 157.0 for Windows x64 is in the
  image (`tools/bundle_firefox.py` downloads it and checks it against
  Mozilla's SHA-512 sums) at `C:\Programs\Mozilla Firefox\core`, and opens
  web links and local HTML files. NetSurf stays installed and takes over
  when Firefox is missing. The nightly app corpus loads an HTTPS page in
  it and compares a screenshot ([building-firefox.md](building-firefox.md)).
- **Hyper-V**: in a Generation 2 VM, whose only input devices are on
  VMBus, NovaOS's VMBus driver (hypercalls, the SynIC, channel offers, ring
  buffers) runs Hyper-V's synthetic keyboard and mouse and answers its
  shutdown, heartbeat and time sync integration services (the host's Shut
  Down and Restart work). Its synthetic SCSI controller's disks are block
  devices, so NovaOS installs to a VHDX. Its synthetic network and video
  have no driver yet ([hyperv.md](hyperv.md)).

<!-- END generated:inside -->
