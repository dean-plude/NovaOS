/*
 * wdi.h — the WDI (WLAN Device Driver Interface) messages a Wi-Fi miniport
 * and its upper edge exchange, as dot11wdi.h and wditypes.hpp define them
 * (kernel/nt/wdi.c is NovaOS's upper edge, the part of wdiwifi.sys that
 * drives the IHV driver)
 *
 * A WDI command is an NDIS method OID (OID_WDI_*, always NdisRequestMethod)
 * whose buffer is a WDI_MESSAGE_HEADER followed by TLVs; the IHV writes its
 * answer (header and TLVs) into the same buffer.  A task (OID_WDI_TASK_*)
 * finishes later with an NDIS status indication, NDIS_STATUS_WDI_INDICATION_*
 * carrying a header with the task's TransactionId and the result; other
 * indications (a scan's BSS entries) come unsolicited.  Every TLV is a
 * 16-bit type, a 16-bit length and the value, little-endian, with structure
 * fields packed at their own widths (a bool is one byte, an enum four).
 * The numbers are Microsoft's (the WDK headers and WABIModel.xml, the WDI
 * wire model); only what NovaOS sends, reads or its test miniport answers is
 * named.  Shared with tools/drivertest/wditest.c, so only C's own types
 * (after ndis.h).
 */
#pragma once

#define WDI_OID(id)        (0xE4400000u | (id))
#define WDI_INDICATION(id) (0x40050000u | (id))
#define WDI_IS_INDICATION(code) (((unsigned int)(code) & 0xFFFF0000u) == 0x40050000u)

/* message IDs */
#define WDI_TASK_OPEN                        1
#define WDI_TASK_CLOSE                       2
#define WDI_TASK_SCAN                        4
#define WDI_TASK_CONNECT                     6
#define WDI_TASK_DOT11_RESET                 7
#define WDI_TASK_DISCONNECT                  8
#define WDI_TASK_SET_RADIO_STATE             11
#define WDI_TASK_CREATE_PORT                 12
#define WDI_TASK_DELETE_PORT                 13
#define WDI_SET_PRIVACY_EXEMPTION_LIST       28
#define WDI_SET_ADD_CIPHER_KEYS              29
#define WDI_GET_ADAPTER_CAPABILITIES         37
#define WDI_INDICATION_DISCONNECT_COMPLETE   21
#define WDI_GET_BSS_ENTRY_LIST               47
#define WDI_INDICATION_SCAN_COMPLETE         60
#define WDI_INDICATION_BSS_ENTRY_LIST        62
#define WDI_INDICATION_CONNECT_COMPLETE      64
#define WDI_INDICATION_RADIO_STATUS          67
#define WDI_INDICATION_CREATE_PORT_COMPLETE  68
#define WDI_INDICATION_DELETE_PORT_COMPLETE  69
#define WDI_INDICATION_ASSOCIATION_RESULT    76
#define WDI_SET_ADAPTER_CONFIGURATION        85
#define WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED 134
#define WDI_SET_SAE_AUTH_PARAMS              135

#define OID_WDI_TASK_SCAN                    WDI_OID(WDI_TASK_SCAN)
#define OID_WDI_TASK_CREATE_PORT             WDI_OID(WDI_TASK_CREATE_PORT)
#define OID_WDI_TASK_DELETE_PORT             WDI_OID(WDI_TASK_DELETE_PORT)
#define OID_WDI_GET_ADAPTER_CAPABILITIES     WDI_OID(WDI_GET_ADAPTER_CAPABILITIES)
#define OID_WDI_SET_ADAPTER_CONFIGURATION    WDI_OID(WDI_SET_ADAPTER_CONFIGURATION)
#define OID_WDI_TASK_CONNECT                 WDI_OID(WDI_TASK_CONNECT)
#define OID_WDI_TASK_DISCONNECT              WDI_OID(WDI_TASK_DISCONNECT)
#define OID_WDI_SET_PRIVACY_EXEMPTION_LIST   WDI_OID(WDI_SET_PRIVACY_EXEMPTION_LIST)
#define OID_WDI_SET_ADD_CIPHER_KEYS          WDI_OID(WDI_SET_ADD_CIPHER_KEYS)
#define OID_WDI_SET_SAE_AUTH_PARAMS          WDI_OID(WDI_SET_SAE_AUTH_PARAMS)

