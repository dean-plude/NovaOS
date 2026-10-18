# Anti-cheat and DRM: what they ask of the operating system

Roadmap step 24.7.  Games are the bar for NovaOS, and most popular
multiplayer games ship an anti-cheat, most paid ones a DRM.  Both work by
asking the operating system questions and refusing to run on the wrong
answers.  This note lists, per system, what it asks, what NovaOS answers
today and what is missing, so the store and gaming steps (24.1 to 24.6)
know what they can promise.

The one-line status of each system is also in the [compatibility
list](../compatibility.md#anti-cheat-and-drm-systems).

## The rule

Anti-cheat, DRM and code signing are **never bypassed, spoofed or patched
around**.  NovaOS does not hide that it is a virtual machine, fake a
Secure Boot state, forge a signature, hook the checks or edit the
program.  The faithful fix is always the same: provide the real Windows
facility the system checks (the system-call ABI, kernel callbacks, process
protection, the driver signing policy, measured boot), so that a system
which trusts Windows has a reason to trust NovaOS.  Where a facility needs
hardware NovaOS may not have (a TPM, Secure Boot), the answer is that the
game does not run on that machine, as on Windows.  A vendor that
refuses an operating system it does not know is the vendor's call, and
the note says so plainly.

## What these systems check

| Check | What it looks at | NovaOS today |
|---|---|---|
| System-call ABI | Raw `syscall` instructions with Windows 10 service numbers, bypassing ntdll | **Done.** ntdll carries a stub for all 464 Windows 10 1903 services in number order and the kernel decodes the number like Windows ([history](../history/2026-10-04-nt-syscall-table.md)) |
| Alignment, segment and context behaviour | Misaligned SSE fixups, the 32-bit code segment (`0x23`), self `NtGetContextThread` | **Done** (Windows' GDT layout, alignment fixup, self get/set context) |
| ntdll and kernel version | `RtlGetVersion`, the ntdll version resource, `KUSER_SHARED_DATA` | Partly: ntdll reports 10.0.18362.1, but `RtlGetVersion` says 19045 and the PEB/KUSER say 18362; these should agree |
| Module and process lists | `NtQuerySystemInformation` (modules, processes, handles) | Modules and processes: done.  `SystemHandleInformation` and the extended handle table: missing |
| Debugger and tamper checks | `ProcessDebugPort`, `ThreadHideFromDebugger`, `SystemKernelDebuggerInformation`, `IsDebuggerPresent` | Present as Windows answers them |
| Virtual machine detection | CPUID hypervisor bit and vendor leaf, SMBIOS, display adapter, device names | **Answered honestly.**  Under QEMU or Hyper-V NovaOS is a guest and says so.  Bare metal answers as hardware |
| Firmware and boot state | Secure Boot (`SystemSecureBootInformation`, UEFI variables), `SystemBootEnvironmentInformation`, test-signing flag | Test signing: done (`TESTSIGNING`).  Secure Boot state and UEFI variable reads: missing; NovaOS boots from its own loader without measured boot |
| TPM and attestation | TBS (`tbs.dll`), TPM 2.0 quotes, health attestation | Missing.  Needs a TPM driver (a Phase 30 package) and a TBS service |
| Code integrity | `SystemCodeIntegrityInformation`, signed-image checks, HVCI/memory integrity state | Signing policy: done for drivers (Phase 30.7).  The query and HVCI state: query constant only; HVCI cannot be offered without a hypervisor-enforced mode |
| Image trust | `WinVerifyTrust`, catalog and Authenticode verification, Microsoft roots | Done (wintrust, crypt32 roots) |
| Kernel callbacks | `PsSetCreateProcessNotifyRoutine`, `PsSetCreateThreadNotifyRoutine`, `PsSetLoadImageNotifyRoutine`, `ObRegisterCallbacks`, `CmRegisterCallback` | **Missing.**  The object manager has no callback lists; the driver host does not export these |
| Protected processes | PPL levels, `ProcessProtectionInformation`, `PsProtectedSigner*`, handle-access stripping | **Missing.**  No protection field on the process object |
| Kernel memory access | `MmCopyMemory`, physical-memory mapping from a driver, `MmGetSystemRoutineAddress` | Partly: drivers load through the Phase 30 NT interface; `MmCopyMemory` is not exported |
| Driver loading | A vendor `.sys` (EAC, BattlEye, Vanguard) installed as a service and started | Possible once signed: Phase 30 loads real packages under Windows signing policy; anti-cheat drivers also need the callbacks and protection above |
| Hardware ID | Disk serials, MAC, SMBIOS UUID, volume IDs for licence binding | Mostly present (SMBIOS table, IOCTL disk queries); values are real, never randomised |
| Time and clock | Tick drift, `QueryPerformanceCounter` vs `RDTSC`, time-skew checks | Present; under TCG emulation timings are slow, which some checks read as tampering |

## Per system

| System | Used by | What it needs | Status on NovaOS | Recommendation |
|---|---|---|---|---|
| **Hyperion** (Byfron) | Roblox | Raw syscalls, alignment fixup, 32-bit segment, thread context, SMBIOS and display reads | Passes all of those; then stops at "Virtual Machine detected" under QEMU, as it does on Windows in any VM.  Untested on bare metal | Test on the T14 (24.5).  Do not hide the VM.  If it passes there, Roblox is the first anti-cheat game |
| **Easy Anti-Cheat** (Epic) | Fortnite, Apex Legends, Rust, many others | User-mode module plus an `EasyAntiCheat_EOS.sys` kernel service in kernel mode: process and object callbacks, handle stripping, image-load notifications, PPL in some modes, driver signing | Cannot load: no kernel callbacks, no protection levels.  Some titles ship a "user-mode only" EAC that may run once the syscall ABI and process queries are enough | Build the callbacks and PPL (below).  Check per title which mode it ships; treat Linux/Proton-style user-mode EAC titles as the first candidates |
| **BattlEye** | PUBG, Rainbow Six Siege, Escape from Tarkov | `BEDaisy.sys` with process/object/thread callbacks, memory scanning, a signed kernel driver, hypervisor and VM checks | Cannot load for the same reasons; also strict on hypervisor presence | Same dependency.  BattlEye has opted Linux/Proton in per title; NovaOS gets no special treatment, so expect it last |
| **Vanguard** (Riot) | Valorant, League of Legends | Boot-time driver, TPM 2.0 and Secure Boot required, HVCI-aware, blocks VMs and vulnerable drivers | Not possible until Secure Boot, TPM and measured boot exist | Out of scope for 0.2.  Revisit with the T14 firmware and TPM work (Phase 23) |
| **FACEIT, ESEA** | CS2 leagues | Own kernel driver, Secure Boot and TPM, Windows 10/11 only | Same as Vanguard | Out of scope |
| **VAC** (Valve) | CS2, Dota 2, TF2 | User-mode scanner inside the game process; no kernel driver; bans by detection of known cheat modules | Should run once the game runs; no operating-system facility beyond the module and process lists | Allowed by the rule: do nothing special; keep module and process lists honest |
| **Denuvo Anti-Tamper** | Many single-player AAA games | Code virtualisation plus hardware ID binding and online activation; reads CPUID, SMBIOS, disk and MAC; sensitive to timing | Runs wherever the hardware answers are stable and real; slow under emulation | Keep hardware IDs real and stable across boots; measure performance on bare metal; do not randomise IDs |
| **Steam DRM** (`steam_api64.dll`, SteamStub) | Most Steam games | Steam client running, `steamclient64.dll` over local IPC, `SteamService` | SteamService runs; CEF/login window not working yet (24.1) | Finish 24.1; no OS change expected beyond the browser |
| **GOG Galaxy** | GOG games | Client sign-in; GOG games are DRM-free by default | Install of DRM-free offline installers works (corpus); Galaxy login blocked on a certificate permission | Finish 24.2; no OS change expected |
| **Epic Games Launcher** | Epic store | WebView2 GPU path, EOS overlay, EAC for some titles | Depends on 25.1 (WebView2 GPU); EAC titles as above | 24.6 after WebView2 sign-in |
| **Widevine / PlayReady** | Streaming in browsers and apps | Widevine L3 (software) works in a browser without any special OS state; L1 needs a TEE | L3 only; no L1 | Accept L3.  PlayReady SL3000 is out of reach without a TEE |
| **VMProtect, Themida, ASProtect** | Packers used by games and DRM | Pure user-mode virtualisation; reads CPUID, timing, debugger flags | Runs where syscall ABI and CPUID are faithful | No work beyond staying faithful |
| **EOS (Epic Online Services) anti-cheat client** | Smaller titles | A user-mode build of EAC that skips the driver | Candidate for the first EAC games | Test a title with it once 24.6 lands |

## The plan, in order

1. **Keep what is done.**  The Windows-numbered syscall table, GDT,
   alignment fixup and thread context are what every system above
   relies on first.  Their tests (`syscalltest`, `gatetest`,
   `aligntest`) stay in the self-test suite.
2. **Make the answers agree.**  One Windows build number across
   `RtlGetVersion`, the PEB and `KUSER_SHARED_DATA`; add
   `SystemHandleInformation` and its extended form.
3. **Kernel callbacks** (new step, prerequisite for EAC and BattlEye):
   process, thread and image-load notification lists in the process
   manager, `ObRegisterCallbacks` on the object manager (pre/post
   operations that can strip access bits), `CmRegisterCallback` on the
   registry, exported through the Phase 30 driver interface with the
   Windows prototypes.
4. **Process protection levels:** a protection field on the process
   object (`PS_PROTECTION`), enforced at handle open and thread
   injection, reported through `ProcessProtectionInformation`.
5. **Memory access for drivers:** `MmCopyMemory`, `MmMapIoSpace` and the
   routines anti-cheat drivers use to read a game process, with the
   same safety checks as Windows.
6. **Secure Boot and TPM** (Phase 23 follow-up): read the firmware's
   Secure Boot state and UEFI variables, add a TPM 2.0 driver package
   and TBS.  Needed by Vanguard and FACEIT; not needed by Steam, GOG,
   Hyperion or user-mode EAC.
7. **Test on the T14** with Roblox (24.5), then pick the first
   anti-cheat title per system in the table.

A step ships only when a real system accepts it; none ships a switch that
makes a check pass without the underlying behaviour.

## Where each fact is checked

- The syscall, segment and alignment work is in
  [the NT syscall table history entry](../history/2026-10-04-nt-syscall-table.md)
  and [Roblox and anti-cheat](../compatibility.md#roblox-and-anti-cheat).
- The driver signing policy and what stays failing is in
  [Windows drivers](windows-drivers.md#signing-policy).
- The callbacks, protection levels, Secure Boot and TPM rows were
  checked by searching `kernel/` and `userland/` for the Windows routine
  names; none were found, so they are listed as missing.
