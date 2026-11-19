/* netlistmgr.h — the Network List Manager API (netprofm.dll's
 * NetworkListManager class): INetworkListManager, INetwork,
 * INetworkConnection, their enumerators, the event interfaces, the cost
 * interfaces, their CLSID, IIDs and enums (NovaOS).  C only: each interface
 * lists its inherited methods in order, as Windows' vtables have them. */
#pragma once
#include <objbase.h>
#include <oleauto.h>

typedef enum NLM_CONNECTIVITY {
    NLM_CONNECTIVITY_DISCONNECTED      = 0x0000,
    NLM_CONNECTIVITY_IPV4_NOTRAFFIC    = 0x0001,
    NLM_CONNECTIVITY_IPV6_NOTRAFFIC    = 0x0002,
    NLM_CONNECTIVITY_IPV4_SUBNET       = 0x0010,
    NLM_CONNECTIVITY_IPV4_LOCALNETWORK = 0x0020,
    NLM_CONNECTIVITY_IPV4_INTERNET     = 0x0040,
    NLM_CONNECTIVITY_IPV6_SUBNET       = 0x0100,
    NLM_CONNECTIVITY_IPV6_LOCALNETWORK = 0x0200,
    NLM_CONNECTIVITY_IPV6_INTERNET     = 0x0400
} NLM_CONNECTIVITY;
typedef enum NLM_DOMAIN_TYPE {
    NLM_DOMAIN_TYPE_NON_DOMAIN_NETWORK = 0, NLM_DOMAIN_TYPE_DOMAIN_NETWORK = 1,
    NLM_DOMAIN_TYPE_DOMAIN_AUTHENTICATED = 2
} NLM_DOMAIN_TYPE;
typedef enum NLM_ENUM_NETWORK {
    NLM_ENUM_NETWORK_CONNECTED = 1, NLM_ENUM_NETWORK_DISCONNECTED = 2, NLM_ENUM_NETWORK_ALL = 3
} NLM_ENUM_NETWORK;
typedef enum NLM_NETWORK_CATEGORY {
    NLM_NETWORK_CATEGORY_PUBLIC = 0, NLM_NETWORK_CATEGORY_PRIVATE = 1,
    NLM_NETWORK_CATEGORY_DOMAIN_AUTHENTICATED = 2
} NLM_NETWORK_CATEGORY;
typedef enum NLM_NETWORK_PROPERTY_CHANGE {
    NLM_NETWORK_PROPERTY_CHANGE_CONNECTION = 0x01, NLM_NETWORK_PROPERTY_CHANGE_DESCRIPTION = 0x02,
    NLM_NETWORK_PROPERTY_CHANGE_NAME = 0x04, NLM_NETWORK_PROPERTY_CHANGE_ICON = 0x08,
    NLM_NETWORK_PROPERTY_CHANGE_CATEGORY_VALUE = 0x10
} NLM_NETWORK_PROPERTY_CHANGE;
typedef enum NLM_CONNECTION_PROPERTY_CHANGE {
    NLM_CONNECTION_PROPERTY_CHANGE_AUTHENTICATION = 0x01
} NLM_CONNECTION_PROPERTY_CHANGE;
typedef enum NLM_CONNECTION_COST {
    NLM_CONNECTION_COST_UNKNOWN = 0, NLM_CONNECTION_COST_UNRESTRICTED = 0x1,
    NLM_CONNECTION_COST_FIXED = 0x2, NLM_CONNECTION_COST_VARIABLE = 0x4,
    NLM_CONNECTION_COST_OVERDATALIMIT = 0x10000, NLM_CONNECTION_COST_CONGESTED = 0x20000,
    NLM_CONNECTION_COST_ROAMING = 0x40000, NLM_CONNECTION_COST_APPROACHINGDATALIMIT = 0x80000
} NLM_CONNECTION_COST;

#define NLM_UNKNOWN_DATAPLAN_STATUS 0xFFFFFFFF
#define NLM_MAX_ADDRESS_LIST_SIZE   10

