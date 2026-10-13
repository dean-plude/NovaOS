/*
 * fwcfg.c — QEMU's firmware configuration files (fwcfg.h)
 */
#include "fwcfg.h"
#include "../arch/x86_64/cpu.h"
#include "../lib/string.h"

/* QEMU's firmware configuration device (I/O ports 0x510 and 0x511): its
 * file @name into @buf, at most @cap bytes; the count, or -1.  The
 * self-tests hand NovaOS their test key (fs/update.c) and a PCI stand-in
 * (hal/pci.c) this way; only the virtual
 * machine's host can set it, and the ports are only touched when CPUID
 * says this is QEMU (TCG or KVM). */
int FwCfgFile(const char *name, char *buf, int cap)
{
    if (!(cpuid(1, 0).ecx & (1u << 31))) return -1;      /* (no hypervisor) */
    CpuidResult hv = cpuid(0x40000000, 0);
    UINT32 vendor[3] = { hv.ebx, hv.ecx, hv.edx };
    if (memcmp(vendor, "TCGTCGTCGTCG", 12) && memcmp(vendor, "KVMKVMKVM\0\0\0", 12)) return -1;
    UINT8 b[64];
    outw(0x510, 0x0000);                                 /* the signature */
    for (int i = 0; i < 4; i++) b[i] = inb(0x511);
    if (memcmp(b, "QEMU", 4)) return -1;
    outw(0x510, 0x0019);                                 /* the file directory */
    for (int i = 0; i < 4; i++) b[i] = inb(0x511);
    UINT32 count = (UINT32)b[0] << 24 | (UINT32)b[1] << 16 | (UINT32)b[2] << 8 | b[3];
    for (UINT32 f = 0; f < count && f < 1024; f++) {
        for (int i = 0; i < 64; i++) b[i] = inb(0x511);  /* size, selector, reserved (big-endian), name */
        b[63] = '\0';
        if (strcmp((const char *)b + 8, name)) continue;
        UINT32 size = (UINT32)b[0] << 24 | (UINT32)b[1] << 16 | (UINT32)b[2] << 8 | b[3];
        int n = size < (UINT32)cap ? (int)size : cap;
        outw(0x510, (UINT16)(b[4] << 8 | b[5]));
        for (int i = 0; i < n; i++) buf[i] = (char)inb(0x511);
        return n;
    }
    return -1;
}
