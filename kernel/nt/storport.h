/*
 * storport.h — the Storport structures a storage miniport shares with its
 * port driver, in Windows' x64 layout (NovaOS's own definitions, from the
 * documented layouts; checked against what virtio-win's viostor.sys reads
 * and writes)
 *
 * A miniport registers with StorPortInitialize, handing over its
 * HW_INITIALIZATION_DATA (its entry points and extension sizes).  The port
 * driver (storport.c) fills a PORT_CONFIGURATION_INFORMATION for each
 * adapter it starts and sends it requests as STORAGE_REQUEST_BLOCKs
 * (SRB_EX): a header, the target's address and one data block holding the
 * SCSI command (CDB).
 */
#pragma once
#include "wdm.h"

/* ---- HW_INITIALIZATION_DATA (208 bytes) ---- */
typedef struct _STOR_HW_INITIALIZATION_DATA {
    unsigned int   HwInitializationDataSize;
    int            AdapterInterfaceType;        /* PCIBus 5 */
    void          *HwInitialize, *HwStartIo, *HwInterrupt, *HwFindAdapter, *HwResetBus, *HwDmaStarted,
                  *HwAdapterState;
    unsigned int   DeviceExtensionSize, SpecificLuExtensionSize, SrbExtensionSize, NumberOfAccessRanges;
    void          *Reserved;
    unsigned char  MapBuffers, NeedPhysicalAddresses, TaggedQueuing, AutoRequestSense, MultipleRequestPerLu,
                   ReceiveEvent;
    unsigned short VendorIdLength;
    void          *VendorId;
    unsigned short PortVersionFlags, DeviceIdLength;
    void          *DeviceId;
    void          *HwAdapterControl;
    /* Storport's additions */
    void          *HwBuildIo, *HwFreeAdapterResources, *HwProcessServiceRequest, *HwCompleteServiceIrp,
                  *HwInitializeTracing, *HwCleanupTracing, *HwTracingEnabled;
    unsigned int   FeatureSupport, SrbTypeFlags, AddressTypeFlags, Reserved1;
    void          *HwUnitControl;
} STOR_HW_INITIALIZATION_DATA;

#define SRB_TYPE_FLAG_STORAGE_REQUEST_BLOCK 0x2

/* ---- PORT_CONFIGURATION_INFORMATION (224 bytes) ---- */
typedef struct _STOR_ACCESS_RANGE {
    long long      RangeStart;
    unsigned int   RangeLength;
    unsigned char  RangeInMemory;
} STOR_ACCESS_RANGE;

typedef struct _STOR_PORT_CONFIGURATION_INFORMATION {
    unsigned int   Length, SystemIoBusNumber;
    int            AdapterInterfaceType;
    unsigned int   BusInterruptLevel, BusInterruptVector;
    int            InterruptMode;               /* LevelSensitive 0, Latched 1 */
    unsigned int   MaximumTransferLength, NumberOfPhysicalBreaks, DmaChannel, DmaPort;
    int            DmaWidth, DmaSpeed;
    unsigned int   AlignmentMask, NumberOfAccessRanges;
    STOR_ACCESS_RANGE (*AccessRanges)[];
    void          *MiniportDumpData;
    unsigned char  NumberOfBuses;
    char           InitiatorBusId[8];
    unsigned char  ScatterGather, Master, CachesData, AdapterScansDown, AtdiskPrimaryClaimed,
                   AtdiskSecondaryClaimed, Dma32BitAddresses, DemandMode, MapBuffers, NeedPhysicalAddresses,
                   TaggedQueuing, AutoRequestSense, MultipleRequestPerLu, ReceiveEvent, RealModeInitialized,
                   BufferAccessScsiPortControlled, MaximumNumberOfTargets, SrbType, AddressType;
    unsigned int   SlotNumber, BusInterruptLevel2, BusInterruptVector2;
    int            InterruptMode2;
    unsigned int   DmaChannel2, DmaPort2;
    int            DmaWidth2, DmaSpeed2;
    unsigned int   DeviceExtensionSize, SpecificLuExtensionSize, SrbExtensionSize;
    unsigned char  Dma64BitAddresses, ResetTargetSupported, MaximumNumberOfLogicalUnits, WmiDataProvider;
    int            SynchronizationModel;        /* StorSynchronizeHalfDuplex 0, FullDuplex 1 */
    void          *HwMSInterruptRoutine;
    int            InterruptSynchronizationMode;
    struct { void *VirtualBase; long long PhysicalBase; unsigned int Length; } DumpRegion;
    unsigned int   RequestedDumpBufferSize;
    unsigned char  VirtualDevice, DumpMode, DmaAddressWidth;
    unsigned int   ExtendedFlags1, MaxNumberOfIO, MaxIOsPerLun, InitialLunQueueDepth, BusResetHoldTime,
                   FeatureSupport;
} STOR_PORT_CONFIGURATION_INFORMATION;

