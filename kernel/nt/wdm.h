/*
 * wdm.h — the Windows kernel-mode driver structures, in Windows' x64 layout
 *
 * A Windows driver reads and writes DRIVER_OBJECT, DEVICE_OBJECT, IRP,
 * IO_STACK_LOCATION and FILE_OBJECT directly, at the offsets Microsoft's
 * WDK headers give them, so the export layer (kernel/nt/) uses exactly
 * those layouts, written from Microsoft's public documentation and checked
 * below offset by offset.  NovaOS's own I/O manager (io/io.h) keeps its own
 * structures; the names here carry an NT_ prefix so both can be used side
 * by side.  NovaOS's test drivers (tools/drivertest/) include this file
 * too: it uses only C's own types, and works in the kernel (System V ABI)
 * and in a driver built for x86_64-pc-windows-msvc alike.
 *
 * Every function pointer a driver hands in, and every export it calls, is
 * NT_API: the Microsoft x64 calling convention.
 *
 * Only what the drivers of roadmap step 30.1 use is complete; the rest is
 * there for the layout (opaque members, reserved space) and is filled in
 * by the steps that need it.
 */
#pragma once

#define NT_API __attribute__((ms_abi))

typedef int                NT_STATUS;
typedef unsigned short     NT_WCHAR;
typedef unsigned long long NT_ULONG_PTR;

#define NT_SUCCESS_(s)     ((NT_STATUS)(s) >= 0)

typedef struct _NT_LIST_ENTRY {
    struct _NT_LIST_ENTRY *Flink, *Blink;
} NT_LIST_ENTRY;

typedef struct _NT_UNICODE_STRING {
    unsigned short Length;              /* bytes, without a terminator */
    unsigned short MaximumLength;
    NT_WCHAR      *Buffer;
} NT_UNICODE_STRING;

typedef struct _NT_IO_STATUS_BLOCK {
    union {
        NT_STATUS Status;
        void     *Pointer;
    };
    NT_ULONG_PTR Information;
} NT_IO_STATUS_BLOCK;

/* DISPATCHER_HEADER and KEVENT: the I/O manager's completion event uses
 * SignalState only (the wait objects themselves are step 30.2) */
typedef struct _NT_DISPATCHER_HEADER {
    unsigned char Type, Signalling, Size, Reserved1;
    int           SignalState;
    NT_LIST_ENTRY WaitListHead;
} NT_DISPATCHER_HEADER;

typedef struct _NT_KEVENT {
    NT_DISPATCHER_HEADER Header;
} NT_KEVENT;

typedef struct _NT_KDEVICE_QUEUE_ENTRY {
    NT_LIST_ENTRY      DeviceListEntry;
    unsigned int       SortKey;
    unsigned char      Inserted;
} NT_KDEVICE_QUEUE_ENTRY;

typedef struct _NT_KDEVICE_QUEUE {
    short              Type, Size;
    NT_LIST_ENTRY      DeviceListHead;
    NT_ULONG_PTR       Lock;
    unsigned char      Busy;
} NT_KDEVICE_QUEUE;

typedef struct _NT_KDPC {
    unsigned char      Type, Importance;
    unsigned short     Number;
    void              *DpcListEntry;
    NT_ULONG_PTR       ProcessorHistory;
    void              *DeferredRoutine, *DeferredContext, *SystemArgument1, *SystemArgument2, *DpcData;
} NT_KDPC;

typedef struct _NT_WAIT_CONTEXT_BLOCK {
    NT_KDEVICE_QUEUE_ENTRY WaitQueueEntry;
    void              *DeviceRoutine, *DeviceContext;
    unsigned int       NumberOfMapRegisters;
    void              *DeviceObject, *CurrentIrp, *BufferChainingDpc;
} NT_WAIT_CONTEXT_BLOCK;

