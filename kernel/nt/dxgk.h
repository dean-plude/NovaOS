/*
 * dxgk.h — the WDDM display miniport interface, in Windows' x64 layout
 * (kernel/nt/dxgkrnl.c is NovaOS's stand-in for dxgkrnl.sys)
 *
 * A Windows display driver is a miniport of Microsoft's DirectX graphics
 * kernel (dxgkrnl.sys): its DriverEntry hands dxgkrnl a table of DxgkDdi*
 * functions, dxgkrnl answers Plug and Play for it and gives it, at
 * DxgkDdiStartDevice, a DXGKRNL_INTERFACE of DxgkCb* callbacks.  Display
 * topology and modes travel as a VidPN (video present network): paths
 * from sources (views) to targets (outputs), each with a mode set, that
 * the driver inspects and fills through the VidPN manager's interfaces.
 *
 * The structures and orders here are Microsoft's, written from the WDK's
 * public documentation (dispmprt.h, d3dkmddi.h, d3dkmdt.h, d3dukmdt.h on
 * learn.microsoft.com) and checked below where a member's offset matters
 * to the driver.  Step 1 of the Intel GPU design (docs/design/intel-gpu.md)
 * uses the display-only part (KMDDOD_INITIALIZATION_DATA, as Microsoft's
 * KMDOD sample registers); step 3 the render DDIs (DRIVER_INITIALIZATION_DATA,
 * allocations and segments so far).  It is
 * shared by the host and NovaOS's test miniport (tools/drivertest/
 * wddmdod.c, wddmtest.c), so it uses only C's own types and wdm.h's.
 */
#pragma once

#include "wdm.h"

typedef unsigned int D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef unsigned int D3DDDI_VIDEO_PRESENT_TARGET_ID;
typedef unsigned int D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID;
typedef unsigned int D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID;
typedef void *D3DKMDT_HVIDPN;
typedef void *D3DKMDT_HVIDPNTOPOLOGY;
typedef void *D3DKMDT_HVIDPNSOURCEMODESET;
typedef void *D3DKMDT_HVIDPNTARGETMODESET;
typedef unsigned long long DXGK_PHYSICAL_ADDRESS;
struct DXGKARGCB_GETHANDLEDATA;

/* \Device\DxgKrnl's internal control code that returns dxgkrnl's
 * initialisation function, which displib.lib's DxgkInitialize sends and
 * calls with a DRIVER_INITIALIZATION_DATA:
 * CTL_CODE(FILE_DEVICE_VIDEO, 0xF, METHOD_NEITHER, FILE_ANY_ACCESS).
 * What DxgkInitializeDisplayOnlyDriver (Windows 8's, for a
 * KMDDOD_INITIALIZATION_DATA) sends instead is not documented; NovaOS's
 * displib stand-in asks for the display-only function with the next
 * function code (an assumption step 2 checks against a real KMDOD driver) */
#define IOCTL_VIDEO_DDI_FUNC_REGISTER     0x0023003Fu
#define IOCTL_VIDEO_DDI_FUNC_REGISTER_DOD 0x00230043u   /* (NovaOS's: function 0x10) */

#define DXGKDDI_INTERFACE_VERSION_WIN8  0x300E
#define DXGKDDI_INTERFACE_VERSION_WDDM2_0 0x5023
#define DXGKDDI_WDDMv1_2                0x1200
#define DXGKDDI_WDDMv2                  0x2000
#define DXGKDDI_INTERFACE_VERSION_WIN7  0x2005
#define DXGKDDI_INTERFACE_VERSION_WDDM1_3 0x4002

#define D3DDDIFMT_A8R8G8B8 21
#define D3DDDIFMT_X8R8G8B8 22

/* NTSTATUS values the display interfaces use */
#define STATUS_GRAPHICS_MODE_NOT_PINNED                    ((NT_STATUS)0x401E0307)   /* (informational: succeeded) */
#define STATUS_GRAPHICS_DATASET_IS_EMPTY                    ((NT_STATUS)0x401E034B)
#define STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET         ((NT_STATUS)0x401E034C)
#define STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE        ((NT_STATUS)0xC01E0302)
#define STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET        ((NT_STATUS)0xC01E0303)
#define STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN     ((NT_STATUS)0xC01E030B)
#define STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY              ((NT_STATUS)0xC01E0304)
#define STATUS_GRAPHICS_PATH_NOT_IN_TOPOLOGY                ((NT_STATUS)0xC01E0327)
#define STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET             ((NT_STATUS)0xC01E0314)
#define STATUS_GRAPHICS_INVALID_FREQUENCY                   ((NT_STATUS)0xC01E030E)
#define STATUS_MONITOR_NO_DESCRIPTOR                        ((NT_STATUS)0xC01D0001)

/* -----------------------------------------------------------------------
 * d3dkmdt.h: modes and paths
 * ----------------------------------------------------------------------- */
typedef struct { unsigned int cx, cy; } D3DKMDT_2DREGION, D3DKMDT_2DOFFSET;
typedef struct { unsigned int Numerator, Denominator; } D3DDDI_RATIONAL;

enum { D3DKMDT_RMT_UNINITIALIZED, D3DKMDT_RMT_GRAPHICS, D3DKMDT_RMT_TEXT };            /* source mode type */
enum { D3DKMDT_CB_UNINITIALIZED, D3DKMDT_CB_INTENSITY, D3DKMDT_CB_SRGB, D3DKMDT_CB_SCRGB };
enum { D3DKMDT_PVAM_UNINITIALIZED, D3DKMDT_PVAM_DIRECT, D3DKMDT_PVAM_PRESETPALETTE };
enum { D3DKMDT_MP_UNINITIALIZED, D3DKMDT_MP_PREFERRED, D3DKMDT_MP_NOTPREFERRED };      /* mode preference */
#define D3DKMDT_VSS_OTHER 255                     /* video signal standard: none of the TV or VESA ones */

typedef struct {
    D3DKMDT_2DREGION PrimSurfSize;
    D3DKMDT_2DREGION VisibleRegionSize;
    unsigned long long Stride;                    /* SIZE_T */
    int              PixelFormat;                 /* D3DDDIFORMAT */
    int              ColorBasis;                  /* D3DKMDT_COLOR_BASIS */
    int              PixelValueAccessMode;
} D3DKMDT_GRAPHICS_RENDERING_FORMAT;

typedef struct {
    D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID Id;
    int Type;                                     /* D3DKMDT_RMT_* */
    union {
        D3DKMDT_GRAPHICS_RENDERING_FORMAT Graphics;
        struct { int PixelFormat; int ColorBasis; } Text;   /* (no text modes here) */
    } Format;
} D3DKMDT_VIDPN_SOURCE_MODE;

typedef struct {
    int              VideoStandard;               /* D3DKMDT_VIDEO_SIGNAL_STANDARD */
    D3DKMDT_2DREGION TotalSize;
    D3DKMDT_2DREGION ActiveSize;
    D3DDDI_RATIONAL  VSyncFreq;
    D3DDDI_RATIONAL  HSyncFreq;
    unsigned long long PixelRate;                 /* SIZE_T */
    unsigned int     ScanLineOrdering;            /* (union with AdditionalSignalInfo's bit fields) */
} D3DKMDT_VIDEO_SIGNAL_INFO;

typedef struct {
    D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID Id;
    D3DKMDT_VIDEO_SIGNAL_INFO VideoSignalInfo;
    unsigned int     WireFormatAndPreference;     /* D3DKMDT_WIRE_FORMAT_AND_PREFERENCE bit fields */
    int              Preference;                  /* D3DKMDT_MODE_PREFERENCE */
} D3DKMDT_VIDPN_TARGET_MODE;

enum { D3DKMDT_VPPS_UNINITIALIZED, D3DKMDT_VPPS_IDENTITY, D3DKMDT_VPPS_CENTERED, D3DKMDT_VPPS_STRETCHED };
enum { D3DKMDT_VPPR_UNINITIALIZED, D3DKMDT_VPPR_IDENTITY };
enum { D3DKMDT_VPPI_UNINITIALIZED, D3DKMDT_VPPI_PRIMARY };
enum { D3DKMDT_VPPC_UNINITIALIZED, D3DKMDT_VPPC_GRAPHICS };

typedef struct {
    int          Scaling;                         /* D3DKMDT_VPPS_* */
    unsigned int ScalingSupport;                  /* bit 0 identity, 1 centered, 2 stretched, ... */
    int          Rotation;                        /* D3DKMDT_VPPR_* */
    unsigned int RotationSupport;                 /* bit 0 identity, ... */
} D3DKMDT_VIDPN_PRESENT_PATH_TRANSFORMATION;

