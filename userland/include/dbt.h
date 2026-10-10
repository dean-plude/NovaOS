/* dbt.h — WM_DEVICECHANGE's events and their DEV_BROADCAST_* data */
#pragma once
#include <windows.h>

#define DBT_DEVNODES_CHANGED        0x0007
#define DBT_QUERYCHANGECONFIG       0x0017
#define DBT_CONFIGCHANGED           0x0018
#define DBT_DEVICEARRIVAL           0x8000
#define DBT_DEVICEQUERYREMOVE       0x8001
#define DBT_DEVICEQUERYREMOVEFAILED 0x8002
#define DBT_DEVICEREMOVEPENDING     0x8003
#define DBT_DEVICEREMOVECOMPLETE    0x8004
#define DBT_DEVICETYPESPECIFIC      0x8005
#define DBT_CUSTOMEVENT             0x8006

#define DBT_DEVTYP_OEM              0x00000000
#define DBT_DEVTYP_VOLUME           0x00000002
#define DBT_DEVTYP_PORT             0x00000003
#define DBT_DEVTYP_DEVICEINTERFACE  0x00000005
#define DBT_DEVTYP_HANDLE           0x00000006

#define DBTF_MEDIA                  0x0001
#define DBTF_NET                    0x0002

typedef struct { DWORD dbch_size, dbch_devicetype, dbch_reserved; } DEV_BROADCAST_HDR, *PDEV_BROADCAST_HDR;
typedef struct {
    DWORD dbcv_size, dbcv_devicetype, dbcv_reserved, dbcv_unitmask;
    WORD  dbcv_flags;
} DEV_BROADCAST_VOLUME, *PDEV_BROADCAST_VOLUME;
typedef struct {
    DWORD dbcc_size, dbcc_devicetype, dbcc_reserved;
    GUID  dbcc_classguid;
    WCHAR dbcc_name[1];
} DEV_BROADCAST_DEVICEINTERFACE_W, *PDEV_BROADCAST_DEVICEINTERFACE_W;
typedef struct {
    DWORD dbcc_size, dbcc_devicetype, dbcc_reserved;
    GUID  dbcc_classguid;
    char  dbcc_name[1];
} DEV_BROADCAST_DEVICEINTERFACE_A, *PDEV_BROADCAST_DEVICEINTERFACE_A;
typedef struct {
    DWORD  dbch_size, dbch_devicetype, dbch_reserved;
    HANDLE dbch_handle;
    void  *dbch_hdevnotify;
    GUID   dbch_eventguid;
    LONG   dbch_nameoffset;
    BYTE   dbch_data[1];
} DEV_BROADCAST_HANDLE, *PDEV_BROADCAST_HANDLE;
#ifdef UNICODE
typedef DEV_BROADCAST_DEVICEINTERFACE_W DEV_BROADCAST_DEVICEINTERFACE;
#else
typedef DEV_BROADCAST_DEVICEINTERFACE_A DEV_BROADCAST_DEVICEINTERFACE;
#endif
