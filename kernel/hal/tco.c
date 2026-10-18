/*
 * tco.c — the ICH9 family's TCO watchdog timer (see tco.h)
 *
 * Written from Intel's ICH9 datasheet: the LPC bridge
 * (bus 0, device 31, function 0) holds the ACPI I/O base (PMBASE, config
 * 0x40) and the root complex register block's address (RCBA, config
 * 0xF0).  The TCO registers are I/O ports at PMBASE + 0x60; the chipset
 * reboots on the timer's second timeout unless the NO_REBOOT bit of the
 * General Control and Status register (RCBA + 0x3410) is set, which it is
 * after a reset on QEMU's ICH9 and on many boards.
 */

#include "tco.h"
#include "pci.h"
#include "../arch/x86_64/cpu.h"
#include "../ke/printf.h"

#define LPC_PMBASE      0x40
#define LPC_ACPI_CNTL   0x44
#define  ACPI_EN        0x80
#define LPC_RCBA        0xF0
#define  RCBA_EN        0x01
#define RCBA_DEFAULT    0xFED1C000u     /* where firmware usually puts it */
#define RCBA_SIZE       0x4000
#define RCBA_GCS        0x3410
#define  GCS_NO_REBOOT  (1u << 5)

#define PM_SMI_EN       0x30            /* (PMBASE + 0x30) */
#define  SMI_TCO_EN     (1u << 13)

/* TCO I/O registers, from PMBASE + 0x60 */
#define TCO_OFFSET      0x60
#define TCO_RLD         0x00            /* any write reloads the timer */
#define TCO1_STS        0x04
#define  TCO_TIMEOUT    (1u << 3)
#define TCO2_STS        0x06
#define  SECOND_TO_STS  (1u << 1)
#define  BOOT_STS       (1u << 2)
#define TCO1_CNT        0x08
#define  TCO_TMR_HLT    (1u << 11)
#define TCO_TMR         0x12            /* the initial count, 0.6 s ticks (10 bits) */
#define  TMR_MASK       0x3FF
#define  TMR_MIN        4               /* (smaller values are not allowed) */

static const UINT16 g_lpc_ids[] = {
    0x2910, 0x2912, 0x2914, 0x2916, 0x2917, 0x2918, 0x2919,    /* ICH9 (0x2918: QEMU's q35) */
    0x3A14, 0x3A16, 0x3A18, 0x3A1A,                            /* ICH10 */
};

static UINT16 g_tco;                    /* the TCO registers' I/O base (0: not found yet) */
static bool   g_running;

/* The TCO registers, with the chipset's reboot on a second timeout on */
static bool find(void)
{
    if (g_tco) return true;
    PciDevice d;
    if (!PciFind(0x8086, g_lpc_ids, (int)(sizeof(g_lpc_ids) / sizeof(g_lpc_ids[0])), &d)) return false;
    if (!(PciRead32(d.bus, d.dev, d.func, LPC_ACPI_CNTL) & ACPI_EN)) {
        kprintf("[TCO] The chipset's ACPI registers are switched off: no watchdog\n");
        return false;
    }
    UINT16 pmbase = (UINT16)(PciRead32(d.bus, d.dev, d.func, LPC_PMBASE) & 0xFF80);
    if (!pmbase) return false;
    UINT32 rcba = PciRead32(d.bus, d.dev, d.func, LPC_RCBA);
    if (!(rcba & RCBA_EN)) {                                   /* (OVMF leaves it off) */
        PciWrite32(d.bus, d.dev, d.func, LPC_RCBA, RCBA_DEFAULT | RCBA_EN);
        rcba = PciRead32(d.bus, d.dev, d.func, LPC_RCBA);
    }
    volatile UINT32 *gcs = NULL;
    if (rcba & RCBA_EN) {
        volatile UINT8 *rcrb = (volatile UINT8 *)PciMapPhysical(rcba & ~0x3FFFu, RCBA_SIZE);
        if (rcrb) gcs = (volatile UINT32 *)(rcrb + RCBA_GCS);
    }
    if (!gcs) {
        kprintf("[TCO] The chipset's root complex registers are not mapped: no watchdog\n");
        return false;
    }
    if (*gcs & GCS_NO_REBOOT) *gcs &= ~GCS_NO_REBOOT;
    if (*gcs & GCS_NO_REBOOT) {
        kprintf("[TCO] The firmware locked the watchdog's reset off: no watchdog\n");
        return false;
    }
    /* no SMI at the first timeout: the firmware's SMI handler could reload
     * the timer behind the kernel's back (Linux's iTCO driver does the same) */
    outl(pmbase + PM_SMI_EN, inl(pmbase + PM_SMI_EN) & ~SMI_TCO_EN);
    g_tco = (UINT16)(pmbase + TCO_OFFSET);
    return true;
}

bool TcoStart(UINT32 seconds)
{
    if (!find()) return false;
    UINT32 ticks = (seconds * 10 + 11) / 12;                   /* two timeouts of 0.6 s ticks each */
    if (ticks < TMR_MIN) ticks = TMR_MIN;
    if (ticks > TMR_MASK) ticks = TMR_MASK;
    outw(g_tco + TCO1_CNT, (UINT16)(inw(g_tco + TCO1_CNT) | TCO_TMR_HLT));
    outw(g_tco + TCO_TMR, (UINT16)((inw(g_tco + TCO_TMR) & ~TMR_MASK) | ticks));
    outw(g_tco + TCO1_STS, TCO_TIMEOUT);                       /* (write 1 to clear) */
    outw(g_tco + TCO2_STS, SECOND_TO_STS | BOOT_STS);
    outw(g_tco + TCO_RLD, 1);
    outw(g_tco + TCO1_CNT, (UINT16)(inw(g_tco + TCO1_CNT) & ~TCO_TMR_HLT));
    if (inw(g_tco + TCO1_CNT) & TCO_TMR_HLT) {
        kprintf("[TCO] The watchdog would not start (locked by the firmware)\n");
        return false;
    }
    g_running = true;
    kprintf("[TCO] Watchdog running at I/O %x: the PC resets %u.%u s after the last reload\n",
            g_tco, ticks * 12 / 10, ticks * 12 % 10);
    return true;
}

bool TcoRunning(void) { return g_running; }

void TcoReload(void)
{
    if (!g_running) return;
    outw(g_tco + TCO1_STS, TCO_TIMEOUT);                       /* (a first timeout no longer counts) */
    outw(g_tco + TCO_RLD, 1);
}

void TcoStop(void)
{
    if (!g_running) return;
    outw(g_tco + TCO1_CNT, (UINT16)(inw(g_tco + TCO1_CNT) | TCO_TMR_HLT));
    g_running = false;
    kprintf("[TCO] Watchdog stopped\n");
}