#define NdisRequestMethod 12

#define WDI_PORT_ID_ADAPTER        0xFFFF   /* a command for the adapter, not a port */
#define WDI_TRANSACTION_ID_UNSOLICIT 0      /* an indication no task asked for */
#define WDI_OPERATION_MODE_STA     0x01

/* (the header has no message ID or length: those are the OID's or the
 * status code's, and the buffer's size) */
typedef struct {
    unsigned short      PortId;
    unsigned short      Reserved;
    NDIS_STATUS Status;
    unsigned int      TransactionId;
    unsigned int      IhvSpecificId;
} WDI_MESSAGE_HEADER;

typedef struct __attribute__((packed)) {
    unsigned short      Type, Length;
} WDI_TLV_HEADER;

/* TLV types */
#define WDI_TLV_BSSID                    0x02
#define WDI_TLV_SCAN_MODE                0x06
#define WDI_TLV_SCAN_DWELL_TIME          0x07
#define WDI_TLV_BSS_ENTRY                0x08
#define WDI_TLV_BSS_ENTRY_DEVICE_CONTEXT 0x0D
#define WDI_TLV_PROBE_RESPONSE_FRAME     0x09
#define WDI_TLV_BEACON_FRAME             0x0A
#define WDI_TLV_BSS_ENTRY_SIGNAL_INFO    0x0B
#define WDI_TLV_INTERFACE_CAPABILITIES   0x0F
#define WDI_TLV_BAND_CAPABILITIES        0x1A
#define WDI_TLV_INTERFACE_ATTRIBUTES     0x21
#define WDI_TLV_STATION_ATTRIBUTES       0x22
#define WDI_TLV_BAND_INFO                0x27
#define WDI_TLV_CREATE_PORT_PARAMETERS   0x28
#define WDI_TLV_PORT_ATTRIBUTES          0x29
#define WDI_TLV_DELETE_PORT_PARAMETERS   0x2A
#define WDI_TLV_ASSOCIATION_RESULT_PARAMETERS 0x2D
#define WDI_TLV_ASSOCIATION_REQUEST_FRAME 0x2E
#define WDI_TLV_CONNECT_PARAMETERS       0x33
#define WDI_TLV_CONNECT_BSS_ENTRY        0x34
#define WDI_TLV_ASSOCIATION_RESULT       0x35
#define WDI_TLV_DISCONNECT_PARAMETERS    0x36
#define WDI_TLV_BSS_ENTRY_CHANNEL_INFO   0x3A
#define WDI_TLV_SSID                     0x3B
#define WDI_TLV_AUTH_ALGO_LIST           0x3C
#define WDI_TLV_MULTICAST_CIPHER_ALGO_LIST 0x3D
#define WDI_TLV_UNICAST_CIPHER_ALGO_LIST 0x3E
#define WDI_TLV_CONNECTION_SETTINGS      0x3F
#define WDI_TLV_PRIVACY_EXEMPTION_ENTRY  0x48
#define WDI_TLV_PEER_MAC_ADDRESS         0x4C
#define WDI_TLV_CIPHER_KEY_ID            0x4D
#define WDI_TLV_CIPHER_KEY_TYPE_INFO     0x4E
#define WDI_TLV_CIPHER_KEY_RECEIVE_SEQUENCE_COUNT 0x4F
#define WDI_TLV_CIPHER_KEY_CCMP_KEY      0x50
#define WDI_TLV_CIPHER_KEY_BIP_KEY       0x51
#define WDI_TLV_SET_CIPHER_KEY_INFO      0x52
#define WDI_TLV_DATAPATH_ATTRIBUTES      0xB8
#define WDI_TLV_DATAPATH_CAPABILITIES    0xB9
#define WDI_TLV_BSS_ENTRY_AGE_INFO       0xBA
#define WDI_TLV_LINK_QUALITY_BAR_MAP     0xD8
#define WDI_TLV_PLDR_SUPPORT             0x11A
#define WDI_TLV_FIRMWARE_VERSION         0xF4
#define WDI_TLV_SAE_INDICATION_TYPE      0x14B
#define WDI_TLV_SAE_STATUS               0x14C
#define WDI_TLV_SAE_COMMIT_RESPONSE      0x14D
#define WDI_TLV_SAE_CONFIRM_RESPONSE     0x14E
#define WDI_TLV_SAE_REQUEST_TYPE         0x14F
#define WDI_TLV_SAE_COMMIT_REQUEST       0x150
#define WDI_TLV_SAE_CONFIRM_REQUEST      0x151
#define WDI_TLV_SAE_FINITE_CYCLIC_GROUP  0x152
#define WDI_TLV_SAE_SCALAR               0x153
#define WDI_TLV_SAE_ELEMENT              0x154
#define WDI_TLV_SAE_ANTI_CLOGGING_TOKEN  0x155
#define WDI_TLV_SAE_SEND_CONFIRM         0x156
#define WDI_TLV_SAE_CONFIRM              0x157

