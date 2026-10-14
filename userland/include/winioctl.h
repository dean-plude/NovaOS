/*
 * winioctl.h — disk, storage and volume device controls (DeviceIoControl)
 *
 * The parts of Windows' winioctl.h NovaOS implements on \\.\PhysicalDriveN,
 * \\.\X: and \\?\Volume{...} handles (kernel/um/um_disk.c), with the same
 * codes and structure layouts.
 */
#pragma once

#include <windows.h>

#define CTL_CODE(t, f, m, a) (((DWORD)(t) << 16) | ((DWORD)(a) << 14) | ((DWORD)(f) << 2) | (DWORD)(m))
#define METHOD_BUFFERED   0
#define FILE_ANY_ACCESS   0
#ifndef FILE_READ_ACCESS
#define FILE_READ_ACCESS  1
#define FILE_WRITE_ACCESS 2
#endif

#define FILE_DEVICE_DISK            0x00000007
#define FILE_DEVICE_MASS_STORAGE    0x0000002D
#define IOCTL_DISK_BASE             FILE_DEVICE_DISK
#define IOCTL_STORAGE_BASE          FILE_DEVICE_MASS_STORAGE
#define IOCTL_VOLUME_BASE           0x00000056

#define IOCTL_DISK_GET_DRIVE_GEOMETRY      CTL_CODE(IOCTL_DISK_BASE, 0x0000, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_GET_PARTITION_INFO      CTL_CODE(IOCTL_DISK_BASE, 0x0001, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DISK_IS_WRITABLE             CTL_CODE(IOCTL_DISK_BASE, 0x0009, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_GET_PARTITION_INFO_EX   CTL_CODE(IOCTL_DISK_BASE, 0x0012, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_GET_DRIVE_LAYOUT_EX     CTL_CODE(IOCTL_DISK_BASE, 0x0014, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_SET_DRIVE_LAYOUT_EX     CTL_CODE(IOCTL_DISK_BASE, 0x0015, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_DISK_CREATE_DISK             CTL_CODE(IOCTL_DISK_BASE, 0x0016, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_DISK_GET_LENGTH_INFO         CTL_CODE(IOCTL_DISK_BASE, 0x0017, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DISK_GET_DRIVE_GEOMETRY_EX   CTL_CODE(IOCTL_DISK_BASE, 0x0028, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_UPDATE_PROPERTIES       CTL_CODE(IOCTL_DISK_BASE, 0x0050, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_STORAGE_CHECK_VERIFY         CTL_CODE(IOCTL_STORAGE_BASE, 0x0200, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_STORAGE_GET_DEVICE_NUMBER    CTL_CODE(IOCTL_STORAGE_BASE, 0x0420, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_STORAGE_QUERY_PROPERTY       CTL_CODE(IOCTL_STORAGE_BASE, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS CTL_CODE(IOCTL_VOLUME_BASE, 0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUNTDEV_QUERY_DEVICE_NAME   CTL_CODE(0x0000004D, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define FSCTL_LOCK_VOLUME                  CTL_CODE(0x00000009, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_UNLOCK_VOLUME                CTL_CODE(0x00000009, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_DISMOUNT_VOLUME              CTL_CODE(0x00000009, 8, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef enum _MEDIA_TYPE { Unknown = 0, RemovableMedia = 11, FixedMedia = 12 } MEDIA_TYPE;

typedef struct _DISK_GEOMETRY {
    LARGE_INTEGER Cylinders;
    MEDIA_TYPE    MediaType;
    DWORD         TracksPerCylinder, SectorsPerTrack, BytesPerSector;
} DISK_GEOMETRY, *PDISK_GEOMETRY;

typedef struct _DISK_GEOMETRY_EX {
    DISK_GEOMETRY Geometry;
    LARGE_INTEGER DiskSize;
    BYTE          Data[1];
} DISK_GEOMETRY_EX, *PDISK_GEOMETRY_EX;