typedef struct _NT_DRIVER_OBJECT   NT_DRIVER_OBJECT;
typedef struct _NT_DEVICE_OBJECT   NT_DEVICE_OBJECT;
typedef struct _NT_IRP             NT_IRP;
typedef struct _NT_FILE_OBJECT     NT_FILE_OBJECT;

typedef NT_STATUS (NT_API *NT_DRIVER_INITIALIZE)(NT_DRIVER_OBJECT *, NT_UNICODE_STRING *RegistryPath);
typedef NT_STATUS (NT_API *NT_DRIVER_DISPATCH)(NT_DEVICE_OBJECT *, NT_IRP *);
typedef void      (NT_API *NT_DRIVER_UNLOAD)(NT_DRIVER_OBJECT *);
typedef NT_STATUS (NT_API *NT_DRIVER_ADD_DEVICE)(NT_DRIVER_OBJECT *, NT_DEVICE_OBJECT *Pdo);
typedef NT_STATUS (NT_API *NT_IO_COMPLETION_ROUTINE)(NT_DEVICE_OBJECT *, NT_IRP *, void *Context);

#define NT_IRP_MJ_CREATE                0x00
#define NT_IRP_MJ_CLOSE                 0x02
#define NT_IRP_MJ_READ                  0x03
#define NT_IRP_MJ_WRITE                 0x04
#define NT_IRP_MJ_DEVICE_CONTROL        0x0E
#define NT_IRP_MJ_CLEANUP               0x12
#define NT_IRP_MJ_MAXIMUM_FUNCTION      0x1B

typedef struct _NT_DRIVER_EXTENSION {
    NT_DRIVER_OBJECT    *DriverObject;
    NT_DRIVER_ADD_DEVICE AddDevice;
    unsigned int         Count;
    NT_UNICODE_STRING    ServiceKeyName;
} NT_DRIVER_EXTENSION;

struct _NT_DRIVER_OBJECT {
    short                Type;          /* 4 */
    short                Size;
    NT_DEVICE_OBJECT    *DeviceObject;  /* its devices, newest first, through NextDevice */
    unsigned int         Flags;
    void                *DriverStart;
    unsigned int         DriverSize;
    void                *DriverSection;
    NT_DRIVER_EXTENSION *DriverExtension;
    NT_UNICODE_STRING    DriverName;    /* \Driver\<service name> */
    NT_UNICODE_STRING   *HardwareDatabase;
    void                *FastIoDispatch;
    NT_DRIVER_INITIALIZE DriverInit;
    void                *DriverStartIo;
    NT_DRIVER_UNLOAD     DriverUnload;
    NT_DRIVER_DISPATCH   MajorFunction[NT_IRP_MJ_MAXIMUM_FUNCTION + 1];
};

/* DEVICE_OBJECT.Flags */
#define NT_DO_BUFFERED_IO           0x00000004
#define NT_DO_EXCLUSIVE             0x00000008
#define NT_DO_DIRECT_IO             0x00000010
#define NT_DO_DEVICE_INITIALIZING   0x00000080

struct _NT_DEVICE_OBJECT {
    short                Type;          /* 3 */
    unsigned short       Size;
    int                  ReferenceCount;
    NT_DRIVER_OBJECT    *DriverObject;
    NT_DEVICE_OBJECT    *NextDevice;
    NT_DEVICE_OBJECT    *AttachedDevice;
    NT_IRP              *CurrentIrp;
    void                *Timer;
    unsigned int         Flags;
    unsigned int         Characteristics;
    void                *Vpb;
    void                *DeviceExtension;
    unsigned int         DeviceType;
    char                 StackSize;
    union {
        NT_LIST_ENTRY         ListEntry;
        NT_WAIT_CONTEXT_BLOCK Wcb;
    } Queue;
    unsigned int         AlignmentRequirement;
    NT_KDEVICE_QUEUE     DeviceQueue;
    NT_KDPC              Dpc;
    unsigned int         ActiveThreadCount;
    void                *SecurityDescriptor;
    NT_KEVENT            DeviceLock;
    unsigned short       SectorSize, Spare1;
    void                *DeviceObjectExtension;
    void                *Reserved;
} __attribute__((aligned(16)));                     /* (DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT)) */