/* WDI_TLV_SCAN_MODE (WDI_SCAN_MODE) */
#define WDI_SCAN_TYPE_ACTIVE_ONLY   1
#define WDI_SCAN_TYPE_PASSIVE_ONLY  2
#define WDI_SCAN_TYPE_AUTO          3
#define WDI_SCAN_TRIGGER_MANUAL     1
typedef struct __attribute__((packed)) {
    unsigned char  ScanRepeatCount;
    unsigned int AllowedScanType;
    unsigned char  LiveUpdatesNeeded;
    unsigned int ScanTrigger;
} WDI_SCAN_MODE;

/* WDI_TLV_SCAN_DWELL_TIME (milliseconds) */
typedef struct __attribute__((packed)) {
    unsigned int ActiveChannelDwellTime, PassiveChannelDwellTime, MaximumScanTime;
} WDI_SCAN_DWELL_TIME;

/* WDI_TLV_BSS_ENTRY_SIGNAL_INFO */
typedef struct __attribute__((packed)) {
    int  RSSI;            /* dBm */
    unsigned int LinkQuality;     /* 0-100 */
} WDI_SIGNAL_INFO;

/* WDI_TLV_BSS_ENTRY_CHANNEL_INFO */
#define WDI_BAND_ID_2400 1
#define WDI_BAND_ID_5000 2
#define WDI_BAND_ID_6000 5
typedef struct __attribute__((packed)) {
    unsigned int ChannelNumber, BandId;
} WDI_BSS_ENTRY_CHANNEL_INFO;

/* WDI_TLV_BAND_CAPABILITIES */
typedef struct __attribute__((packed)) {
    unsigned int BandID;
    unsigned char  BandState;
} WDI_BAND_CAPABILITIES;

/* WDI_TLV_CREATE_PORT_PARAMETERS */
typedef struct __attribute__((packed)) {
    unsigned short OpModeMask;
    unsigned int NdisPortNumber;
} WDI_CREATE_PORT_PARAMETERS;

/* WDI_TLV_PORT_ATTRIBUTES (WDI_CREATE_PORT_COMPLETE_STRUCT) */
typedef struct __attribute__((packed)) {
    unsigned char  MacAddress[6];
    unsigned short PortNumber;
} WDI_PORT_ATTRIBUTES;

/* WDI_TLV_DELETE_PORT_PARAMETERS */
typedef struct __attribute__((packed)) {
    unsigned short PortNumber;
} WDI_DELETE_PORT_PARAMETERS;

/* WDI_TLV_INTERFACE_CAPABILITIES: its first fields, the same in every WDI
 * version (later versions add more booleans after them) */
typedef struct __attribute__((packed)) {
    unsigned int MTUSize, MaxMultiCastListSize;
    unsigned short BackFillSize;
    unsigned char  Address[6];
    unsigned int MaxTxRate, MaxRxRate;    /* kbit/s */
    unsigned char  HardwareRadioState, SoftwareRadioState;
} WDI_INTERFACE_CAPABILITIES;

/* WDI_TLV_DATAPATH_CAPABILITIES (in WDI_TLV_DATAPATH_ATTRIBUTES, among the
 * adapter's capabilities): what TalTxRxStart is configured with */
