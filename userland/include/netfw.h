/* netfw.h — the Windows Firewall API (hnetcfg.dll's NetFwPolicy2 and
 * NetFwRule classes): INetFwPolicy2, INetFwRules, INetFwRule..3, their
 * CLSIDs, IIDs and enums (NovaOS).  C only: each interface lists its
 * inherited methods in order, as Windows' vtables have them. */
#pragma once
#include <objbase.h>
#include <oleauto.h>

typedef enum NET_FW_PROFILE_TYPE2_ {
    NET_FW_PROFILE2_DOMAIN = 0x1, NET_FW_PROFILE2_PRIVATE = 0x2,
    NET_FW_PROFILE2_PUBLIC = 0x4, NET_FW_PROFILE2_ALL = 0x7FFFFFFF
} NET_FW_PROFILE_TYPE2;
typedef enum NET_FW_IP_PROTOCOL_ {
    NET_FW_IP_PROTOCOL_TCP = 6, NET_FW_IP_PROTOCOL_UDP = 17, NET_FW_IP_PROTOCOL_ANY = 256
} NET_FW_IP_PROTOCOL;
typedef enum NET_FW_RULE_DIRECTION_ {
    NET_FW_RULE_DIR_IN = 1, NET_FW_RULE_DIR_OUT = 2, NET_FW_RULE_DIR_MAX = 3
} NET_FW_RULE_DIRECTION;
typedef enum NET_FW_ACTION_ {
    NET_FW_ACTION_BLOCK = 0, NET_FW_ACTION_ALLOW = 1, NET_FW_ACTION_MAX = 2
} NET_FW_ACTION;
typedef enum NET_FW_MODIFY_STATE_ {
    NET_FW_MODIFY_STATE_OK = 0, NET_FW_MODIFY_STATE_GP_OVERRIDE = 1,
    NET_FW_MODIFY_STATE_INBOUND_BLOCKED = 2
} NET_FW_MODIFY_STATE;

DEFINE_GUID(CLSID_NetFwPolicy2,           0xE2B3C97F, 0x6AE1, 0x41AC, 0x81, 0x7A, 0xF6, 0xF9, 0x21, 0x66, 0xD7, 0xDD);
DEFINE_GUID(CLSID_NetFwRule,              0x2C5BC43E, 0x3369, 0x4C33, 0xAB, 0x0C, 0xBE, 0x94, 0x69, 0x67, 0x7A, 0xF4);
DEFINE_GUID(IID_INetFwPolicy2,            0x98325047, 0xC671, 0x4174, 0x8D, 0x81, 0xDE, 0xFC, 0xD3, 0xF0, 0x31, 0x86);
DEFINE_GUID(IID_INetFwRules,              0x9C4C6277, 0x5027, 0x441E, 0xAF, 0xAE, 0xCA, 0x1F, 0x54, 0x2D, 0xA0, 0x09);
DEFINE_GUID(IID_INetFwRule,               0xAF230D27, 0xBABA, 0x4E42, 0xAC, 0xED, 0xF5, 0x24, 0xF2, 0x2C, 0xFC, 0xE2);
DEFINE_GUID(IID_INetFwRule2,              0x9C27C8DA, 0x189B, 0x4DDE, 0x89, 0xF7, 0x8B, 0x39, 0xA3, 0x16, 0x78, 0x2C);
DEFINE_GUID(IID_INetFwRule3,              0xB21563FF, 0xD696, 0x4222, 0xAB, 0x46, 0x4E, 0x89, 0xB7, 0x3A, 0xB3, 0x4A);
DEFINE_GUID(IID_INetFwServiceRestriction, 0x8267BBE3, 0xF890, 0x491C, 0xB7, 0xB6, 0x2D, 0xB1, 0xEF, 0x0E, 0x5D, 0x2B);

typedef interface INetFwRule INetFwRule;
typedef interface INetFwRule2 INetFwRule2;
typedef interface INetFwRule3 INetFwRule3;
typedef interface INetFwRules INetFwRules;
typedef interface INetFwServiceRestriction INetFwServiceRestriction;
typedef interface INetFwPolicy2 INetFwPolicy2;

