/*
 * pnp.h — the Plug and Play device tree
 *
 * Every device NovaOS knows is a device node here, as Windows' PnP
 * manager keeps them: the root ("HTREE\ROOT\0"), the PCI functions found
 * at boot, each USB controller's root hub, and the USB devices plugged in
 * (at boot or later) with a child node for each function a NovaOS driver
 * took (a keyboard, a mouse, a disk...).  A node has a Windows device
 * instance ID, hardware and compatible IDs, a setup class, a description
 * and the NovaOS driver running it (its "service").
 *
 * A device that leaves keeps its node, marked not present (Windows'
 * "phantom" devices): the same device on the same port comes back to the
 * same node and instance ID.  Each arrival and removal is an event with a
 * sequence number; programs read the nodes and wait for the events
 * through NtNovaGuiCtl's CTL_PNP (kernel/um/um_gui.c), which cfgmgr32,
 * setupapi, user32 (WM_DEVICECHANGE) and the PlugPlay service use.
 *
 * Callers: the PCI scan (PnpInit), PciClaim (the driver of a function),
 * and the USB core on its "usb" thread (usb.c).  Every function here may
 * be called from any thread with interrupts on; none of them sleeps but
 * PnpWaitEvents.
 */

#pragma once

#include "../include/types.h"

#define PNP_ROOT          1         /* the root node's ID */

/* PnpNode.flags */
#define PNP_PRESENT       0x01      /* plugged in (clear: a phantom) */
#define PNP_STARTED       0x02      /* a NovaOS driver runs it */
#define PNP_REMOVABLE     0x04      /* can be unplugged (USB) */
#define PNP_WINDOWS       0x08      /* the driver running it is a Windows driver (kernel/nt/pnp.c) */
#define PNP_INBOX         0x10      /* its NovaOS driver steps aside for an installed Windows driver */
#define PNP_RAW           0x20      /* started raw: its service is its bus driver's, not one installed for it */
#define PNP_DELETED       0x40      /* uninstalled (DIF_REMOVE): not even a phantom; programs no longer see it */

/* What a program gets (NovaPnpNode in userland/include/novapnp.h).  The
 * ID lists are several NUL-terminated strings ended by an empty one
 * (REG_MULTI_SZ). */
typedef struct {
    UINT32 id;                      /* 1 (the root) and up */
    UINT32 parent;                  /* 0 for the root */
    UINT32 flags;                   /* PNP_* */
    UINT32 problem;                 /* CM_PROB_*: 0, or 28 (no driver) */
    UINT32 seq;                     /* the event that last brought it or took it */
    UINT32 reserved;
    char   instance[128];           /* "USB\VID_0627&PID_0001\6&1&0&3" */
    char   hwids[160];
    char   compat[160];
    char   cls[32];                 /* setup class: "USB", "HIDClass", "Keyboard"... */
    char   service[32];             /* the NovaOS driver ("usbhid"), or "" */
    char   desc[96];                /* "USB Input Device" */
    char   mfg[48];                 /* "(Standard USB Host Controller)" */
    char   location[64];            /* "Port_#0003.Hub_#0001" */
    char   friendly[96];            /* the device's own name (a USB product string), or "" */
} PnpNode;

/* An event (NovaPnpEvent) */
#define PNP_ARRIVAL       1
#define PNP_REMOVAL       2
#define PNP_QUERY_REMOVE  3
#define PNP_QUERY_REMOVE_FAILED 4
typedef struct {
    UINT32 seq;                     /* from 1, one more for each event */
    UINT32 kind;                    /* PNP_ARRIVAL or PNP_REMOVAL */
    UINT32 id;                      /* the node */
    UINT32 reserved;
} PnpEvent;

/* The root and the PCI functions (after PciInitialize, before the drivers
 * claim functions) */
void PnpInit(void);

/* Add a node under @parent, or bring back the phantom with the same
 * instance ID (its fields are refreshed from @n); an arrival event either
 * way.  Returns the node's ID, or 0 (no room). */
UINT32 PnpAdd(UINT32 parent, const PnpNode *n);
/* The node and everything under it left (a removal event for each, the
 * deepest first); they stay as phantoms */
void PnpRemove(UINT32 id);
/* Record a PnP notification event for an existing node. */
void PnpNotify(UINT32 kind, UINT32 id);
/* Remove node @id and delete it (PNP_DELETED), as uninstalling a device
 * does: a later PnpAdd of its instance brings it back */
void PnpDelete(UINT32 id);
/* A driver took the node: its service, and started */
void PnpSetDriver(UINT32 id, const char *service);
/* The same, for a Windows driver (PNP_WINDOWS) */
void PnpSetWindowsDriver(UINT32 id, const char *service);
/* Started raw (PNP_RAW), its bus driver @service named as its driver */
void PnpSetRawDriver(UINT32 id, const char *service);
/* Its setup class and friendly name, as its driver's install wrote them
 * (each left as it was when NULL or "") */
void PnpSetNames(UINT32 id, const char *cls, const char *friendly);
/* The node's driver stopped, or did not start: not started, with
 * @problem (CM_PROB_*: 10 failed to start, 22 disabled...) */
void PnpSetProblem(UINT32 id, UINT32 problem);
/* Whether the node's NovaOS driver would step aside for an installed
 * Windows driver (PNP_INBOX) */
void PnpSetInbox(UINT32 id, bool on);
/* The node of PCI function bus:dev.func, or 0 */
UINT32 PnpPciNode(UINT8 bus, UINT8 dev, UINT8 func);

/* A resource a node's device decodes (NovaPnpResource): what Device
 * Manager's Resources tab lists, as CM_Get_First_Log_Conf reads it */
#define PNP_RES_MEMORY    1
#define PNP_RES_PORT      2
#define PNP_RES_INTERRUPT 3
typedef struct {
    UINT32 type;                    /* PNP_RES_* */
    UINT32 flags;                   /* memory: 1 prefetchable; interrupt: 1 level-triggered */
    UINT64 start, length;           /* an interrupt: its GSI (IRQ), length 0 */
} PnpResource;
/* Node @id's resources (a PCI function's ranges and interrupt; none for
 * other nodes): how many, at most @max */
int PnpResources(UINT32 id, PnpResource *out, int max);

/* A copy of node @id; false when there is none */
bool PnpGet(UINT32 id, PnpNode *out);
/* The highest node ID given out (IDs are never reused) */
UINT32 PnpCount(void);
/* The events after @after (at most @max into @out), waiting up to @ticks
 * for one; returns how many, *@newest the newest event's seq (when
 * @after is older than the events kept, the oldest kept come first and a
 * reader should list the nodes again) */
int PnpWaitEvents(UINT32 after, PnpEvent *out, int max, UINT64 ticks, UINT32 *newest);
