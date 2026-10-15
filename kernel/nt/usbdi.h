/*
 * usbdi.h — the USB driver interface as the WDK lays it out on x64
 * (usb.h, usbioctl.h): the URBs a USB client driver sends its device's
 * PDO in IRP_MJ_INTERNAL_DEVICE_CONTROL, and the IOCTLs that carry them.
 * usbd.c (the hub side) and hidusb.c (a client) share them.
 */
#pragma once
#include "wdm.h"

#define URB_FUNCTION_SELECT_CONFIGURATION          0x0000
#define URB_FUNCTION_SELECT_INTERFACE              0x0001
#define URB_FUNCTION_ABORT_PIPE                    0x0002
#define URB_FUNCTION_GET_CURRENT_FRAME_NUMBER      0x0007
#define URB_FUNCTION_CONTROL_TRANSFER              0x0008
#define URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER    0x0009
#define URB_FUNCTION_ISOCH_TRANSFER                0x000A
#define URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE    0x000B
#define URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE      0x000C
#define URB_FUNCTION_SET_FEATURE_TO_DEVICE         0x000D
#define URB_FUNCTION_SET_FEATURE_TO_INTERFACE      0x000E
#define URB_FUNCTION_SET_FEATURE_TO_ENDPOINT       0x000F
#define URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE       0x0010
#define URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE    0x0011
#define URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT     0x0012
#define URB_FUNCTION_GET_STATUS_FROM_DEVICE        0x0013
#define URB_FUNCTION_GET_STATUS_FROM_INTERFACE     0x0014
#define URB_FUNCTION_GET_STATUS_FROM_ENDPOINT      0x0015
#define URB_FUNCTION_VENDOR_DEVICE                 0x0017
#define URB_FUNCTION_VENDOR_INTERFACE              0x0018
#define URB_FUNCTION_VENDOR_ENDPOINT               0x0019
#define URB_FUNCTION_CLASS_DEVICE                  0x001A
#define URB_FUNCTION_CLASS_INTERFACE               0x001B
#define URB_FUNCTION_CLASS_ENDPOINT                0x001C
#define URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL 0x001E
#define URB_FUNCTION_CLASS_OTHER                   0x001F
#define URB_FUNCTION_VENDOR_OTHER                  0x0020
#define URB_FUNCTION_GET_STATUS_FROM_OTHER         0x0021
#define URB_FUNCTION_CLEAR_FEATURE_TO_OTHER        0x0022
#define URB_FUNCTION_SET_FEATURE_TO_OTHER          0x0023
#define URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT  0x0024
#define URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT    0x0025
#define URB_FUNCTION_GET_CONFIGURATION             0x0026
#define URB_FUNCTION_GET_INTERFACE                 0x0027
#define URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE 0x0028
#define URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE   0x0029
#define URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR     0x002A
#define URB_FUNCTION_SYNC_RESET_PIPE               0x0030
#define URB_FUNCTION_SYNC_CLEAR_STALL              0x0031
#define URB_FUNCTION_CONTROL_TRANSFER_EX           0x0032

#define USBD_TRANSFER_DIRECTION_IN   0x01
#define USBD_SHORT_TRANSFER_OK       0x02
#define USBD_START_ISO_TRANSFER_ASAP 0x04
#define USBD_DEFAULT_PIPE_TRANSFER   0x08

#define USBD_STATUS_SUCCESS                 0x00000000
#define USBD_STATUS_PENDING                 0x40000000
#define USBD_STATUS_STALL_PID               0xC0000004
#define USBD_STATUS_DEV_NOT_RESPONDING      0xC0000005
#define USBD_STATUS_INVALID_URB_FUNCTION    0x80000200
#define USBD_STATUS_INVALID_PARAMETER       0x80000300
#define USBD_STATUS_INVALID_PIPE_HANDLE     0x80000600
#define USBD_STATUS_NOT_SUPPORTED           0xC0000E00
#define USBD_STATUS_CANCELED                0xC0010000
#define USBD_STATUS_DEVICE_GONE             0xC0007000
#define USBD_STATUS_ISO_NOT_ACCESSED_BY_HW  0xC0020000
#define USBD_STATUS_ISOCH_REQUEST_FAILED    0xC0000B00

#define IOCTL_INTERNAL_USB_SUBMIT_URB             0x00220003
#define IOCTL_INTERNAL_USB_RESET_PORT             0x00220007
#define IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO        0x0022000F
#define IOCTL_INTERNAL_USB_GET_PORT_STATUS        0x00220013
#define IOCTL_INTERNAL_USB_ENABLE_PORT            0x00220017
#define IOCTL_INTERNAL_USB_GET_HUB_COUNT          0x0022001B
#define IOCTL_INTERNAL_USB_CYCLE_PORT             0x0022001F
#define IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION 0x00220027
#define IOCTL_INTERNAL_USB_GET_DEVICE_CONFIG_INFO 0x0022044F
#define USBD_PORT_ENABLED    1
#define USBD_PORT_CONNECTED  2

