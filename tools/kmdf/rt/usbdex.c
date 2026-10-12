/*
 * usbdex.c — the WDK's USBD client library (usbdex.lib), the routines the
 * framework's USB targets link
 *
 * A USBD handle is the client's registration with the USB driver stack;
 * URBs the client allocates through it can carry the stack's extensions.
 * NovaOS's USB driver stack (Phase 30.8) reports no capabilities yet, so a
 * handle records only its two devices and URBs are plain pool allocations,
 * the form every USB stack accepts.
 */
#include <ntddk.h>
#include <usb.h>
#include "usbdlib.h"

#define USBD_TAG 'xDBU'

/* (NovaOS's identifier: Microsoft's usbdlib.h is not published with the
 * framework, and no NovaOS USB stack reports this capability) */
const GUID GUID_USB_CAPABILITY_SSP_ISOCH_PIPE_FLAGS =
    { 0x98f3ac3a, 0xa0a6, 0x4a87, { 0x9f, 0x34, 0x5b, 0xed, 0x0c, 0xbe, 0xa5, 0xd6 } };

typedef struct _USBD_CLIENT {
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT TargetDeviceObject;
    ULONG ContractVersion;
    ULONG PoolTag;
} USBD_CLIENT;

NTSTATUS USBD_CreateHandle(PDEVICE_OBJECT DeviceObject, PDEVICE_OBJECT TargetDeviceObject, ULONG USBDClientContractVersion,
                           ULONG PoolTag, USBD_HANDLE *USBDHandle)
{
    USBD_CLIENT *c;

    if (DeviceObject == NULL || TargetDeviceObject == NULL || USBDHandle == NULL ||
        USBDClientContractVersion == USBD_CLIENT_CONTRACT_VERSION_INVALID) {
        return STATUS_INVALID_PARAMETER;
    }
    c = (USBD_CLIENT *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), USBD_TAG);
    if (c == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    c->DeviceObject = DeviceObject;
    c->TargetDeviceObject = TargetDeviceObject;
    c->ContractVersion = USBDClientContractVersion;
    c->PoolTag = PoolTag;
    *USBDHandle = (USBD_HANDLE)c;
    return STATUS_SUCCESS;
}

VOID USBD_CloseHandle(USBD_HANDLE USBDHandle)
{
    if (USBDHandle != NULL) {
        ExFreePoolWithTag(USBDHandle, USBD_TAG);
    }
}

NTSTATUS USBD_QueryUsbCapability(USBD_HANDLE USBDHandle, const GUID *CapabilityType, ULONG OutputBufferLength,
                                 PUCHAR OutputBuffer, PULONG ResultLength)
{
    (void)USBDHandle;
    (void)CapabilityType;
    (void)OutputBufferLength;
    (void)OutputBuffer;
    if (ResultLength != NULL) {
        *ResultLength = 0;
    }
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS urb_alloc(USBD_HANDLE USBDHandle, SIZE_T Size, PURB *Urb)
{
    USBD_CLIENT *c = (USBD_CLIENT *)USBDHandle;

    if (c == NULL || Urb == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Urb = (PURB)ExAllocatePool2(POOL_FLAG_NON_PAGED, Size, c->PoolTag ? c->PoolTag : USBD_TAG);
    return *Urb != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS USBD_UrbAllocate(USBD_HANDLE USBDHandle, PURB *Urb)
{
    return urb_alloc(USBDHandle, sizeof(URB), Urb);
}

NTSTATUS USBD_IsochUrbAllocate(USBD_HANDLE USBDHandle, ULONG NumberOfIsochPacket, PURB *Urb)
{
    if (NumberOfIsochPacket == 0 || NumberOfIsochPacket > 1024) {
        return STATUS_INVALID_PARAMETER;
    }
    return urb_alloc(USBDHandle, GET_ISO_URB_SIZE(NumberOfIsochPacket), Urb);
}

VOID USBD_UrbFree(USBD_HANDLE USBDHandle, PURB Urb)
{
    (void)USBDHandle;
    if (Urb != NULL) {
        ExFreePool(Urb);
    }
}

VOID USBD_AssignUrbToIoStackLocation(USBD_HANDLE USBDHandle, PIO_STACK_LOCATION IoStackLocation, PURB Urb)
{
    (void)USBDHandle;
    IoStackLocation->Parameters.Others.Argument1 = Urb;
}
