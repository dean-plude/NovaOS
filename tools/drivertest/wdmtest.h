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
/* METHOD_IN_DIRECT: the sum of the output buffer's bytes, read through
 * its MDL, as the request's Information (step 30.2) */
#define IOCTL_WDMTEST_DIRECT     WDMTEST_CTL(0x803, 1)
/* METHOD_OUT_DIRECT: the output buffer filled through its MDL, byte i =
 * the input's first byte + i, and a WDMTEST_MDL about the MDL in the
 * first bytes */
#define IOCTL_WDMTEST_DIRECT_OUT WDMTEST_CTL(0x804, 2)
/* METHOD_BUFFERED: the kernel API checks run in the driver, a WDMTEST_KERNEL */
#define IOCTL_WDMTEST_KERNEL     WDMTEST_CTL(0x805, 0)
/* METHOD_BUFFERED: returns STATUS_PENDING; a timer's DPC completes it
 * about 30 ms later with "late" and the DPC's IRQL */
#define IOCTL_WDMTEST_PENDING    WDMTEST_CTL(0x806, 0)

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

typedef struct {
    unsigned int   flags;                        /* MDL.MdlFlags after mapping it */
    unsigned int   byte_count, byte_offset;
    unsigned int   pages;                        /* PFNs in it */
} WDMTEST_MDL;

/* What IOCTL_WDMTEST_KERNEL found: each 1 when it worked as on Windows */
typedef struct {
    unsigned char  irql_passive;                 /* the request came at PASSIVE_LEVEL */
    unsigned char  irql_cr8;                     /* CR8 written to DISPATCH_LEVEL reads back, KeGetCurrentIrql agrees */
    unsigned char  irql_export;                  /* KeRaiseIrqlToDpcLevel returned 0, raised to 2, KeLowerIrql back */
    unsigned char  dpc_held;                     /* queued under a spin lock: not run while it is held */
    unsigned char  dpc_once;                     /* a second KeInsertQueueDpc while queued returned FALSE */
    unsigned char  dpc_ran;                      /* ran when the lock was let go, at DISPATCH_LEVEL, with its arguments */
    unsigned char  pool;                         /* ExAllocatePoolWithTag 16-aligned, ExAllocatePool2 zeroed */
    unsigned char  event;                        /* a synchronization event: set, taken once, then a zero wait times out */
    unsigned char  timer;                        /* a 20 ms timer waited on: signalled after 20 ms or more, never early */
    unsigned char  fast_mutex;                   /* held at APC_LEVEL, released back at PASSIVE_LEVEL */
    unsigned char  pool_mdl;                     /* MmBuildMdlForNonPagedPool: the system address is the buffer */
    unsigned char  routine;                      /* MmGetSystemRoutineAddress("KeSetEvent") is the import */
    unsigned char  semaphore;                    /* KeReleaseSemaphore by 2: two waits succeed, a third times out */
    unsigned char  queued_lock;                  /* an in-stack queued spin lock raises to DISPATCH_LEVEL and back */
    unsigned int   timer_ms;                     /* how long the 20 ms timer took */
} WDMTEST_KERNEL;

typedef struct {
    char           text[4];                      /* "late" */
    unsigned char  dpc_irql;                     /* the IRQL its DPC ran at */
    unsigned char  pending_returned;             /* IoMarkIrpPending: SL_PENDING_RETURNED was set */
} WDMTEST_PENDING;
