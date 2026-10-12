/*
 * wdmtest.h — what wdmtest.sys (tools/drivertest/wdmtest.c) and its test
 * program (userland/programs/wdmtest.c) agree on
 */
#pragma once

#define WDMTEST_DEVICE   L"\\Device\\WdmTest"
#define WDMTEST_LINK     L"\\DosDevices\\WdmTest"

/* CTL_CODE(FILE_DEVICE_UNKNOWN, fn, method, FILE_ANY_ACCESS) */
#define WDMTEST_CTL(fn, method)  ((0x22u << 16) | ((fn) << 2) | (method))
/* METHOD_BUFFERED: the input, reversed */
#define IOCTL_WDMTEST_REVERSE    WDMTEST_CTL(0x800, 0)
/* METHOD_NEITHER: each input byte plus one, written to the output buffer */
#define IOCTL_WDMTEST_NEITHER    WDMTEST_CTL(0x801, 3)
/* METHOD_BUFFERED: a WDMTEST_INFO about the objects as the driver sees them */
#define IOCTL_WDMTEST_INFO       WDMTEST_CTL(0x802, 0)
/* METHOD_IN_DIRECT: not served before MDLs exist (step 30.2) */
#define IOCTL_WDMTEST_DIRECT     WDMTEST_CTL(0x803, 1)

typedef struct {
    short          driver_type, driver_size;     /* DRIVER_OBJECT: 4, 0x150 */
    short          device_type, device_size;     /* DEVICE_OBJECT: 3, 0x150 + extension */
    short          irp_type;                     /* IRP: 6 */
    char           irp_stack_count, irp_current_location;
    short          file_type, file_size;         /* FILE_OBJECT: 5, 0xD8 */
    unsigned char  requestor_mode;               /* UserMode: 1 */
    unsigned char  buffered;                     /* the request came in AssociatedIrp.SystemBuffer */
    unsigned int   opens;                        /* IRP_MJ_CREATEs seen */
    unsigned short driver_name[64];              /* DRIVER_OBJECT.DriverName */
    unsigned short registry_path[96];            /* DriverEntry's RegistryPath */
    unsigned short service_key[32];              /* DRIVER_EXTENSION.ServiceKeyName */
    unsigned short file_name[32];                /* FILE_OBJECT.FileName of this open */
} WDMTEST_INFO;
