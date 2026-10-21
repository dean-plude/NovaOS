/*
 * novafw.h — what NovaOS's network security (the filtering engine,
 * kernel/net/wfp.c, and the firewall, kernel/net/firewall.c, through
 * kernel/um/um_socket.c) and the user-mode API surface (fwpuclnt.dll,
 * hnetcfg.dll) say to each other
 *
 * On Windows fwpuclnt.dll talks to the Base Filtering Engine service by
 * RPC and hnetcfg.dll to the Windows Defender Firewall service (MpsSvc).
 * NovaOS keeps both in its kernel, beside the network stack they filter,
 * and the DLLs ask through NtNovaSockCtl with no handle:
 *
 *   op 22  the filtering engine: arg = NOVAWFP_* | session << 16, out a
 *          request of the sub-op's type (below); returns 0 or an HRESULT
 *          (FWP_E_*, E_ACCESSDENIED) as an unsigned 32-bit number;
 *   op 23  the firewall: arg = NOVAFWC_* | profile << 16, out as below;
 *          returns 0 or an HRESULT, or (NOVAFWC_CURRENT) the profiles in
 *          force.
 *
 * Everything here is plain C (no windows.h): the kernel includes it too.
 */
#pragma once

#define NOVAFW_WFP 22
#define NOVAFW_FW  23

typedef struct { unsigned char b[16]; } NovaGuid;   /* a GUID, as it lies in memory */

/* ---------------------------------------------------------------------------
 * The filtering engine (the Windows Filtering Platform's shape)
 * ------------------------------------------------------------------------- */
/* Layers (fwpuclnt.dll maps FWPM_LAYER_* GUIDs to these) */
enum {
    NOVAWFP_L_IN_TRANSPORT_V4, NOVAWFP_L_IN_TRANSPORT_V6,     /* every TCP/UDP packet that arrives */
    NOVAWFP_L_OUT_TRANSPORT_V4, NOVAWFP_L_OUT_TRANSPORT_V6,   /* ... and that leaves by the adapter */
    NOVAWFP_L_CONNECT_V4, NOVAWFP_L_CONNECT_V6,               /* ALE_AUTH_CONNECT: connect, a UDP send to a new peer */
    NOVAWFP_L_RECV_ACCEPT_V4, NOVAWFP_L_RECV_ACCEPT_V6,       /* ALE_AUTH_RECV_ACCEPT: a SYN to a listener, a datagram from a new peer */
    NOVAWFP_L_LISTEN_V4, NOVAWFP_L_LISTEN_V6,                 /* ALE_AUTH_LISTEN: listen() */
    NOVAWFP_LAYERS
};

/* Condition fields (FWPM_CONDITION_*) */
enum {
    NOVAWFP_F_PROTOCOL = 1, NOVAWFP_F_LOCAL_ADDR, NOVAWFP_F_REMOTE_ADDR, NOVAWFP_F_LOCAL_PORT,
    NOVAWFP_F_REMOTE_PORT, NOVAWFP_F_APP_ID, NOVAWFP_F_FLAGS, NOVAWFP_F_IF_INDEX, NOVAWFP_F_IF_LUID,
};
/* A condition's value */
enum { NOVAWFP_V_NUM, NOVAWFP_V_RANGE, NOVAWFP_V_ADDR4, NOVAWFP_V_ADDR6, NOVAWFP_V_APP };

/* Windows' numbers, which the DLLs pass through unchanged */
#define NOVAWFP_ACTION_BLOCK   0x1001u          /* FWP_ACTION_BLOCK */
#define NOVAWFP_ACTION_PERMIT  0x1002u          /* FWP_ACTION_PERMIT */
#define NOVAWFP_FILTER_PERSISTENT        0x01u  /* FWPM_FILTER_FLAG_* */
#define NOVAWFP_FILTER_CLEAR_ACTION_RIGHT 0x08u
#define NOVAWFP_FILTER_DISABLED          0x20u
#define NOVAWFP_SESSION_DYNAMIC          0x01u  /* FWPM_SESSION_FLAG_DYNAMIC */
#define NOVAWFP_FLAG_IS_LOOPBACK         0x01u  /* FWP_CONDITION_FLAG_IS_LOOPBACK */
#define NOVAWFP_MATCH_EQUAL 0                   /* FWP_MATCH_*: EQUAL ... NOT_EQUAL */
#define NOVAWFP_MATCH_GREATER 1
#define NOVAWFP_MATCH_LESS 2
#define NOVAWFP_MATCH_GREATER_OR_EQUAL 3
#define NOVAWFP_MATCH_LESS_OR_EQUAL 4
#define NOVAWFP_MATCH_RANGE 5
#define NOVAWFP_MATCH_FLAGS_ALL_SET 6
#define NOVAWFP_MATCH_FLAGS_ANY_SET 7
#define NOVAWFP_MATCH_FLAGS_NONE_SET 8
#define NOVAWFP_MATCH_EQUAL_CASE_INSENSITIVE 9
#define NOVAWFP_MATCH_NOT_EQUAL 10

