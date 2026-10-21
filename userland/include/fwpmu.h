/*
 * fwpmu.h — the Windows Filtering Platform's management API (fwpuclnt.dll):
 * the types, GUIDs and functions NovaOS's fwpuclnt.dll serves.  Layouts
 * follow the Windows SDK's fwptypes.h, fwpmtypes.h and fwpmu.h (as the
 * public-domain mingw-w64 headers give them); the GUIDs are Windows'.
 */
#pragma once
#include <windows.h>
#include <winsec.h>

#ifndef DEFINE_GUID_FWP
#define DEFINE_GUID_FWP(n, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    static const GUID n = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
#endif

typedef UINT32 FWP_ACTION_TYPE;
#define FWP_ACTION_FLAG_TERMINATING     0x00001000
#define FWP_ACTION_FLAG_NON_TERMINATING 0x00002000
#define FWP_ACTION_FLAG_CALLOUT         0x00004000
#define FWP_ACTION_BLOCK                (0x1 | FWP_ACTION_FLAG_TERMINATING)
#define FWP_ACTION_PERMIT               (0x2 | FWP_ACTION_FLAG_TERMINATING)
#define FWP_ACTION_CALLOUT_TERMINATING  (0x3 | FWP_ACTION_FLAG_CALLOUT | FWP_ACTION_FLAG_TERMINATING)
#define FWP_ACTION_CALLOUT_INSPECTION   (0x4 | FWP_ACTION_FLAG_CALLOUT | FWP_ACTION_FLAG_NON_TERMINATING)
#define FWP_CONDITION_FLAG_IS_LOOPBACK  0x00000001

#define FWPM_FILTER_FLAG_NONE                0x00000000
#define FWPM_FILTER_FLAG_PERSISTENT          0x00000001
#define FWPM_FILTER_FLAG_BOOTTIME            0x00000002
#define FWPM_FILTER_FLAG_HAS_PROVIDER_CONTEXT 0x00000004
#define FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT  0x00000008
#define FWPM_FILTER_FLAG_PERMIT_IF_CALLOUT_UNREGISTERED 0x00000010
#define FWPM_FILTER_FLAG_DISABLED            0x00000020
#define FWPM_SESSION_FLAG_DYNAMIC            0x00000001
#define FWPM_SUBLAYER_FLAG_PERSISTENT        0x00000001
#define FWPM_PROVIDER_FLAG_PERSISTENT        0x00000001
#define RPC_C_AUTHN_WINNT                    10
#define RPC_C_AUTHN_DEFAULT                  0xFFFFFFFFL

typedef enum FWP_DATA_TYPE_ {
    FWP_EMPTY = 0, FWP_UINT8 = 1, FWP_UINT16 = 2, FWP_UINT32 = 3, FWP_UINT64 = 4, FWP_INT8 = 5, FWP_INT16 = 6,
    FWP_INT32 = 7, FWP_INT64 = 8, FWP_FLOAT = 9, FWP_DOUBLE = 10, FWP_BYTE_ARRAY16_TYPE = 11, FWP_BYTE_BLOB_TYPE = 12,
    FWP_SID = 13, FWP_SECURITY_DESCRIPTOR_TYPE = 14, FWP_TOKEN_INFORMATION_TYPE = 15,
    FWP_TOKEN_ACCESS_INFORMATION_TYPE = 16, FWP_UNICODE_STRING_TYPE = 17, FWP_BYTE_ARRAY6_TYPE = 18,
    FWP_SINGLE_DATA_TYPE_MAX = 0xff, FWP_V4_ADDR_MASK = 0x100, FWP_V6_ADDR_MASK = 0x101, FWP_RANGE_TYPE = 0x102,
    FWP_DATA_TYPE_MAX = 0x103
} FWP_DATA_TYPE;

typedef enum FWP_MATCH_TYPE_ {
    FWP_MATCH_EQUAL = 0, FWP_MATCH_GREATER = 1, FWP_MATCH_LESS = 2, FWP_MATCH_GREATER_OR_EQUAL = 3,
    FWP_MATCH_LESS_OR_EQUAL = 4, FWP_MATCH_RANGE = 5, FWP_MATCH_FLAGS_ALL_SET = 6, FWP_MATCH_FLAGS_ANY_SET = 7,
    FWP_MATCH_FLAGS_NONE_SET = 8, FWP_MATCH_EQUAL_CASE_INSENSITIVE = 9, FWP_MATCH_NOT_EQUAL = 10,
    FWP_MATCH_TYPE_MAX = 11
} FWP_MATCH_TYPE;