typedef struct {
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId;
    int              ImportanceOrdinal;           /* D3DKMDT_VPPI_* */
    D3DKMDT_VIDPN_PRESENT_PATH_TRANSFORMATION ContentTransformation;
    D3DKMDT_2DOFFSET VisibleFromActiveTLOffset;
    D3DKMDT_2DOFFSET VisibleFromActiveBROffset;
    int              VidPnTargetColorBasis;       /* D3DKMDT_COLOR_BASIS */
    unsigned int     VidPnTargetColorCoeffDynamicRanges[4];
    int              Content;                     /* D3DKMDT_VPPC_* */
    struct {
        int           CopyProtectionType;
        unsigned int  APSTriggerBits;
        unsigned char OEMCopyProtection[256];
        unsigned int  CopyProtectionSupport;
    } CopyProtection;
    struct {
        int                Type;                  /* D3DDDI_GAMMARAMP_TYPE */
        unsigned long long DataSize;
        void              *Data;
    } GammaRamp;
} D3DKMDT_VIDPN_PRESENT_PATH;

/* -----------------------------------------------------------------------
 * d3dkmddi.h: the VidPN manager's interfaces (DxgkCbQueryVidPnInterface)
 * ----------------------------------------------------------------------- */
#define DXGK_VIDPN_INTERFACE_VERSION_V1 1

typedef struct {
    NT_STATUS (NT_API *pfnGetNumPaths)(D3DKMDT_HVIDPNTOPOLOGY, unsigned long long *n);
    NT_STATUS (NT_API *pfnGetNumPathsFromSource)(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID,
                                                 unsigned long long *n);
    NT_STATUS (NT_API *pfnEnumPathTargetsFromSource)(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID,
                                                     unsigned long long index, D3DDDI_VIDEO_PRESENT_TARGET_ID *t);
    NT_STATUS (NT_API *pfnGetPathSourceFromTarget)(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_TARGET_ID,
                                                   D3DDDI_VIDEO_PRESENT_SOURCE_ID *s);
    NT_STATUS (NT_API *pfnAcquirePathInfo)(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID,
                                           D3DDDI_VIDEO_PRESENT_TARGET_ID, const D3DKMDT_VIDPN_PRESENT_PATH **);
    NT_STATUS (NT_API *pfnAcquireFirstPathInfo)(D3DKMDT_HVIDPNTOPOLOGY, const D3DKMDT_VIDPN_PRESENT_PATH **);
    NT_STATUS (NT_API *pfnAcquireNextPathInfo)(D3DKMDT_HVIDPNTOPOLOGY, const D3DKMDT_VIDPN_PRESENT_PATH *cur,
                                               const D3DKMDT_VIDPN_PRESENT_PATH **next);
    NT_STATUS (NT_API *pfnUpdatePathSupportInfo)(D3DKMDT_HVIDPNTOPOLOGY, const D3DKMDT_VIDPN_PRESENT_PATH *);
    NT_STATUS (NT_API *pfnReleasePathInfo)(D3DKMDT_HVIDPNTOPOLOGY, const D3DKMDT_VIDPN_PRESENT_PATH *);
    NT_STATUS (NT_API *pfnCreateNewPathInfo)(D3DKMDT_HVIDPNTOPOLOGY, D3DKMDT_VIDPN_PRESENT_PATH **);
    NT_STATUS (NT_API *pfnAddPath)(D3DKMDT_HVIDPNTOPOLOGY, D3DKMDT_VIDPN_PRESENT_PATH *);
    NT_STATUS (NT_API *pfnRemovePath)(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID,
                                      D3DDDI_VIDEO_PRESENT_TARGET_ID);
} DXGK_VIDPNTOPOLOGY_INTERFACE;

typedef struct {
    NT_STATUS (NT_API *pfnGetNumModes)(D3DKMDT_HVIDPNSOURCEMODESET, unsigned long long *n);
    NT_STATUS (NT_API *pfnAcquireFirstModeInfo)(D3DKMDT_HVIDPNSOURCEMODESET, const D3DKMDT_VIDPN_SOURCE_MODE **);
    NT_STATUS (NT_API *pfnAcquireNextModeInfo)(D3DKMDT_HVIDPNSOURCEMODESET, const D3DKMDT_VIDPN_SOURCE_MODE *cur,
                                               const D3DKMDT_VIDPN_SOURCE_MODE **next);
    NT_STATUS (NT_API *pfnAcquirePinnedModeInfo)(D3DKMDT_HVIDPNSOURCEMODESET, const D3DKMDT_VIDPN_SOURCE_MODE **);
    NT_STATUS (NT_API *pfnReleaseModeInfo)(D3DKMDT_HVIDPNSOURCEMODESET, const D3DKMDT_VIDPN_SOURCE_MODE *);
    NT_STATUS (NT_API *pfnCreateNewModeInfo)(D3DKMDT_HVIDPNSOURCEMODESET, D3DKMDT_VIDPN_SOURCE_MODE **);
    NT_STATUS (NT_API *pfnAddMode)(D3DKMDT_HVIDPNSOURCEMODESET, const D3DKMDT_VIDPN_SOURCE_MODE *);
    NT_STATUS (NT_API *pfnPinMode)(D3DKMDT_HVIDPNSOURCEMODESET, D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID);
} DXGK_VIDPNSOURCEMODESET_INTERFACE;

typedef struct {
    NT_STATUS (NT_API *pfnGetNumModes)(D3DKMDT_HVIDPNTARGETMODESET, unsigned long long *n);
    NT_STATUS (NT_API *pfnAcquireFirstModeInfo)(D3DKMDT_HVIDPNTARGETMODESET, const D3DKMDT_VIDPN_TARGET_MODE **);
    NT_STATUS (NT_API *pfnAcquireNextModeInfo)(D3DKMDT_HVIDPNTARGETMODESET, const D3DKMDT_VIDPN_TARGET_MODE *cur,
                                               const D3DKMDT_VIDPN_TARGET_MODE **next);
    NT_STATUS (NT_API *pfnAcquirePinnedModeInfo)(D3DKMDT_HVIDPNTARGETMODESET, const D3DKMDT_VIDPN_TARGET_MODE **);
    NT_STATUS (NT_API *pfnReleaseModeInfo)(D3DKMDT_HVIDPNTARGETMODESET, const D3DKMDT_VIDPN_TARGET_MODE *);
    NT_STATUS (NT_API *pfnCreateNewModeInfo)(D3DKMDT_HVIDPNTARGETMODESET, D3DKMDT_VIDPN_TARGET_MODE **);
    NT_STATUS (NT_API *pfnAddMode)(D3DKMDT_HVIDPNTARGETMODESET, const D3DKMDT_VIDPN_TARGET_MODE *);
    NT_STATUS (NT_API *pfnPinMode)(D3DKMDT_HVIDPNTARGETMODESET, D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID);
} DXGK_VIDPNTARGETMODESET_INTERFACE;

typedef struct {
    int Version;                                  /* DXGK_VIDPN_INTERFACE_VERSION */
    NT_STATUS (NT_API *pfnGetTopology)(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNTOPOLOGY *,
                                       const DXGK_VIDPNTOPOLOGY_INTERFACE **);
    NT_STATUS (NT_API *pfnAcquireSourceModeSet)(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID,
                                                D3DKMDT_HVIDPNSOURCEMODESET *, const DXGK_VIDPNSOURCEMODESET_INTERFACE **);
    NT_STATUS (NT_API *pfnReleaseSourceModeSet)(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNSOURCEMODESET);
    NT_STATUS (NT_API *pfnCreateNewSourceModeSet)(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID,
                                                  D3DKMDT_HVIDPNSOURCEMODESET *, const DXGK_VIDPNSOURCEMODESET_INTERFACE **);
    NT_STATUS (NT_API *pfnAssignSourceModeSet)(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DKMDT_HVIDPNSOURCEMODESET);
    NT_STATUS (NT_API *pfnAssignMultisamplingMethodSet)(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID,
                                                        unsigned long long n, const void *methods);
    NT_STATUS (NT_API *pfnAcquireTargetModeSet)(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_TARGET_ID,
                                                D3DKMDT_HVIDPNTARGETMODESET *, const DXGK_VIDPNTARGETMODESET_INTERFACE **);
    NT_STATUS (NT_API *pfnReleaseTargetModeSet)(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNTARGETMODESET);
    NT_STATUS (NT_API *pfnCreateNewTargetModeSet)(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_TARGET_ID,
                                                  D3DKMDT_HVIDPNTARGETMODESET *, const DXGK_VIDPNTARGETMODESET_INTERFACE **);
    NT_STATUS (NT_API *pfnAssignTargetModeSet)(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HVIDPNTARGETMODESET);
} DXGK_VIDPN_INTERFACE;

/* -----------------------------------------------------------------------
 * dispmprt.h: children (outputs), device information, the callbacks
 * ----------------------------------------------------------------------- */
enum { TypeUninitialized, TypeVideoOutput, TypeOther };                       /* DXGK_CHILD_DEVICE_TYPE */
enum { HpdAwarenessUninitialized, HpdAwarenessAlwaysConnected, HpdAwarenessNone, HpdAwarenessPolled,
       HpdAwarenessInterruptible };
