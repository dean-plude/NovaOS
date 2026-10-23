program: **Visual C++ Redistributable** (App Store)
kind: runtime
rating: Platinum
works: NovaOS has its own `vcruntime140` and `msvcp140`, so most programs do not need it, but GOG GALAXY needs its MFC.  Its installer (WiX Burn) installs the Minimum and Additional Runtimes, the 32-bit one into `SysWOW64` and the 64-bit one into `System32` (14.44 x86 and 14.51 x64 checked), replacing NovaOS's own copies there.  Run by hand, its licence window comes up as on Windows (14.44 x64 checked)
problems: —
checked: by hand