#ifndef NOVA_IENUMVARIANT_DEFINED
#define NOVA_IENUMVARIANT_DEFINED
/* INetFwRules::get__NewEnum hands one out */
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

#define NETFW_IUNKNOWN \
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE; \
    STDMETHOD_(ULONG, AddRef)(THIS) PURE; \
    STDMETHOD_(ULONG, Release)(THIS) PURE;
#define NETFW_IDISPATCH NETFW_IUNKNOWN \
    STDMETHOD(GetTypeInfoCount)(THIS_ UINT *n) PURE; \
    STDMETHOD(GetTypeInfo)(THIS_ UINT i, LCID lcid, ITypeInfo **out) PURE; \
    STDMETHOD(GetIDsOfNames)(THIS_ REFIID riid, LPOLESTR *names, UINT n, LCID lcid, DISPID *ids) PURE; \
    STDMETHOD(Invoke)(THIS_ DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params, \
                      VARIANT *result, EXCEPINFO *ei, UINT *argerr) PURE;

#define NETFW_INETFWRULE NETFW_IDISPATCH \
    STDMETHOD(get_Name)(THIS_ BSTR *name) PURE; \
    STDMETHOD(put_Name)(THIS_ BSTR name) PURE; \
    STDMETHOD(get_Description)(THIS_ BSTR *desc) PURE; \
    STDMETHOD(put_Description)(THIS_ BSTR desc) PURE; \
    STDMETHOD(get_ApplicationName)(THIS_ BSTR *imagename) PURE; \
    STDMETHOD(put_ApplicationName)(THIS_ BSTR imagename) PURE; \
    STDMETHOD(get_ServiceName)(THIS_ BSTR *service) PURE; \
    STDMETHOD(put_ServiceName)(THIS_ BSTR service) PURE; \
    STDMETHOD(get_Protocol)(THIS_ LONG *protocol) PURE; \
    STDMETHOD(put_Protocol)(THIS_ LONG protocol) PURE; \
    STDMETHOD(get_LocalPorts)(THIS_ BSTR *ports) PURE; \
    STDMETHOD(put_LocalPorts)(THIS_ BSTR ports) PURE; \
    STDMETHOD(get_RemotePorts)(THIS_ BSTR *ports) PURE; \
    STDMETHOD(put_RemotePorts)(THIS_ BSTR ports) PURE; \
    STDMETHOD(get_LocalAddresses)(THIS_ BSTR *address) PURE; \
    STDMETHOD(put_LocalAddresses)(THIS_ BSTR address) PURE; \
    STDMETHOD(get_RemoteAddresses)(THIS_ BSTR *address) PURE; \
    STDMETHOD(put_RemoteAddresses)(THIS_ BSTR address) PURE; \
    STDMETHOD(get_IcmpTypesAndCodes)(THIS_ BSTR *codes) PURE; \
    STDMETHOD(put_IcmpTypesAndCodes)(THIS_ BSTR codes) PURE; \
    STDMETHOD(get_Direction)(THIS_ NET_FW_RULE_DIRECTION *dir) PURE; \
    STDMETHOD(put_Direction)(THIS_ NET_FW_RULE_DIRECTION dir) PURE; \
    STDMETHOD(get_Interfaces)(THIS_ VARIANT *interfaces) PURE; \
    STDMETHOD(put_Interfaces)(THIS_ VARIANT interfaces) PURE; \
    STDMETHOD(get_InterfaceTypes)(THIS_ BSTR *types) PURE; \
    STDMETHOD(put_InterfaceTypes)(THIS_ BSTR types) PURE; \
    STDMETHOD(get_Enabled)(THIS_ VARIANT_BOOL *enabled) PURE; \
    STDMETHOD(put_Enabled)(THIS_ VARIANT_BOOL enabled) PURE; \
    STDMETHOD(get_Grouping)(THIS_ BSTR *context) PURE; \
    STDMETHOD(put_Grouping)(THIS_ BSTR context) PURE; \
    STDMETHOD(get_Profiles)(THIS_ LONG *profiles) PURE; \
    STDMETHOD(put_Profiles)(THIS_ LONG profiles) PURE; \
    STDMETHOD(get_EdgeTraversal)(THIS_ VARIANT_BOOL *enabled) PURE; \
    STDMETHOD(put_EdgeTraversal)(THIS_ VARIANT_BOOL enabled) PURE; \
    STDMETHOD(get_Action)(THIS_ NET_FW_ACTION *action) PURE; \
    STDMETHOD(put_Action)(THIS_ NET_FW_ACTION action) PURE;

