/* devpropdef.h — unified device property keys and types (DEVPROPKEY,
 * DEVPROPTYPE, DEFINE_DEVPROPKEY), as the Windows SDK has them; cfgmgr32's
 * CM_Get_DevNode_PropertyW and CM_Get_Device_Interface_PropertyW and
 * setupapi's SetupDiGetDeviceProperty take them */
#pragma once
#include <guiddef.h>

typedef ULONG DEVPROPTYPE, *PDEVPROPTYPE;
typedef GUID DEVPROPGUID, *PDEVPROPGUID;
typedef ULONG DEVPROPID, *PDEVPROPID;
typedef CHAR DEVPROP_BOOLEAN, *PDEVPROP_BOOLEAN;
#define DEVPROP_TRUE  ((DEVPROP_BOOLEAN)-1)
#define DEVPROP_FALSE ((DEVPROP_BOOLEAN)0)

typedef struct _DEVPROPKEY { DEVPROPGUID fmtid; DEVPROPID pid; } DEVPROPKEY, *PDEVPROPKEY;

#define DEVPROP_TYPEMOD_ARRAY       0x00001000
#define DEVPROP_TYPEMOD_LIST        0x00002000
#define DEVPROP_TYPE_EMPTY          0x00000000
#define DEVPROP_TYPE_NULL           0x00000001
#define DEVPROP_TYPE_SBYTE          0x00000002
#define DEVPROP_TYPE_BYTE           0x00000003
#define DEVPROP_TYPE_INT16          0x00000004
#define DEVPROP_TYPE_UINT16         0x00000005
#define DEVPROP_TYPE_INT32          0x00000006
#define DEVPROP_TYPE_UINT32         0x00000007
#define DEVPROP_TYPE_INT64          0x00000008
#define DEVPROP_TYPE_UINT64         0x00000009
#define DEVPROP_TYPE_GUID           0x0000000D
#define DEVPROP_TYPE_FILETIME       0x00000010
#define DEVPROP_TYPE_BOOLEAN        0x00000011
#define DEVPROP_TYPE_STRING         0x00000012
#define DEVPROP_TYPE_STRING_LIST    (DEVPROP_TYPE_STRING | DEVPROP_TYPEMOD_LIST)
#define DEVPROP_TYPE_BINARY         (DEVPROP_TYPE_BYTE | DEVPROP_TYPEMOD_ARRAY)

#define DEFINE_DEVPROPKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) \
    static const DEVPROPKEY name __attribute__((unused)) = { { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }, pid }