enum { StatusUninitialized, StatusConnection, StatusRotation };              /* DXGK_CHILD_STATUS_TYPE */
enum { D3DKMDT_MCC_UNINITIALIZED, D3DKMDT_MCC_IGNORE, D3DKMDT_MCC_ENFORCE };   /* monitor connectivity checks */
#define D3DKMDT_VOT_OTHER    (-1)
#define D3DKMDT_VOT_INTERNAL ((int)0x80000000)

typedef struct {
    union {
        struct {
            int           InterfaceTechnology;    /* D3DKMDT_VIDEO_OUTPUT_TECHNOLOGY */
            int           MonitorOrientationAwareness;
            unsigned char SupportsSdtvModes;
        } VideoOutput;
        struct { unsigned int MustBeZero; } Other;
    } Type;
    int HpdAwareness;                             /* DXGK_CHILD_DEVICE_HPD_AWARENESS */
} DXGK_CHILD_CAPABILITIES;

typedef struct {
    int                     ChildDeviceType;
    DXGK_CHILD_CAPABILITIES ChildCapabilities;
    unsigned int            AcpiUid;
    unsigned int            ChildUid;
} DXGK_CHILD_DESCRIPTOR;

typedef struct {
    int          Type;                            /* DXGK_CHILD_STATUS_TYPE */
    unsigned int ChildUid;
    union {
        struct { unsigned char Connected; } HotPlug;
        struct { unsigned char Angle; } Rotation;
        unsigned char Reserved[16];
    };
} DXGK_CHILD_STATUS;

typedef struct {
    unsigned int  DescriptorOffset;
    unsigned int  DescriptorLength;
    void         *DescriptorBuffer;
} DXGK_DEVICE_DESCRIPTOR;

typedef struct {
    void             *MiniportDeviceContext;
    NT_DEVICE_OBJECT *PhysicalDeviceObject;
    NT_UNICODE_STRING DeviceRegistryPath;
    NT_CM_RESOURCE_LIST *TranslatedResourceList;
    long long         SystemMemorySize;
    DXGK_PHYSICAL_ADDRESS HighestPhysicalAddress;
    DXGK_PHYSICAL_ADDRESS AgpApertureBase;
    unsigned long long AgpApertureSize;
    int               DockingState;
} DXGK_DEVICE_INFO;

/* What dxgkrnl and a display-only driver hand each other about the screen
 * the firmware (or the previous driver) left: DxgkCbAcquirePostDisplayOwnership
 * and DxgkDdiStopDeviceAndReleasePostDisplayOwnership */
typedef struct {
    unsigned int Width, Height, Pitch;
    int          ColorFormat;                     /* D3DDDIFORMAT */
    DXGK_PHYSICAL_ADDRESS PhysicAddress;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    unsigned int AcpiId;
} DXGK_DISPLAY_INFORMATION;

typedef struct {
    unsigned int RequiredDmaQueueEntry;
    NT_GUID      AdapterGuid;
    struct { unsigned int LowPart; int HighPart; } AdapterLuid;
} DXGK_START_INFO;

#define DXGK_WHICHSPACE_CONFIG 0                  /* DxgkCbReadDeviceSpace: PCI configuration space */

typedef unsigned char (NT_API *PKSYNCHRONIZE_ROUTINE)(void *ctx);

typedef struct {
    unsigned int Size;
    unsigned int Version;
    void        *DeviceHandle;
    NT_STATUS (NT_API *DxgkCbEvalAcpiMethod)(void *h, unsigned int uid, void *in, unsigned int in_len, void *out,
                                             unsigned int out_len);
    NT_STATUS (NT_API *DxgkCbGetDeviceInformation)(void *h, DXGK_DEVICE_INFO *info);
    NT_STATUS (NT_API *DxgkCbIndicateChildStatus)(void *h, DXGK_CHILD_STATUS *st);
    NT_STATUS (NT_API *DxgkCbMapMemory)(void *h, DXGK_PHYSICAL_ADDRESS pa, unsigned int len, unsigned char io,
                                        unsigned char user, int cache, void **va);
    unsigned char (NT_API *DxgkCbQueueDpc)(void *h);
    NT_STATUS (NT_API *DxgkCbQueryServices)(void *h, int type, NT_INTERFACE *iface);
    NT_STATUS (NT_API *DxgkCbReadDeviceSpace)(void *h, unsigned int space, void *buf, unsigned int off,
                                              unsigned int len, unsigned int *done);
    NT_STATUS (NT_API *DxgkCbSynchronizeExecution)(void *h, PKSYNCHRONIZE_ROUTINE fn, void *ctx, unsigned int msg,
                                                   unsigned char *ret);
    NT_STATUS (NT_API *DxgkCbUnmapMemory)(void *h, void *va);
    NT_STATUS (NT_API *DxgkCbWriteDeviceSpace)(void *h, unsigned int space, void *buf, unsigned int off,
                                               unsigned int len, unsigned int *done);
    NT_STATUS (NT_API *DxgkCbIsDevicePresent)(void *h, void *params, unsigned char *present);
    void *(NT_API *DxgkCbGetHandleData)(const struct DXGKARGCB_GETHANDLEDATA *);
    void *DxgkCbGetHandleParent, *DxgkCbEnumHandleChildren;
    void (NT_API *DxgkCbNotifyInterrupt)(void *h, const void *data);
    void (NT_API *DxgkCbNotifyDpc)(void *h);
    NT_STATUS (NT_API *DxgkCbQueryVidPnInterface)(D3DKMDT_HVIDPN, int version, const DXGK_VIDPN_INTERFACE **);
    NT_STATUS (NT_API *DxgkCbQueryMonitorInterface)(void *h, int version, const void **iface);
    void *DxgkCbGetCaptureAddress, *DxgkCbLogEtwEvent, *DxgkCbExcludeAdapterAccess;
    /* DXGKDDI_INTERFACE_VERSION_WIN8 */
    void *DxgkCbCreateContextAllocation, *DxgkCbDestroyContextAllocation;
    void *DxgkCbSetPowerComponentActive, *DxgkCbSetPowerComponentIdle;
    NT_STATUS (NT_API *DxgkCbAcquirePostDisplayOwnership)(void *h, DXGK_DISPLAY_INFORMATION *info);
    void *DxgkCbPowerRuntimeControlRequest, *DxgkCbSetPowerComponentLatency, *DxgkCbSetPowerComponentResidency;
    void *DxgkCbCompleteFStateTransition;
    /* later versions: the rest of the 70 callbacks Windows 11's WDK lists
     * (WDDM 3.1), none offered yet (NULL), reserved at their size */
    void *Later[40];
} DXGKRNL_INTERFACE;

/* -----------------------------------------------------------------------
 * The DDIs a display-only driver implements
 * ----------------------------------------------------------------------- */
typedef struct {
    int          Type;                            /* DXGK_QUERYADAPTERINFOTYPE */
    void        *pInputData;
    unsigned int InputDataSize;
    void        *pOutputData;
    unsigned int OutputDataSize;
    unsigned int Flags;
    void        *hKmdProcessHandle;
} DXGKARG_QUERYADAPTERINFO;

#define DXGKQAITYPE_DRIVERCAPS 1

/* DXGK_DRIVERCAPS, through WDDM 2.1's members; the bit-field unions are
 * UINTs here.  What follows them in later WDKs (MiscCaps, hardware-queued
 * flips) is reserved at a generous size: dxgkrnl hands the driver its own
 * structure's size, and a driver checks only that it is big enough */
typedef struct {
    DXGK_PHYSICAL_ADDRESS HighestAcceptableAddress;
    unsigned int MaxAllocationListSlotId;
    unsigned long long ApertureSegmentCommitLimit;
    unsigned int MaxPointerWidth, MaxPointerHeight;
    unsigned int PointerCaps;                     /* DXGK_POINTERFLAGS */
    unsigned int InterruptMessageNumber;
    unsigned int NumberOfSwizzlingRanges;
    unsigned int MaxOverlays;
    unsigned int GammaRampCaps;                   /* (or ColorTransformCaps) */
    unsigned int PresentationCaps;                /* DXGK_PRESENTATIONCAPS */
    unsigned int MaxQueuedFlipOnVSync;
    unsigned int FlipCaps;                        /* DXGK_FLIPCAPS */
    unsigned int SchedulingCaps;                  /* DXGK_VIDSCHCAPS */
    struct { unsigned int Value, PagingNode; } MemoryManagementCaps;   /* DXGK_VIDMMCAPS */
    struct { unsigned int NbAsymetricProcessingNodes, Reserved[64]; } GpuEngineTopology;
    int          WDDMVersion;                     /* DXGK_WDDMVERSION */
    unsigned int ReservedVirtualAddressCaps[7];   /* (DXGK_VIRTUALADDRESSCAPS_DEPRECATED) */
    unsigned int ReservedDmaBufferCaps[40];       /* (DXGK_DMABUFFERCAPS_DEPRECATED) */
    struct { int Graphics, Compute; } PreemptionCaps;   /* D3DKMDT_PREEMPTION_CAPS */
    unsigned char SupportNonVGA, SupportSmoothRotation, SupportPerEngineTDR, SupportDirectFlip,
                  SupportMultiPlaneOverlay, SupportRuntimePowerManagement, SupportSurpriseRemovalInHibernation;
    unsigned char HybridDiscrete;                 /* WDDM 1.3 */
    unsigned int  MaxOverlayPlanes;
    unsigned char HybridIntegrated;               /* WDDM 2.0 */
    unsigned long long InternalGpuVirtualAddressRangeStart, InternalGpuVirtualAddressRangeEnd;
    unsigned char SupportSurpriseRemoval;
    unsigned char SupportMultiPlaneOverlayImmediateFlip, CursorScaledWithMultiPlaneOverlayPlane0,
                  HybridAcpiChainingRequired;     /* WDDM 2.1 */
    unsigned int  MaxQueuedMultiPlaneOverlayFlipVSync;
    unsigned char Later[256];
} DXGK_DRIVERCAPS;

