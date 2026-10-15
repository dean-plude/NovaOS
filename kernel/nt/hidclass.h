/*
 * hidclass.h — the HID class driver interface, as the WDK lays it out on
 * x64 (hidport.h, hidclass.h): what a HID minidriver registers with, the
 * device extension hidclass gives it, the internal requests it serves, and
 * the control codes programs send a HID collection (hid.dll's HidD_*).
 * hidclass.c (the class driver) and hidusb.c (NovaOS's USB minidriver)
 * share them.
 */
#pragma once
#include "wdm.h"

#define HID_REVISION 0x00000001

/* HID_MINIDRIVER_REGISTRATION */
typedef struct {
    UINT32               Revision;
    NT_DRIVER_OBJECT    *DriverObject;
    NT_UNICODE_STRING   *RegistryPath;
    UINT32               DeviceExtensionSize;
    UINT8                DevicesArePolled;
    UINT8                Reserved[3];
} HidMinidriverRegistration;

/* HID_DEVICE_EXTENSION: what the FDO's DeviceExtension starts with */
typedef struct {
    NT_DEVICE_OBJECT *PhysicalDeviceObject;
    NT_DEVICE_OBJECT *NextDeviceObject;
    void             *MiniDeviceExtension;
} HidDeviceExtension;

/* HID_DESCRIPTOR (packed, as the USB HID class descriptor) */
typedef struct __attribute__((packed)) {
    UINT8  bLength, bDescriptorType;
    UINT16 bcdHID;
    UINT8  bCountry, bNumDescriptors;
    UINT8  bReportType;
    UINT16 wReportLength;
} HidDescriptor;

/* HID_DEVICE_ATTRIBUTES */
typedef struct {
    UINT32 Size;
    UINT16 VendorID, ProductID, VersionNumber;
    UINT16 Reserved[11];
} HidDeviceAttributes;

/* HID_XFER_PACKET: a report and its ID (the buffer starts with the ID byte) */
typedef struct {
    UINT8  *reportBuffer;
    UINT32  reportBufferLen;
    UINT8   reportId;
} HidXferPacket;

/* HID_COLLECTION_INFORMATION (IOCTL_HID_GET_COLLECTION_INFORMATION) */
typedef struct {
    UINT32 DescriptorSize;
    UINT8  Polled;
    UINT8  Reserved1[1];
    UINT16 VendorID, ProductID, VersionNumber;
} HidCollectionInformation;

_Static_assert(sizeof(HidMinidriverRegistration) == 32 && sizeof(HidDescriptor) == 9 &&
               sizeof(HidDeviceAttributes) == 32 && sizeof(HidXferPacket) == 16 &&
               sizeof(HidCollectionInformation) == 12, "HID class layouts (x64)");

/* Control codes: FILE_DEVICE_KEYBOARD, function, method */
#define HID_CTL(fn, method) NT_CTL_CODE(0x0B, (fn), (method), 0)

/* What hidclass sends its minidriver (IRP_MJ_INTERNAL_DEVICE_CONTROL) */
#define IOCTL_HID_GET_DEVICE_DESCRIPTOR       HID_CTL(0, NT_METHOD_NEITHER)
#define IOCTL_HID_GET_REPORT_DESCRIPTOR       HID_CTL(1, NT_METHOD_NEITHER)
#define IOCTL_HID_READ_REPORT                 HID_CTL(2, NT_METHOD_NEITHER)
#define IOCTL_HID_WRITE_REPORT                HID_CTL(3, NT_METHOD_NEITHER)
#define IOCTL_HID_GET_STRING                  HID_CTL(4, NT_METHOD_NEITHER)
#define IOCTL_HID_ACTIVATE_DEVICE             HID_CTL(7, NT_METHOD_NEITHER)
#define IOCTL_HID_DEACTIVATE_DEVICE           HID_CTL(8, NT_METHOD_NEITHER)
#define IOCTL_HID_GET_DEVICE_ATTRIBUTES       HID_CTL(9, NT_METHOD_NEITHER)
#define IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST HID_CTL(10, NT_METHOD_NEITHER)

/* What programs send a collection (IRP_MJ_DEVICE_CONTROL); the report and
 * string ones also go to the minidriver, internal */
