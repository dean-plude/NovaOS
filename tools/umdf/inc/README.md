# tools/umdf/inc — the headers the UMDF 2 source is built with

Microsoft's published UMDF 2 source (`tools/umdf/build_umdf.py`) includes
headers from Microsoft's internal build that it does not ship: the
interfaces between the framework and its host process (`WUDFHost.exe`),
the loader, and the trace and telemetry headers.  The files here stand in
for them, or add what the KMDF build's headers (`tools/kmdf/inc`, also on
the include path) and mingw-w64's lack.  Every file is NovaOS's own,
written from the declarations the source uses and Microsoft's public
documentation; where the host interfaces' layout matters (the framework
calls them through vtables), it is the order the source calls them in.

## Build environment

| File | What it is |
|---|---|
| `novaos_umdf.h` | What the WDK's build defines for every framework file (`-include`d): user mode, x64, Windows 10, UMDF 2.33, NTSTATUS codes |
| `novaos_umdf_client.h` | The same for a UMDF driver (`umdftest.c`, `stub.c`) |

## The host and the loader (NovaOS's own interfaces)

| File | What it is |
|---|---|
| `novaos_wudf_host.h` | The COM interfaces the framework calls on its host (`IWudfHost2`, `IWudfDeviceStack`, `IWudfDevice`, `IWudfIrp`, `IWudfIoIrp`, `IWudfPnpIrp`, `IWudfFile`, ...) and those it implements for the host (`IFxMessageDispatch`); `tools/umdf/host` implements the host's side |
| `wudfx.h`, `wudfx_namespace_on.h`, `wudfx_namespace_off.h` | The UMDF 1 types the framework and the host still share, in namespace `UMINT` apart from UMDF 2's public types of the same names |
| `wdfplatform.h` | The platform interfaces the host hands the framework (`IUMDFPlatform`) |
| `novaos_wudf_loader.h` | How a driver binds to the framework: the loader interface the host passes to the driver's `FxDriverEntryUm`, which NovaOS's stub (`tools/umdf/stub.c`) uses.  Microsoft's stub binds through an interface it has not published, so a UMDF driver runs on NovaOS only when linked with this stub |
| `wdmdefs.h` | The kernel types user-mode framework code names that UMDF's public `wudfwdm.h` lacks |
| `FxObjectInfoDataUm.hpp` | The framework's object table for user mode |
| `wdfhid.h` | UMDF's HID DDIs |
| `usbbusif.h` | mingw-w64's `usbbusif.h` without its own `USB_BUS_INTERFACE_USBDI_GUID` (the source defines it) |

## Compiled out

| File | Why |
|---|---|
| `DriverFrameworks-UserMode-UmEvents.h`, `telemetry/MicrosoftTelemetryAssert.h` | NovaOS records no framework ETW events and sends no telemetry |
| `HostFxUtil.h` | The verifier checks (`FX_VERIFY`): a failed check ends the host process, as on Windows (`tools/umdf/rt/verify.cpp`) |
| `FxCompanion.hpp`, `FxTaskQueue.hpp` | Driver companions, which NovaOS's host does not offer |
| `FxProbeAndLock.h`, `wmium.h` | MDL probing and WMI, declared but never called in user mode |
| `debug.h`, `devreg.h`, `umdfstub.h`, `wdfplatformimpl.h` | Included by the source; nothing in them is used |

The runtime the framework links with (`tools/umdf/rt`) is the KMDF build's
(`tools/kmdf/rt`) for trace messages and binding, with the user-mode
pieces: the debug print and registry query (`rt.c`), the C++ runtime, the verifier, companions (refused) and COM's `IID_IUnknown`.