#define NOVAWFP_APP_MAX 160
typedef struct {
    unsigned short     field;           /* NOVAWFP_F_* */
    unsigned char      match;           /* NOVAWFP_MATCH_* */
    unsigned char      kind;            /* NOVAWFP_V_* */
    unsigned char      prefix;          /* an address's prefix length */
    unsigned char      pad[3];
    unsigned long long lo, hi;          /* a number; a range lo..hi */
    unsigned char      addr[16];        /* network byte order (IPv4: the first 4) */
    char               app[NOVAWFP_APP_MAX];   /* an application id: its path, lower case, no volume ("\programs\x.exe") */
} NovaWfpCond;

#define NOVAWFP_MAX_COND 8
#define NOVAWFP_NAME_MAX 64
typedef struct {
    unsigned long long id;              /* FILTER_ADD: out; GET_ID: in; AT: in, the index */
    NovaGuid           key, sublayer, provider;
    unsigned int       has_provider;
    unsigned int       layer;           /* NOVAWFP_L_* */
    unsigned int       action;          /* NOVAWFP_ACTION_* */
    unsigned int       flags;           /* NOVAWFP_FILTER_* */
    unsigned int       weight_type;     /* FWP_EMPTY 0 (automatic), FWP_UINT8 1 (0-15), FWP_UINT64 4 */
    unsigned int       ncond;
    unsigned long long weight;          /* in: as given; out: the effective weight */
    char               name[NOVAWFP_NAME_MAX], desc[96];
    NovaWfpCond        cond[NOVAWFP_MAX_COND];
} NovaWfpFilter;

typedef struct {
    NovaGuid       key, provider;
    unsigned int   has_provider, flags;   /* FWPM_SUBLAYER_FLAG_PERSISTENT 1 */
    unsigned int   weight;                /* 0-0xFFFF */
    unsigned int   index;                 /* SUBLAYER_AT: in */
    char           name[NOVAWFP_NAME_MAX];
} NovaWfpSublayer;

typedef struct {
    NovaGuid       key;
    unsigned int   flags;                 /* FWPM_PROVIDER_FLAG_PERSISTENT 1 */
    unsigned int   index;
    char           name[NOVAWFP_NAME_MAX];
} NovaWfpProvider;

/* Sub-ops of op 22 (out: the request named) */
enum {
    NOVAWFP_OPEN = 1,             /* unsigned int: in the session flags, out the session (1-63) */
    NOVAWFP_CLOSE,                /* (the session's dynamic objects go with it) */
    NOVAWFP_FILTER_ADD,           /* NovaWfpFilter */
    NOVAWFP_FILTER_DELETE_ID,     /* unsigned long long */
    NOVAWFP_FILTER_DELETE_KEY,    /* NovaGuid */
    NOVAWFP_FILTER_GET_ID,        /* NovaWfpFilter (id in) */
    NOVAWFP_FILTER_GET_KEY,       /* NovaWfpFilter (key in) */
    NOVAWFP_FILTER_AT,            /* NovaWfpFilter (id = index in); FWP_E_NOT_FOUND past the last */
    NOVAWFP_SUBLAYER_ADD,         /* NovaWfpSublayer */
    NOVAWFP_SUBLAYER_DELETE,      /* NovaGuid */
    NOVAWFP_SUBLAYER_GET,         /* NovaWfpSublayer (key in) */
    NOVAWFP_SUBLAYER_AT,          /* NovaWfpSublayer (index in) */
    NOVAWFP_PROVIDER_ADD,         /* NovaWfpProvider */
    NOVAWFP_PROVIDER_DELETE,      /* NovaGuid */
    NOVAWFP_PROVIDER_GET,         /* NovaWfpProvider (key in) */
    NOVAWFP_PROVIDER_AT,          /* NovaWfpProvider (index in) */
    NOVAWFP_TXN_BEGIN,
    NOVAWFP_TXN_COMMIT,
    NOVAWFP_TXN_ABORT,
};