/* IO_STACK_LOCATION: one per driver in the stack the IRP passes through */
typedef struct _NT_IO_STACK_LOCATION {
    unsigned char MajorFunction, MinorFunction, Flags, Control;
    union {
        struct {
            void          *SecurityContext;
            unsigned int   Options;     /* CreateDisposition << 24 | CreateOptions */
            unsigned short FileAttributes, ShareAccess;
            unsigned int   EaLength;
        } Create;
        struct {
            unsigned int   Length;
            unsigned int   Key __attribute__((aligned(8)));
            long long      ByteOffset;
        } Read, Write;
        struct {
            unsigned int   OutputBufferLength;
            unsigned int   InputBufferLength __attribute__((aligned(8)));
            unsigned int   IoControlCode __attribute__((aligned(8)));
            void          *Type3InputBuffer;
        } DeviceIoControl;
        struct {
            void *Argument1, *Argument2, *Argument3, *Argument4;
        } Others;
    } Parameters;
    NT_DEVICE_OBJECT        *DeviceObject;
    NT_FILE_OBJECT          *FileObject;
    NT_IO_COMPLETION_ROUTINE CompletionRoutine;
    void                    *Context;
} NT_IO_STACK_LOCATION;

/* IO_STACK_LOCATION.Control: when its completion routine runs */
#define NT_SL_PENDING_RETURNED   0x01
#define NT_SL_INVOKE_ON_CANCEL   0x20
#define NT_SL_INVOKE_ON_SUCCESS  0x40
#define NT_SL_INVOKE_ON_ERROR    0x80

struct _NT_IRP {
    short                Type;          /* 6 */
    unsigned short       Size;
    unsigned short       AllocationProcessorNumber, Reserved;
    void                *MdlAddress;
    unsigned int         Flags;
    union {
        NT_IRP *MasterIrp;
        int     IrpCount;
        void   *SystemBuffer;           /* buffered I/O: the kernel's copy of the caller's buffer */
    } AssociatedIrp;
    NT_LIST_ENTRY        ThreadListEntry;
    NT_IO_STATUS_BLOCK   IoStatus;
    char                 RequestorMode; /* 0 KernelMode, 1 UserMode */
    unsigned char        PendingReturned;
    char                 StackCount;
    char                 CurrentLocation;
    unsigned char        Cancel;
    unsigned char        CancelIrql;
    char                 ApcEnvironment;
    unsigned char        AllocationFlags;
    NT_IO_STATUS_BLOCK  *UserIosb;
    NT_KEVENT           *UserEvent;
    union {
        struct { void *UserApcRoutine, *UserApcContext; } AsynchronousParameters;
        long long AllocationSize;
    } Overlay;
    void                *CancelRoutine;
    void                *UserBuffer;    /* neither I/O: the caller's own buffer */
    union {
        struct {
            union {
                NT_KDEVICE_QUEUE_ENTRY DeviceQueueEntry;
                void *DriverContext[4];
            };
            void                 *Thread;
            char                 *AuxiliaryBuffer;
            NT_LIST_ENTRY         ListEntry;
            NT_IO_STACK_LOCATION *CurrentStackLocation;
            NT_FILE_OBJECT       *OriginalFileObject;
            void                 *IrpExtension;
        } Overlay;
        unsigned char Apc[0x58];        /* KAPC */
        void         *CompletionKey;
    } Tail;
};

