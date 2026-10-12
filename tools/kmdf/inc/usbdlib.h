/*
 * usbdlib.h — mingw-w64's usbdlib.h plus the Windows 8 USBD handle API
 *
 * The framework's USB targets register with the USB stack through
 * USBDEX.lib (USBD_CreateHandle and friends), which the WDK links into
 * every client.  NovaOS's stand-ins live in tools/kmdf/usbdex.c; the
 * declarations follow Microsoft's documentation.
 */
#pragma once
#include_next <usbdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

DECLARE_HANDLE(USBD_HANDLE);

#define USBD_CLIENT_CONTRACT_VERSION_INVALID  0xFFFFFFFF
#define USBD_CLIENT_CONTRACT_VERSION_602      0x602

#ifndef USBD_PF_SSP_HIGH_BANDWIDTH_ISOCH
#define USBD_PF_SSP_HIGH_BANDWIDTH_ISOCH        0x00000010
#endif
#ifndef USBD_PF_HANDLES_SSP_HIGH_BANDWIDTH_ISOCH
#define USBD_PF_HANDLES_SSP_HIGH_BANDWIDTH_ISOCH 0x00000020
#endif

extern const GUID GUID_USB_CAPABILITY_SSP_ISOCH_PIPE_FLAGS;

NTSTATUS USBD_CreateHandle(_In_ PDEVICE_OBJECT DeviceObject, _In_ PDEVICE_OBJECT TargetDeviceObject,
                           _In_ ULONG USBDClientContractVersion, _In_ ULONG PoolTag, _Out_ USBD_HANDLE *USBDHandle);
VOID USBD_CloseHandle(_In_ USBD_HANDLE USBDHandle);
NTSTATUS USBD_QueryUsbCapability(_In_ USBD_HANDLE USBDHandle, _In_ const GUID *CapabilityType,
                                 _In_ ULONG OutputBufferLength, _Inout_opt_ PUCHAR OutputBuffer,
                                 _Out_opt_ PULONG ResultLength);
NTSTATUS USBD_UrbAllocate(_In_ USBD_HANDLE USBDHandle, _Outptr_ PURB *Urb);
NTSTATUS USBD_IsochUrbAllocate(_In_ USBD_HANDLE USBDHandle, _In_ ULONG NumberOfIsochPacket, _Outptr_ PURB *Urb);
VOID USBD_UrbFree(_In_ USBD_HANDLE USBDHandle, _In_ PURB Urb);
VOID USBD_AssignUrbToIoStackLocation(_In_ USBD_HANDLE USBDHandle, _In_ PIO_STACK_LOCATION IoStackLocation, _In_ PURB Urb);

#ifdef __cplusplus
}
#endif