/* HRESULTs the engine answers with (winerror.h) */
#define NOVAWFP_E_CALLOUT_NOT_FOUND   0x80320001u
#define NOVAWFP_E_FILTER_NOT_FOUND    0x80320003u
#define NOVAWFP_E_LAYER_NOT_FOUND     0x80320004u
#define NOVAWFP_E_PROVIDER_NOT_FOUND  0x80320005u
#define NOVAWFP_E_SUBLAYER_NOT_FOUND  0x80320007u
#define NOVAWFP_E_NOT_FOUND           0x80320008u
#define NOVAWFP_E_ALREADY_EXISTS      0x80320009u
#define NOVAWFP_E_IN_USE              0x8032000Au
#define NOVAWFP_E_NO_TXN_IN_PROGRESS  0x8032000Du
#define NOVAWFP_E_TXN_IN_PROGRESS     0x8032000Eu
#define NOVAWFP_E_INVALID_ACTION_TYPE 0x80320024u
#define NOVAWFP_E_INVALID_WEIGHT      0x80320025u
#define NOVAWFP_E_INVALID_PARAMETER   0x80320035u
#define NOVAWFP_E_OUT_OF_BOUNDS       0x80320028u
#define NOVAWFP_E_ACCESS_DENIED       0x80070005u    /* E_ACCESSDENIED: changing it needs an administrator */
#define NOVAWFP_E_OUTOFMEMORY         0x8007000Eu

/* ---------------------------------------------------------------------------
 * The firewall (Windows Defender Firewall's shape)
 * ------------------------------------------------------------------------- */
#define NOVAFW_DOMAIN  1u                 /* NET_FW_PROFILE2_* */
#define NOVAFW_PRIVATE 2u
#define NOVAFW_PUBLIC  4u
#define NOVAFW_ALL     0x7FFFFFFFu

typedef struct {
    unsigned int profile;                 /* one NOVAFW_* (in) */
    int enabled;                          /* EnableFirewall */
    int block_all;                        /* DoNotAllowExceptions: every inbound connection, allowed apps' too */
    int notify_off;                       /* DisableNotifications */
    int unicast_off;                      /* DisableUnicastResponsesToMulticastBroadcast */
    int default_in, default_out;          /* NET_FW_ACTION: 0 block, 1 allow */
} NovaFwProfile;

#define NOVAFW_TEXT 128
typedef struct {
    int  index;                           /* RULE_AT: in */
    char id[48];                          /* the FirewallRules value's name ("{GUID}") */
    char name[NOVAFW_TEXT], desc[NOVAFW_TEXT], group[NOVAFW_TEXT];
    char app[260], service[64];           /* "" any */
    int  enabled;
    int  dir;                             /* NET_FW_RULE_DIRECTION: 1 in, 2 out */
    int  action;                          /* NET_FW_ACTION: 0 block, 1 allow */
    int  protocol;                        /* 6 TCP, 17 UDP, 1 ICMPv4, 58 ICMPv6, 256 any */
    unsigned int profiles;                /* NOVAFW_* bits (NOVAFW_ALL) */
    int  edge;                            /* EdgeTraversal */
    char lports[NOVAFW_TEXT], rports[NOVAFW_TEXT];       /* "80,443,8000-8080" ("" or "*": any) */
    char laddrs[NOVAFW_TEXT], raddrs[NOVAFW_TEXT];       /* "10.0.0.0/255.0.0.0,LocalSubnet,fd00::/8" */
} NovaFwRule;

typedef struct {
    char guid[40];                        /* its NetworkList profile key ("{...}"), "" none yet */
    char name[64];                        /* ProfileName ("Network", "Network 2") */
    int  category;                        /* 0 public, 1 private, 2 domain (NLM_NETWORK_CATEGORY) */
    int  connected;                       /* the adapter has an address on it now */
    int  prompt;                          /* the "make this PC discoverable?" question is waiting */
    unsigned char gw_mac[6];
} NovaFwNetwork;

/* Sub-ops of op 23 (profile in arg's bits 16-30 where named) */
enum {
    NOVAFWC_CURRENT = 1,          /* returns the NOVAFW_* profiles in force */
    NOVAFWC_PROFILE_GET,          /* NovaFwProfile (profile in) */
    NOVAFWC_PROFILE_SET,          /* NovaFwProfile */
    NOVAFWC_RULE_COUNT,           /* returns how many */
    NOVAFWC_RULE_AT,              /* NovaFwRule (index in) */
    NOVAFWC_RULE_GET,             /* NovaFwRule (id in) */
    NOVAFWC_RULE_ADD,             /* NovaFwRule (id out) */
    NOVAFWC_RULE_SET,             /* NovaFwRule (id in): replaces it */
    NOVAFWC_RULE_DELETE,          /* NovaFwRule (id in) */
    NOVAFWC_NETWORK_GET,          /* NovaFwNetwork */
    NOVAFWC_NETWORK_SET,          /* NovaFwNetwork: category (and name) of the network the adapter is on */
    NOVAFWC_RESTORE_DEFAULTS,     /* no rules, every profile on, inbound blocked */
};