typedef struct { int left, top, right, bottom; } DXGK_RECT;
typedef struct { struct { int x, y; } SourcePoint; DXGK_RECT DestRect; } D3DKMT_MOVE_RECT;

typedef struct {
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    void            *pSource;
    unsigned int     BytesPerPixel;
    int              Pitch;
    unsigned int     Flags;                       /* D3DKMT_PRESENT_DISPLAY_ONLY_FLAGS: bit 0 Rotate */
    unsigned int     NumMoves;
    D3DKMT_MOVE_RECT *pMoves;
    unsigned int     NumDirtyRects;
    DXGK_RECT       *pDirtyRect;
    void            *pfnPresentDisplayOnlyProgress;
} DXGKARG_PRESENT_DISPLAYONLY;

typedef struct { D3DKMDT_HVIDPN hDesiredVidPn; unsigned char IsVidPnSupported; } DXGKARG_ISSUPPORTEDVIDPN;

enum { D3DKMDT_EPT_UNINITIALIZED, D3DKMDT_EPT_VIDPNSOURCE, D3DKMDT_EPT_VIDPNTARGET, D3DKMDT_EPT_SCALING,
       D3DKMDT_EPT_ROTATION, D3DKMDT_EPT_NOPIVOT };
typedef struct {
    D3DKMDT_HVIDPN hConstrainingVidPn;
    int            EnumPivotType;                 /* D3DKMDT_EPT_* */
    struct { D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId; } EnumPivot;
} DXGKARG_ENUMVIDPNCOFUNCMODALITY;

typedef struct { D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; unsigned char Visible; } DXGKARG_SETVIDPNSOURCEVISIBILITY;

typedef struct {
    D3DKMDT_HVIDPN hFunctionalVidPn;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID AffectedVidPnSourceId;
    int            MonitorConnectivityChecks;     /* D3DKMDT_MONITOR_CONNECTIVITY_CHECKS */
    void          *hPrimaryAllocation;
    unsigned int   Flags;                         /* DXGKARG_COMMITVIDPN_FLAGS */
} DXGKARG_COMMITVIDPN;

typedef struct { D3DKMDT_VIDPN_PRESENT_PATH VidPnPresentPathInfo; } DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH;

typedef struct {
    D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId;
    void *hMonitorSourceModeSet;
    const void *pMonitorSourceModeSetInterface;
} DXGKARG_RECOMMENDMONITORMODES;

typedef struct {
    D3DKMDT_HVIDPN hVidPn;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    unsigned int   VidPnHWCaps;                   /* D3DKMDT_VIDPN_HW_CAPABILITY bit fields */
} DXGKARG_QUERYVIDPNHWCAPABILITY;

/* -----------------------------------------------------------------------
 * d3dkmddi.h: the render DDIs' arguments (step 3 of the Intel GPU design)
 * ----------------------------------------------------------------------- */
#define DXGKQAITYPE_QUERYSEGMENT4 11
#define DXGKQAITYPE_GPUMMUCAPS    13

/* DXGK_SEGMENTFLAGS bits */
#define DXGK_SEGMENT_APERTURE              0x001
#define DXGK_SEGMENT_CPU_VISIBLE           0x004
#define DXGK_SEGMENT_CACHE_COHERENT        0x010
#define DXGK_SEGMENT_POPULATED_FROM_SYSMEM 0x040
#define DXGK_SEGMENT_USE_64KB_PAGES        0x800

typedef struct {
    unsigned int  Flags;                          /* DXGK_SEGMENTFLAGS */
    DXGK_PHYSICAL_ADDRESS BaseAddress;
    unsigned long long Size, CommitLimit, SystemMemoryEndAddress;
    union {
        DXGK_PHYSICAL_ADDRESS CpuTranslatedAddress;
        struct { unsigned long long PhysicalAddress; unsigned int SizeInPages; } CpuHostAperture;
    };
    unsigned int  NumInvalidMemoryRanges;
    unsigned long long VprRangeStartOffset, VprRangeSize;
    unsigned int  VprAlignment, NumVprSupported, VprReserveSize, NumUEFIFrameBufferRanges;
} DXGK_SEGMENTDESCRIPTOR4;

/* Asked twice: with NbSegment 0 for the count, then with the array */
typedef struct {
    unsigned int  NbSegment;
    unsigned char *pSegmentDescriptor;            /* (SegmentDescriptorStride apart) */
    unsigned int  PagingBufferSegmentId, PagingBufferSize, PagingBufferPrivateDataSize;
    unsigned long long SegmentDescriptorStride;
} DXGK_QUERYSEGMENTOUT4;

typedef struct {
    void        *pPrivateDriverData;              /* in: the user-mode driver's, per allocation */
    unsigned int PrivateDriverDataSize;
    unsigned int Alignment;                       /* (or MinimumPageSize, RecommendedPageSize) */
    unsigned long long Size, PitchAlignedSize;
    unsigned int HintedBank;                      /* DXGK_SEGMENTBANKPREFERENCE */
    unsigned int PreferredSegment;                /* DXGK_SEGMENTPREFERENCE: 5-bit segment ids */
    unsigned int SupportedReadSegmentSet;         /* (or MmuSet) bit n: segment n + 1 */
    unsigned int SupportedWriteSegmentSet;        /* (WDDM 2: the one VidMm uses) */
    unsigned int EvictionSegmentSet;
    unsigned int MaximumRenamingListLength;
    void        *hAllocation;                     /* out: the driver's */
    unsigned int Flags;                           /* DXGK_ALLOCATIONINFOFLAGS(_WDDM2_0) */
    void        *pAllocationUsageHint;
    unsigned int AllocationPriority;
    unsigned int Flags2;
} DXGK_ALLOCATIONINFO;

typedef struct {
    const void  *pPrivateDriverData;              /* the resource's */
    unsigned int PrivateDriverDataSize;
    unsigned int NumAllocations;
    DXGK_ALLOCATIONINFO *pAllocationInfo;
    void        *hResource;
    unsigned int Flags;                           /* DXGK_CREATEALLOCATIONFLAGS: bit 0 Resource */
} DXGKARG_CREATEALLOCATION;

typedef struct {
    unsigned int NumAllocations;
    void *const *pAllocationList;                 /* the driver's hAllocations */
    void        *hResource;
    unsigned int Flags;                           /* bit 0 DestroyResource */
} DXGKARG_DESTROYALLOCATION;

typedef struct {
    void        *hDevice;                         /* in: dxgkrnl's; out: the driver's */
    union { unsigned int Flags; void *pInfo; };   /* DXGK_CREATEDEVICEFLAGS: bit 0 SystemDevice, 1 GdiDevice */
    unsigned int Pasid;
    void        *hKmdProcess;
} DXGKARG_CREATEDEVICE;

typedef struct {
    unsigned int hAllocation;                     /* dxgkrnl's (D3DKMT_HANDLE): DxgkCbGetHandleData finds the driver's */
    void        *pPrivateDriverData;
    unsigned int PrivateDriverDataSize;
    void        *hDeviceSpecificAllocation;       /* out */
} DXGK_OPENALLOCATIONINFO;

typedef struct {
    unsigned int NumAllocations;
    DXGK_OPENALLOCATIONINFO *pOpenAllocation;
    void        *pPrivateDriverData;
    unsigned int PrivateDriverSize;
    unsigned int Flags;                           /* DXGK_OPENALLOCATIONFLAGS: bit 0 Create, 1 ReadOnly */
    unsigned int SubresourceIndex;
    unsigned long long SubresourceOffset;
    unsigned int Pitch;
} DXGKARG_OPENALLOCATION;

typedef struct {
    unsigned int NumAllocations;
    void *const *pOpenHandleList;                 /* the hDeviceSpecificAllocations */
} DXGKARG_CLOSEALLOCATION;

