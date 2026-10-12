# tools/kmdf/inc — the WDK headers the KMDF source is built with

Microsoft's published KMDF source (`tools/kmdf/build_kmdf.py`) includes
headers from the Windows Driver Kit and from Microsoft's internal build,
neither of which it ships.  mingw-w64's DDK headers (public domain, from
the `mingw-w64-common` package) stand in for the WDK's; the files here add
what they lack, or replace an internal header with what the framework
needs from it.  Every file is NovaOS's own, written from the declarations
the source uses and Microsoft's public documentation.  `build_kmdf.py`
also compiles against a copy of mingw-w64's `wdm.h` with a few structures
brought up to Windows 10 (its `WDM_EDITS`).

## Build environment

| File | What it is |
|---|---|
| `novaos_kmdf.h` | What the WDK's build defines for every framework file (`-include`d): kernel mode, x64, Windows 10, the KMDF version, tracing through `DoTraceLevelMessage` |
| `novaos_client.h` | The same for a KMDF client driver (`kmdftest.c`, `stub.c`): x64, Windows 10, KMDF 1.33 |
| `novaos_sal.h` | The source annotations (SAL), each empty, as for the compiler |
| `novaos_wpp.h` | What WPP's preprocessor writes into a `.tmh` file: every `.tmh` the source includes is this (`tools/kmdf/rt/wpp.c` prints the messages) |

## WDK headers mingw-w64 has, completed

| File | What it adds |
|---|---|
| `wdm.h` | mingw-w64's `wdm.h`, then `novaos_wdm_ext.h` |
| `novaos_wdm_ext.h` | Windows 8 and 10 declarations: `ExAllocatePool2`, the Ex timers' flags, `IoOpenDriverRegistryKey`, `IoQueryFullDriverPath`, activity IDs, `RtlQueryRegistryValuesEx`, the power framework (PoFx v1 to v3), D3cold, `WDF_VIOLATION`, ... |
| `novaos_device_description.h` | `DEVICE_DESCRIPTION` version 3 (replaces mingw-w64's in the `wdm.h` copy) |
| `novaos_dma_operations.h` | `DMA_OPERATIONS` as of Windows 10, `DMA_TRANSFER_INFO` (likewise) |
| `ntstrsafe.h` | `RtlUnicodeStringPrintf`, `RtlUnicodeStringCopy` and the other `UNICODE_STRING` routines |
| `usbdlib.h`, `usbdrivr.h` | The Windows 8 USBD handle API (`USBD_CreateHandle`, ...); `usbdrivr.h` so mingw-w64's includes this `usbdlib.h` |
| `aux_klib.h` | `aux_klib.lib`'s module list (`tools/kmdf/rt/auxklib.c`) |
| `procgrp.h` | `procgrp.lib`'s initialisation (nothing to do on Windows 10) |

## Internal headers, as far as the framework uses them

| File | What it is |
|---|---|
| `wdfldr.h` | The KMDF loader's routines (`tools/kmdf/wdfldr.c` implements them) |
| `wdfcx.h`, `wdfcxbase.h` | Class extension declarations the framework compiles against (class extensions do not load yet) |
| `wdfdevicepri.h`, `wdfiotargetpri.h` | The framework's private device and I/O target DDIs |
| `FxCompanion.hpp`, `FxCompanionLibrary.hpp`, `FxCompanionTarget.hpp` | Driver companions (UMDF companions of a KMDF driver): not available, every companion call fails |
| `FxDsf.h` | The Device Simulation Framework interface (never present) |
| `MxWnf.h` | Windows Notification Facility subscriptions: not available |
| `SleepStudyHelper.h` | Sleep Study: not available |
| `WdfVersionLog.h` | The framework's event-log message IDs |
| `DriverFrameworks-KernelMode-KmEvents.h` | The framework's ETW events, compiled out |
| `ntwmi.h` | The kernel logger's WDF performance hooks |
| `traceloggingprovider.h`, `winmeta.h`, `telemetry/` | TraceLogging and telemetry, compiled out (NovaOS sends none) |
| `etwkmcallbacks.h`, `cobbled.hpp`, `WudfRdNonPnP.hpp`, `FeatureStaging*.h`, `wil/staging.h`, `wdmdefs.h` | Included but nothing in them is used: empty |
