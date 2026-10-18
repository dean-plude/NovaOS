/*
 * novabth.h — what NovaOS's Bluetooth host (kernel/nt/bthport.c, the role
 * Windows' bthport.sys plays) and Bluetooth's user-mode API
 * (userland/bthprops, bthprops.cpl) say to each other
 *
 * A radio is a device interface, GUID_BTHPORT_DEVICE_INTERFACE, as on
 * Windows.  It answers Windows' documented control codes of bthioctl.h
 * with their structures, laid out as there:
 *
 *   IOCTL_BTH_GET_LOCAL_INFO    the radio: its address, name, class of
 *                               device, HCI and LMP versions, manufacturer,
 *                               whether it is discoverable and connectable
 *                               (BTH_LOCAL_RADIO_INFO);
 *   IOCTL_BTH_GET_DEVICE_INFO   the devices it knows: paired ones and the
 *                               ones its last inquiry found
 *                               (BTH_DEVICE_INFO_LIST);
 *   IOCTL_BTH_DISCONNECT_DEVICE close a device's connection (a BTH_ADDR).
 *
 * The rest of what bthprops.cpl does (an inquiry, pairing, a service
 * switched on or off, a device forgotten, the radio made discoverable)
 * goes through private control codes on Windows, so NovaOS has its own:
 *
 *   IOCTL_NOVABTH_INQUIRY       search (NovaBthInquiry: in units of 1.28
 *                               seconds, as HCI's Inquiry takes them) and
 *                               resolve each new device's name; returns
 *                               when done, with how many it found (UINT32);
 *   IOCTL_NOVABTH_PAIR          pair with a device (a BTH_ADDR): Secure
 *                               Simple Pairing "Just Works", or PIN 0000 for
 *                               an older one, then read its SDP records;
 *   IOCTL_NOVABTH_SERVICES      a device's services, from its SDP records
 *                               (NovaBthServices);
 *   IOCTL_NOVABTH_SET_SERVICE   switch one on or off (NovaBthSetService):
 *                               on, the service gets its own device node
 *                               (BTHENUM\{uuid}...), which Plug and Play
 *                               gives its driver (HID: hidbth);
 *   IOCTL_NOVABTH_REMOVE        forget a device (a BTH_ADDR): its link key,
 *                               its services' nodes, its connection;
 *   IOCTL_NOVABTH_SET_SCAN      inquiry scan (discoverable) and page scan
 *                               (connectable): NOVABTH_SCAN_* in a UINT32.
 */
#pragma once

#define NOVABTH_CTL(fn, method) ((0x41u << 16) | ((unsigned int)(fn) << 2) | (method))   /* (FILE_DEVICE_BLUETOOTH) */
#define IOCTL_BTH_GET_LOCAL_INFO     NOVABTH_CTL(0x00, 0)
#define IOCTL_BTH_GET_RADIO_INFO     NOVABTH_CTL(0x01, 0)
#define IOCTL_BTH_GET_DEVICE_INFO    NOVABTH_CTL(0x02, 0)
#define IOCTL_BTH_DISCONNECT_DEVICE  NOVABTH_CTL(0x03, 0)
#define IOCTL_NOVABTH_INQUIRY        NOVABTH_CTL(0x900, 0)
#define IOCTL_NOVABTH_PAIR           NOVABTH_CTL(0x901, 0)
#define IOCTL_NOVABTH_SERVICES       NOVABTH_CTL(0x902, 0)
#define IOCTL_NOVABTH_SET_SERVICE    NOVABTH_CTL(0x903, 0)
#define IOCTL_NOVABTH_REMOVE         NOVABTH_CTL(0x904, 0)
#define IOCTL_NOVABTH_SET_SCAN       NOVABTH_CTL(0x905, 0)

/* GUID_BTHPORT_DEVICE_INTERFACE {0850302A-B344-4fda-9BE9-90576B8D46F0} */
#define NOVABTH_RADIO_GUID_INIT { 0x0850302A, 0xB344, 0x4fda, { 0x9B, 0xE9, 0x90, 0x57, 0x6B, 0x8D, 0x46, 0xF0 } }

#define NOVABTH_MAX_NAME 248
#define NOVABTH_MAX_SERVICES 16

/* BTH_DEVICE_INFO.flags (BDIF_*) */
#define NOVABTH_BDIF_ADDRESS   0x0001
#define NOVABTH_BDIF_COD       0x0002
#define NOVABTH_BDIF_NAME      0x0004
#define NOVABTH_BDIF_PAIRED    0x0008
#define NOVABTH_BDIF_PERSONAL  0x0010
#define NOVABTH_BDIF_CONNECTED 0x0020
#define NOVABTH_BDIF_SSP_SUPPORTED 0x0100

/* BTH_LOCAL_RADIO_INFO.flags (LOCAL_RADIO_*) */
#define NOVABTH_RADIO_DISCOVERABLE 0x0001
#define NOVABTH_RADIO_CONNECTABLE  0x0002

#define NOVABTH_SCAN_INQUIRY 0x1
#define NOVABTH_SCAN_PAGE    0x2

typedef struct {                       /* BTH_DEVICE_INFO */
    unsigned int       flags;          /* NOVABTH_BDIF_* */
    unsigned long long address;        /* BTH_ADDR: the 48-bit address, its first byte the most significant */
    unsigned int       classOfDevice;  /* BTH_COD */
    char               name[NOVABTH_MAX_NAME];   /* UTF-8 */
} NovaBthDeviceInfo;

typedef struct {                       /* BTH_RADIO_INFO */
    unsigned long long lmpSupportedFeatures;
    unsigned short     mfg;
    unsigned short     lmpSubversion;
    unsigned char      lmpVersion;
} NovaBthRadioInfo;

typedef struct {                       /* BTH_LOCAL_RADIO_INFO */
    NovaBthDeviceInfo  localInfo;
    unsigned int       flags;          /* NOVABTH_RADIO_* */
    unsigned short     hciRevision;
    unsigned char      hciVersion;
    NovaBthRadioInfo   radioInfo;
} NovaBthLocalRadioInfo;

typedef struct {                       /* BTH_DEVICE_INFO_LIST */
    unsigned int       numOfDevices;
    NovaBthDeviceInfo  deviceList[1];
} NovaBthDeviceInfoList;

typedef struct {                       /* IOCTL_NOVABTH_INQUIRY's input */
    unsigned int       length;         /* 1.28 s units (1-48) */
} NovaBthInquiry;

typedef struct {                       /* IOCTL_NOVABTH_SERVICES' output */
    unsigned int       count;
    unsigned short     uuid[NOVABTH_MAX_SERVICES];    /* 16-bit service class UUIDs (0x1124 HID, ...) */
    unsigned short     enabled[NOVABTH_MAX_SERVICES]; /* (1: switched on) */
} NovaBthServices;

typedef struct {                       /* IOCTL_NOVABTH_SET_SERVICE's input */
    unsigned long long address;
    unsigned short     uuid;
    unsigned short     enable;
} NovaBthSetService;