/* GPU virtual addresses (WDDM 2.0's GpuMmu model): the driver describes
 * its page tables, VidMm owns them and asks the driver to write their
 * entries through DxgkDdiBuildPagingBuffer */
#define DXGKQAITYPE_PAGETABLELEVELDESC 14
#define DXGK_VIDMMCAPS_GPUMMU          0x40       /* DXGK_VIDMMCAPS.GpuMmuSupported */
enum { DXGK_PAGETABLEUPDATE_CPU_VIRTUAL, DXGK_PAGETABLEUPDATE_GPU_VIRTUAL, DXGK_PAGETABLEUPDATE_GPU_PHYSICAL };

typedef struct {
    unsigned int Flags;                           /* ReadOnlyMemorySupported, NoExecuteMemorySupported, ... */
    int          PageTableUpdateMode;             /* DXGK_PAGETABLEUPDATEMODE */
    unsigned int VirtualAddressBitCount;
    unsigned int LeafPageTableSizeFor64KPagesInBytes;
    unsigned int PageTableLevelCount;             /* 2 to 4; with 2 the root is sized by DxgkDdiGetRootPageTableSize */
    unsigned int LegacyBehaviors;
} DXGK_GPUMMUCAPS;

/* Asked once per level, the level (a UINT, 0 the root) as input */
typedef struct {
    unsigned int PageTableIndexBitCount;
    unsigned int PageTableSegmentId;              /* 0: system memory (then at most 4 KiB a table) */
    unsigned int PagingProcessPageTableSegmentId;
    unsigned int PageTableSizeInBytes;
    unsigned int PageTableAlignmentInBytes;
} DXGK_PAGE_TABLE_LEVEL_DESC;

/* d3dukmdt.h's DXGK_PTE: VidMm's description of an entry, which the driver
 * turns into its own format */
#define DXGK_PTE_VALID          0x01ull
#define DXGK_PTE_ZERO           0x02ull
#define DXGK_PTE_CACHE_COHERENT 0x04ull
#define DXGK_PTE_READ_ONLY      0x08ull
#define DXGK_PTE_NO_EXECUTE     0x10ull
#define DXGK_PTE_SEGMENT(s)     ((unsigned long long)(s) << 5)
typedef struct {
    unsigned long long Flags;
    unsigned long long PageAddress;               /* (or PageTableAddress) the physical address >> 12 */
} DXGK_PTE;

typedef struct {
    void        *hDxgkProcess;
    void        *hKmdProcess;                     /* out */
    unsigned int Flags;                           /* DXGK_CREATEPROCESSFLAGS: bit 0 SystemProcess, 1 GdiProcess */
    unsigned int NumPasid;
    unsigned int *pPasid;
    void        *hKmdVmWorkerProcess;
    unsigned int ProcessNameLength;
    NT_WCHAR    *pProcessName;
} DXGKARG_CREATEPROCESS;

typedef struct { unsigned int NumberOfPte, PhysicalAdapterIndex; } DXGKARG_GETROOTPAGETABLESIZE;

#define DXGK_OPERATION_UPDATE_PAGE_TABLE 11
#define DXGK_OPERATION_FLUSH_TLB         12
/* DXGK_UPDATEPAGETABLEFLAGS bits */
#define DXGK_UPDATEPT_REPEAT         0x1
#define DXGK_UPDATEPT_INITIAL_UPDATE 0x2

typedef struct {
    unsigned int PageTableLevel;
    void        *hAllocation;                     /* the driver's; NULL for page tables */
    union { void *CpuVirtual; unsigned long long GpuPhysical, GpuVirtual; } PageTableAddress;
    DXGK_PTE    *pPageTableEntries;
    unsigned int StartIndex, NumPageTableEntries, Reserved0;
    unsigned int Flags;                           /* DXGK_UPDATEPAGETABLEFLAGS */
    unsigned long long DriverProtection, AllocationOffsetInBytes;
    void        *hProcess;                        /* the driver's (DxgkDdiCreateProcess) */
    int          UpdateMode;                      /* DXGK_PAGETABLEUPDATEMODE */
    DXGK_PTE    *pPageTableEntries64KB;
    unsigned long long FirstPteVirtualAddress;
} DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE;

typedef struct {
    unsigned long long RootPageTableAddress;      /* physical */
    void        *hProcess;
    unsigned long long StartVirtualAddress, EndVirtualAddress;
} DXGK_BUILDPAGINGBUFFER_FLUSHTLB;

typedef struct {
    void        *pDmaBuffer;                      /* in/out: the paging buffer's next free byte */
    unsigned int DmaSize;
    void        *pDmaBufferPrivateData;
    unsigned int DmaBufferPrivateDataSize;
    int          Operation;                       /* DXGK_BUILDPAGINGBUFFER_OPERATION */
    unsigned int MultipassOffset;
    union {
        DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE UpdatePageTable;
        DXGK_BUILDPAGINGBUFFER_FLUSHTLB        FlushTlb;
        unsigned int Reserved[64];                /* (the other operations' structures) */
    };
    void        *hSystemContext;
    unsigned long long DmaBufferGpuVirtualAddress;
    unsigned int DmaBufferWriteOffset;
} DXGKARG_BUILDPAGINGBUFFER;

enum { DXGK_HANDLE_ALLOCATION = 1, DXGK_HANDLE_RESOURCE = 2 };
typedef struct DXGKARGCB_GETHANDLEDATA {
    unsigned int hObject;
    int          Type;                            /* DXGK_HANDLE_TYPE */
    unsigned int Flags;                           /* bit 0 DeviceSpecific */
} DXGKARGCB_GETHANDLEDATA;

/* Contexts and submission (WDDM 2.0's GPU virtual addressing model) */
typedef struct {
    unsigned int DmaBufferSize, DmaBufferSegmentSet, DmaBufferPrivateDataSize;
    unsigned int AllocationListSize, PatchLocationListSize, Reserved;
    unsigned int Caps;                            /* DXGK_CONTEXTINFO_CAPS */
    unsigned int PagingCompanionNodeId;
} DXGK_CONTEXTINFO;

/* DXGK_CREATECONTEXTFLAGS bits */
#define DXGK_CONTEXT_SYSTEM             0x1
#define DXGK_CONTEXT_VIRTUAL_ADDRESSING 0x4

typedef struct {
    void            *hContext;                    /* out: the driver's */
    unsigned int     NodeOrdinal, EngineAffinity;
    unsigned int     Flags;                       /* DXGK_CREATECONTEXTFLAGS */
    void            *pPrivateDriverData;
    unsigned int     PrivateDriverDataSize;
    DXGK_CONTEXTINFO ContextInfo;                 /* out */
} DXGKARG_CREATECONTEXT;

typedef struct { unsigned int SegmentId, Padding; unsigned long long SegmentOffset; } D3DGPU_PHYSICAL_ADDRESS;

typedef struct {
    void                   *hContext;             /* the driver's */
    D3DGPU_PHYSICAL_ADDRESS Address;              /* the root page table: segment 0 is system memory */
    unsigned int            NumEntries;
} DXGKARG_SETROOTPAGETABLE;

/* DXGK_SUBMITCOMMANDFLAGS bits */
#define DXGK_SUBMIT_PAGING 0x1

typedef struct {
    void              *hContext;                  /* the driver's */
    unsigned long long DmaBufferVirtualAddress;   /* (in the context's process's GPU address space) */
    unsigned int       DmaBufferSize;
    void              *pDmaBufferPrivateData;
    unsigned int       DmaBufferPrivateDataSize, DmaBufferUmdPrivateDataSize;
    unsigned int       SubmissionFenceId;         /* the node's: rises by one each submission */
    unsigned int       VidPnSourceId;
    int                FlipInterval;
    unsigned int       Flags;                     /* DXGK_SUBMITCOMMANDFLAGS */
    unsigned int       EngineOrdinal, NodeOrdinal;
} DXGKARG_SUBMITCOMMANDVIRTUAL;

/* DXGK_INTERRUPT_TYPE: what DxgkCbNotifyInterrupt reports */
#define DXGK_INTERRUPT_DMA_COMPLETED            1
#define DXGK_INTERRUPT_DMA_PREEMPTED            2
#define DXGK_INTERRUPT_DMA_FAULTED              4
#define DXGK_INTERRUPT_DMA_PAGE_FAULTED         9
#define DXGK_INTERRUPT_MONITORED_FENCE_SIGNALED 11

typedef struct {
    int InterruptType;                            /* DXGK_INTERRUPT_TYPE */
    union {
        struct { unsigned int SubmissionFenceId, NodeOrdinal, EngineOrdinal; } DmaCompleted;
        struct { unsigned int PreemptionFenceId, LastCompletedFenceId, NodeOrdinal, EngineOrdinal; } DmaPreempted;
        struct { unsigned int FaultedFenceId; NT_STATUS Status; unsigned int NodeOrdinal, EngineOrdinal; } DmaFaulted;
        struct { unsigned int NodeOrdinal, EngineOrdinal; } MonitoredFenceSignaled;
        unsigned long long Reserved[8];           /* (the other interrupts' structures) */
    };
    unsigned int Flags;                           /* DXGKCB_NOTIFY_INTERRUPT_DATA_FLAGS */
} DXGKARGCB_NOTIFY_INTERRUPT_DATA;

