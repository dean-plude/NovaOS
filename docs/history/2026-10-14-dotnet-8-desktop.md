## .NET 8 desktop programs start

Roadmap step 27.1 (grow the corpus) tried HandBrake 1.9.0, the App
Store's video transcoder, a single-file WPF program on .NET 8.  It does
not reach its window yet, but the way there fixed .NET 8 itself:

- **The App Store's .NET 8.**  Microsoft's Desktop Runtime zip holds only
  `Microsoft.WindowsDesktop.App`: no `dotnet.exe`, host or base runtime,
  so the Store's ".NET Desktop Runtime 8" installed a runtime nothing could
  start (and its install check, `dotnet\dotnet.exe`, never matched).  A new
  ".NET Runtime 8" entry brings the base runtime; both unpack into
  `C:\Programs\dotnet`, where a .NET program's launcher looks, and both are
  pinned to 8.0.32 instead of the moving `aka.ms` link.
- **The JIT starts.**  .NET 8's `clrjit.dll` imports the UCRT's
  `_copysignf` and `_isnanf`, now exported, and hands
  `WideCharToMultiByte` a NULL string; `WideCharToMultiByte` and
  `MultiByteToWideChar` now check their arguments as Windows does
  (`ERROR_INVALID_PARAMETER`) instead of faulting.
- **Placeholders.**  A single-file program's assemblies sit inside its
  `.exe`; coreclr maps each one section by section into a reserved range:
  `VirtualAlloc2` with `MEM_RESERVE_PLACEHOLDER`, `VirtualFree` with
  `MEM_PRESERVE_PLACEHOLDER` to split it, `MapViewOfFile3` and
  `VirtualAlloc2` with `MEM_REPLACE_PLACEHOLDER` to fill each piece at a
  page (not 64 KiB) boundary.  The kernel now keeps placeholder regions,
  and a `PAGE_WRITECOPY` view (or `VirtualProtect` to write-copy) gives the
  process its own copies of the pages, so writes reach neither the section
  nor the file.
- **The exception chain.**  ntdll's dispatcher keeps a chain of the
  exceptions being handled on the stack; coreclr's managed exceptions leave
  entries behind and reuse their stack, and the dispatcher followed a
  stale entry's link into garbage, faulting again on every exception.  An
  entry is now checked before its link is followed.
- **What WPF imports.**  `ntdll` gains the interlocked list functions
  (`RtlInterlockedPushEntrySList` and the rest; `kernel32`'s now call
  them, so a list one DLL pushes and another flushes shares one lock), bit
  maps (`RtlInitializeBitMap`, `RtlFindClearBitsAndSet`, ...) and
  `DbgPrint`/`DbgPrintEx`/`DbgPrompt`/`DbgBreakPoint`; `advapi32` gains
  `TraceMessage`, `gdi32` `GdiEntry13` (the display settings uniqueness
  that `wpfgfx` calls unchecked) and `urlmon` `FindMimeFromData`.

`apitest` checks the placeholders, write-copy views, the lists, bit maps,
the code page checks, the UCRT helpers and MIME sniffing.  HandBrake now
runs its managed start-up and loads `hb.dll`, then stops where WPF loads
its first image: NovaOS has no Windows Imaging Component
(`WindowsCodecs.dll` exports only stubs), so HandBrake is rated Borked
until WIC exists.
