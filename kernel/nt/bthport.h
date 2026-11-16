/*
 * bthport.h — NovaOS's Bluetooth host (bthport.c, the role Windows'
 * bthport.sys plays), as its transport (bthusb.c) and its profile drivers
 * (hidbth.c, bthaudio.c) reach it
 */
#pragma once
#include "wdm.h"

typedef struct BthRadio BthRadio;

/* What a transport gives the host: an HCI command packet (opcode,
 * length, parameters) and an ACL data packet (handle and flags, length,
 * data) to send to the controller; and for voice, the number of SCO links
 * of 16-bit samples to carry (0: none: a USB radio's interface 1 back at
 * its setting without bandwidth), and an SCO data packet (handle, length,
 * samples) to send */
typedef struct {
    NT_STATUS (*send_cmd)(void *ctx, const UINT8 *pkt, UINT32 len);
    NT_STATUS (*send_acl)(void *ctx, const UINT8 *pkt, UINT32 len);
    NT_STATUS (*sco_links)(void *ctx, int links);
    NT_STATUS (*send_sco)(void *ctx, const UINT8 *pkt, UINT32 len);
} BthTransport;

/* A radio on @fdo (its transport's device object, which is also the bus
 * its services' nodes are children of; @pdo: the transport's own PDO,
 * which the radio's device interface is registered on).  The controller
 * is brought up on a thread of its own, after the transport's reads run. */
BthRadio *BthRadioCreate(NT_DEVICE_OBJECT *fdo, NT_DEVICE_OBJECT *pdo, const BthTransport *t, void *ctx);
void      BthRadioStart(BthRadio *r);
/* From the transport: a whole HCI event (code, length, parameters), a
 * whole ACL packet (header and data) */
void      BthRadioEvent(BthRadio *r, const UINT8 *evt, UINT32 len);
void      BthRadioAcl(BthRadio *r, const UINT8 *pkt, UINT32 len);
void      BthRadioSco(BthRadio *r, const UINT8 *pkt, UINT32 len);
/* The transport is gone (@surprise) or stopping: the radio stops using it */
void      BthRadioStop(BthRadio *r);
void      BthRadioFree(BthRadio *r);
/* IRP_MJ_DEVICE_CONTROL on the radio (its device interface) */
NT_STATUS BthRadioIoctl(BthRadio *r, NT_IRP *irp, NT_IO_STACK_LOCATION *sl, NT_ULONG_PTR *info);
/* IRP_MN_QUERY_DEVICE_RELATIONS BusRelations: the services' nodes */
NT_STATUS BthRadioRelations(BthRadio *r, NT_ULONG_PTR *info);

/* A service's node (a PDO the radio made): its IRPs */
bool      BthIsServicePdo(NT_DEVICE_OBJECT *d);
NT_STATUS BthServiceDispatch(NT_DEVICE_OBJECT *pdo, NT_IRP *irp);

/* ---- for HID's driver (hidbth.c), on a HID service's node ---- */

typedef void (*BthHidInput)(void *ctx, const UINT8 *data, UINT32 len);

typedef struct {
    UINT16 vid, pid, version;          /* (the device's PnP Information record; 0 without one) */
    UINT8  country;
    const UINT8 *rdesc;                /* the report descriptor (the HID record's HIDDescriptorList) */
    UINT16 rdesc_len;
    char   name[64];
} BthHidInfo;

bool      BthHidGetInfo(NT_DEVICE_OBJECT *pdo, BthHidInfo *out);
/* Connect: the ACL link (paging the device), authentication with its link
 * key, encryption, then L2CAP's HID control (PSM 0x11) and interrupt
 * (0x13) channels; @input gets each frame from the interrupt channel
 * (the HIDP header first) */
