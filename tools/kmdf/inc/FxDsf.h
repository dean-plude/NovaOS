/*
 * FxDsf.h — the Device Simulation Framework interface
 *
 * DSF is Microsoft's test harness that simulates USB hardware under a
 * driver; when a driver's WDF registry key sets DsfOn, the framework asks
 * the lower stack for this interface and routes interrupt connection
 * through it.  NovaOS has no DSF, so no stack answers the query and the
 * framework uses the kernel's routines, as it does on Windows without DSF.
 */
#pragma once

/* (NovaOS's identifier for the interface: nothing on NovaOS exposes it) */
DEFINE_GUID(GUID_WDF_DSF_INTERFACE, 0x8ab2e5a3, 0x1c07, 0x4b1e, 0x9c, 0x40, 0x4e, 0x6f, 0x76, 0x61, 0x44, 0x53);

#define WDM_DSF_INTERFACE_V1_0 0x00010000

typedef struct _WDF_DSF_INTERFACE {
    INTERFACE Interface;
    PFN_IO_CONNECT_INTERRUPT_EX IoConnectInterruptEx;
    PFN_IO_DISCONNECT_INTERRUPT_EX IoDisconnectInterruptEx;
    PFN_IO_REPORT_INTERRUPT_ACTIVE IoReportInterruptActive;
    PFN_IO_REPORT_INTERRUPT_INACTIVE IoReportInterruptInactive;
} WDF_DSF_INTERFACE, *PWDF_DSF_INTERFACE;
