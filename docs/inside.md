# What is inside NovaOS

The full tour; the [README](../README.md#what-is-inside) has the short version and [HISTORY.md](HISTORY.md) the details of every phase.

<!-- BEGIN generated:inside -->

- **Boot**: a UEFI bootloader (a PE32+ EFI application) loads the ELF kernel
  from the EFI System Partition, or from the ISO on a CD or written to a
  USB stick (the installation media, run live; started from a stick,
  NovaOS writes its log into `\EFI\NOVA\bootlog.txt` on it).
- **Kernel** (`kernel/`): NT-style executive: object manager and handles,
  processes and threads, virtual memory with sections and guard pages
(the read-only pages of loaded DLLs are shared by every process with the
same bytes, as Windows shares image sections, and a module another
running process loaded at the same address is mapped from its kept pages
without being copied, relocated or hashed again), I/O,
  registry, security tokens (restricted tokens, impersonation) and
  security descriptors checked when named objects and files on drive C:
  are opened.  SMP with per-core scheduling and fine-grained
  locks; wait queues; APCs; pipes; the NT system-call table at Windows 10
  1903 numbers.  Timers are the local APIC's, one-shot or TSC-deadline and
  calibrated against the HPET (from CPUID leaf 0x15 where the firmware
  hides it), so `Sleep(1)`, wait timeouts and waitable
  timers end within a fraction of a millisecond even with every CPU busy;
  a thread woken by a timer preempts the running one instead of waiting
  for its time slice to end.  NT's priority boosts: a thread woken by an
  event, a lock, I/O, a window message or input runs above its base
  priority (+1 to +6; +2 for window messages and the input they carry,
  as win32k gives) and preempts busy threads of that priority, then
  decays back one level per quantum; a balance set lifts threads that
  have starved for 3 s.  Priority classes and thread priorities
  (`SetPriorityClass`, `SetThreadPriority`) set NT's base priorities;
  every kernel thread (input, audio, the network, USB, the desktop,
  saving drive C:) stays above anything a program can ask for, and the
  process whose window is active (or the console program running in the
  active Terminal) gets NT's foreground boost (+2 after every wait) and
  three times the time slice (60 ms against 20 ms).  The Multimedia
  Class Scheduler (`AvSetMmThreadCharacteristics`) runs a program's audio
  threads at real-time priority 18, above every program thread and the
  desktop, with Windows' 80% budget so one cannot freeze the machine;
  NovaOS's own sound threads (waveOut, waveIn, DirectSound, WASAPI) use
  it too.
- **Drivers**: AHCI SATA and NVMe disks (NovaOS installs to and boots from
  either), FAT16/FAT32, GPT, NTFS (read, write and format: drive C: with
  file ACLs and hard links, and other drives); Intel e1000/e1000e network cards (82540EM, 82574L
  and the I219 that Intel PCs have built in) and virtio-net; Intel High Definition Audio (playback
  and recording, laptop controllers with the audio DSP on included, the
  speakers turned off while headphones are plugged in; a laptop's digital
  microphones through Sound Open Firmware on Intel's audio DSP) with a kernel mixer;
  PS/2 keyboards and mice; I2C-HID touchpads on Intel's LPSS I2C
  controllers (found through ACPI; tap to click, two-finger tap for the
  right button and two-finger scrolling as mouse-wheel input; read when
  their interrupt pin on Intel's GPIO controller fires); USB (xHCI, EHCI, OHCI and UHCI controllers, any
  number of each) with hubs and HID keyboards (lock-key LEDs and media
  keys included), mice (five buttons and both wheels), tablets, pens
  (pressure, X/Y tilt, barrel rotation, barrel buttons and eraser, for
  Wintab and as `WM_POINTER` pen messages) and
  multi-touch screens (report protocol), USB sticks (FAT and NTFS, as
  the next drive letter, hot-plugged) and USB speakers, headsets and
  microphones (USB Audio Class 1 and 2 over isochronous transfers, at the
  device's own sampling rate and channel count, which the mixer runs at,
  asynchronous devices' rate feedback followed, played on and recorded
  from as soon as they are plugged in, or chosen in Settings' Sound page,
  each with its own volume; the choice and the levels are kept across
  restarts), and game controllers (wired Xbox 360 and Xbox One
  controllers, with their motors, and HID game pads, for XInput,
  DirectInput 8, Raw Input and `hid.dll`); a Plug and Play device tree
  of the PCI and USB devices (Windows instance, hardware and compatible
  IDs, setup classes and drivers; arrivals and removals as events, an
  unplugged device kept as not present; walked with cfgmgr32, setupapi
  and `pnputil /enum-devices`); virtio multi-touch screens,
  pens (pressure, tilt, rotation) and tablets; CMOS clock; a VBE display
  driver for QEMU's standard VGA, QXL, virtio-vga and VMware adapters,
  bochs-display and VirtualBox (resolutions switched at run time, page
  flipping, the mode set again after sleep and kept across restarts; more
  adapters, such as QEMU's secondary-vga, are more monitors of one
  desktop, arranged in Settings, each able to show DPI-aware programs
  its own DPI) and a Cirrus GD5446 one, with
  the UEFI framebuffer as the fallback; a virtio GPU driver for
  QEMU's virtio-vga and virtio-gpu-pci, whose outputs are several
  monitors on one card, plugged in and unplugged while NovaOS runs, and
  which on a 3D one (`virtio-vga-gl`) gives Mesa's Venus and virgl (App
  Store) their contexts, host-visible blobs, 3D resources, transfers and
  fences, so Vulkan, DXVK and OpenGL run on the host's GPU, and their
  frames (OpenGL's, and Vulkan's swapchains, so DXVK's and VKD3D-Proton's)
  are written into the screen there, on any monitor and at scale 2 too,
  while their window is alone on it (direct presentation; DirectComposition's
  GPU swap chains, WebView2's, go the same way through `dcomp.dll`); ACPI power-off, reset, power buttons,
  sleep (S3, or low-power S0 idle on firmware without it), batteries and
  AC adapters, the lid, thermal zones, wake devices and PCI interrupt
  routing (AML interpreted by uACPI, with the SCI a real interrupt
  through the I/O APIC), and the embedded controller laptops keep their
  lid and battery behind.
- **Networking**: lwIP (TCP/IP over IPv4 and IPv6: DHCP, SLAAC, DNS over
  either, or a static address and DNS servers set in Settings > Network or
  with `netsh` and kept across restarts), an HTTP/1.1 client, and Mbed TLS with the Mozilla root store.
  The loopback interface carries 127.0.0.1 and ::1 (and traffic to the
  machine's own addresses), and `localhost` resolves to both without DNS.
  Winsock (`ws2_32`, and `wsock32` with Winsock 1.1's ordinals) speaks IPv6
  and dual-stack sockets with `getaddrinfo`, and its socket options reach
  the TCP/IP stack (`TCP_NODELAY`, `SO_RCVTIMEO`/`SO_SNDTIMEO`,
  `SO_LINGER`, `SO_REUSEADDR`, `SO_KEEPALIVE`, `SO_BROADCAST`, `IP_TTL`,
  `SO_RCVBUF`/`SO_SNDBUF`, and `IPV6_V6ONLY`, on by default as on
  Windows, so `[::]` and `0.0.0.0` can share a port) and read back; a program keeps hundreds of
  sockets open at once, as a browser does, and `select`, `WSAPoll` and
  `WSAEventSelect` wait on any number of them; a UDP socket can be
  connected to a peer, and `WSADuplicateSocket` hands a socket to another
  process (Chromium's DNS client and its network process use both);
  overlapped requests that have to wait (an `AcceptEx`, a `ConnectEx`, a
  `WSARecv` with nothing to read yet) stay pending and complete on an I/O
  completion port, which proactor event loops such as Python's asyncio need;
  `winhttp` is a real HTTP client over Schannel TLS, with HTTP/2 by ALPN
  (nghttp2).
- **Windows userland** (`userland/`): about 35 system DLLs written from
  scratch and compiled with clang for `x86_64-pc-windows-msvc`, and again
  for `i686` in `SysWOW64`: `ntdll`, `kernel32`, `msvcrt`/`ucrtbase` with
  the `api-ms-win-crt-*` API sets (`errno` and the rest of the C
  runtime's per-thread state kept per thread, as on Windows, with
  `_configthreadlocale` per-thread locales and `getenv` results other
  threads cannot overwrite; `ntdll` gives fiber-local storage its own
  slots and runs `FlsAlloc` callbacks when a thread or the process ends;
  `TlsAlloc` has the 1024 expansion slots past the TEB's 64), `vcruntime140`/`vcruntime140_1` (C++
  exceptions, FH3 and FH4 tables), `msvcp140` and its satellites (the C++
  standard library: Microsoft's own STL, compiled with clang, with
  Boost.Math under `msvcp140_2`'s special math functions),
  `user32`/`gdi32` (a real window system, controls, menus, dialogs, MDI,
  hooks, per-monitor and per-thread DPI awareness with `WM_DPICHANGED`, controls and fonts at each window's DPI and rescaled when it changes, window coordinates converted between awareness contexts,
  touch, pens and the mouse as `WM_POINTER` messages with `GetPointerPenInfo`, title bars included, and the pen signature in `GetMessageExtraInfo`; `SetCursorPos` and `ClipCursor` for the window in front, as games recentre and confine the pointer), `gdiplus` (GDI+ on the MIT-licensed plutovg rasteriser),
  `comdlg32` (the Open and Save As dialogs, classic and `IFileDialog`),
  `comctl32`, `riched20`/`msftedit` (Rich Edit controls that take RTF,
  as setup programs' licence pages need), `shell32`, `ole32`/`oleaut32` (COM and OLE Automation with
  type libraries, and calls between processes over named pipes with the standard marshaler and the `IDispatch` proxy; the Windows Runtime's strings and the few runtime classes Win32 programs ask for, such as `UISettings` for the user's colours), `rpcrt4` (the NDR engine COM proxy/stub DLLs run on:
  stubless proxies, `NdrStubCall2`, `CStdStubBuffer`, the `NdrDll*` entry points), `advapi32`, `ws2_32`, `oleacc`,
  `winmm` (joysticks too: `joyGetPosEx` and `joyGetDevCaps` over NovaOS's
  game controllers, as SDL 1.2 games read pads) and `mmdevapi` (sound: `waveOut`, `waveIn`, `PlaySound`, MIDI,
  WASAPI playback and capture, a device ID or endpoint for each sound
  device, device 0 being the default as on Windows, each with its own
  endpoint volume; sound plays at its own rate and is converted once, by
  the mixer, to the device's), `dsound` (DirectSound, with
  every device enumerated and openable by its GUID),
  `xaudio2_7`/`xaudio2_8`/`xaudio2_9` and `x3daudio1_7` (on FAudio; every
  output listed and openable by its device ID), `msi`
  (with `msiscript` running JScript and VBScript custom actions on the
  ISC-licensed mujs; product, patch and source-list queries by
  installation context, as bootstrappers such as WiX Burn make them), `msxml6` (MSXML: the XML DOM with XPath, SAX and
  `XMLHTTP` on the MIT-licensed libxml2, answering the MSXML 3 classes
  such as `Msxml2.DOMDocument` too),
  `secur32` with Schannel (TLS 1.3/1.2 for programs, on Mbed TLS),
  `crypt32` and `wintrust` (certificate stores and chains, signed PKCS #7
  messages, and Authenticode: `WinVerifyTrust` checks a program's
  signature, its timestamp and its chain to the trusted roots, and the
  Microsoft root policy tells Microsoft's own signatures apart),
  `usp10` (Uniscribe), `normaliz` (IDN), `urlmon` (`CreateUri`, and the
  Internet security manager's zones),
  `wintab32` (Wintab pen tablets: pressure, tilt and barrel rotation for GTK,
  Qt and Krita), `cabinet` (the FDI functions installers extract cabinets
  with, MSZIP and LZX, on `msi`'s cabinet readers), `wldap32` (LDAP
  sessions, for programs that link to it; no directory servers yet),
  `opengl32` (Mesa's llvmpipe, OpenGL 4.5 on the CPU, built in; virgl on a
  3D virtio-gpu from the App Store; or with no driver NovaOS's own
  OpenGL 1.1, which draws 2D as ScummVM needs: textures, vertex arrays,
  blending, scissor), `ddraw` (DirectDraw for 32-bit programs: the
  MIT-licensed cnc-ddraw, every surface in memory and the screen drawn
  with Direct3D 9, OpenGL or GDI, in a display mode switched for the
  program), `dinput` (DirectInput 3 to 7, over the same devices as
  `dinput8`), `shfolder` (the old
  `SHGetFolderPath` DLL setup programs look for), `crtdll` (the NT 3
  C runtime, forwarding to `msvcrt`), `d3dcompiler_47` (the HLSL compiler: Wine's vkd3d-shader,
  LGPL-2.1, turns HLSL into Direct3D 9, 10, 11 and 12 (shader model 5.1, with
  descriptor arrays indexed at run time as wgpu's sampler heap needs, and
  RWByteAddressBuffer's Load, GetDimensions and Interlocked methods for its
  storage buffers, and stores to indexed groupshared arrays for its workgroup
  memory) bytecode
  for Chromium's ANGLE, wgpu and games, with `D3DReflect`, `D3DDisassemble` and the shader
  container parts), `dxgi`, `d3d11` and `dcomp` without a GPU (Windows'
  Microsoft Basic Render Driver adapter, a WARP device whose textures
  the CPU maps and copies, and DirectComposition showing its swap chains:
  what Chromium's software compositor draws WebView2's pages with, with
  DXVK installed too; with DXVK, also Chromium's GPU DirectComposition path:
  `DCompositionCreateDevice3`, surfaces and composition swap chains on the
  GPU device, read back for DirectComposition, through resizes, teardown
  and windows of another process), and
  the DLLs Firefox delay-loads (`d3d11`, `credui`, `winspool.drv`,
  `dhcpcsvc`), the ones Qt WebEngine imports
  (`bthprops.cpl`, `d3d12`, `winusb`: no Bluetooth radio, Direct3D 12
  device (until the App Store's VKD3D-Proton is installed, which `d3d12`
  then hands every call to) or WinUSB device, as on a PC without them, with Bluetooth's SDP
  record parsers working in full), and more.
- **Text**: `novatext.dll`, the text core built once and shared, carries
  HarfBuzz (shaping) and FreeType (fonts).  Uniscribe (`usp10`) itemizes
  text by script and direction and shapes it with HarfBuzz, and GDI's
  `ExtTextOut` sends complex scripts through it, as Windows' LPK does, so
  Arabic, Hebrew and the Indic scripts join, reorder and run right to left.
  Arabic and Devanagari draw with Noto Sans; GDI falls back to them by
  script.  DirectWrite (`dwrite`) lays text out on the same core, with
  font fallback: `tools/dwtest` draws Latin, Arabic and Devanagari in one
  line from a Latin-only font.  Its factory is an `IDWriteFactory3`, with
  Windows 10's font sets, font face references and `IDWriteFontFallback`,
  which Chromium's browsers (Steam's, WebView2) ask for
  (`tools/dw3test`).
- **2D drawing**: Direct2D (`d2d1.dll`) draws in software: geometries
  (rectangles, ellipses, paths with Béziers and arcs, groups, transforms,
  combining, widening, tessellation), strokes with caps, joins and dashes,
  solid, gradient and bitmap brushes, layers and clips, on HWND, DC and
  bitmap render targets.  Text goes through DirectWrite's text formats and
  layouts.
- **Media tools**: a current Windows build of ffmpeg runs unchanged and
  converts H.264 and AAC to VP9 and Opus with all its threads.
- **Media players and editors**: VLC plays video with sound and Audacity
  records and saves, on the same WASAPI, GDI and message-loop code paths
  real Windows programs take (Qt's `WH_GETMESSAGE` hook, wxWidgets'
  buffered painting).
- **ICU** as Windows 10 ships it: `icu.dll` (ICU 77.1, x64 and x86,
  built by `tools/build_icu.py`) with its data in
  `C:\Windows\Globalization\ICU`.  .NET does its globalization through
  it, and kernel32 answers `GetLocaleInfoEx` from it for every one of the
  864 Windows locales and formats dates, times, numbers and money in them
  (`GetDateFormat`, `GetTimeFormat`, `GetNumberFormat`,
  `GetCurrencyFormat`, `GetDurationFormat`) in their calendars (Japanese
  eras, Thai Buddhist, Hebrew, Hijri, Um Al Qura, Persian...).  The user's
  regional format is set with `intl NAME` or Settings > Time & language and
  kept in the registry, with the user's own changes to it
  (`SetLocaleInfo`).
- **Program support**: the PE loader with TLS, `DllMain`, forwarders and
  API sets; x64 and x86 structured exceptions, with Windows'
  alignment-fault fixup for misaligned SSE moves; Windows' segment
  selectors, so 64-bit programs can far-jump into 32-bit code; registry saved to disk;
  COM in-process and local (`LocalServer32`) servers, type libraries,
  proxy/stub DLLs and calls between processes; drag and drop; a shared clipboard; `.lnk`
  shortcuts; Windows Installer packages; services (`advapi32`'s service
  control manager); event tracing as Windows answers with no logging
  session running (providers register, controllers and consumers find no
  session); scheduled tasks (Task Scheduler 2.0, kept in
  `C:\Windows\System32\Tasks`); the Data Protection API.
- **Updates**: the App Store's Updates page downloads a newer NovaOS from
  an update channel signed with the release key (Ed25519), and the next
  restart starts it, going back to the old one if it does not start
  ([docs/updates.md](docs/updates.md)).
  Every green build of `main` is also an update, on its own channel.
- **Firefox**: bundled Mozilla Windows x64 browser, default for web links; NetSurf remains available.
- **Hyper-V**: a VMBus driver (hypercalls, the SynIC, channel offers, ring buffers) with Hyper-V's synthetic keyboard and mouse, the only input a Generation 2 VM has ([docs/hyperv.md](docs/hyperv.md)).

<!-- END generated:inside -->