typedef enum FWP_FILTER_ENUM_TYPE_ { FWP_FILTER_ENUM_FULLY_CONTAINED = 0, FWP_FILTER_ENUM_OVERLAPPING = 1 } FWP_FILTER_ENUM_TYPE;

typedef struct FWP_BYTE_ARRAY6_ { UINT8 byteArray6[6]; } FWP_BYTE_ARRAY6;
typedef struct FWP_BYTE_ARRAY16_ { UINT8 byteArray16[16]; } FWP_BYTE_ARRAY16;
typedef struct FWP_BYTE_BLOB_ { UINT32 size; UINT8 *data; } FWP_BYTE_BLOB;
typedef struct FWP_V4_ADDR_AND_MASK_ { UINT32 addr; UINT32 mask; } FWP_V4_ADDR_AND_MASK;   /* host byte order */
typedef struct FWP_V6_ADDR_AND_MASK_ { UINT8 addr[16]; UINT8 prefixLength; } FWP_V6_ADDR_AND_MASK;

typedef struct FWP_VALUE0_ {
    FWP_DATA_TYPE type;
    union {
        UINT8 uint8; UINT16 uint16; UINT32 uint32; UINT64 *uint64;
        INT8 int8; INT16 int16; INT32 int32; INT64 *int64;
        float float32; double *double64;
        FWP_BYTE_ARRAY16 *byteArray16; FWP_BYTE_BLOB *byteBlob; SID *sid; FWP_BYTE_BLOB *sd;
        void *tokenInformation; FWP_BYTE_BLOB *tokenAccessInformation; LPWSTR unicodeString;
        FWP_BYTE_ARRAY6 *byteArray6;
    };
} FWP_VALUE0;

typedef struct FWP_RANGE0_ { FWP_VALUE0 valueLow; FWP_VALUE0 valueHigh; } FWP_RANGE0;

typedef struct FWP_CONDITION_VALUE0_ {
    FWP_DATA_TYPE type;
    union {
        UINT8 uint8; UINT16 uint16; UINT32 uint32; UINT64 *uint64;
        INT8 int8; INT16 int16; INT32 int32; INT64 *int64;
        float float32; double *double64;
        FWP_BYTE_ARRAY16 *byteArray16; FWP_BYTE_BLOB *byteBlob; SID *sid; FWP_BYTE_BLOB *sd;
        void *tokenInformation; FWP_BYTE_BLOB *tokenAccessInformation; LPWSTR unicodeString;
        FWP_BYTE_ARRAY6 *byteArray6; FWP_V4_ADDR_AND_MASK *v4AddrMask; FWP_V6_ADDR_AND_MASK *v6AddrMask;
        FWP_RANGE0 *rangeValue;
    };
} FWP_CONDITION_VALUE0;

typedef struct FWPM_DISPLAY_DATA0_ { wchar_t *name; wchar_t *description; } FWPM_DISPLAY_DATA0;

typedef struct FWPM_ACTION0_ {
    FWP_ACTION_TYPE type;
    union { GUID filterType; GUID calloutKey; };
} FWPM_ACTION0;

typedef struct FWPM_FILTER_CONDITION0_ {
    GUID                 fieldKey;
    FWP_MATCH_TYPE       matchType;
    FWP_CONDITION_VALUE0 conditionValue;
} FWPM_FILTER_CONDITION0;

typedef struct FWPM_FILTER0_ {
    GUID                    filterKey;
    FWPM_DISPLAY_DATA0      displayData;
    UINT32                  flags;
    GUID                   *providerKey;
    FWP_BYTE_BLOB           providerData;
    GUID                    layerKey;
    GUID                    subLayerKey;
    FWP_VALUE0              weight;
    UINT32                  numFilterConditions;
    FWPM_FILTER_CONDITION0 *filterCondition;
    FWPM_ACTION0            action;
    union { UINT64 rawContext; GUID providerContextKey; };
    GUID                   *reserved;
    UINT64                  filterId;
    FWP_VALUE0              effectiveWeight;
} FWPM_FILTER0;

