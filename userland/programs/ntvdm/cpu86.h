/* cpu86.h — a real-mode x86 CPU (8086 with the 80186 additions) for the
 * NovaOS DOS host (ntvdm.exe, roadmap Phase 31).  Long mode has no
 * virtual-8086 mode, so DOS runs on this interpreter inside a normal
 * NovaOS process.  It is NovaOS's own code (MIT, like the rest of the
 * tree) and knows nothing of Windows: the machine around it (memory, I/O
 * ports, the BIOS) is pcbios.c, and tools/msdos/vdm_test.py builds both
 * for the host to boot MS-DOS outside NovaOS. */
#ifndef NOVA_CPU86_H
#define NOVA_CPU86_H

#include <stdint.h>

#define CPU86_MEM_SIZE 0x110000          /* 1 MiB and the 64 KiB past it (A20 off: wraps) */

enum { CPU86_ES, CPU86_CS, CPU86_SS, CPU86_DS };
enum { CPU86_AX, CPU86_CX, CPU86_DX, CPU86_BX, CPU86_SP, CPU86_BP, CPU86_SI, CPU86_DI };

#define CPU86_CF 0x0001
#define CPU86_PF 0x0004
#define CPU86_AF 0x0010
#define CPU86_ZF 0x0040
#define CPU86_SF 0x0080
#define CPU86_TF 0x0100
#define CPU86_IF 0x0200
#define CPU86_DF 0x0400
#define CPU86_OF 0x0800

typedef struct Cpu86 Cpu86;

struct Cpu86 {
    union { uint16_t w[8]; uint8_t b[16]; } r;     /* AX CX DX BX SP BP SI DI */
    uint16_t sreg[4];                              /* ES CS SS DS */
    uint16_t ip, flags;
    uint8_t *mem;                                  /* CPU86_MEM_SIZE bytes */

    /* the machine: I/O ports and the BIOS.  bop(nn) runs for the
     * instruction C4 C4 nn ("BIOS operation", as NTVDM called it: LES with
     * a register operand, which no program uses), the BIOS's way into C */
    void *machine;
    uint8_t (*in8)(void *machine, uint16_t port);
    void (*out8)(void *machine, uint16_t port, uint8_t v);
    void (*bop)(void *machine, uint8_t n);
    /* a hardware interrupt the machine wants delivered (-1: none); the
     * CPU calls ack() when it takes it, between instructions with IF set */
    int (*irq)(void *machine);
    void (*ack)(void *machine, int vector);

    int halted;                                    /* HLT until an interrupt */
    uint64_t count;                                /* instructions run */

    /* decoding the current instruction */
    int seg_ovr, rep, mod, reg, rm, inhibit;
    uint16_t ea_seg, ea_off, start_ip;
};

void cpu86_reset(Cpu86 *c);
/* runs up to @n instructions; returns early when halted */
void cpu86_run(Cpu86 *c, uint32_t n);
/* software interrupt @vector now (as INT n would) */
void cpu86_interrupt(Cpu86 *c, uint8_t vector);

static inline uint32_t cpu86_lin(uint16_t seg, uint16_t off) { return (((uint32_t)seg << 4) + off) & 0xFFFFF; }
uint8_t cpu86_rd8(Cpu86 *c, uint16_t seg, uint16_t off);
uint16_t cpu86_rd16(Cpu86 *c, uint16_t seg, uint16_t off);
void cpu86_wr8(Cpu86 *c, uint16_t seg, uint16_t off, uint8_t v);
void cpu86_wr16(Cpu86 *c, uint16_t seg, uint16_t off, uint16_t v);
void cpu86_push(Cpu86 *c, uint16_t v);
uint16_t cpu86_pop(Cpu86 *c);

#endif