typedef struct _GET_LENGTH_INFORMATION { LARGE_INTEGER Length; } GET_LENGTH_INFORMATION;

typedef enum _PARTITION_STYLE { PARTITION_STYLE_MBR = 0, PARTITION_STYLE_GPT = 1, PARTITION_STYLE_RAW = 2 } PARTITION_STYLE;

#define PARTITION_ENTRY_UNUSED  0x00
#define PARTITION_FAT_12        0x01
#define PARTITION_FAT_16        0x04
#define PARTITION_HUGE          0x06
#define PARTITION_IFS           0x07
#define PARTITION_FAT32         0x0B
#define PARTITION_FAT32_XINT13  0x0C
#define PARTITION_XINT13        0x0E

#define GPT_BASIC_DATA_ATTRIBUTE_NO_DRIVE_LETTER 0x8000000000000000ull

typedef struct _PARTITION_INFORMATION {
    LARGE_INTEGER StartingOffset, PartitionLength;
    DWORD         HiddenSectors, PartitionNumber;
    BYTE          PartitionType;
    BOOLEAN       BootIndicator, RecognizedPartition, RewritePartition;
} PARTITION_INFORMATION, *PPARTITION_INFORMATION;

typedef struct _PARTITION_INFORMATION_MBR {
    BYTE    PartitionType;
    BOOLEAN BootIndicator, RecognizedPartition;
    DWORD   HiddenSectors;
    GUID    PartitionId;
} PARTITION_INFORMATION_MBR;

typedef struct _PARTITION_INFORMATION_GPT {
    GUID    PartitionType, PartitionId;
    DWORD64 Attributes;
    WCHAR   Name[36];
} PARTITION_INFORMATION_GPT;

typedef struct _PARTITION_INFORMATION_EX {
    PARTITION_STYLE PartitionStyle;
    LARGE_INTEGER   StartingOffset, PartitionLength;
    DWORD           PartitionNumber;
    BOOLEAN         RewritePartition, IsServicePartition;
    union { PARTITION_INFORMATION_MBR Mbr; PARTITION_INFORMATION_GPT Gpt; };
} PARTITION_INFORMATION_EX, *PPARTITION_INFORMATION_EX;

typedef struct _DRIVE_LAYOUT_INFORMATION_MBR { DWORD Signature, CheckSum; } DRIVE_LAYOUT_INFORMATION_MBR;
typedef struct _DRIVE_LAYOUT_INFORMATION_GPT {
    GUID          DiskId;
    LARGE_INTEGER StartingUsableOffset, UsableLength;
    DWORD         MaxPartitionCount;
} DRIVE_LAYOUT_INFORMATION_GPT;

typedef struct _DRIVE_LAYOUT_INFORMATION_EX {
    DWORD PartitionStyle, PartitionCount;
    union { DRIVE_LAYOUT_INFORMATION_MBR Mbr; DRIVE_LAYOUT_INFORMATION_GPT Gpt; };
    PARTITION_INFORMATION_EX PartitionEntry[1];
} DRIVE_LAYOUT_INFORMATION_EX, *PDRIVE_LAYOUT_INFORMATION_EX;

typedef struct _CREATE_DISK_MBR { DWORD Signature; } CREATE_DISK_MBR;
typedef struct _CREATE_DISK_GPT { GUID DiskId; DWORD MaxPartitionCount; } CREATE_DISK_GPT;
typedef struct _CREATE_DISK {
    PARTITION_STYLE PartitionStyle;
    union { CREATE_DISK_MBR Mbr; CREATE_DISK_GPT Gpt; };
} CREATE_DISK, *PCREATE_DISK;

typedef struct _STORAGE_DEVICE_NUMBER { DWORD DeviceType, DeviceNumber, PartitionNumber; } STORAGE_DEVICE_NUMBER;

typedef enum _STORAGE_PROPERTY_ID { StorageDeviceProperty = 0, StorageAdapterProperty = 1,
                                    StorageDeviceSeekPenaltyProperty = 7 } STORAGE_PROPERTY_ID;