typedef struct FWPM_FILTER_ENUM_TEMPLATE0_ {
    GUID                   *providerKey;
    GUID                    layerKey;
    FWP_FILTER_ENUM_TYPE    enumType;
    UINT32                  flags;
    void                   *providerContextTemplate;
    UINT32                  numFilterConditions;
    FWPM_FILTER_CONDITION0 *filterCondition;
    UINT32                  actionMask;
    GUID                   *calloutKey;
} FWPM_FILTER_ENUM_TEMPLATE0;

typedef struct FWPM_SESSION0_ {
    GUID               sessionKey;
    FWPM_DISPLAY_DATA0 displayData;
    UINT32             flags;
    UINT32             txnWaitTimeoutInMSec;
    DWORD              processId;
    SID               *sid;
    wchar_t           *username;
    BOOL               kernelMode;
} FWPM_SESSION0;

typedef struct FWPM_SUBLAYER0_ {
    GUID               subLayerKey;
    FWPM_DISPLAY_DATA0 displayData;
    UINT16             flags;
    GUID              *providerKey;
    FWP_BYTE_BLOB      providerData;
    UINT16             weight;
} FWPM_SUBLAYER0;

typedef struct FWPM_SUBLAYER_ENUM_TEMPLATE0_ { GUID *providerKey; } FWPM_SUBLAYER_ENUM_TEMPLATE0;

typedef struct FWPM_PROVIDER0_ {
    GUID               providerKey;
    FWPM_DISPLAY_DATA0 displayData;
    UINT32             flags;
    FWP_BYTE_BLOB      providerData;
    wchar_t           *serviceName;
} FWPM_PROVIDER0;

/* Layers */
DEFINE_GUID_FWP(FWPM_LAYER_INBOUND_TRANSPORT_V4,  0x5926dfc8, 0xe3cf, 0x4426, 0xa2, 0x83, 0xdc, 0x39, 0x3f, 0x5d, 0x0f, 0x9d);
DEFINE_GUID_FWP(FWPM_LAYER_INBOUND_TRANSPORT_V6,  0x634a869f, 0xfc23, 0x4b90, 0xb0, 0xc1, 0xbf, 0x62, 0x0a, 0x36, 0xae, 0x6f);
DEFINE_GUID_FWP(FWPM_LAYER_OUTBOUND_TRANSPORT_V4, 0x09e61aea, 0xd214, 0x46e2, 0x9b, 0x21, 0xb2, 0x6b, 0x0b, 0x2f, 0x28, 0xc8);
DEFINE_GUID_FWP(FWPM_LAYER_OUTBOUND_TRANSPORT_V6, 0xe1735bde, 0x013f, 0x4655, 0xb3, 0x51, 0xa4, 0x9e, 0x15, 0x76, 0x2d, 0xf0);
DEFINE_GUID_FWP(FWPM_LAYER_ALE_AUTH_CONNECT_V4,   0xc38d57d1, 0x05a7, 0x4c33, 0x90, 0x4f, 0x7f, 0xbc, 0xee, 0xe6, 0x0e, 0x82);
DEFINE_GUID_FWP(FWPM_LAYER_ALE_AUTH_CONNECT_V6,   0x4a72393b, 0x319f, 0x44bc, 0x84, 0xc3, 0xba, 0x54, 0xdc, 0xb3, 0xb6, 0xb4);
DEFINE_GUID_FWP(FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, 0xe1cd9fe7, 0xf4b5, 0x4273, 0x96, 0xc0, 0x59, 0x2e, 0x48, 0x7b, 0x86, 0x50);
DEFINE_GUID_FWP(FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, 0xa3b42c97, 0x9f04, 0x4672, 0xb8, 0x7e, 0xce, 0xe9, 0xc4, 0x83, 0x25, 0x7f);
DEFINE_GUID_FWP(FWPM_LAYER_ALE_AUTH_LISTEN_V4,    0x88bb5dad, 0x76d7, 0x4227, 0x9c, 0x71, 0xdf, 0x0a, 0x3e, 0xd7, 0xbe, 0x7e);
DEFINE_GUID_FWP(FWPM_LAYER_ALE_AUTH_LISTEN_V6,    0x7ac9de24, 0x17dd, 0x4814, 0xb4, 0xbd, 0xa9, 0xfb, 0xc9, 0x5a, 0x32, 0x1b);