/* DxgkDdiPreemptCommand: the GPU is to stop the node's work at its next
 * preemption point and report DMA_PREEMPTED with PreemptionFenceId (a new
 * ID of the node's) and the last buffer it finished */
typedef struct {
    unsigned int PreemptionFenceId, LastCompletedFenceId, NodeOrdinal, EngineOrdinal;
} DXGKARG_PREEMPTCOMMAND;

/* DxgkDdiResetEngine (WDDM 1.2, per-engine TDR): the engine reset, its
 * queue dropped; the driver says the last fence ID it aborted */
typedef struct {
    unsigned int NodeOrdinal, EngineOrdinal, LastAbortedFenceId;
} DXGKARG_RESETENGINE;

/* D3DKMDT_GRAPHICS_PREEMPTION_GRANULARITY, D3DKMDT_COMPUTE_PREEMPTION_GRANULARITY */
#define D3DKMDT_GRAPHICS_PREEMPTION_NONE                0
#define D3DKMDT_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY 100
#define D3DKMDT_GRAPHICS_PREEMPTION_PRIMITIVE_BOUNDARY  200
#define D3DKMDT_COMPUTE_PREEMPTION_NONE                 0
#define D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY  100
#define D3DKMDT_COMPUTE_PREEMPTION_DISPATCH_BOUNDARY    200

#define DXGK_DDI(ret, name, ...) ret (NT_API *name)(__VA_ARGS__)

/* KMDDOD_INITIALIZATION_DATA: a display-only driver's DDI table, in the
 * order dispmprt.h declares it */
typedef struct {
    unsigned int Version;
    DXGK_DDI(NT_STATUS, DxgkDdiAddDevice, NT_DEVICE_OBJECT *pdo, void **ctx);
    DXGK_DDI(NT_STATUS, DxgkDdiStartDevice, void *ctx, DXGK_START_INFO *, DXGKRNL_INTERFACE *, unsigned int *sources,
             unsigned int *children);
    DXGK_DDI(NT_STATUS, DxgkDdiStopDevice, void *ctx);
    DXGK_DDI(NT_STATUS, DxgkDdiRemoveDevice, void *ctx);
    DXGK_DDI(NT_STATUS, DxgkDdiDispatchIoRequest, void *ctx, unsigned int source, void *vrp);
    DXGK_DDI(unsigned char, DxgkDdiInterruptRoutine, void *ctx, unsigned int msg);
    DXGK_DDI(void, DxgkDdiDpcRoutine, void *ctx);
    DXGK_DDI(NT_STATUS, DxgkDdiQueryChildRelations, void *ctx, DXGK_CHILD_DESCRIPTOR *rel, unsigned int size);
    DXGK_DDI(NT_STATUS, DxgkDdiQueryChildStatus, void *ctx, DXGK_CHILD_STATUS *st, unsigned char nondestructive);
    DXGK_DDI(NT_STATUS, DxgkDdiQueryDeviceDescriptor, void *ctx, unsigned int uid, DXGK_DEVICE_DESCRIPTOR *d);
    DXGK_DDI(NT_STATUS, DxgkDdiSetPowerState, void *ctx, unsigned int uid, int dstate, int action);
    DXGK_DDI(NT_STATUS, DxgkDdiNotifyAcpiEvent, void *ctx, int type, unsigned int ev, void *arg, unsigned int *flags);
    DXGK_DDI(void, DxgkDdiResetDevice, void *ctx);
    DXGK_DDI(void, DxgkDdiUnload, void);
    DXGK_DDI(NT_STATUS, DxgkDdiQueryInterface, void *ctx, void *qi);
    DXGK_DDI(void, DxgkDdiControlEtwLogging, unsigned char enable, unsigned int flags, unsigned char level);
    DXGK_DDI(NT_STATUS, DxgkDdiQueryAdapterInfo, void *adapter, const DXGKARG_QUERYADAPTERINFO *);
    DXGK_DDI(NT_STATUS, DxgkDdiSetPalette, void *adapter, const void *);
    DXGK_DDI(NT_STATUS, DxgkDdiSetPointerPosition, void *adapter, const void *);
    DXGK_DDI(NT_STATUS, DxgkDdiSetPointerShape, void *adapter, const void *);
    DXGK_DDI(NT_STATUS, DxgkDdiEscape, void *adapter, const void *);
    DXGK_DDI(NT_STATUS, DxgkDdiCollectDbgInfo, void *adapter, const void *);
    DXGK_DDI(NT_STATUS, DxgkDdiIsSupportedVidPn, void *adapter, DXGKARG_ISSUPPORTEDVIDPN *);
    DXGK_DDI(NT_STATUS, DxgkDdiRecommendFunctionalVidPn, void *adapter, const void *);
    DXGK_DDI(NT_STATUS, DxgkDdiEnumVidPnCofuncModality, void *adapter, const DXGKARG_ENUMVIDPNCOFUNCMODALITY *);
    DXGK_DDI(NT_STATUS, DxgkDdiSetVidPnSourceVisibility, void *adapter, const DXGKARG_SETVIDPNSOURCEVISIBILITY *);
    DXGK_DDI(NT_STATUS, DxgkDdiCommitVidPn, void *adapter, const DXGKARG_COMMITVIDPN *);
    DXGK_DDI(NT_STATUS, DxgkDdiUpdateActiveVidPnPresentPath, void *adapter, const DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *);
    DXGK_DDI(NT_STATUS, DxgkDdiRecommendMonitorModes, void *adapter, const DXGKARG_RECOMMENDMONITORMODES *);
    DXGK_DDI(NT_STATUS, DxgkDdiGetScanLine, void *adapter, void *);
    DXGK_DDI(NT_STATUS, DxgkDdiQueryVidPnHWCapability, void *adapter, DXGKARG_QUERYVIDPNHWCAPABILITY *);
    DXGK_DDI(NT_STATUS, DxgkDdiPresentDisplayOnly, void *adapter, const DXGKARG_PRESENT_DISPLAYONLY *);
    DXGK_DDI(NT_STATUS, DxgkDdiStopDeviceAndReleasePostDisplayOwnership, void *ctx, D3DDDI_VIDEO_PRESENT_TARGET_ID,
             DXGK_DISPLAY_INFORMATION *);
    DXGK_DDI(NT_STATUS, DxgkDdiSystemDisplayEnable, void *ctx, D3DDDI_VIDEO_PRESENT_TARGET_ID, void *flags,
             unsigned int *w, unsigned int *h, int *fmt);
    DXGK_DDI(void, DxgkDdiSystemDisplayWrite, void *ctx, void *src, unsigned int w, unsigned int h,
             unsigned int stride, unsigned int x, unsigned int y);
    DXGK_DDI(NT_STATUS, DxgkDdiGetChildContainerId, void *ctx, unsigned int uid, void *id);
    DXGK_DDI(NT_STATUS, DxgkDdiControlInterrupt, void *adapter, int type, unsigned char enable);
    DXGK_DDI(NT_STATUS, DxgkDdiSetPowerComponentFState, void *ctx, unsigned int comp, unsigned int fstate);
    DXGK_DDI(NT_STATUS, DxgkDdiPowerRuntimeControlRequest, void *ctx, const NT_GUID *, void *, unsigned long long,
             void *, unsigned long long, unsigned long long *);
    DXGK_DDI(NT_STATUS, DxgkDdiNotifySurpriseRemoval, void *ctx, int kind);
    DXGK_DDI(NT_STATUS, DxgkDdiPowerRuntimeSetDeviceHandle, void *ctx, void *handle);
} KMDDOD_INITIALIZATION_DATA;

/* DRIVER_INITIALIZATION_DATA: a full (render, or render and display)
 * driver's DDI table, which DxgkInitialize hands dxgkrnl, in dispmprt.h's
 * order.  Its first seventeen entries are KMDDOD's; the functions the host
 * calls are typed, the rest kept as pointers in their places.  The table
 * grows with the DDI version (Version): the host reads it only as far as
 * the version the driver says it was built for has it. */