struct _NT_FILE_OBJECT {
    short                Type;          /* 5 */
    short                Size;
    NT_DEVICE_OBJECT    *DeviceObject;
    void                *Vpb;
    void                *FsContext, *FsContext2;
    void                *SectionObjectPointer, *PrivateCacheMap;
    NT_STATUS            FinalStatus;
    NT_FILE_OBJECT      *RelatedFileObject;
    unsigned char        LockOperation, DeletePending, ReadAccess, WriteAccess, DeleteAccess,
                         SharedRead, SharedWrite, SharedDelete;
    unsigned int         Flags;
    NT_UNICODE_STRING    FileName;      /* what follows the device's name, e.g. \x for \\.\Dev\x */
    long long            CurrentByteOffset;
    unsigned int         Waiters, Busy;
    void                *LastLock;
    NT_KEVENT            Lock, Event;
    void                *CompletionContext;
    NT_ULONG_PTR         IrpListLock;
    NT_LIST_ENTRY        IrpList;
    void                *FileObjectExtension;
};

/* FILE_OBJECT.Flags */
#define NT_FO_SYNCHRONOUS_IO     0x00000002

/* Status codes the export layer and the test drivers use */
#define NT_STATUS_SUCCESS                  ((NT_STATUS)0x00000000)
#define NT_STATUS_PENDING                  ((NT_STATUS)0x00000103)
#define NT_STATUS_MORE_PROCESSING_REQUIRED ((NT_STATUS)0xC0000016)
#define NT_STATUS_INVALID_PARAMETER        ((NT_STATUS)0xC000000D)
#define NT_STATUS_INVALID_DEVICE_REQUEST   ((NT_STATUS)0xC0000010)
#define NT_STATUS_END_OF_FILE              ((NT_STATUS)0xC0000011)
#define NT_STATUS_NO_MEMORY                ((NT_STATUS)0xC0000017)
#define NT_STATUS_BUFFER_TOO_SMALL         ((NT_STATUS)0xC0000023)
#define NT_STATUS_OBJECT_NAME_INVALID      ((NT_STATUS)0xC0000033)
#define NT_STATUS_OBJECT_NAME_NOT_FOUND    ((NT_STATUS)0xC0000034)
#define NT_STATUS_OBJECT_NAME_COLLISION    ((NT_STATUS)0xC0000035)
#define NT_STATUS_NOT_SUPPORTED            ((NT_STATUS)0xC00000BB)
#define NT_STATUS_INSUFFICIENT_RESOURCES   ((NT_STATUS)0xC000009A)

/* CTL_CODE and the transfer methods */
#define NT_CTL_CODE(type, fn, method, access) (((type) << 16) | ((access) << 14) | ((fn) << 2) | (method))
#define NT_METHOD_BUFFERED       0
#define NT_METHOD_IN_DIRECT      1
#define NT_METHOD_OUT_DIRECT     2
#define NT_METHOD_NEITHER        3
#define NT_FILE_DEVICE_UNKNOWN   0x00000022

/* The inline helpers the WDK gives drivers */
static inline NT_IO_STACK_LOCATION *NtIrpCurrentStack(NT_IRP *irp)
{
    return irp->Tail.Overlay.CurrentStackLocation;
}
static inline NT_IO_STACK_LOCATION *NtIrpNextStack(NT_IRP *irp)
{
    return irp->Tail.Overlay.CurrentStackLocation - 1;
}

/* The layouts as Windows x64 has them (offsets from the WDK's wdm.h) */
#define NT_OFFSET(t, m) __builtin_offsetof(t, m)
_Static_assert(sizeof(NT_UNICODE_STRING) == 0x10 && sizeof(NT_IO_STATUS_BLOCK) == 0x10, "UNICODE_STRING, IO_STATUS_BLOCK");
_Static_assert(sizeof(NT_KEVENT) == 0x18 && sizeof(NT_KDEVICE_QUEUE) == 0x28 && sizeof(NT_KDPC) == 0x40, "KEVENT, KDEVICE_QUEUE, KDPC");
_Static_assert(sizeof(NT_WAIT_CONTEXT_BLOCK) == 0x48, "WAIT_CONTEXT_BLOCK");
_Static_assert(NT_OFFSET(NT_DRIVER_OBJECT, DriverExtension) == 0x30 && NT_OFFSET(NT_DRIVER_OBJECT, DriverName) == 0x38 &&
               NT_OFFSET(NT_DRIVER_OBJECT, DriverUnload) == 0x68 && NT_OFFSET(NT_DRIVER_OBJECT, MajorFunction) == 0x70 &&
               sizeof(NT_DRIVER_OBJECT) == 0x150, "DRIVER_OBJECT");