/* Conditions */
DEFINE_GUID_FWP(FWPM_CONDITION_IP_PROTOCOL,       0x3971ef2b, 0x623e, 0x4f9a, 0x8c, 0xb1, 0x6e, 0x79, 0xb8, 0x06, 0xb9, 0xa7);
DEFINE_GUID_FWP(FWPM_CONDITION_IP_LOCAL_ADDRESS,  0xd9ee00de, 0xc1ef, 0x4617, 0xbf, 0xe3, 0xff, 0xd8, 0xf5, 0xa0, 0x89, 0x57);
DEFINE_GUID_FWP(FWPM_CONDITION_IP_REMOTE_ADDRESS, 0xb235ae9a, 0x1d64, 0x49b8, 0xa4, 0x4c, 0x5f, 0xf3, 0xd9, 0x09, 0x50, 0x45);
DEFINE_GUID_FWP(FWPM_CONDITION_IP_LOCAL_PORT,     0x0c1ba1af, 0x5765, 0x453f, 0xaf, 0x22, 0xa8, 0xf7, 0x91, 0xac, 0x77, 0x5b);
DEFINE_GUID_FWP(FWPM_CONDITION_IP_REMOTE_PORT,    0xc35a604d, 0xd22b, 0x4e1a, 0x91, 0xb4, 0x68, 0xf6, 0x74, 0xee, 0x67, 0x4b);
DEFINE_GUID_FWP(FWPM_CONDITION_ALE_APP_ID,        0xd78e1e87, 0x8644, 0x4ea5, 0x94, 0x37, 0xd8, 0x09, 0xec, 0xef, 0xc9, 0x71);
DEFINE_GUID_FWP(FWPM_CONDITION_FLAGS,             0x632ce23b, 0x5167, 0x435c, 0x86, 0xd7, 0xe9, 0x03, 0x68, 0x4a, 0xa8, 0x0c);
DEFINE_GUID_FWP(FWPM_CONDITION_INTERFACE_INDEX,   0x667fd755, 0xd695, 0x434a, 0x8a, 0xf5, 0xd3, 0x83, 0x5a, 0x12, 0x59, 0xbc);
DEFINE_GUID_FWP(FWPM_CONDITION_IP_LOCAL_INTERFACE, 0x4cd62a49, 0x59c3, 0x4969, 0xb7, 0xf3, 0xbd, 0xa5, 0xd3, 0x28, 0x90, 0xa4);

/* Sublayers */
DEFINE_GUID_FWP(FWPM_SUBLAYER_UNIVERSAL,          0xeebecc03, 0xced4, 0x4380, 0x81, 0x9a, 0x27, 0x34, 0x39, 0x7b, 0x2b, 0x74);
DEFINE_GUID_FWP(FWPM_SUBLAYER_MPSSVC_WF,          0xb3cdd441, 0xaf90, 0x41ba, 0xa7, 0x45, 0x7c, 0x60, 0x08, 0xff, 0x23, 0x01);

#ifndef FWP_E_FILTER_NOT_FOUND
#define FWP_E_CALLOUT_NOT_FOUND   ((DWORD)0x80320001)
#define FWP_E_FILTER_NOT_FOUND    ((DWORD)0x80320003)
#define FWP_E_LAYER_NOT_FOUND     ((DWORD)0x80320004)
#define FWP_E_PROVIDER_NOT_FOUND  ((DWORD)0x80320005)
#define FWP_E_SUBLAYER_NOT_FOUND  ((DWORD)0x80320007)
#define FWP_E_NOT_FOUND           ((DWORD)0x80320008)
#define FWP_E_ALREADY_EXISTS      ((DWORD)0x80320009)
#define FWP_E_IN_USE              ((DWORD)0x8032000A)
#define FWP_E_NO_TXN_IN_PROGRESS  ((DWORD)0x8032000D)
#define FWP_E_TXN_IN_PROGRESS     ((DWORD)0x8032000E)
#define FWP_E_INVALID_ACTION_TYPE ((DWORD)0x80320024)
#define FWP_E_INVALID_WEIGHT      ((DWORD)0x80320025)
#define FWP_E_INVALID_PARAMETER   ((DWORD)0x80320035)
#endif

