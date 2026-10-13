/*
 * verify.cpp — the user-mode framework's verifier stops and HRESULT mapping
 * (tools/umdf/inc/HostFxUtil.h), from Microsoft's host utility library,
 * which was not published
 *
 * A failed FX_VERIFY is a driver or framework bug: Windows' host reports it
 * and ends the host process, and the reflector fails the device.  NovaOS's
 * does the same: the check goes to the debug output and the process exits
 * with STATUS_ASSERTION_FAILURE.
 */
#include <windows.h>
#include <stdio.h>
#include "wdmdefs.h"
#include "HostFxUtil.h"

extern "C" NTSTATUS WinErrorToNtStatus(ULONG WinError);   // (shared/primitives/um/errtostatus.cpp)

extern "C" DECLSPEC_NORETURN void __cdecl NovaUmdfVerifyFailed(const char *Kind, const char *Check, const char *Name,
                                                               const char *File, int Line)
{
    char msg[512];
    _snprintf(msg, sizeof(msg) - 1, "Wudfx02000: verifier stop %s: %s%s%s failed at %s:%d\n", Kind, Check,
              Name ? " for " : "", Name ? Name : "", File, Line);
    msg[sizeof(msg) - 1] = 0;
    OutputDebugStringA(msg);
    TerminateProcess(GetCurrentProcess(), (UINT)STATUS_ASSERTION_FAILURE);
    for (;;) {
    }
}

extern "C" VOID UfxVerifierTrapNotImpl(VOID)
{
    NovaUmdfVerifyFailed("INTERNAL", "a framework path UMDF does not implement", NULL, __FILE__, __LINE__);
}

/* An HRESULT the host returned, as the driver sees it: an NTSTATUS the host
 * wrapped (HRESULT_FROM_NT) unwrapped, a Win32 error through the framework's
 * own Win32-to-NTSTATUS table, and the COM errors a host returns by name.
 * (The driver's version and the compatibility flag choose among older
 * mappings on Windows; NovaOS has one.) */
NTSTATUS CHostFxUtil::NtStatusFromHr(HRESULT hr, ULONG DriverMajorVersion, ULONG DriverMinorVersion,
                                     BOOL PreserveCompatibility)
{
    (void)DriverMajorVersion; (void)DriverMinorVersion; (void)PreserveCompatibility;
    if (SUCCEEDED(hr)) {
        return STATUS_SUCCESS;
    }
    if (hr & FACILITY_NT_BIT) {
        return (NTSTATUS)(hr & ~FACILITY_NT_BIT);
    }
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32) {
        return WinErrorToNtStatus(HRESULT_CODE(hr));
    }
    switch (hr) {
    case E_OUTOFMEMORY:  return STATUS_INSUFFICIENT_RESOURCES;
    case E_INVALIDARG:   return STATUS_INVALID_PARAMETER;
    case E_POINTER:      return STATUS_INVALID_PARAMETER;
    case E_NOTIMPL:      return STATUS_NOT_IMPLEMENTED;
    case E_NOINTERFACE:  return STATUS_NOT_SUPPORTED;
    case E_ACCESSDENIED: return STATUS_ACCESS_DENIED;
    default:             return STATUS_UNSUCCESSFUL;
    }
}
