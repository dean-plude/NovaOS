/*
 * kmdftest.h — what kmdftest.sys (tools/drivertest/kmdftest.c) and its test
 * program (userland/programs/kmdftest.c) agree on
 */
#pragma once

#define KMDFTEST_DEVICE   L"\\Device\\KmdfTest"
#define KMDFTEST_LINK     L"\\DosDevices\\KmdfTest"

/* CTL_CODE(FILE_DEVICE_UNKNOWN, fn, method, FILE_ANY_ACCESS) */
#define KMDFTEST_CTL(fn, method)  ((0x22u << 16) | ((fn) << 2) | (method))
/* METHOD_BUFFERED: the input, reversed (WdfRequestRetrieveInputBuffer and
 * WdfRequestRetrieveOutputBuffer) */
#define IOCTL_KMDFTEST_REVERSE    KMDFTEST_CTL(0x800, 0)
/* METHOD_BUFFERED: a KMDFTEST_INFO */
#define IOCTL_KMDFTEST_INFO       KMDFTEST_CTL(0x801, 0)

/* What the driver saw of the framework: each 1 when it worked */
typedef struct {
    unsigned int   major, minor;                 /* WdfDriverIsVersionAvailable's answer: 1.33 */
    unsigned char  driver_entry;                 /* DriverEntry ran and WdfDriverCreate succeeded */
    unsigned char  device_created;               /* the control device and its queue came up */
    unsigned char  context;                      /* the device's typed context held what DriverEntry stored */
    unsigned char  passive;                      /* EvtIoDeviceControl came at PASSIVE_LEVEL */
    unsigned int   creates;                      /* EvtDeviceFileCreate calls */
    unsigned int   writes;                       /* EvtIoWrite calls */
    unsigned short registry_path[96];            /* WdfDriverGetRegistryPath */
} KMDFTEST_INFO;