#define IOCTL_HID_GET_DRIVER_CONFIG           HID_CTL(100, NT_METHOD_BUFFERED)
#define IOCTL_HID_SET_DRIVER_CONFIG           HID_CTL(101, NT_METHOD_BUFFERED)
#define IOCTL_HID_GET_POLL_FREQUENCY_MSEC     HID_CTL(102, NT_METHOD_BUFFERED)
#define IOCTL_HID_SET_POLL_FREQUENCY_MSEC     HID_CTL(103, NT_METHOD_BUFFERED)
#define IOCTL_GET_NUM_DEVICE_INPUT_BUFFERS    HID_CTL(104, NT_METHOD_BUFFERED)
#define IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS    HID_CTL(105, NT_METHOD_BUFFERED)
#define IOCTL_HID_GET_COLLECTION_INFORMATION  HID_CTL(106, NT_METHOD_BUFFERED)
#define IOCTL_HID_ENABLE_WAKE_ON_SX           HID_CTL(107, NT_METHOD_BUFFERED)
#define IOCTL_HID_SET_S0_IDLE_TIMEOUT         HID_CTL(108, NT_METHOD_BUFFERED)
#define IOCTL_HID_GET_COLLECTION_DESCRIPTOR   HID_CTL(100, NT_METHOD_NEITHER)
#define IOCTL_HID_FLUSH_QUEUE                 HID_CTL(101, NT_METHOD_NEITHER)
#define IOCTL_HID_SET_FEATURE                 HID_CTL(100, NT_METHOD_IN_DIRECT)
#define IOCTL_HID_SET_OUTPUT_REPORT           HID_CTL(101, NT_METHOD_IN_DIRECT)
#define IOCTL_HID_GET_FEATURE                 HID_CTL(100, NT_METHOD_OUT_DIRECT)
#define IOCTL_GET_PHYSICAL_DESCRIPTOR         HID_CTL(102, NT_METHOD_OUT_DIRECT)
#define IOCTL_HID_GET_HARDWARE_ID             HID_CTL(103, NT_METHOD_OUT_DIRECT)
#define IOCTL_HID_GET_INPUT_REPORT            HID_CTL(104, NT_METHOD_OUT_DIRECT)
#define IOCTL_HID_GET_OUTPUT_REPORT           HID_CTL(105, NT_METHOD_OUT_DIRECT)
#define IOCTL_HID_GET_MANUFACTURER_STRING     HID_CTL(110, NT_METHOD_OUT_DIRECT)
#define IOCTL_HID_GET_PRODUCT_STRING          HID_CTL(111, NT_METHOD_OUT_DIRECT)
#define IOCTL_HID_GET_SERIALNUMBER_STRING     HID_CTL(112, NT_METHOD_OUT_DIRECT)
#define IOCTL_HID_GET_INDEXED_STRING          HID_CTL(120, NT_METHOD_OUT_DIRECT)
#define IOCTL_HID_GET_MS_GENRE_DESCRIPTOR     HID_CTL(121, NT_METHOD_OUT_DIRECT)

/* NovaOS's own, from kernel callers only (internal device control on a
 * collection, METHOD_NEITHER): the collection's usage, the lengths of its
 * input, output and feature reports and the device's whole report
 * descriptor, as its minidriver gave it.  The keyboard, mouse and
 * digitizer class drivers (kbdhid.c) decode the collection's reports
 * with it. */
#define IOCTL_HID_NOVA_GET_REPORT_DESCRIPTOR  HID_CTL(0x800, NT_METHOD_NEITHER)
typedef struct {
    UINT16 InputLength, OutputLength;   /* with the report ID byte (0: none of that type) */
    UINT16 FeatureLength;
    UINT16 UsagePage, Usage;            /* the collection's */
    UINT16 Reserved;
    UINT32 DescriptorLength;
    UINT8  Descriptor[];
} HidNovaReportDescriptor;

/* IOCTL_HID_GET_STRING's string IDs (Type3InputBuffer: ID | language << 16) */
#define HID_STRING_ID_IMANUFACTURER 14
#define HID_STRING_ID_IPRODUCT      15
#define HID_STRING_ID_ISERIALNUMBER 16

/* hidusb.c: NovaOS's USB HID minidriver, built into the kernel (the
 * service "HidUsb", as input.inf names Windows' hidusb.sys) */
NTSTATUS NT_API HidUsbDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg);
/* mshidkmdf.c: the pass-through minidriver a KMDF HID driver is a lower
 * filter of (the service "mshidkmdf", MsHidKmdf.inf's mshidkmdf.sys) */
NTSTATUS NT_API MsHidKmdfDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg);
NT_API NT_STATUS nt_HidRegisterMinidriver(HidMinidriverRegistration *r);
/* kbdhid.c: the keyboard and mouse class drivers on HID collections (the
 * services "kbdhid" and "mouhid", as keyboard.inf and msmouse.inf name
 * Windows' kbdhid.sys and mouhid.sys), "hiddigi" on touch screens,
 * pens and precision touchpads (what win32k reads on Windows) and
 * "gamehid" on game pads and joysticks (what DirectInput reads on
 * Windows) */
NTSTATUS NT_API KbdHidDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg);
NTSTATUS NT_API MouHidDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg);
NTSTATUS NT_API DigiHidDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg);
NTSTATUS NT_API GameHidDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg);