#define NETFW_INETFWRULE2 NETFW_INETFWRULE \
    STDMETHOD(get_EdgeTraversalOptions)(THIS_ LONG *lOptions) PURE; \
    STDMETHOD(put_EdgeTraversalOptions)(THIS_ LONG lOptions) PURE;

#define NETFW_INETFWRULE3 NETFW_INETFWRULE2 \
    STDMETHOD(get_LocalAppPackageId)(THIS_ BSTR *wszPackageId) PURE; \
    STDMETHOD(put_LocalAppPackageId)(THIS_ BSTR wszPackageId) PURE; \
    STDMETHOD(get_LocalUserOwner)(THIS_ BSTR *wszUserOwner) PURE; \
    STDMETHOD(put_LocalUserOwner)(THIS_ BSTR wszUserOwner) PURE; \
    STDMETHOD(get_LocalUserAuthorizedList)(THIS_ BSTR *wszUserAuthList) PURE; \
    STDMETHOD(put_LocalUserAuthorizedList)(THIS_ BSTR wszUserAuthList) PURE; \
    STDMETHOD(get_RemoteUserAuthorizedList)(THIS_ BSTR *wszUserAuthList) PURE; \
    STDMETHOD(put_RemoteUserAuthorizedList)(THIS_ BSTR wszUserAuthList) PURE; \
    STDMETHOD(get_RemoteMachineAuthorizedList)(THIS_ BSTR *wszUserAuthList) PURE; \
    STDMETHOD(put_RemoteMachineAuthorizedList)(THIS_ BSTR wszUserAuthList) PURE; \
    STDMETHOD(get_SecureFlags)(THIS_ LONG *lOptions) PURE; \
    STDMETHOD(put_SecureFlags)(THIS_ LONG lOptions) PURE;

#define NETFW_INETFWRULES NETFW_IDISPATCH \
    STDMETHOD(get_Count)(THIS_ LONG *count) PURE; \
    STDMETHOD(Add)(THIS_ INetFwRule *rule) PURE; \
    STDMETHOD(Remove)(THIS_ BSTR name) PURE; \
    STDMETHOD(Item)(THIS_ BSTR name, INetFwRule **rule) PURE; \
    STDMETHOD(get__NewEnum)(THIS_ IUnknown **newEnum) PURE;

#define NETFW_INETFWSERVICERESTRICTION NETFW_IDISPATCH \
    STDMETHOD(RestrictService)(THIS_ BSTR serviceName, BSTR appName, VARIANT_BOOL restrictService, VARIANT_BOOL serviceSidRestricted) PURE; \
    STDMETHOD(ServiceRestricted)(THIS_ BSTR serviceName, BSTR appName, VARIANT_BOOL *serviceRestricted) PURE; \
    STDMETHOD(get_Rules)(THIS_ INetFwRules **rules) PURE;

