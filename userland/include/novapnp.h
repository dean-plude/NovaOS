/*
 * novapnp.h — the Plug and Play device tree as NovaOS's kernel keeps it
 *
 * NtNovaGuiCtl(0, NOVA_CTL_PNP, op | id << 8, ptr) reads the device nodes
 * of kernel/io/pnp.h (whose PnpNode and PnpEvent these mirror) and waits
 * for devices to arrive and leave: cfgmgr32, setupapi, user32
 * (WM_DEVICECHANGE), the PlugPlay service and the self-test pnptest use
 * it.  A node's ID is its DEVINST; the root is 1.
 */
#pragma once
#include <windows.h>
#include <winternl.h>

#define NOVA_CTL_PNP 56

#define NOVA_PNP_ROOT       1
#define NOVA_PNP_PRESENT    0x01
#define NOVA_PNP_STARTED    0x02
#define NOVA_PNP_REMOVABLE  0x04

typedef struct {
    DWORD id, parent, flags, problem, seq, reserved;
    char  instance[128];
    char  hwids[160];               /* REG_MULTI_SZ */
    char  compat[160];              /* REG_MULTI_SZ */
    char  cls[32];                  /* setup class name */
    char  service[32];              /* the NovaOS driver, or "" */
    char  desc[96];
    char  mfg[48];
    char  location[64];
    char  friendly[96];             /* the device's own name, or "" */
} NovaPnpNode;

#define NOVA_PNP_ARRIVAL 1
#define NOVA_PNP_REMOVAL 2
typedef struct { DWORD seq, kind, id, reserved; } NovaPnpEvent;

typedef struct {
    DWORD after, wait_ms, max;      /* in */
    DWORD newest;                   /* out */
    NovaPnpEvent e[32];
} NovaPnpRead;

/* The highest node ID */
static inline DWORD nova_pnp_count(void)
{
    return (DWORD)NtNovaGuiCtl(0, NOVA_CTL_PNP, 0, NULL);
}

static inline BOOL nova_pnp_node(DWORD id, NovaPnpNode *n)
{
    return NtNovaGuiCtl(0, NOVA_CTL_PNP, 1 | (ULONG_PTR)id << 8, n) != 0;
}

/* The events after r->after (r->max of them at most), waiting up to
 * r->wait_ms (1 s at most) for one; returns how many, r->newest the
 * newest's seq */
static inline int nova_pnp_events(NovaPnpRead *r)
{
    return (int)NtNovaGuiCtl(0, NOVA_CTL_PNP, 2, r);
}
