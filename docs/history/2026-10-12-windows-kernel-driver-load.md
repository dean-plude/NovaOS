## The first Windows kernel-mode driver

Step 30.1 of the Windows driver model (roadmap Phase 30,
[design/windows-drivers.md](design/windows-drivers.md)): NovaOS loads a
real Windows `.sys` file into its kernel, runs its `DriverEntry`, and lets
programs talk to it through Windows IRPs.

- **Kernel PE loader** (`kernel/nt/ldr.c`): a native-subsystem PE32+
  image from the service's `ImagePath`, mapped section by section with
  W^X protections into a 1 GiB driver range every process shares,
  relocated, bound to the kernel's exports, `/GS` cookie set.  A missing
  import is logged by name and the load fails.
- **`ntoskrnl.exe` and `hal.dll` exports** (`kernel/nt/exports.h`,
  `ntoskrnl.c`): the device, symbolic-link and IRP calls a first WDM driver
  makes, `DbgPrint` into the kernel log and the string helpers, all
  `ms_abi`, on Windows' exact x64 `DRIVER_OBJECT`, `DEVICE_OBJECT`, `IRP`,
  `IO_STACK_LOCATION` and `FILE_OBJECT` layouts (`kernel/nt/wdm.h`).
- **I/O manager** (`kernel/nt/io.c`): `\Device` names and `\DosDevices`
  links; `CreateFile("\\.\NAME")`, `ReadFile`, `WriteFile`,
  `DeviceIoControl` and `CloseHandle` become `IRP_MJ_CREATE`, `READ`,
  `WRITE`, `DEVICE_CONTROL`, `CLEANUP` and `CLOSE`, buffered and neither
  I/O, with completion routines.
- **`NtLoadDriver` and `NtUnloadDriver`**, behind `SeLoadDriverPrivilege`,
  and advapi32's service manager starting and stopping
  `SERVICE_KERNEL_DRIVER` services with them; a new **`sc`** (`create`,
  `start`, `stop`, `query`, `delete`).
- **kernel32 `\\.\NAME` and `DeviceIoControl`**: a `CreateFile` name such as `\\.\WdmTest` (no
  drive, volume or UNC path after the prefix) now reaches the kernel as
  `\??\WdmTest`, as on Windows, instead of a file in the current folder;
  `DeviceIoControl` passes every control code to the kernel, where it
  passed only video ones.
- **Test signing only, for now**: NovaOS checks no driver signatures until
  step 30.7, so drivers load only with `TestSigning` set under
  `HKLM\SYSTEM\CurrentControlSet\Control\NovaOS` and a restart; otherwise
  Windows' error 577.
- **`wdmtest.sys`** (`tools/drivertest/wdmtest.c`, clang and lld-link
  `/driver`, no WDK) and the devices suite's "wdm" boot: refused before
  test signing, then `sc create type= kernel`, `sc start`, `wdmtest`'s
  round trips and `sc stop`.
