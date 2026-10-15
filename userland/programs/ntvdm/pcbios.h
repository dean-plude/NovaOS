/* pcbios.h — the PC around the real-mode CPU (cpu86.h): memory, the
 * interrupt controller, timer, CMOS clock and keyboard ports, and a BIOS
 * written in C (text-mode video INT 10h, equipment and memory INT 11h/12h,
 * floppy disk INT 13h on a disk image held in memory, serial INT 14h,
 * system services INT 15h, keyboard INT 16h, printer INT 17h, bootstrap
 * INT 19h and the clock INT 1Ah).  The BIOS's ROM is a page of stubs
 * "BOP nn; IRET" (cpu86.h) that the interrupt vectors point at.
 *
 * It knows nothing of the system it runs on: the host (ntvdm.c on NovaOS,
 * tools/msdos/vdm_test.c on Linux) gives it the time, takes its serial
 * output, feeds keys in with vdm_key and draws the text screen
 * (vdm_text, vdm_cursor) in a console. */
#ifndef NOVA_PCBIOS_H
#define NOVA_PCBIOS_H

#include "cpu86.h"

typedef struct VdmHost {
    void *ctx;
    uint64_t (*ms)(void *ctx);                       /* a millisecond clock */
    void (*localtime)(void *ctx, int t[6]);          /* year month day hour minute second */
    void (*serial)(void *ctx, uint8_t ch);           /* what DOS sends to COM1 (AUX) */
    void (*tty)(void *ctx, uint8_t ch);              /* optional: what DOS writes to the screen (INT 10h AH=0Eh) */
} VdmHost;

typedef struct Vdm {
    Cpu86 cpu;
    VdmHost host;
    uint8_t *disk;                                   /* drive A: */
    uint32_t disk_size;
    int cyls, heads, spt, drive_type;
    uint8_t pic_mask, pic_isr, pic_irr, cmos_index, port61, crtc_index;
    uint16_t pit_count;
    double next_tick;
    int wait;                                        /* VDM_WAIT / VDM_IDLE asked by the BIOS */
    int polls;                                       /* INT 16h status calls with no key */
} Vdm;

enum { VDM_RAN, VDM_WAIT, VDM_IDLE };

/* a machine with 640 KiB, a colour text screen and @disk (a floppy image:
 * 160 KB to 2.88 MB) as drive A:, about to boot from it; 0 if the image
 * is not one */
int vdm_init(Vdm *v, uint8_t *mem, uint8_t *disk, uint32_t size, const VdmHost *host);
/* runs up to @n instructions.  VDM_WAIT: DOS waits for a key (or HLT
 * waits for an interrupt), so the host can sleep until a key comes or the
 * next timer tick (vdm_next_tick); VDM_IDLE: DOS keeps asking for a key
 * that is not there (sleep a little) */
int vdm_run(Vdm *v, uint32_t n);
double vdm_next_tick(Vdm *v);
/* a key into the BIOS's keyboard buffer: scan code << 8 | character; 0
 * when the buffer is full (15 keys) */
int vdm_key(Vdm *v, uint16_t key);
/* the key that types @ch (code page 437) on a US keyboard, as vdm_key
 * takes it */
uint16_t vdm_char_key(uint8_t ch);
/* the shift state the BIOS reports (INT 16h AH=02h) */
void vdm_shift_state(Vdm *v, uint8_t flags);
/* the text screen: @cols x @rows cells of character, attribute */
const uint8_t *vdm_text(Vdm *v, int *cols, int *rows);
void vdm_cursor(Vdm *v, int *x, int *y, int *visible);

#endif
