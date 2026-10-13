/*
 * edutest.h — what the drivers for QEMU's edu device, edutest.sys (WDM,
 * tools/drivertest/edutest.c) and kmdfedu.sys (KMDF,
 * tools/drivertest/kmdfedu.c), and their test program
 * (userland/programs/edutest.c) agree on
 */
#pragma once

/* The device interface the driver registers: {6b1e0a1c-5f3d-4c8e-9a37-3e0d2b8c4f11} */
#define EDUTEST_GUID_INIT { 0x6b1e0a1c, 0x5f3d, 0x4c8e, { 0x9a, 0x37, 0x3e, 0x0d, 0x2b, 0x8c, 0x4f, 0x11 } }
#define EDUTEST_HWID      "PCI\\VEN_1234&DEV_11E8"

/* CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS) */
#define EDUTEST_CTL(fn)          ((0x22u << 16) | ((fn) << 2))
/* an EDUTEST_INFO about the device as IRP_MN_START_DEVICE gave it */
#define IOCTL_EDUTEST_INFO       EDUTEST_CTL(0x900)
/* in: a 32-bit n; out: n! computed by the device, which raises its
 * interrupt when done (an EDUTEST_FACT) */
#define IOCTL_EDUTEST_FACTORIAL  EDUTEST_CTL(0x901)
/* in: up to 4096 bytes; out: an EDUTEST_DMA, then the bytes as the device
 * sent them back */
#define IOCTL_EDUTEST_DMA        EDUTEST_CTL(0x902)

typedef struct {
    unsigned int       ident;              /* register 0x00: 0x010000ed */
    unsigned int       liveness;           /* register 0x04 after writing 0x12345678: its complement */
    unsigned int       memory, ports, interrupts;   /* translated resources of each type */
    unsigned int       bar_length;         /* the memory BAR's length */
    long long          bar_raw;            /* its bus address */
    unsigned int       vector;             /* the translated interrupt */
    unsigned int       irql;
    unsigned int       raw_level;          /* the raw interrupt's (the GSI) */
    unsigned int       latched;            /* CM_RESOURCE_INTERRUPT_LATCHED */
    unsigned int       connect_version;    /* the IoConnectInterruptEx version it got */
    unsigned int       dma_registers;      /* IoGetDmaAdapter's map registers */
    unsigned int       dma_version;
    unsigned int       starts, opens;
    unsigned int       capabilities_address;   /* IRP_MN_QUERY_CAPABILITIES' Address: device << 16 | function */
    unsigned int       message;            /* the interrupt came as CM_RESOURCE_INTERRUPT_MESSAGE (MSI) */
    unsigned int       message_count;      /* the raw descriptor's MessageCount */
    unsigned int       message_irql;       /* IO_INTERRUPT_MESSAGE_INFO.UnifiedIrql */
    unsigned int       message_vector;     /* the table's first Vector */
    unsigned int       message_data;       /* its MessageData */
    long long          message_address;    /* its MessageAddress (0xFEExxxxx) */
    unsigned short     location[64];       /* IoGetDeviceProperty(DevicePropertyLocationInformation) */
    unsigned int       kmdf_minor;         /* kmdfedu.sys: the KMDF 1.x it runs on (0: edutest.sys, WDM) */
    unsigned int       wdf_message;        /* kmdfedu.sys: WdfInterruptGetInfo's MessageSignaled */
} EDUTEST_INFO;

typedef struct {
    unsigned int       value;              /* n! (32 bits, as the device keeps it) */
    unsigned int       isr_status;         /* the interrupt status the ISR read (1: factorial done) */
    unsigned int       isr_irql;           /* the IRQL the ISR ran at */
    unsigned int       dpc_irql;           /* and its DPC */
    unsigned int       interrupts;         /* the ISR's count so far */
    unsigned int       message_interrupts; /* of them, through the message routine (with MessageId 0) */
} EDUTEST_FACT;

typedef struct {
    unsigned int       sg_elements;        /* to the device: GetScatterGatherList's list */
    unsigned int       common_ok;          /* back into a common buffer (AllocateCommonBuffer): the same bytes */
    unsigned int       map_ok;             /* back through AllocateAdapterChannel and MapTransfer */
    unsigned int       interrupts;         /* DMA-done interrupts (status 0x100) */
    long long          common_logical;     /* the common buffer's device address (below 256 MiB) */
    long long          sg_address;         /* the first element's */
    long long          map_address;        /* MapTransfer's */
} EDUTEST_DMA;