#define WDI_INTERCONNECT_MEMORY_MAPPED 0
#define WDI_INTERCONNECT_MESSAGE_BASED 1
typedef struct __attribute__((packed)) {
    unsigned int   InterconnectType;
    unsigned char  MaxNumPeers, TxTargetPriorityQueueing;
    unsigned short TxMaxScatterGatherElementsPerFrame;
    unsigned char  TxExplicitSendCompleteFlagRequired;
    unsigned short TxMinEffectiveFrameSize, TxFrameSizeGranularity;
    unsigned char  RxTxForwarding;
    unsigned int   RxMaxThroughput;
} WDI_DATAPATH_CAPABILITIES;

/* WDI_TLV_LINK_QUALITY_BAR_MAP (OID_WDI_SET_ADAPTER_CONFIGURATION): one entry per signal bar */
typedef struct __attribute__((packed)) {
    unsigned char StartRange, EndRange, BarNumber;
} WDI_LINK_QUALITY_BAR_MAP_ENTRY;

/* OID_WDI_TASK_CONNECT: WDI_TLV_CONNECT_PARAMETERS holds the settings,
 * the SSID, and the algorithms allowed; each WDI_TLV_CONNECT_BSS_ENTRY an
 * access point to try (its BSSID, beacon, signal and channel, as a scan
 * indicated them).  It ends with WDI_INDICATION_ASSOCIATION_RESULT (one
 * WDI_TLV_ASSOCIATION_RESULT per access point tried), then
 * WDI_INDICATION_CONNECT_COMPLETE. */
#define WDI_AUTH_ALGO_80211_OPEN  1
#define WDI_AUTH_ALGO_RSNA_PSK    7
#define WDI_AUTH_ALGO_WPA3_SAE    9
#define WDI_CIPHER_ALGO_NONE      0x00
#define WDI_CIPHER_ALGO_CCMP      0x04
#define WDI_CIPHER_ALGO_BIP       0x06      /* (BIP-CMAC-128: management frame protection's group key) */
typedef struct __attribute__((packed)) {    /* (WDI 1.0.1 and later) */
    unsigned char  RoamRequest, HiddenNetwork, ExcludeUnencrypted, MFPEnabled, HostFIPSModeEnabled;
    unsigned int   RoamNeededReason, RoamTrigger;
    unsigned char  BSSTransitionSupported;
} WDI_CONNECTION_SETTINGS;

#define WDI_ASSOC_STATUS_SUCCESS  0
#define WDI_ASSOC_STATUS_NO_AUTH_RESPONSE 41
#define WDI_ASSOC_STATUS_AUTH_FAILED_BY_PEER 44
typedef struct __attribute__((packed)) {    /* (its first fields: later ones vary by version) */
    unsigned int   AssociationStatus, StatusCode;
    unsigned char  ReAssociation;
    unsigned int   AuthAlgorithm, UnicastCipherAlgorithm, MulticastDataCipherAlgorithm, MulticastMgmtCipherAlgorithm;
    unsigned char  FourAddressSupported, PortAuthorized, WMMQoSEnabled;
} WDI_ASSOCIATION_RESULT_PARAMETERS;

/* OID_WDI_SET_PRIVACY_EXEMPTION_LIST: frames of an Ethertype (EAPOL's,
 * 0x888E) that may go and come unencrypted on a secured connection; one
 * WDI_TLV_PRIVACY_EXEMPTION_ENTRY each.  A frame's own exemption is in its
 * WDI_TX_METADATA. */
#define WDI_EXEMPT_NO_EXEMPTION                  0
#define WDI_EXEMPT_ALWAYS                        1
#define WDI_EXEMPT_ON_KEY_MAPPING_KEY_UNAVAILABLE 2   /* (unencrypted until there is a pairwise key) */
#define WDI_EXEMPT_PACKET_TYPE_UNICAST   1
#define WDI_EXEMPT_PACKET_TYPE_MULTICAST 2
#define WDI_EXEMPT_PACKET_TYPE_BOTH      3
typedef struct __attribute__((packed)) {
    unsigned short EtherType;       /* (big-endian) */
    unsigned int   ExemptionActionType, ExemptionPacketType;
} WDI_PRIVACY_EXEMPTION_PARAMETERS;