typedef struct {
    unsigned int Version;
    DXGK_DDI(NT_STATUS, DxgkDdiAddDevice, NT_DEVICE_OBJECT *pdo, void **ctx);
    DXGK_DDI(NT_STATUS, DxgkDdiStartDevice, void *ctx, DXGK_START_INFO *, DXGKRNL_INTERFACE *, unsigned int *sources,
             unsigned int *children);
    DXGK_DDI(NT_STATUS, DxgkDdiStopDevice, void *ctx);
    DXGK_DDI(NT_STATUS, DxgkDdiRemoveDevice, void *ctx);
    void *DxgkDdiDispatchIoRequest;
    DXGK_DDI(unsigned char, DxgkDdiInterruptRoutine, void *ctx, unsigned int msg);
    DXGK_DDI(void, DxgkDdiDpcRoutine, void *ctx);
    DXGK_DDI(NT_STATUS, DxgkDdiQueryChildRelations, void *ctx, DXGK_CHILD_DESCRIPTOR *rel, unsigned int size);
    DXGK_DDI(NT_STATUS, DxgkDdiQueryChildStatus, void *ctx, DXGK_CHILD_STATUS *st, unsigned char nondestructive);
    DXGK_DDI(NT_STATUS, DxgkDdiQueryDeviceDescriptor, void *ctx, unsigned int uid, DXGK_DEVICE_DESCRIPTOR *d);
    void *DxgkDdiSetPowerState, *DxgkDdiNotifyAcpiEvent, *DxgkDdiResetDevice;
    DXGK_DDI(void, DxgkDdiUnload, void);
    void *DxgkDdiQueryInterface, *DxgkDdiControlEtwLogging;
    DXGK_DDI(NT_STATUS, DxgkDdiQueryAdapterInfo, void *adapter, const DXGKARG_QUERYADAPTERINFO *);
    DXGK_DDI(NT_STATUS, DxgkDdiCreateDevice, void *adapter, DXGKARG_CREATEDEVICE *);
    DXGK_DDI(NT_STATUS, DxgkDdiCreateAllocation, void *adapter, DXGKARG_CREATEALLOCATION *);
    DXGK_DDI(NT_STATUS, DxgkDdiDestroyAllocation, void *adapter, const DXGKARG_DESTROYALLOCATION *);
    void *DxgkDdiDescribeAllocation, *DxgkDdiGetStandardAllocationDriverData;
    void *DxgkDdiAcquireSwizzlingRange, *DxgkDdiReleaseSwizzlingRange;
    void *DxgkDdiPatch, *DxgkDdiSubmitCommand;
    DXGK_DDI(NT_STATUS, DxgkDdiPreemptCommand, void *adapter, const DXGKARG_PREEMPTCOMMAND *);
    DXGK_DDI(NT_STATUS, DxgkDdiBuildPagingBuffer, void *adapter, DXGKARG_BUILDPAGINGBUFFER *);
    void *DxgkDdiSetPalette, *DxgkDdiSetPointerPosition, *DxgkDdiSetPointerShape;
    DXGK_DDI(NT_STATUS, DxgkDdiResetFromTimeout, void *adapter);
    DXGK_DDI(NT_STATUS, DxgkDdiRestartFromTimeout, void *adapter);
    void *DxgkDdiEscape, *DxgkDdiCollectDbgInfo, *DxgkDdiQueryCurrentFence;
    DXGK_DDI(NT_STATUS, DxgkDdiIsSupportedVidPn, void *adapter, DXGKARG_ISSUPPORTEDVIDPN *);
    void *DxgkDdiRecommendFunctionalVidPn;
    DXGK_DDI(NT_STATUS, DxgkDdiEnumVidPnCofuncModality, void *adapter, const DXGKARG_ENUMVIDPNCOFUNCMODALITY *);
    void *DxgkDdiSetVidPnSourceAddress;
    DXGK_DDI(NT_STATUS, DxgkDdiSetVidPnSourceVisibility, void *adapter, const DXGKARG_SETVIDPNSOURCEVISIBILITY *);
    DXGK_DDI(NT_STATUS, DxgkDdiCommitVidPn, void *adapter, const DXGKARG_COMMITVIDPN *);
    void *DxgkDdiUpdateActiveVidPnPresentPath, *DxgkDdiRecommendMonitorModes, *DxgkDdiRecommendVidPnTopology;
    void *DxgkDdiGetScanLine, *DxgkDdiStopCapture, *DxgkDdiControlInterrupt, *DxgkDdiCreateOverlay;
    DXGK_DDI(NT_STATUS, DxgkDdiDestroyDevice, void *device);
    DXGK_DDI(NT_STATUS, DxgkDdiOpenAllocation, void *device, const DXGKARG_OPENALLOCATION *);
    DXGK_DDI(NT_STATUS, DxgkDdiCloseAllocation, void *device, const DXGKARG_CLOSEALLOCATION *);
    void *DxgkDdiRender, *DxgkDdiPresent;
    void *DxgkDdiUpdateOverlay, *DxgkDdiFlipOverlay, *DxgkDdiDestroyOverlay;
    DXGK_DDI(NT_STATUS, DxgkDdiCreateContext, void *device, DXGKARG_CREATECONTEXT *);
    DXGK_DDI(NT_STATUS, DxgkDdiDestroyContext, void *context);
    void *DxgkDdiLinkDevice, *DxgkDdiSetDisplayPrivateDriverFormat;
    /* DXGKDDI_INTERFACE_VERSION_WIN7 */
    void *DxgkDdiDescribePageTable, *DxgkDdiUpdatePageTable, *DxgkDdiUpdatePageDirectory, *DxgkDdiMovePageDirectory;
    void *DxgkDdiSubmitRender, *DxgkDdiCreateAllocation2, *DxgkDdiRenderKm, *Reserved;
    void *DxgkDdiQueryVidPnHWCapability;
    /* DXGKDDI_INTERFACE_VERSION_WIN8 */
    void *DxgkDdiSetPowerComponentFState, *DxgkDdiQueryDependentEngineGroup, *DxgkDdiQueryEngineStatus;
    DXGK_DDI(NT_STATUS, DxgkDdiResetEngine, void *adapter, DXGKARG_RESETENGINE *);
    DXGK_DDI(NT_STATUS, DxgkDdiStopDeviceAndReleasePostDisplayOwnership, void *ctx, D3DDDI_VIDEO_PRESENT_TARGET_ID,
             DXGK_DISPLAY_INFORMATION *);
    void *DxgkDdiSystemDisplayEnable, *DxgkDdiSystemDisplayWrite, *DxgkDdiCancelCommand;
    void *DxgkDdiGetChildContainerId, *DxgkDdiPowerRuntimeControlRequest;
    void *DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay, *DxgkDdiNotifySurpriseRemoval;
    /* DXGKDDI_INTERFACE_VERSION_WDDM1_3 */
    void *DxgkDdiGetNodeMetadata, *DxgkDdiSetPowerPState, *DxgkDdiControlInterrupt2;
    void *DxgkDdiCheckMultiPlaneOverlaySupport, *DxgkDdiCalibrateGpuClock, *DxgkDdiFormatHistoryBuffer;
    /* DXGKDDI_INTERFACE_VERSION_WDDM2_0 */
    void *DxgkDdiRenderGdi;
    DXGK_DDI(NT_STATUS, DxgkDdiSubmitCommandVirtual, void *adapter, const DXGKARG_SUBMITCOMMANDVIRTUAL *);
    DXGK_DDI(void, DxgkDdiSetRootPageTable, void *adapter, const DXGKARG_SETROOTPAGETABLE *);
    DXGK_DDI(unsigned long long, DxgkDdiGetRootPageTableSize, void *adapter, DXGKARG_GETROOTPAGETABLESIZE *);
    void *DxgkDdiMapCpuHostAperture, *DxgkDdiUnmapCpuHostAperture, *DxgkDdiCheckMultiPlaneOverlaySupport2;
    DXGK_DDI(NT_STATUS, DxgkDdiCreateProcess, void *ctx, DXGKARG_CREATEPROCESS *);
    DXGK_DDI(NT_STATUS, DxgkDdiDestroyProcess, void *adapter, void *kmd_process);
    void *DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay2;
    void *Reserved1, *Reserved2;
    void *DxgkDdiPowerRuntimeSetDeviceHandle, *DxgkDdiSetStablePowerState, *DxgkDdiSetVideoProtectedRegion;
    /* later versions (WDDM 2.1 to 3.2): the rest of the 193 entries
     * Windows 11's WDK lists, none called yet, reserved at their size */
    void *Later[89];
} DRIVER_INITIALIZATION_DATA;

/* The entries a driver built for DDI version @v has, counting Version as one */
static inline unsigned int DxgkInitEntries(unsigned int v)
{
    return v >= DXGKDDI_INTERFACE_VERSION_WDDM2_0 ? 104 : v >= DXGKDDI_INTERFACE_VERSION_WDDM1_3 ? 89
         : v >= DXGKDDI_INTERFACE_VERSION_WIN8 ? 83 : v >= DXGKDDI_INTERFACE_VERSION_WIN7 ? 71 : 62;
}

/* dxgkrnl's initialisation function, which IOCTL_VIDEO_DDI_FUNC_REGISTER returns */
typedef NT_STATUS (NT_API *DXGKRNL_DPIINITIALIZE)(NT_DRIVER_OBJECT *, NT_UNICODE_STRING *registry, void *init_data);

