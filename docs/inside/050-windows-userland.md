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
    Windows Runtime's strings and activation, in-process runtime classes
    found through the registry's `ActivatableClassId` keys) and `rpcrt4`
    (the NDR engine proxy/stub DLLs run on).
  - **UWP app model**: `twinapi.appcore` (`CoreApplication.Run` driving an
    app's `IFrameworkView`, its `CoreWindow` with keyboard, pointer
    (`PointerPoint`), size, visibility and close events, and the
    `CoreDispatcher`; DXGI swap chains present into a CoreWindow) and
    `Windows.UI.Composition` in `dcomp` (the `Compositor`, container and
    sprite visuals, colour and surface brushes, drawing surfaces on
    Direct3D 11, targets on a window or a CoreWindow), the first steps
    towards `Windows.UI.Xaml` ([design](../design/windows-ui-xaml.md)).
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