#define FWPAPI __declspec(dllimport)
#ifdef FWPUCLNT_BUILD
#undef FWPAPI
#define FWPAPI __declspec(dllexport)
#endif

FWPAPI DWORD WINAPI FwpmEngineOpen0(const wchar_t *serverName, UINT32 authnService, void *authIdentity,
                                    const FWPM_SESSION0 *session, HANDLE *engineHandle);
FWPAPI DWORD WINAPI FwpmEngineClose0(HANDLE engineHandle);
FWPAPI DWORD WINAPI FwpmTransactionBegin0(HANDLE engineHandle, UINT32 flags);
FWPAPI DWORD WINAPI FwpmTransactionCommit0(HANDLE engineHandle);
FWPAPI DWORD WINAPI FwpmTransactionAbort0(HANDLE engineHandle);
FWPAPI DWORD WINAPI FwpmFilterAdd0(HANDLE engineHandle, const FWPM_FILTER0 *filter, void *sd, UINT64 *id);
FWPAPI DWORD WINAPI FwpmFilterDeleteById0(HANDLE engineHandle, UINT64 id);
FWPAPI DWORD WINAPI FwpmFilterDeleteByKey0(HANDLE engineHandle, const GUID *key);
FWPAPI DWORD WINAPI FwpmFilterGetById0(HANDLE engineHandle, UINT64 id, FWPM_FILTER0 **filter);
FWPAPI DWORD WINAPI FwpmFilterGetByKey0(HANDLE engineHandle, const GUID *key, FWPM_FILTER0 **filter);
FWPAPI DWORD WINAPI FwpmFilterCreateEnumHandle0(HANDLE engineHandle, const FWPM_FILTER_ENUM_TEMPLATE0 *enumTemplate, HANDLE *enumHandle);
FWPAPI DWORD WINAPI FwpmFilterEnum0(HANDLE engineHandle, HANDLE enumHandle, UINT32 numEntriesRequested,
                                    FWPM_FILTER0 ***entries, UINT32 *numEntriesReturned);
FWPAPI DWORD WINAPI FwpmFilterDestroyEnumHandle0(HANDLE engineHandle, HANDLE enumHandle);
FWPAPI DWORD WINAPI FwpmSubLayerAdd0(HANDLE engineHandle, const FWPM_SUBLAYER0 *subLayer, void *sd);
FWPAPI DWORD WINAPI FwpmSubLayerDeleteByKey0(HANDLE engineHandle, const GUID *key);
FWPAPI DWORD WINAPI FwpmSubLayerGetByKey0(HANDLE engineHandle, const GUID *key, FWPM_SUBLAYER0 **subLayer);
FWPAPI DWORD WINAPI FwpmSubLayerCreateEnumHandle0(HANDLE engineHandle, const FWPM_SUBLAYER_ENUM_TEMPLATE0 *enumTemplate, HANDLE *enumHandle);
FWPAPI DWORD WINAPI FwpmSubLayerEnum0(HANDLE engineHandle, HANDLE enumHandle, UINT32 numEntriesRequested,
                                      FWPM_SUBLAYER0 ***entries, UINT32 *numEntriesReturned);
FWPAPI DWORD WINAPI FwpmSubLayerDestroyEnumHandle0(HANDLE engineHandle, HANDLE enumHandle);
FWPAPI DWORD WINAPI FwpmProviderAdd0(HANDLE engineHandle, const FWPM_PROVIDER0 *provider, void *sd);
FWPAPI DWORD WINAPI FwpmProviderDeleteByKey0(HANDLE engineHandle, const GUID *key);
FWPAPI DWORD WINAPI FwpmProviderGetByKey0(HANDLE engineHandle, const GUID *key, FWPM_PROVIDER0 **provider);
FWPAPI DWORD WINAPI FwpmGetAppIdFromFileName0(const wchar_t *fileName, FWP_BYTE_BLOB **appId);
FWPAPI void  WINAPI FwpmFreeMemory0(void **p);