_Static_assert(NT_OFFSET(NT_DEVICE_OBJECT, Flags) == 0x30 && NT_OFFSET(NT_DEVICE_OBJECT, DeviceExtension) == 0x40 &&
               NT_OFFSET(NT_DEVICE_OBJECT, DeviceType) == 0x48 && NT_OFFSET(NT_DEVICE_OBJECT, StackSize) == 0x4C &&
               NT_OFFSET(NT_DEVICE_OBJECT, AlignmentRequirement) == 0x98 && NT_OFFSET(NT_DEVICE_OBJECT, Dpc) == 0xC8 &&
               NT_OFFSET(NT_DEVICE_OBJECT, DeviceLock) == 0x118 && NT_OFFSET(NT_DEVICE_OBJECT, DeviceObjectExtension) == 0x138 &&
               sizeof(NT_DEVICE_OBJECT) == 0x150, "DEVICE_OBJECT");
_Static_assert(NT_OFFSET(NT_IO_STACK_LOCATION, Parameters) == 0x08 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.Read.ByteOffset) == 0x18 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.DeviceIoControl.InputBufferLength) == 0x10 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.DeviceIoControl.IoControlCode) == 0x18 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, Parameters.DeviceIoControl.Type3InputBuffer) == 0x20 &&
               NT_OFFSET(NT_IO_STACK_LOCATION, DeviceObject) == 0x28 && NT_OFFSET(NT_IO_STACK_LOCATION, CompletionRoutine) == 0x38 &&
               sizeof(NT_IO_STACK_LOCATION) == 0x48, "IO_STACK_LOCATION");
_Static_assert(NT_OFFSET(NT_IRP, MdlAddress) == 0x08 && NT_OFFSET(NT_IRP, AssociatedIrp) == 0x18 &&
               NT_OFFSET(NT_IRP, IoStatus) == 0x30 && NT_OFFSET(NT_IRP, RequestorMode) == 0x40 &&
               NT_OFFSET(NT_IRP, CurrentLocation) == 0x43 && NT_OFFSET(NT_IRP, UserIosb) == 0x48 &&
               NT_OFFSET(NT_IRP, UserEvent) == 0x50 && NT_OFFSET(NT_IRP, CancelRoutine) == 0x68 &&
               NT_OFFSET(NT_IRP, UserBuffer) == 0x70 && NT_OFFSET(NT_IRP, Tail.Overlay.Thread) == 0x98 &&
               NT_OFFSET(NT_IRP, Tail.Overlay.CurrentStackLocation) == 0xB8 &&
               NT_OFFSET(NT_IRP, Tail.Overlay.OriginalFileObject) == 0xC0 && sizeof(NT_IRP) == 0xD0, "IRP");
_Static_assert(NT_OFFSET(NT_FILE_OBJECT, FsContext) == 0x18 && NT_OFFSET(NT_FILE_OBJECT, FinalStatus) == 0x38 &&
               NT_OFFSET(NT_FILE_OBJECT, Flags) == 0x50 && NT_OFFSET(NT_FILE_OBJECT, FileName) == 0x58 &&
               NT_OFFSET(NT_FILE_OBJECT, CurrentByteOffset) == 0x68 && NT_OFFSET(NT_FILE_OBJECT, Event) == 0x98 &&
               NT_OFFSET(NT_FILE_OBJECT, IrpList) == 0xC0 && sizeof(NT_FILE_OBJECT) == 0xD8, "FILE_OBJECT");