typedef struct {                       /* _URB_HEADER */
    UINT16 Length, Function;
    UINT32 Status;
    void  *UsbdDeviceHandle;
    UINT32 UsbdFlags;
} UrbHeader;

typedef struct {                       /* USBD_PIPE_INFORMATION */
    UINT16 MaximumPacketSize;
    UINT8  EndpointAddress, Interval;
    int    PipeType;                   /* UsbdPipeTypeControl 0, Isochronous 1, Bulk 2, Interrupt 3 */
    void  *PipeHandle;
    UINT32 MaximumTransferSize, PipeFlags;
} UsbdPipeInfo;

typedef struct {                       /* USBD_INTERFACE_INFORMATION */
    UINT16 Length;
    UINT8  InterfaceNumber, AlternateSetting, Class, SubClass, Protocol, Reserved;
    void  *InterfaceHandle;
    UINT32 NumberOfPipes;
    UsbdPipeInfo Pipes[1];
} UsbdIfaceInfo;

typedef struct {                       /* _URB_SELECT_CONFIGURATION */
    UrbHeader     Hdr;
    UINT8        *ConfigurationDescriptor;
    void         *ConfigurationHandle;
    UsbdIfaceInfo Interface;
} UrbSelectConfig;

typedef struct {                       /* _URB_SELECT_INTERFACE */
    UrbHeader     Hdr;
    void         *ConfigurationHandle;
    UsbdIfaceInfo Interface;
} UrbSelectIface;

typedef struct {                       /* _URB_PIPE_REQUEST */
    UrbHeader Hdr;
    void     *PipeHandle;
    UINT32    Reserved;
} UrbPipe;

typedef struct {                       /* _URB_GET_CURRENT_FRAME_NUMBER */
    UrbHeader Hdr;
    UINT32    FrameNumber;
} UrbFrame;

/* The transfer URBs share their first part: _URB_BULK_OR_INTERRUPT_TRANSFER,
 * _URB_CONTROL_TRANSFER(_EX), and the control requests whose PipeHandle
 * and TransferFlags fields are reserved (the default pipe; the direction
 * comes from the function) */
typedef struct {
    UrbHeader Hdr;
    void     *PipeHandle;
    UINT32    TransferFlags;
    UINT32    TransferBufferLength;
    void     *TransferBuffer;
    NT_MDL   *TransferBufferMDL;
    void     *UrbLink;                 /* (_EX: Timeout, ms) */
    void     *hca[8];
    UINT8     Setup[8];                /* control: the SETUP packet; the requests: their fields */
} UrbXfer;

/* _URB_ISOCH_TRANSFER: the transfer URBs' first part, then the frame and
 * the packets (USBD_ISO_PACKET_DESCRIPTOR: each one's place in the
 * buffer, and on return its length and status) */
typedef struct { UINT32 Offset, Length, Status; } UsbdIsoPacket;

typedef struct {
    UrbHeader Hdr;
    void     *PipeHandle;
    UINT32    TransferFlags;
    UINT32    TransferBufferLength;
    void     *TransferBuffer;
    NT_MDL   *TransferBufferMDL;
    void     *UrbLink;
    void     *hca[8];
    UINT32    StartFrame, NumberOfPackets, ErrorCount;
    UsbdIsoPacket IsoPacket[1];
} UrbIso;

_Static_assert(sizeof(UrbHeader) == 24 && sizeof(UsbdPipeInfo) == 24 && sizeof(UsbdIfaceInfo) == 48 &&
               __builtin_offsetof(UrbSelectConfig, Interface) == 40 &&
               __builtin_offsetof(UrbSelectIface, Interface) == 32 &&
               __builtin_offsetof(UrbXfer, TransferBufferLength) == 36 &&
               __builtin_offsetof(UrbXfer, hca) == 64 && __builtin_offsetof(UrbXfer, Setup) == 128 &&
               __builtin_offsetof(UrbIso, StartFrame) == 128 && __builtin_offsetof(UrbIso, IsoPacket) == 140 &&
               sizeof(UrbIso) == 152,
               "URB layouts (usb.h, x64)");

/* USBD_INTERFACE_LIST_ENTRY, and the usbd.sys export that builds a
 * SELECT_CONFIGURATION URB from a list of them (usbd.c) */
typedef struct { UINT8 *InterfaceDescriptor; UsbdIfaceInfo *Interface; } UsbdIfaceListEntry;
NT_API UrbSelectConfig *nt_USBD_CreateConfigurationRequestEx(UINT8 *cfg, UsbdIfaceListEntry *list);
