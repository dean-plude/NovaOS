/* wudfx.h — the UMDF 1 types the framework and the host still share
 * (NovaOS stand-in for the internal header, README.md), declared in
 * namespace UMINT (wudfx_namespace_on.h) */
#pragma once

typedef enum _WDF_REQUEST_TYPE {
    WdfRequestUndefined = 0,
    WdfRequestCreate,
    WdfRequestCleanup,
    WdfRequestRead,
    WdfRequestWrite,
    WdfRequestDeviceIoControl,
    WdfRequestClose,
    WdfRequestInternalIoctl,
    WdfRequestFlushBuffers,
    WdfRequestQueryInformation,
    WdfRequestSetInformation,
    WdfRequestUsb,
    WdfRequestOther,
    WdfRequestMaximum
} WDF_REQUEST_TYPE;

typedef enum _WDF_DEVICE_IO_TYPE {
    WdfDeviceIoUndefined = 0,
    WdfDeviceIoNeither,
    WdfDeviceIoBuffered,
    WdfDeviceIoDirect,
    WdfDeviceIoBufferedOrDirect,
    WdfDeviceIoMaximum
} WDF_DEVICE_IO_TYPE;

typedef enum _WDF_DEVICE_IO_BUFFER_RETRIEVAL {
    WdfDeviceIoBufferRetrievalUndefined = 0,
    WdfDeviceIoBufferRetrievalCopyImmediately,
    WdfDeviceIoBufferRetrievalDeferred,
    WdfDeviceIoBufferRetrievalMaximum
} WDF_DEVICE_IO_BUFFER_RETRIEVAL;

typedef enum _WDF_PROPERTY_STORE_ROOT_CLASS {
    WdfPropertyStoreRootClassHardwareKey = 1,
    WdfPropertyStoreRootClassSoftwareKey,
    WdfPropertyStoreRootClassDeviceInterfaceKey,
    WdfPropertyStoreRootClassLegacyHardwareKey,
    WdfPropertyStoreRootDriverParametersKey,
    WdfPropertyStoreRootDriverPersistentStateKey
} WDF_PROPERTY_STORE_ROOT_CLASS;

typedef struct _WDF_PROPERTY_STORE_ROOT {
    ULONG LengthCb;
    WDF_PROPERTY_STORE_ROOT_CLASS RootClass;
    union {
        struct { PCWSTR ServiceName; } HardwareKey;
        struct { const GUID *InterfaceGUID; PCWSTR ReferenceString; } DeviceInterfaceKey;
        struct { PCWSTR LegacyMapName; } LegacyHardwareKey;
        struct { PCWSTR ServiceName; } ParametersKey;
    } Qualifier;
} WDF_PROPERTY_STORE_ROOT, *PWDF_PROPERTY_STORE_ROOT;

typedef enum _WDF_PROPERTY_STORE_RETRIEVE_FLAGS {
    WdfPropertyStoreNormal = 0,
    WdfPropertyStoreCreateIfMissing = 1,
    WdfPropertyStoreCreateVolatile = 2
} WDF_PROPERTY_STORE_RETRIEVE_FLAGS;

typedef enum _WDF_PROPERTY_STORE_DISPOSITION {
    CreatedNewStore = 0,
    OpenedExistingStore = 1
} WDF_PROPERTY_STORE_DISPOSITION;

typedef enum _WDF_DEVICE_HWACCESS_TARGET_TYPE {
    WdfDeviceHwAccessTargetTypeInvalid = 0,
    WdfDeviceHwAccessTargetTypeRegister,
    WdfDeviceHwAccessTargetTypeRegisterBuffer,
    WdfDeviceHwAccessTargetTypePort,
    WdfDeviceHwAccessTargetTypePortBuffer,
    WdfDeviceHwAccessTargetTypeMaximum
} WDF_DEVICE_HWACCESS_TARGET_TYPE;

typedef enum _WDF_DEVICE_HWACCESS_TARGET_SIZE {
    WdfDeviceHwAccessTargetSizeInvalid = 0,
    WdfDeviceHwAccessTargetSizeUchar,
    WdfDeviceHwAccessTargetSizeUshort,
    WdfDeviceHwAccessTargetSizeUlong,
    WdfDeviceHwAccessTargetSizeUlong64,
    WdfDeviceHwAccessTargetSizeMaximum
} WDF_DEVICE_HWACCESS_TARGET_SIZE;