#define SP_UNINITIALIZED_VALUE 0xFFFFFFFFu
#define SP_RETURN_NOT_FOUND 0
#define SP_RETURN_FOUND     1

/* ---- the scatter/gather list StorPortGetScatterGatherList returns ---- */
typedef struct _STOR_SCATTER_GATHER_ELEMENT {
    long long      PhysicalAddress;
    unsigned int   Length;
    NT_ULONG_PTR   Reserved;
} STOR_SCATTER_GATHER_ELEMENT;

typedef struct _STOR_SCATTER_GATHER_LIST {
    unsigned int   NumberOfElements;
    NT_ULONG_PTR   Reserved;
    STOR_SCATTER_GATHER_ELEMENT List[];
} STOR_SCATTER_GATHER_LIST;

/* ---- STORAGE_REQUEST_BLOCK (SRB_EX) ---- */
typedef struct _STORAGE_REQUEST_BLOCK {
    unsigned short Length;
    unsigned char  Function;                    /* SRB_FUNCTION_STORAGE_REQUEST_BLOCK */
    unsigned char  SrbStatus;
    unsigned int   ReservedUlong1, Signature, Version, SrbLength, SrbFunction, SrbFlags, ReservedUlong2,
                   RequestTag;
    unsigned short RequestPriority, RequestAttribute;
    unsigned int   TimeOutValue, SystemStatus, ZeroGuard1, AddressOffset, NumSrbExData, DataTransferLength;
    void          *DataBuffer, *ZeroGuard2, *OriginalRequest, *ClassContext, *PortContext, *MiniportContext;
    struct _STORAGE_REQUEST_BLOCK *NextSrb;
    unsigned int   SrbExDataOffset[1];
} STORAGE_REQUEST_BLOCK;                        /* (128 bytes with its padding) */

/* STOR_ADDR_BTL8: the target, at AddressOffset */
typedef struct _STOR_ADDR_BTL8 {
    unsigned short Type, Port;                  /* STOR_ADDRESS_TYPE_BTL8 1 */
    unsigned int   AddressLength;               /* 4 */
    unsigned char  Path, Target, Lun, Reserved;
} STOR_ADDR_BTL8;

/* SRBEX_DATA_SCSI_CDB16: the command, the first data block */
typedef struct _SRBEX_DATA_SCSI_CDB16 {
    unsigned int   Type;                        /* SrbExDataTypeScsiCdb16 0x40 */
    unsigned int   Length;                      /* the bytes after Length */
    unsigned char  ScsiStatus, SenseInfoBufferLength, CdbLength, Reserved;
    unsigned int   Reserved1;
    void          *SenseInfoBuffer;
    unsigned char  Cdb[16];
} SRBEX_DATA_SCSI_CDB16;

#define SRB_FUNCTION_EXECUTE_SCSI          0x00
#define SRB_FUNCTION_STORAGE_REQUEST_BLOCK 0x28
#define SRB_SIGNATURE                      0x53524258      /* 'SRBX' */
#define STORAGE_REQUEST_BLOCK_VERSION_1    1
#define STOR_ADDRESS_TYPE_BTL8             1
#define SrbExDataTypeScsiCdb16             0x40

#define SRB_FLAGS_DISABLE_SYNCH_TRANSFER   0x00000008
#define SRB_FLAGS_DATA_IN                  0x00000040
#define SRB_FLAGS_DATA_OUT                 0x00000080
#define SRB_FLAGS_NO_DATA_TRANSFER         0x00000000
#define SRB_FLAGS_DISABLE_AUTOSENSE        0x00000020

#define SRB_STATUS_PENDING                 0x00
#define SRB_STATUS_SUCCESS                 0x01
#define SRB_STATUS_BUSY                    0x05
#define SRB_STATUS_DATA_OVERRUN            0x12
#define SRB_STATUS_AUTOSENSE_VALID         0x80
#define SRB_STATUS(s)                      ((s) & 0x3F)

#define SCSIOP_TEST_UNIT_READY             0x00
#define SCSIOP_INQUIRY                     0x12
#define SCSIOP_READ_CAPACITY               0x25
#define SCSIOP_READ                        0x28
#define SCSIOP_WRITE                       0x2A
#define SCSIOP_SYNCHRONIZE_CACHE           0x35
#define SCSIOP_READ16                      0x88
#define SCSIOP_WRITE16                     0x8A
#define SCSIOP_SERVICE_ACTION_IN16         0x9E
#define SERVICE_ACTION_READ_CAPACITY16     0x10