#define NETFW_INETFWPOLICY2 NETFW_IDISPATCH \
    STDMETHOD(get_CurrentProfileTypes)(THIS_ LONG *profile) PURE; \
    STDMETHOD(get_FirewallEnabled)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT_BOOL *enabled) PURE; \
    STDMETHOD(put_FirewallEnabled)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT_BOOL enabled) PURE; \
    STDMETHOD(get_ExcludedInterfaces)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT *interfaces) PURE; \
    STDMETHOD(put_ExcludedInterfaces)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT interfaces) PURE; \
    STDMETHOD(get_BlockAllInboundTraffic)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT_BOOL *block) PURE; \
    STDMETHOD(put_BlockAllInboundTraffic)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT_BOOL block) PURE; \
    STDMETHOD(get_NotificationsDisabled)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT_BOOL *disabled) PURE; \
    STDMETHOD(put_NotificationsDisabled)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT_BOOL disabled) PURE; \
    STDMETHOD(get_UnicastResponsesToMulticastBroadcastDisabled)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT_BOOL *disabled) PURE; \
    STDMETHOD(put_UnicastResponsesToMulticastBroadcastDisabled)(THIS_ NET_FW_PROFILE_TYPE2 profileType, VARIANT_BOOL disabled) PURE; \
    STDMETHOD(get_Rules)(THIS_ INetFwRules **rules) PURE; \
    STDMETHOD(get_ServiceRestriction)(THIS_ INetFwServiceRestriction **ServiceRestriction) PURE; \
    STDMETHOD(EnableRuleGroup)(THIS_ LONG profileTypesBitmask, BSTR group, VARIANT_BOOL enable) PURE; \
    STDMETHOD(IsRuleGroupEnabled)(THIS_ LONG profileTypesBitmask, BSTR group, VARIANT_BOOL *enabled) PURE; \
    STDMETHOD(RestoreLocalFirewallDefaults)(THIS) PURE; \
    STDMETHOD(get_DefaultInboundAction)(THIS_ NET_FW_PROFILE_TYPE2 profileType, NET_FW_ACTION *action) PURE; \
    STDMETHOD(put_DefaultInboundAction)(THIS_ NET_FW_PROFILE_TYPE2 profileType, NET_FW_ACTION action) PURE; \
    STDMETHOD(get_DefaultOutboundAction)(THIS_ NET_FW_PROFILE_TYPE2 profileType, NET_FW_ACTION *action) PURE; \
    STDMETHOD(put_DefaultOutboundAction)(THIS_ NET_FW_PROFILE_TYPE2 profileType, NET_FW_ACTION action) PURE; \
    STDMETHOD(get_IsRuleGroupCurrentlyEnabled)(THIS_ BSTR group, VARIANT_BOOL *enabled) PURE; \
    STDMETHOD(get_LocalPolicyModifyState)(THIS_ NET_FW_MODIFY_STATE *modifyState) PURE;

#undef INTERFACE
#define INTERFACE INetFwRule
DECLARE_INTERFACE_(INetFwRule, IDispatch) { BEGIN_INTERFACE NETFW_INETFWRULE END_INTERFACE };
#undef INTERFACE
#define INTERFACE INetFwRule2
DECLARE_INTERFACE_(INetFwRule2, INetFwRule) { BEGIN_INTERFACE NETFW_INETFWRULE2 END_INTERFACE };
#undef INTERFACE
#define INTERFACE INetFwRule3
DECLARE_INTERFACE_(INetFwRule3, INetFwRule2) { BEGIN_INTERFACE NETFW_INETFWRULE3 END_INTERFACE };
#undef INTERFACE
#define INTERFACE INetFwRules
DECLARE_INTERFACE_(INetFwRules, IDispatch) { BEGIN_INTERFACE NETFW_INETFWRULES END_INTERFACE };
#undef INTERFACE
#define INTERFACE INetFwServiceRestriction
DECLARE_INTERFACE_(INetFwServiceRestriction, IDispatch) { BEGIN_INTERFACE NETFW_INETFWSERVICERESTRICTION END_INTERFACE };
#undef INTERFACE
#define INTERFACE INetFwPolicy2
DECLARE_INTERFACE_(INetFwPolicy2, IDispatch) { BEGIN_INTERFACE NETFW_INETFWPOLICY2 END_INTERFACE };
#undef INTERFACE
