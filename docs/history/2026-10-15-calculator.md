## Calculator

Dean asked for Windows Calculator (github.com/microsoft/calculator).  The
app Microsoft ships from that repository is a UWP package (C# view models
and XAML on WinUI 2) that NovaOS cannot run yet, but its calculation
engine, CalcManager, is portable C++ under the MIT licence.  It is now
vendored unchanged in `third_party/calcmanager` and built into
`calc.exe`, a Windows program in `C:\Windows\System32` that draws the
Standard, Scientific and Programmer calculators of Windows 11 with the
GDI.  So the answers are Windows Calculator's own: Ratpack's
arbitrary-precision rational arithmetic, its operator precedence and
error messages, the expression line, memory, the four radixes and word
sizes.  It is in the Start menu as "Calculator" and runs as `calc`.

- **Programs from several files**: a program's `NAME.json` takes
  `sources` (more `.cpp` files, e.g. a vendored library), `include` and
  `force_include` (the forced precompiled header Visual Studio projects
  use), so CalcManager compiles as its `.vcxproj` builds it.
- **Windows SDK headers**: NovaOS's C++ headers gained what CalcManager's
  precompiled header needs and MinGW-w64's lack: `E_BOUNDS`,
  `_Frees_ptr_opt_`, and an `<intsafe.h>` that brings only the base
  types (MinGW-w64's includes all of `<windows.h>` and OLE, whose
  `DECIMAL` type collided with Ratpack's `DECIMAL` constant).

The OS test `calc` starts Calculator from the Start menu and types
expressions into each calculator: 2+3*4 is 20 in Standard and 14 in
Scientific, 25! comes out exact, and 255 is FF in Programmer.  Still to
do: the converters (CalcManager's unit converter needs the Concurrency
Runtime's tasks) and the real UWP package (WinRT activation and XAML).