typedef struct NLM_USAGE_DATA { DWORD UsageInMegabytes; FILETIME LastSyncTime; } NLM_USAGE_DATA;
typedef struct NLM_DATAPLAN_STATUS {
    GUID InterfaceGuid;
    NLM_USAGE_DATA UsageData;
    DWORD DataLimitInMegabytes, InboundBandwidthInKbps, OutboundBandwidthInKbps;
    FILETIME NextBillingCycle;
    DWORD MaxTransferSizeInMegabytes, Reserved;
} NLM_DATAPLAN_STATUS;
typedef struct NLM_SOCKADDR { BYTE data[128]; } NLM_SOCKADDR;
typedef struct NLM_SIMULATED_PROFILE_INFO {
    WCHAR ProfileName[256];
    NLM_CONNECTION_COST cost;
    DWORD UsageInMegabytes, DataLimitInMegabytes;
} NLM_SIMULATED_PROFILE_INFO;

DEFINE_GUID(CLSID_NetworkListManager,          0xDCB00C01, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkListManager,           0xDCB00000, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkListManagerEvents,     0xDCB00001, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetwork,                      0xDCB00002, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_IEnumNetworks,                 0xDCB00003, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkEvents,                0xDCB00004, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkConnection,            0xDCB00005, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_IEnumNetworkConnections,       0xDCB00006, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkConnectionEvents,      0xDCB00007, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkCostManager,           0xDCB00008, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkCostManagerEvents,     0xDCB00009, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkConnectionCost,        0xDCB0000A, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);
DEFINE_GUID(IID_INetworkConnectionCostEvents,  0xDCB0000B, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B);

typedef interface INetworkListManager INetworkListManager;
typedef interface INetwork INetwork;
typedef interface IEnumNetworks IEnumNetworks;
typedef interface INetworkConnection INetworkConnection;
typedef interface IEnumNetworkConnections IEnumNetworkConnections;
typedef interface INetworkListManagerEvents INetworkListManagerEvents;
typedef interface INetworkEvents INetworkEvents;
typedef interface INetworkConnectionEvents INetworkConnectionEvents;
typedef interface INetworkCostManager INetworkCostManager;
typedef interface INetworkCostManagerEvents INetworkCostManagerEvents;
typedef interface INetworkConnectionCost INetworkConnectionCost;