/* OID_WDI_SET_ADD_CIPHER_KEYS: one WDI_TLV_SET_CIPHER_KEY_INFO per key,
 * holding WDI_TLV_PEER_MAC_ADDRESS (a pairwise key's peer),
 * WDI_TLV_CIPHER_KEY_ID (a group key's index, a UINT32),
 * WDI_TLV_CIPHER_KEY_TYPE_INFO, WDI_TLV_CIPHER_KEY_RECEIVE_SEQUENCE_COUNT
 * (the 48-bit packet number to start from) and the key itself
 * (WDI_TLV_CIPHER_KEY_CCMP_KEY) */
#define WDI_CIPHER_KEY_DIRECTION_INBOUND  1
#define WDI_CIPHER_KEY_DIRECTION_OUTBOUND 2
#define WDI_CIPHER_KEY_DIRECTION_BOTH     3
#define WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY  1
#define WDI_CIPHER_KEY_TYPE_GROUP_KEY     2
#define WDI_CIPHER_KEY_TYPE_IGTK          3     /* (with WDI_TLV_CIPHER_KEY_BIP_KEY, key ID 4 or 5) */
typedef struct __attribute__((packed)) {
    unsigned int   CipherAlgorithm, Direction;
    unsigned char  Static;          /* (kept across a roam) */
    unsigned int   KeyType;
} WDI_CIPHER_KEY_TYPE_INFO;

/* WPA3-Personal's SAE (Microsoft's "WPA3-SAE authentication", WDI 1.1.8):
 * the driver sends and receives the authentication frames, the host makes
 * and checks what is in them.  During OID_WDI_TASK_CONNECT the driver asks
 * with NDIS_STATUS_WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED (WDI_TLV_BSSID,
 * WDI_TLV_SAE_INDICATION_TYPE, the peer's frame as
 * WDI_TLV_SAE_COMMIT_RESPONSE or WDI_TLV_SAE_CONFIRM_RESPONSE, or
 * WDI_TLV_SAE_STATUS) and the host answers with OID_WDI_SET_SAE_AUTH_PARAMS
 * (WDI_TLV_BSSID, WDI_TLV_SAE_REQUEST_TYPE and WDI_TLV_SAE_COMMIT_REQUEST:
 * group, scalar, element and the anti-clogging token if the access point
 * asked for one; or WDI_TLV_SAE_CONFIRM_REQUEST: send-confirm and confirm;
 * or WDI_TLV_SAE_STATUS).  The enums are four bytes, the group and the
 * send-confirm two. */
#define WDI_SAE_INDICATION_TYPE_COMMIT_REQUEST_PARAMS_NEEDED   0
#define WDI_SAE_INDICATION_TYPE_COMMIT_RESPONSE                1
#define WDI_SAE_INDICATION_TYPE_CONFIRM_RESPONSE               2
#define WDI_SAE_INDICATION_TYPE_ERROR                          3
#define WDI_SAE_INDICATION_TYPE_CONFIRM_REQUEST_RESEND_REQUEST 4
#define WDI_SAE_REQUEST_TYPE_COMMIT_REQUEST  0
#define WDI_SAE_REQUEST_TYPE_CONFIRM_REQUEST 1
#define WDI_SAE_REQUEST_TYPE_FAILURE         2
#define WDI_SAE_REQUEST_TYPE_SUCCESS         3
#define WDI_SAE_STATUS_SUCCESS                                 0
#define WDI_SAE_STATUS_FAILURE                                 1
#define WDI_SAE_STATUS_INTERNAL_AUTH_FAILURE                   3
#define WDI_SAE_STATUS_COMMIT_MESSAGE_MALFORMED_AUTH_FRAME     11
#define WDI_SAE_STATUS_COMMIT_MESSAGE_FAILURE_FROM_PEER        12
#define WDI_SAE_STATUS_COMMIT_MESSAGE_UNSUPPORTED_FINITE_GROUP 14
#define WDI_SAE_STATUS_COMMIT_MESSAGE_INVALID_SCALAR_ELEMENT_SIZE 16
#define WDI_SAE_STATUS_COMMIT_MESSAGE_REFLECTION_ATTACK_DETECTED 17
#define WDI_SAE_STATUS_CONFIRM_MESSAGE_RECEIVED_IN_INVALID_STATE 30
#define WDI_SAE_STATUS_CONFIRM_MESSAGE_MALFORMED_AUTH_FRAME    31
#define WDI_SAE_STATUS_CONFIRM_MESSAGE_VERIFICATION_FAILED     35