#define DXGK_OFFSET(t, m) __builtin_offsetof(t, m)
_Static_assert(sizeof(D3DKMDT_VIDPN_SOURCE_MODE) == 48 && DXGK_OFFSET(D3DKMDT_VIDPN_SOURCE_MODE, Format.Graphics.Stride) == 24,
               "D3DKMDT_VIDPN_SOURCE_MODE layout");
_Static_assert(sizeof(D3DKMDT_VIDEO_SIGNAL_INFO) == 56 && sizeof(D3DKMDT_VIDPN_TARGET_MODE) == 72,
               "D3DKMDT_VIDPN_TARGET_MODE layout");
_Static_assert(DXGK_OFFSET(D3DKMDT_VIDPN_PRESENT_PATH, ContentTransformation) == 12 &&
               DXGK_OFFSET(D3DKMDT_VIDPN_PRESENT_PATH, VidPnTargetColorBasis) == 44 &&
               DXGK_OFFSET(D3DKMDT_VIDPN_PRESENT_PATH, CopyProtection) == 68 &&
               DXGK_OFFSET(D3DKMDT_VIDPN_PRESENT_PATH, GammaRamp) == 336 && sizeof(D3DKMDT_VIDPN_PRESENT_PATH) == 360,
               "D3DKMDT_VIDPN_PRESENT_PATH layout");
_Static_assert(DXGK_OFFSET(DXGKRNL_INTERFACE, DxgkCbEvalAcpiMethod) == 16 &&
               DXGK_OFFSET(DXGKRNL_INTERFACE, DxgkCbQueryVidPnInterface) == 16 + 8 * 16 &&
               DXGK_OFFSET(DXGKRNL_INTERFACE, DxgkCbAcquirePostDisplayOwnership) == 16 + 8 * 25 &&
               sizeof(DXGKRNL_INTERFACE) == 16 + 8 * 70, "DXGKRNL_INTERFACE layout");
_Static_assert(DXGK_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiQueryAdapterInfo) == 8 + 8 * 16 &&
               DXGK_OFFSET(KMDDOD_INITIALIZATION_DATA, DxgkDdiPresentDisplayOnly) == 8 + 8 * 31 &&
               sizeof(KMDDOD_INITIALIZATION_DATA) == 8 + 8 * 41, "KMDDOD_INITIALIZATION_DATA layout");
_Static_assert(DXGK_OFFSET(DXGK_DEVICE_INFO, TranslatedResourceList) == 32 && sizeof(DXGK_DISPLAY_INFORMATION) == 32 &&
               sizeof(DXGK_CHILD_DESCRIPTOR) == 28, "dispmprt.h layouts");
_Static_assert(DXGK_OFFSET(DXGK_DRIVERCAPS, MemoryManagementCaps) == 68 && DXGK_OFFSET(DXGK_DRIVERCAPS, WDDMVersion) == 336 &&
               DXGK_OFFSET(DXGK_DRIVERCAPS, PreemptionCaps) == 528 && DXGK_OFFSET(DXGK_DRIVERCAPS, SupportPerEngineTDR) == 538 &&
               DXGK_OFFSET(DXGK_DRIVERCAPS, MaxOverlayPlanes) == 544 &&
               DXGK_OFFSET(DXGK_DRIVERCAPS, InternalGpuVirtualAddressRangeStart) == 552 &&
               DXGK_OFFSET(DXGK_DRIVERCAPS, MaxQueuedMultiPlaneOverlayFlipVSync) == 572, "DXGK_DRIVERCAPS layout");
_Static_assert(sizeof(DXGK_SEGMENTDESCRIPTOR4) == 96 && DXGK_OFFSET(DXGK_SEGMENTDESCRIPTOR4, NumInvalidMemoryRanges) == 56 &&
               sizeof(DXGK_QUERYSEGMENTOUT4) == 40 && DXGK_OFFSET(DXGK_QUERYSEGMENTOUT4, SegmentDescriptorStride) == 32,
               "QUERYSEGMENT4 layouts");
_Static_assert(sizeof(DXGK_ALLOCATIONINFO) == 88 && DXGK_OFFSET(DXGK_ALLOCATIONINFO, hAllocation) == 56 &&
               sizeof(DXGKARG_CREATEALLOCATION) == 40 && sizeof(DXGKARG_DESTROYALLOCATION) == 32 &&
               sizeof(DXGKARG_CREATEDEVICE) == 32 && sizeof(DXGK_OPENALLOCATIONINFO) == 32 &&
               sizeof(DXGKARG_OPENALLOCATION) == 56 && sizeof(DXGKARG_CLOSEALLOCATION) == 16 &&
               sizeof(DXGKARGCB_GETHANDLEDATA) == 12, "allocation DDI layouts");
_Static_assert(DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiQueryAdapterInfo) == 8 * 17 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiIsSupportedVidPn) == 8 * 37 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyDevice) == 8 * 50 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDescribePageTable) == 8 * 62 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetPowerComponentFState) == 8 * 71 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiStopDeviceAndReleasePostDisplayOwnership) == 8 * 75 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiGetNodeMetadata) == 8 * 83 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiRenderGdi) == 8 * 89 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiPreemptCommand) == 8 * 27 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiBuildPagingBuffer) == 8 * 28 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResetFromTimeout) == 8 * 32 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiRestartFromTimeout) == 8 * 33 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiResetEngine) == 8 * 74 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateContext) == 8 * 58 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSubmitCommandVirtual) == 8 * 90 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetRootPageTable) == 8 * 91 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiGetRootPageTableSize) == 8 * 92 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiCreateProcess) == 8 * 96 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiDestroyProcess) == 8 * 97 &&
               DXGK_OFFSET(DRIVER_INITIALIZATION_DATA, DxgkDdiSetVideoProtectedRegion) == 8 * 103 &&
               sizeof(DRIVER_INITIALIZATION_DATA) == 8 * 193, "DRIVER_INITIALIZATION_DATA layout");
_Static_assert(DXGK_OFFSET(DXGKARG_PRESENT_DISPLAYONLY, pMoves) == 32 &&
               DXGK_OFFSET(DXGKARG_PRESENT_DISPLAYONLY, pDirtyRect) == 48, "DXGKARG_PRESENT_DISPLAYONLY layout");
_Static_assert(sizeof(DXGK_GPUMMUCAPS) == 24 && sizeof(DXGK_PAGE_TABLE_LEVEL_DESC) == 20 && sizeof(DXGK_PTE) == 16 &&
               sizeof(DXGKARG_CREATEPROCESS) == 56 && DXGK_OFFSET(DXGKARG_CREATEPROCESS, pProcessName) == 48 &&
               sizeof(DXGKARG_GETROOTPAGETABLESIZE) == 8, "GpuMmu layouts");
_Static_assert(sizeof(DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE) == 96 &&
               DXGK_OFFSET(DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE, Flags) == 44 &&
               DXGK_OFFSET(DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE, hProcess) == 64 &&
               DXGK_OFFSET(DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE, FirstPteVirtualAddress) == 88 &&
               sizeof(DXGK_BUILDPAGINGBUFFER_FLUSHTLB) == 32 &&
               DXGK_OFFSET(DXGKARG_BUILDPAGINGBUFFER, Operation) == 28 &&
               DXGK_OFFSET(DXGKARG_BUILDPAGINGBUFFER, UpdatePageTable) == 40 &&
               DXGK_OFFSET(DXGKARG_BUILDPAGINGBUFFER, hSystemContext) == 296 && sizeof(DXGKARG_BUILDPAGINGBUFFER) == 320,
               "DXGKARG_BUILDPAGINGBUFFER layout");
_Static_assert(sizeof(DXGK_CONTEXTINFO) == 32 && DXGK_OFFSET(DXGKARG_CREATECONTEXT, pPrivateDriverData) == 24 &&
               DXGK_OFFSET(DXGKARG_CREATECONTEXT, ContextInfo) == 36 && sizeof(DXGKARG_CREATECONTEXT) == 72 &&
               sizeof(DXGKARG_SETROOTPAGETABLE) == 32 && DXGK_OFFSET(DXGKARG_SETROOTPAGETABLE, NumEntries) == 24 &&
               DXGK_OFFSET(DXGKARG_SUBMITCOMMANDVIRTUAL, pDmaBufferPrivateData) == 24 &&
               DXGK_OFFSET(DXGKARG_SUBMITCOMMANDVIRTUAL, SubmissionFenceId) == 40 &&
               DXGK_OFFSET(DXGKARG_SUBMITCOMMANDVIRTUAL, NodeOrdinal) == 60 && sizeof(DXGKARG_SUBMITCOMMANDVIRTUAL) == 64 &&
               DXGK_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, DmaCompleted.NodeOrdinal) == 12 &&
               DXGK_OFFSET(DXGKARGCB_NOTIFY_INTERRUPT_DATA, Flags) == 72 && sizeof(DXGKARGCB_NOTIFY_INTERRUPT_DATA) == 80,
               "context and submission layouts");