NT_STATUS BthHidConnect(NT_DEVICE_OBJECT *pdo, BthHidInput input, void *ctx);
/* A frame on the control (@intr false) or interrupt channel */
NT_STATUS BthHidSend(NT_DEVICE_OBJECT *pdo, bool intr, const UINT8 *data, UINT32 len);
void      BthHidDisconnect(NT_DEVICE_OBJECT *pdo);
bool      BthHidConnected(NT_DEVICE_OBJECT *pdo);
/* Once after the device connected to us by itself: true (it should open
 * its channels within a few seconds; if not, the driver opens them) */
bool      BthHidReconnectSoon(NT_DEVICE_OBJECT *pdo);

/* ---- for the audio profiles (bthaudio.c): a paired device's links ---- */

typedef struct BthDev BthDev;
typedef struct BthChan BthChan;
typedef struct BthAudio BthAudio;
typedef void (*BthRx)(void *ctx, const UINT8 *data, UINT32 len);

/* The device's ACL link up, authenticated with its link key and encrypted */
NT_STATUS BthDevLink(BthRadio *r, BthDev *d);
bool      BthDevUp(BthRadio *r, BthDev *d);
/* An L2CAP channel to @psm (@rx gets each frame from it, on the transport's
 * thread: don't wait there); NULL if it did not open */
BthChan  *BthChanOpen(BthRadio *r, BthDev *d, UINT16 psm, BthRx rx, void *ctx);
NT_STATUS BthChanSend(BthRadio *r, BthChan *c, const UINT8 *data, UINT32 len);
UINT16    BthChanMtu(const BthChan *c);
bool      BthChanIsOpen(const BthChan *c);
void      BthChanClose(BthRadio *r, BthChan *c);
/* A voice (SCO or eSCO) link to the device, CVSD on the air, 16-bit 8 kHz
 * mono samples to and from the host; @rx gets each SCO packet's samples */
NT_STATUS BthScoOpen(BthRadio *r, BthDev *d, BthRx rx, void *ctx);
NT_STATUS BthScoSend(BthRadio *r, BthDev *d, const UINT8 *pcm, UINT32 len);
bool      BthScoIsOpen(const BthDev *d);
void      BthScoClose(BthRadio *r, BthDev *d);

/* bthaudio.c: a headset's Hands-Free (@hfp, its RFCOMM channel) and Audio
 * Sink (@a2dp) services switched on: it connects them, and NovaOS's mixer
 * gets its outputs and its microphone (drivers/audio.h) while it is
 * connected.  Stop waits until it has let go of the device. */
BthAudio *BthAudioStart(BthRadio *r, BthDev *d, const char *name, bool hfp, UINT8 channel, bool a2dp);
void      BthAudioStop(BthAudio *a);
/* The headset connected to us by itself: its profiles now, not at the next retry */
void      BthAudioKick(BthAudio *a);

/* ---- for Settings' Bluetooth page (kernel/apps/settings_bluetooth.c) ---- */

typedef struct {
    char name[64];
    char addr[18];                     /* (XX:XX:XX:XX:XX:XX) */
    bool discoverable;
} BthRadioView;

typedef struct {
    UINT64 addr;
    char   name[64];                   /* ("" until its name is known) */
    UINT32 cod;                        /* its class of device */
    bool   paired, connected;
    bool   hid, audio;                 /* its HID service on; its headset profiles running */
} BthDevView;

/* The first radio that is up (false: none) */
bool BthViewRadio(BthRadioView *out);
/* Its devices: the paired ones, then those the last inquiry found */
int  BthViewDevices(BthDevView *out, int cap);
/* One job at a time in the background: an inquiry (BTH_JOB_SCAN), pairing
 * a device and switching its services on (BTH_JOB_PAIR), or forgetting it
 * (BTH_JOB_REMOVE).  Done: its status, and an inquiry's count */
enum { BTH_JOB_SCAN = 1, BTH_JOB_PAIR, BTH_JOB_REMOVE };
bool BthJobStart(int job, UINT64 addr);
bool BthJobDone(NT_STATUS *status, UINT32 *count);

NTSTATUS NT_API BthUsbDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg);
NTSTATUS NT_API HidBthDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg);
