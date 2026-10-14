/* hidport.h — the interface between hidclass.sys and a HID minidriver, as
 * the WDK's hidport.h declares it (NovaOS's own, written from the
 * documented layouts; kernel/nt/hidclass.h is the kernel's side of it).
 * Microsoft's vhidmini2 sample includes it. */
#pragma once
#include <hidclass.h>

typedef struct _HID_MINIDRIVER_REGISTRATION {
    ULONG           Revision;
    PDRIVER_OBJECT  DriverObject;
    PUNICODE_STRING RegistryPath;
    ULONG           DeviceExtensionSize;
    BOOLEAN         DevicesArePolled;
    UCHAR           Reserved[3];
} HID_MINIDRIVER_REGISTRATION, *PHID_MINIDRIVER_REGISTRATION;

typedef struct _HID_DEVICE_EXTENSION {
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT NextDeviceObject;
    PVOID          MiniDeviceExtension;
} HID_DEVICE_EXTENSION, *PHID_DEVICE_EXTENSION;

typedef struct _HID_DEVICE_ATTRIBUTES {
    ULONG  Size;
    USHORT VendorID;
    USHORT ProductID;
    USHORT VersionNumber;
    USHORT Reserved[11];
} HID_DEVICE_ATTRIBUTES, *PHID_DEVICE_ATTRIBUTES;

#include <pshpack1.h>
typedef struct _HID_DESCRIPTOR {
    UCHAR  bLength;
    UCHAR  bDescriptorType;
    USHORT bcdHID;
    UCHAR  bCountry;
    UCHAR  bNumDescriptors;
    struct _HID_DESCRIPTOR_DESC_LIST {
        UCHAR  bReportType;
        USHORT wReportLength;
    } DescriptorList[1];
} HID_DESCRIPTOR, *PHID_DESCRIPTOR;
#include <poppack.h>

typedef VOID (*HID_SEND_IDLE_CALLBACK)(PVOID Context);
typedef struct _HID_SUBMIT_IDLE_NOTIFICATION_CALLBACK_INFO {
    HID_SEND_IDLE_CALLBACK IdleCallback;
    PVOID                  IdleContext;
} HID_SUBMIT_IDLE_NOTIFICATION_CALLBACK_INFO, *PHID_SUBMIT_IDLE_NOTIFICATION_CALLBACK_INFO;

/* What hidclass sends its minidriver (IRP_MJ_INTERNAL_DEVICE_CONTROL) */
#define IOCTL_HID_GET_DEVICE_DESCRIPTOR          HID_CTL_CODE(0)
#define IOCTL_HID_GET_REPORT_DESCRIPTOR          HID_CTL_CODE(1)
#define IOCTL_HID_READ_REPORT                    HID_CTL_CODE(2)
#define IOCTL_HID_WRITE_REPORT                   HID_CTL_CODE(3)
#define IOCTL_HID_GET_STRING                     HID_CTL_CODE(4)
#define IOCTL_HID_ACTIVATE_DEVICE                HID_CTL_CODE(7)
#define IOCTL_HID_DEACTIVATE_DEVICE              HID_CTL_CODE(8)
#define IOCTL_HID_GET_DEVICE_ATTRIBUTES          HID_CTL_CODE(9)
#define IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST HID_CTL_CODE(10)

/* IOCTL_HID_GET_STRING's string IDs (Type3InputBuffer: ID | language << 16) */
#define HID_STRING_ID_IMANUFACTURER 14
#define HID_STRING_ID_IPRODUCT      15
#define HID_STRING_ID_ISERIALNUMBER 16

/* (UMDF HID minidrivers send these to the reflector instead) */
#define IOCTL_UMDF_HID_SET_FEATURE        HID_CTL_CODE(20)
#define IOCTL_UMDF_HID_GET_FEATURE        HID_CTL_CODE(21)
#define IOCTL_UMDF_HID_SET_OUTPUT_REPORT  HID_CTL_CODE(22)
#define IOCTL_UMDF_HID_GET_INPUT_REPORT   HID_CTL_CODE(23)

NTSTATUS HidRegisterMinidriver(PHID_MINIDRIVER_REGISTRATION MinidriverRegistration);
NTSTATUS HidNotifyPresence(PDEVICE_OBJECT DeviceObject, BOOLEAN IsPresent);