typedef enum _STORAGE_QUERY_TYPE { PropertyStandardQuery = 0, PropertyExistsQuery = 1 } STORAGE_QUERY_TYPE;
typedef struct _STORAGE_PROPERTY_QUERY {
    STORAGE_PROPERTY_ID PropertyId;
    STORAGE_QUERY_TYPE  QueryType;
    BYTE                AdditionalParameters[1];
} STORAGE_PROPERTY_QUERY;

typedef enum _STORAGE_BUS_TYPE { BusTypeUnknown = 0, BusTypeScsi = 1, BusTypeAtapi = 2, BusTypeAta = 3,
                                 BusTypeUsb = 7, BusTypeSata = 0x0B, BusTypeVirtual = 0x0E,
                                 BusTypeNvme = 0x11 } STORAGE_BUS_TYPE;

typedef struct _STORAGE_DESCRIPTOR_HEADER { DWORD Version, Size; } STORAGE_DESCRIPTOR_HEADER;
typedef struct _STORAGE_DEVICE_DESCRIPTOR {
    DWORD            Version, Size;
    BYTE             DeviceType, DeviceTypeModifier;
    BOOLEAN          RemovableMedia, CommandQueueing;
    DWORD            VendorIdOffset, ProductIdOffset, ProductRevisionOffset, SerialNumberOffset;
    STORAGE_BUS_TYPE BusType;
    DWORD            RawPropertiesLength;
    BYTE             RawDeviceProperties[1];
} STORAGE_DEVICE_DESCRIPTOR, *PSTORAGE_DEVICE_DESCRIPTOR;

typedef struct _DISK_EXTENT {
    DWORD         DiskNumber;
    LARGE_INTEGER StartingOffset, ExtentLength;
} DISK_EXTENT;
typedef struct _VOLUME_DISK_EXTENTS {
    DWORD       NumberOfDiskExtents;
    DISK_EXTENT Extents[1];
} VOLUME_DISK_EXTENTS, *PVOLUME_DISK_EXTENTS;

typedef struct _MOUNTDEV_NAME { USHORT NameLength; WCHAR Name[1]; } MOUNTDEV_NAME;

/* NovaOS's own, on a volume handle (kernel/um/um_disk.c): what fmifs.dll's
 * FormatEx and SetVolumeMountPoint/DeleteVolumeMountPoint use */
#define IOCTL_NOVA_VOLUME_INFO       CTL_CODE(0x8E56, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_NOVA_VOLUME_FORMAT     CTL_CODE(0x8E56, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_NOVA_VOLUME_SET_LETTER CTL_CODE(0x8E56, 0x802, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define NOVA_VOLUME_SYSTEM    0x01      /* an EFI System Partition */
#define NOVA_VOLUME_BOOT      0x02      /* drive C: */
#define NOVA_VOLUME_NOVA      0x04      /* NovaOS's own: not formatted or given another letter */
#define NOVA_VOLUME_READONLY  0x08
#define NOVA_VOLUME_MOUNTED   0x10
#define NOVA_VOLUME_REMOVABLE 0x20
typedef struct {
    GUID      Guid;                     /* \\?\Volume{...}\ */
    DWORD     Number;                   /* \Device\HarddiskVolumeN */
    DWORD     Flags;                    /* NOVA_VOLUME_* */
    WCHAR     Letter;                   /* 0: none */
    WCHAR     Reserved;
    DWORD     Disk;                     /* \\.\PhysicalDriveN (-1: none, drive C: in memory) */
    DWORD     Partition;
    ULONGLONG Offset, Length;
} NOVA_VOLUME_INFO;
typedef struct { DWORD FileSystem; WCHAR Label[33]; } NOVA_VOLUME_FORMAT;   /* 0: NTFS, 1: FAT32 */
typedef struct { WCHAR Letter; } NOVA_VOLUME_LETTER;                        /* 0: none */
