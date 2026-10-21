/*
 * wfp.h — NovaOS's network filtering engine (the Windows Filtering
 * Platform's shape) and the firewall above it
 *
 * The network stack asks WfpClassify at each filtering layer
 * (userland/include/novafw.h's NOVAWFP_L_*): the TCP/UDP packets that
 * arrive and leave (transport), and the application layer enforcement
 * (ALE) points of a socket's life: connect, listen, and a connection or
 * datagram arriving for it.  Programs add filters through fwpuclnt.dll;
 * the firewall (firewall.c: rules, the Domain/Private/Public profiles and
 * the network the adapter is on) decides as one more sublayer, Windows'
 * MPSSVC_WF.  Everything here runs under the network lock.
 */
#pragma once

#include "../include/types.h"
#include "../../userland/include/novafw.h"

/* What a classification is about (the incoming values) */
typedef struct {
    int         layer;              /* NOVAWFP_L_* */
    UINT8       protocol;           /* 6 TCP, 17 UDP */
    bool        v6;
    UINT8       local[16], remote[16];   /* network byte order (IPv4: the first 4 bytes) */
    UINT16      lport, rport;       /* host byte order */
    const char *app;                /* the socket's application id ("": the system) */
    UINT32      flags;              /* NOVAWFP_FLAG_* */
    UINT32      if_index;
    UINT64      if_luid;
} WfpValues;

/* true: the traffic may pass */
bool WfpClassify(const WfpValues *v);
/* true when a filter exists at @layer (the transport layers skip the work otherwise) */
bool WfpLayerUsed(int layer);
/* NtNovaSockCtl op 22: @req is the kernel's copy of the request (novafw.h);
 * returns 0 or an HRESULT.  Takes the network lock itself. */
UINT32 WfpCtl(UINT32 pid, bool admin, UINT32 op, UINT32 session, void *req);
void   WfpProcessGone(UINT32 pid);           /* its engine sessions close */
void   WfpRestore(void);                     /* persistent filters, once the registry is loaded */

/* Application ids: a program path made the engine's way (lower case, no
 * drive or volume, environment variables of Windows' firewall rules
 * expanded); the sockets keep a small number for theirs */
void   WfpAppNormalize(const char *path, char *out, int cap);
UINT16 WfpAppIntern(const char *path);       /* 0: none */
const char *WfpAppName(UINT16 id);           /* "" for 0 */
void   WfpNewKeyText(char *out, int cap);    /* a new random GUID as "{...}" */

/* ---------------------------------------------------------------------------
 * The firewall (firewall.c)
 * ------------------------------------------------------------------------- */
/* Its decision at an ALE layer: -1 none (no rule applies: another
 * sublayer, or the layer's default, decides), 0 block, 1 permit */
int    FwDecide(const WfpValues *v);
/* NtNovaSockCtl op 23 (not under the network lock; takes it as needed) */
UINT32 FwCtl(bool admin, UINT32 op, UINT32 profile, void *req, INT64 *result);
void   FwRestore(void);                      /* the policy from the registry (at boot) */
/* The desktop thread, about once a second: which network the adapter is
 * on (a new one is Public, and asks), and the policy's registry changes */
void   FwPoll(void);
UINT32 FwCurrentProfiles(void);              /* NOVAFW_* */
/* Settings' pages and the prompt: the network, and choosing its category */
bool   FwGetNetwork(NovaFwNetwork *n);
bool   FwSetCategory(int category);          /* 0 public, 1 private */
bool   FwPromptPending(void);
void   FwPromptAnswered(void);