#ifndef NOVA_IENUMVARIANT_DEFINED
#define NOVA_IENUMVARIANT_DEFINED
DEFINE_GUID(IID_IEnumVARIANT,             0x00020404, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
typedef interface IEnumVARIANT IEnumVARIANT;
#undef INTERFACE
#define INTERFACE IEnumVARIANT
DECLARE_INTERFACE_(IEnumVARIANT, IUnknown) {
    BEGIN_INTERFACE
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD(Next)(THIS_ ULONG n, VARIANT *out, ULONG *got) PURE;
    STDMETHOD(Skip)(THIS_ ULONG n) PURE;
    STDMETHOD(Reset)(THIS) PURE;
    STDMETHOD(Clone)(THIS_ IEnumVARIANT **out) PURE;
    END_INTERFACE
};
#endif

#ifndef NOVA_ICONNECTIONPOINT_DEFINED
#define NOVA_ICONNECTIONPOINT_DEFINED
/* ocidl.h's connection points, which the events are advised through */
DEFINE_GUID(IID_IConnectionPointContainer, 0xB196B284, 0xBAB4, 0x101A, 0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07);
DEFINE_GUID(IID_IConnectionPoint,          0xB196B286, 0xBAB4, 0x101A, 0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07);
#define CONNECT_E_NOCONNECTION  ((HRESULT)0x80040200L)
#define CONNECT_E_ADVISELIMIT   ((HRESULT)0x80040201L)
#define CONNECT_E_CANNOTCONNECT ((HRESULT)0x80040202L)
typedef interface IConnectionPoint IConnectionPoint;
typedef interface IConnectionPointContainer IConnectionPointContainer;
typedef interface IEnumConnections IEnumConnections;
typedef interface IEnumConnectionPoints IEnumConnectionPoints;
#undef INTERFACE
#define INTERFACE IConnectionPoint
DECLARE_INTERFACE_(IConnectionPoint, IUnknown) {
    BEGIN_INTERFACE
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD(GetConnectionInterface)(THIS_ IID *iid) PURE;
    STDMETHOD(GetConnectionPointContainer)(THIS_ IConnectionPointContainer **cpc) PURE;
    STDMETHOD(Advise)(THIS_ IUnknown *sink, DWORD *cookie) PURE;
    STDMETHOD(Unadvise)(THIS_ DWORD cookie) PURE;
    STDMETHOD(EnumConnections)(THIS_ IEnumConnections **e) PURE;
    END_INTERFACE
};
#undef INTERFACE
#define INTERFACE IConnectionPointContainer
DECLARE_INTERFACE_(IConnectionPointContainer, IUnknown) {
    BEGIN_INTERFACE
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
    STDMETHOD(EnumConnectionPoints)(THIS_ IEnumConnectionPoints **e) PURE;
    STDMETHOD(FindConnectionPoint)(THIS_ REFIID iid, IConnectionPoint **cp) PURE;
    END_INTERFACE
};
#endif

#define NLM_IUNKNOWN \
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE; \
    STDMETHOD_(ULONG, AddRef)(THIS) PURE; \
    STDMETHOD_(ULONG, Release)(THIS) PURE;
#define NLM_IDISPATCH NLM_IUNKNOWN \
    STDMETHOD(GetTypeInfoCount)(THIS_ UINT *n) PURE; \
    STDMETHOD(GetTypeInfo)(THIS_ UINT i, LCID lcid, ITypeInfo **out) PURE; \
    STDMETHOD(GetIDsOfNames)(THIS_ REFIID riid, LPOLESTR *names, UINT n, LCID lcid, DISPID *ids) PURE; \
    STDMETHOD(Invoke)(THIS_ DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params, \
                      VARIANT *result, EXCEPINFO *ei, UINT *argerr) PURE;

#undef INTERFACE
#define INTERFACE INetworkListManager
DECLARE_INTERFACE_(INetworkListManager, IDispatch) {
    BEGIN_INTERFACE NLM_IDISPATCH
    STDMETHOD(GetNetworks)(THIS_ NLM_ENUM_NETWORK flags, IEnumNetworks **out) PURE;
    STDMETHOD(GetNetwork)(THIS_ GUID id, INetwork **out) PURE;
    STDMETHOD(GetNetworkConnections)(THIS_ IEnumNetworkConnections **out) PURE;
    STDMETHOD(GetNetworkConnection)(THIS_ GUID id, INetworkConnection **out) PURE;
    STDMETHOD(get_IsConnectedToInternet)(THIS_ VARIANT_BOOL *out) PURE;
    STDMETHOD(get_IsConnected)(THIS_ VARIANT_BOOL *out) PURE;
    STDMETHOD(GetConnectivity)(THIS_ NLM_CONNECTIVITY *out) PURE;
    STDMETHOD(SetSimulatedProfileInfo)(THIS_ NLM_SIMULATED_PROFILE_INFO *info) PURE;
    STDMETHOD(ClearSimulatedProfileInfo)(THIS) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE INetwork
DECLARE_INTERFACE_(INetwork, IDispatch) {
    BEGIN_INTERFACE NLM_IDISPATCH
    STDMETHOD(GetName)(THIS_ BSTR *name) PURE;
    STDMETHOD(SetName)(THIS_ BSTR name) PURE;
    STDMETHOD(GetDescription)(THIS_ BSTR *desc) PURE;
    STDMETHOD(SetDescription)(THIS_ BSTR desc) PURE;
    STDMETHOD(GetNetworkId)(THIS_ GUID *id) PURE;
    STDMETHOD(GetDomainType)(THIS_ NLM_DOMAIN_TYPE *type) PURE;
    STDMETHOD(GetNetworkConnections)(THIS_ IEnumNetworkConnections **out) PURE;
    STDMETHOD(GetTimeCreatedAndConnected)(THIS_ DWORD *created_lo, DWORD *created_hi,
                                          DWORD *connected_lo, DWORD *connected_hi) PURE;
    STDMETHOD(get_IsConnectedToInternet)(THIS_ VARIANT_BOOL *out) PURE;
    STDMETHOD(get_IsConnected)(THIS_ VARIANT_BOOL *out) PURE;
    STDMETHOD(GetConnectivity)(THIS_ NLM_CONNECTIVITY *out) PURE;
    STDMETHOD(GetCategory)(THIS_ NLM_NETWORK_CATEGORY *out) PURE;
    STDMETHOD(SetCategory)(THIS_ NLM_NETWORK_CATEGORY category) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IEnumNetworks
DECLARE_INTERFACE_(IEnumNetworks, IDispatch) {
    BEGIN_INTERFACE NLM_IDISPATCH
    STDMETHOD(get__NewEnum)(THIS_ IEnumVARIANT **out) PURE;
    STDMETHOD(Next)(THIS_ ULONG n, INetwork **out, ULONG *got) PURE;
    STDMETHOD(Skip)(THIS_ ULONG n) PURE;
    STDMETHOD(Reset)(THIS) PURE;
    STDMETHOD(Clone)(THIS_ IEnumNetworks **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE INetworkConnection
DECLARE_INTERFACE_(INetworkConnection, IDispatch) {
    BEGIN_INTERFACE NLM_IDISPATCH
    STDMETHOD(GetNetwork)(THIS_ INetwork **out) PURE;
    STDMETHOD(get_IsConnectedToInternet)(THIS_ VARIANT_BOOL *out) PURE;
    STDMETHOD(get_IsConnected)(THIS_ VARIANT_BOOL *out) PURE;
    STDMETHOD(GetConnectivity)(THIS_ NLM_CONNECTIVITY *out) PURE;
    STDMETHOD(GetConnectionId)(THIS_ GUID *id) PURE;
    STDMETHOD(GetAdapterId)(THIS_ GUID *id) PURE;
    STDMETHOD(GetDomainType)(THIS_ NLM_DOMAIN_TYPE *type) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE IEnumNetworkConnections
DECLARE_INTERFACE_(IEnumNetworkConnections, IDispatch) {
    BEGIN_INTERFACE NLM_IDISPATCH
    STDMETHOD(get__NewEnum)(THIS_ IEnumVARIANT **out) PURE;
    STDMETHOD(Next)(THIS_ ULONG n, INetworkConnection **out, ULONG *got) PURE;
    STDMETHOD(Skip)(THIS_ ULONG n) PURE;
    STDMETHOD(Reset)(THIS) PURE;
    STDMETHOD(Clone)(THIS_ IEnumNetworkConnections **out) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE INetworkListManagerEvents
DECLARE_INTERFACE_(INetworkListManagerEvents, IUnknown) {
    BEGIN_INTERFACE NLM_IUNKNOWN
    STDMETHOD(ConnectivityChanged)(THIS_ NLM_CONNECTIVITY connectivity) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE INetworkEvents
DECLARE_INTERFACE_(INetworkEvents, IUnknown) {
    BEGIN_INTERFACE NLM_IUNKNOWN
    STDMETHOD(NetworkAdded)(THIS_ GUID id) PURE;
    STDMETHOD(NetworkDeleted)(THIS_ GUID id) PURE;
    STDMETHOD(NetworkConnectivityChanged)(THIS_ GUID id, NLM_CONNECTIVITY connectivity) PURE;
    STDMETHOD(NetworkPropertyChanged)(THIS_ GUID id, NLM_NETWORK_PROPERTY_CHANGE flags) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE INetworkConnectionEvents
DECLARE_INTERFACE_(INetworkConnectionEvents, IUnknown) {
    BEGIN_INTERFACE NLM_IUNKNOWN
    STDMETHOD(NetworkConnectionConnectivityChanged)(THIS_ GUID id, NLM_CONNECTIVITY connectivity) PURE;
    STDMETHOD(NetworkConnectionPropertyChanged)(THIS_ GUID id, NLM_CONNECTION_PROPERTY_CHANGE flags) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE INetworkCostManager
DECLARE_INTERFACE_(INetworkCostManager, IUnknown) {
    BEGIN_INTERFACE NLM_IUNKNOWN
    STDMETHOD(GetCost)(THIS_ DWORD *cost, NLM_SOCKADDR *dest) PURE;
    STDMETHOD(GetDataPlanStatus)(THIS_ NLM_DATAPLAN_STATUS *status, NLM_SOCKADDR *dest) PURE;
    STDMETHOD(SetDestinationAddresses)(THIS_ UINT32 n, NLM_SOCKADDR *list, VARIANT_BOOL append) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE INetworkCostManagerEvents
DECLARE_INTERFACE_(INetworkCostManagerEvents, IUnknown) {
    BEGIN_INTERFACE NLM_IUNKNOWN
    STDMETHOD(CostChanged)(THIS_ DWORD cost, NLM_SOCKADDR *dest) PURE;
    STDMETHOD(DataPlanStatusChanged)(THIS_ NLM_SOCKADDR *dest) PURE;
    END_INTERFACE
};

#undef INTERFACE
#define INTERFACE INetworkConnectionCost
DECLARE_INTERFACE_(INetworkConnectionCost, IUnknown) {
    BEGIN_INTERFACE NLM_IUNKNOWN
    STDMETHOD(GetCost)(THIS_ DWORD *cost) PURE;
    STDMETHOD(GetDataPlanStatus)(THIS_ NLM_DATAPLAN_STATUS *status) PURE;
    END_INTERFACE
};
#undef INTERFACE
