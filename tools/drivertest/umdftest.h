/*
 * umdftest.h — what umdftest.dll, the UMDF 2 test driver
 * (tools/drivertest/umdftest.c), and its test program
 * (userland/programs/umdftest.c) agree on
 */
#pragma once

/* {3c0f8e52-91a4-4d6b-b7e1-5a2c9d0f6e13}: the device interface the driver registers */
#define UMDFTEST_GUID_INIT { 0x3c0f8e52, 0x91a4, 0x4d6b, { 0xb7, 0xe1, 0x5a, 0x2c, 0x9d, 0x0f, 0x6e, 0x13 } }

/* CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS) */
#define UMDFTEST_CTL(fn)          ((0x22u << 16) | ((fn) << 2))
/* the input, reversed (WdfRequestRetrieveInputBuffer, WdfRequestRetrieveOutputBuffer) */
#define IOCTL_UMDFTEST_REVERSE    UMDFTEST_CTL(0x900)
/* out: a UMDFTEST_INFO */
#define IOCTL_UMDFTEST_INFO       UMDFTEST_CTL(0x901)

/* The bytes the driver keeps for reads and writes */
#define UMDFTEST_STORE            4096

/* What the driver saw of the framework and the device */
typedef struct {
    unsigned int  major, minor;                 /* WdfDriverIsVersionAvailable's answer: 2.33 */
    unsigned int  host_pid;                     /* the process the driver runs in (its host) */
    unsigned int  requestor_pid;                /* WdfRequestGetRequestorProcessId of this request */
    unsigned int  device_adds;                  /* EvtDriverDeviceAdd calls */
    unsigned int  prepare_hardware;             /* EvtDevicePrepareHardware calls */
    unsigned int  d0_entries;                   /* EvtDeviceD0Entry calls */
    unsigned int  creates, closes;              /* EvtDeviceFileCreate, EvtFileClose calls */
    unsigned int  resources_raw;                /* the raw resource list's descriptors */
    unsigned int  memory, interrupts;           /* of them: memory ranges, interrupts (translated) */
    unsigned int  memory_length;                /* the first memory range's bytes */
    unsigned int  stored;                       /* the bytes written so far (kept, read back) */
    unsigned int  context_ok;                   /* the device's typed context held what EvtDriverDeviceAdd stored */
    unsigned short registry_path[128];          /* WdfDriverGetRegistryPath */
} UMDFTEST_INFO;