/* ---- StorPortNotification's requests ---- */
#define StorRequestComplete                0
#define StorNextRequest                    1
#define StorNextLuRequest                  2
#define StorResetDetected                  3
#define StorRequestTimerCall               6
#define StorBusChangeDetected              7
#define StorTraceNotification              14
#define StorEnablePassiveInitialization    0x1000
#define StorInitializeDpc                  0x1001
#define StorIssueDpc                       0x1002
#define StorAcquireSpinLock                0x1003
#define StorReleaseSpinLock                0x1004
#define StorStateChangeDetected            0x1005

/* STOR_SPINLOCK: which lock StorPortAcquireSpinLock takes */
#define StorDpcLock                        1
#define StorStartIoLock                    2
#define StorInterruptLock                  3

/* STOR_DPC: a KDPC and its lock (72 bytes) */
typedef struct _STOR_DPC {
    NT_KDPC        Dpc;
    NT_KSPIN_LOCK  Lock;
} STOR_DPC;

/* STOR_LOCK_HANDLE: the miniport's stack copy of what a lock needs to be
 * let go.  Windows' is larger; NovaOS uses only its first 24 bytes: the
 * lock taken and the IRQL to go back to. */
typedef struct _STOR_LOCK_HANDLE {
    int            Lock;                        /* STOR_SPINLOCK */
    void          *Taken;                       /* the KSPIN_LOCK, or the interrupt object */
    unsigned char  OldIrql;
} STOR_LOCK_HANDLE;

/* StorPortExtendedFunction's functions NovaOS answers */
#define ExtFunctionAllocatePool                     0
#define ExtFunctionFreePool                         1
#define ExtFunctionGetMessageInterruptInformation   13

/* STOR_STATUS_* */
#define STOR_STATUS_SUCCESS                0x00000000u
#define STOR_STATUS_UNSUCCESSFUL           0xC1000001u
#define STOR_STATUS_NOT_IMPLEMENTED        0xC1000002u
#define STOR_STATUS_INSUFFICIENT_RESOURCES 0xC1000003u
#define STOR_STATUS_INVALID_PARAMETER      0xC1000006u

/* SCSI_ADAPTER_CONTROL_TYPE */
#define ScsiQuerySupportedControlTypes     0
#define ScsiStopAdapter                    1

_Static_assert(sizeof(STOR_HW_INITIALIZATION_DATA) == 208, "HW_INITIALIZATION_DATA");
_Static_assert(__builtin_offsetof(STOR_HW_INITIALIZATION_DATA, HwBuildIo) == 128, "HwBuildIo");
_Static_assert(__builtin_offsetof(STOR_HW_INITIALIZATION_DATA, SrbTypeFlags) == 188, "SrbTypeFlags");
_Static_assert(sizeof(STOR_PORT_CONFIGURATION_INFORMATION) == 224, "PORT_CONFIGURATION_INFORMATION");
_Static_assert(__builtin_offsetof(STOR_PORT_CONFIGURATION_INFORMATION, SlotNumber) == 100, "SlotNumber");
_Static_assert(__builtin_offsetof(STOR_PORT_CONFIGURATION_INFORMATION, HwMSInterruptRoutine) == 152, "HwMSInterruptRoutine");
_Static_assert(__builtin_offsetof(STOR_PORT_CONFIGURATION_INFORMATION, MaxIOsPerLun) == 208, "MaxIOsPerLun");
_Static_assert(sizeof(STOR_ACCESS_RANGE) == 16, "ACCESS_RANGE");
_Static_assert(sizeof(STOR_SCATTER_GATHER_ELEMENT) == 24, "STOR_SCATTER_GATHER_ELEMENT");
_Static_assert(__builtin_offsetof(STORAGE_REQUEST_BLOCK, DataTransferLength) == 60, "DataTransferLength");
_Static_assert(__builtin_offsetof(STORAGE_REQUEST_BLOCK, MiniportContext) == 104, "MiniportContext");
_Static_assert(__builtin_offsetof(STORAGE_REQUEST_BLOCK, SrbExDataOffset) == 120, "SrbExDataOffset");
_Static_assert(sizeof(STORAGE_REQUEST_BLOCK) == 128, "STORAGE_REQUEST_BLOCK");
_Static_assert(sizeof(SRBEX_DATA_SCSI_CDB16) == 40, "SRBEX_DATA_SCSI_CDB16");
_Static_assert(sizeof(STOR_DPC) == 72, "STOR_DPC");