/* WDI_TLV_DISCONNECT_PARAMETERS */
typedef struct __attribute__((packed)) {
    unsigned char  MacAddress[6];
    unsigned short Disassociation80211Reason;
} WDI_DISCONNECT_PARAMETERS;

/* -----------------------------------------------------------------------
 * The data path (dot11wdi.h's TAL TX/RX interface).  It is not WDI
 * messages but calls: the upper edge gives the IHV driver's
 * MiniportWdiTalTxRxInitialize its NDIS_WDI_DATA_API (the TxMgr and RxMgr:
 * indications), and gets back the driver's handlers (its TxEngine,
 * RxEngine and TAL), with the upper edge's data path handle as
 * NdisMiniportDataPathHandle in every indication.  A frame is an NBL of
 * one 802.11 MSDU (MAC header, LLC/SNAP, payload; the driver encrypts and
 * acknowledges); its WDI_FRAME_METADATA is at NBL MiniportReserved[0],
 * allocated by the upper edge for TX and by the driver
 * (AllocateWiFiFrameMetaData) for RX.
 *
 * TX: TxDataSendHandler says frames are queued for a port and peer; the
 * driver takes them with TxDequeueIndication (the upper edge calls
 * TxTargetDescInitHandler on each first), and gives them back with
 * TxTransferCompleteIndication (and TxSendCompleteIndication by frame ID
 * when the metadata asked for it).  RX: the driver says frames are ready
 * with RxInorderDataIndication; the upper edge takes them with
 * RxGetMpdusHandler and returns them with RxReturnFramesHandler.  Peers
 * (the access point, once associated) come with PeerCreateIndication.
 * ----------------------------------------------------------------------- */
typedef unsigned short WDI_PEER_ID;
typedef struct { unsigned char Address[6]; } WDI_MAC_ADDRESS;   /* (passed by value: by reference in the x64 ABI) */
typedef unsigned short WDI_FRAME_ID;
#define WDI_PEER_ANY        0xFFFF
#define WDI_EXT_TID_NON_QOS 16
#define WDI_EXT_TID_UNKNOWN 0x1F

#define WDI_TX_FRAME_STATUS_OK       0
#define WDI_RX_INDICATION_DISPATCH_GENERAL 0
#define WDI_FRAME_MSDU               0

typedef struct {
    unsigned int   InterconnectType;
    unsigned char  TargetPriorityQueueing;
    unsigned short MaxScatterGatherElementsPerFrame;
    unsigned char  ExplicitSendCompleteFlagRequired, Pad;
    unsigned short MinEffectiveSize, FrameSizeGranularity, Pad2;
    unsigned char  RxTxForwarding;          /* (ReceiveCapabilities) */
    unsigned int   MaxThroughput;
} WDI_TXRX_CAPABILITIES;

typedef struct {
    WDI_TXRX_CAPABILITIES TxRxCapabilities;  /* (WDI_TXRX_PARAMETERS) */
    unsigned char  MaxNumPorts, MaxNumPeers;
} WDI_TXRX_TARGET_CONFIGURATION;

typedef struct {
    unsigned short MaxOutstandingTransfers;
} TAL_TXRX_PARAMETERS;

typedef struct {
    unsigned short PortID;
    WDI_PEER_ID    PeerID;
    unsigned char  ExTID, IsUnicast, bAllowLegacyRates;
    unsigned short Ethertype;
    unsigned char  bTxCompleteRequired, PnLength;
    unsigned short TxCost;
    unsigned int   ExemptionAction;
    unsigned long long MpduPn[2];
    unsigned long long ReplayIHVReserved0, ReplayIHVReserved1;
    unsigned short SeqCtl, wPad;
} WDI_TX_METADATA;

typedef struct {
    void          *Flink, *Blink;           /* (LIST_ENTRY Linkage: the upper edge's) */
    NET_BUFFER_LIST *pNBL;
    WDI_FRAME_ID   FrameID;
    unsigned short wPad;
    unsigned int   dPad;
    union {
        WDI_TX_METADATA txMetaData;
        unsigned int    rxPayloadType;      /* (WDI_RX_METADATA) */
    } u;
} WDI_FRAME_METADATA;

