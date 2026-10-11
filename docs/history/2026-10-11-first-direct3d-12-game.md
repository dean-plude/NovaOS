## A Direct3D 12 game: Squash the Creeps on Godot

The last part of the gaming roadmap's Direct3D 12 item (After 0.1, step
24.4).  The game is Squash the Creeps, the finished game of Godot's "Your
first 3D game" tutorial (MIT, from Godot's demo projects), run by the
official Godot 4.7 Windows build (MIT) with `--rendering-driver d3d12`.
Godot was picked because it is free, open source, needs no store or
sign-in, and its Direct3D 12 driver turns its shaders into DXIL itself, so
nothing but `d3d12.dll` (VKD3D-Proton, on Mesa 3D's lavapipe) and DXVK's
`dxgi.dll` stands between it and the screen.

What NovaOS lacked, in the order Godot hit it:

- **shell32 `StrRStrIW`**: Godot's console launcher
  (`Godot_..._console.exe`) finds its own name's `_console.exe` with it.
- **CreateProcess with an unquoted path with spaces**: the launcher starts
  the game as `C:\Apps\Squash the Creeps\Godot....exe ARGS`, unquoted, with
  no program name.  NovaOS took `C:\Apps\Squash` as the program; it now
  tries the path at each space in turn, as Windows does.
- **crypt32 `CryptBinaryToStringA` with `CRYPT_STRING_BASE64HEADER`**
  (PEM, with `CRYPT_STRING_NOCR`): Godot exports the root certificate
  store that way at start, and its loop never moved to the next
  certificate when the call failed, so it printed the same error forever.
- **Job object notifications**: the launcher puts the game in a job with a
  completion port and waits for `JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO`.
  Jobs were stubs; kernel32 now watches the processes assigned to a job
  and posts `NEW_PROCESS`, `EXIT_PROCESS` and `ACTIVE_PROCESS_ZERO` to its
  port (processes they start themselves are not added).
- **uiautomationcore `UiaLookupId`** (Godot's accessibility layer looks up
  newer property GUIDs at start; each GUID gets its own number, as no UI
  Automation client reads them on NovaOS), and the imports Godot names but
  had not called yet: ole32 `GetErrorInfo`/`SetErrorInfo` (handed to
  oleaut32) and dbghelp `ImageDirectoryEntryToData(Ex)`/`ImageRvaToVa`.

- **ntdll `__chkstk` and the half-float helpers** (`__extendhfsf2`,
  `__truncsfhf2` and their double forms): lavapipe's LLVM compiles Godot's
  shaders to machine code and links the helpers it calls by name from the
  loaded DLLs, as Windows' ntdll exports them.  Without F16C (NovaOS's
  test machine has none) every half-precision conversion is such a call,
  and an unresolved one jumped to address 0.
- **Mesa 3D 26.2.0 in the App Store**: lavapipe in Mesa 24.2.4 (LLVM 19)
  crashed inside its own compiler on Godot's shaders; 26.2.0 (LLVM 22)
  compiles them.  The built-in OpenGL stays on 24.2.4.

Godot's default Forward+ renderer now plays the game on Direct3D 12
("D3D12 11_0 - Forward+ - llvmpipe (LLVM 22.1.8)").  Lavapipe still refuses
two of its compute pipelines (`vkCreateComputePipelines` returns
`VK_ERROR_UNKNOWN`); the game draws without them.

- **Test**: the app corpus's `Squash the Creeps` installs Mesa 3D, DXVK and
  VKD3D-Proton from the App Store, imports the game's assets, starts it on
  Direct3D 12 and waits for its field to fill the window; Godot's log must
  name D3D12.  `cmdlinetest spawn` checks the unquoted path and the job's
  completion port.