#define WDI_FRAME_METADATA_OF(nbl) ((WDI_FRAME_METADATA *)(nbl)->MiniportReserved[0])

/* The upper edge's indications (NDIS_WDI_DATA_API, header type 0xC2,
 * revision 1; byte-packed as dot11wdi.h declares it) */
#define NDIS_OBJECT_TYPE_WDI_DATA_API              0xC2
#define NDIS_OBJECT_TYPE_MINIPORT_WDI_DATA_HANDLERS 0xC1
typedef struct __attribute__((packed)) {
    NDIS_OBJECT_HEADER Header;
    void *TxDequeueIndication, *TxTransferCompleteIndication, *TxSendCompleteIndication, *TxQueryRATIDState,
         *TxSendPauseIndication, *TxSendRestartIndication, *TxReleaseFrameIndication, *TxInjectFrameIndication,
         *TxAbortConfirm, *RxInorderDataIndication, *RxStopConfirm, *RxFlushConfirm, *PeerCreateIndication,
         *PeerDeleteIndication, *AllocateWiFiFrameMetaData, *FreeWiFiFrameMetaData,
         *TxQuerySuspectFrameCompleteStatus;
} NDIS_WDI_DATA_API;

/* The driver's (NDIS_MINIPORT_WDI_DATA_HANDLERS, byte-packed) */
typedef struct __attribute__((packed)) {
    NDIS_OBJECT_HEADER Header;
    void *TxAbortHandler, *TxTargetDescInitHandler, *TxTargetDescDeInitHandler, *TxDataSendHandler,
         *TxTalSendHandler, *TxTalSendCompleteHandler, *TxTalQueueInOrderHandler, *TxPeerBacklogHandler,
         *RxStopHandler, *RxFlushHandler, *RxRestartHandler, *RxGetMpdusHandler, *RxReturnFramesHandler,
         *RxResumeHandler, *RxThrottleHandler, *RxPpduRssiHandler, *TalTxRxStartHandler, *TalTxRxStopHandler,
         *TalTxRxAddPortHandler, *TalTxRxDeletePortHandler, *TalTxRxSetPortOpModeHandler,
         *TalTxRxResetPortHandler, *TalTxRxPeerConfigHandler, *TalTxRxPeerDeleteConfirmHandler,
         *TxSuspectFrameAbortHandler;
} NDIS_MINIPORT_WDI_DATA_HANDLERS;

_Static_assert(sizeof(WDI_TXRX_CAPABILITIES) == 24 && sizeof(WDI_TXRX_TARGET_CONFIGURATION) == 28 &&
               sizeof(WDI_TX_METADATA) == 64 && sizeof(WDI_FRAME_METADATA) == 96 &&
               sizeof(NDIS_WDI_DATA_API) == 4 + 17 * 8 && sizeof(NDIS_MINIPORT_WDI_DATA_HANDLERS) == 4 + 25 * 8 &&
               sizeof(WDI_DATAPATH_CAPABILITIES) == 18 && sizeof(WDI_CONNECTION_SETTINGS) == 14 &&
               sizeof(WDI_CIPHER_KEY_TYPE_INFO) == 13 && sizeof(WDI_PRIVACY_EXEMPTION_PARAMETERS) == 10,
               "WDI data path types");

/* 802.11 data frames as the data path carries them (IEEE 802.11-2020 9.3.2.1) */
#define DOT11_FC0_DATA      0x08        /* type data, subtype data */
#define DOT11_FC0_QOS_DATA  0x88
#define DOT11_FC1_TO_DS     0x01
#define DOT11_FC1_FROM_DS   0x02
#define DOT11_FC1_PROTECTED 0x40
#define DOT11_DATA_HEADER   24
typedef struct __attribute__((packed)) {
    unsigned char  FrameControl[2];
    unsigned short Duration;
    unsigned char  Address1[6], Address2[6], Address3[6];
    unsigned short SequenceControl;
} DOT11_DATA_HEADER_T;
/* LLC/SNAP before the Ethertype (RFC 1042) */
#define DOT11_SNAP_LEN 8
#define DOT11_CCMP_HEADER 8             /* (the PN and key ID a protected frame has before its LLC) */
#define ETHERTYPE_EAPOL 0x888E
