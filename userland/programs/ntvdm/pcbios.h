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
 * (vdm_text, vdm_cursor) in a console.  It may also give DOS a drive C:
 * of its files (vdm_drive_c, redir.h) and a program to run there
 * (vdm_run_program): the BIOS then ends when that program does. */
#ifndef NOVA_PCBIOS_H
#define NOVA_PCBIOS_H

#include "cpu86.h"
#include "redir.h"

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
    VdmRedir redir;                                  /* drive C: */
} Vdm;

/* VDM_ENDED: the program vdm_run_program gave ended (or did not start) */
enum { VDM_RAN, VDM_WAIT, VDM_IDLE, VDM_ENDED };

#define VDM_REDIR_AT 0xE800                          /* (ROM offsets: the INT 2Fh handler, an IFS header for C:) */
#define VDM_IFS_AT 0xE810

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
/* drive C: is @fs, DOS's current directory there @cwd (a host path under
 * it, "" for its root); call after vdm_init.  REDIR.COM on the boot disk
 * (AUTOEXEC.BAT runs it) gives DOS the drive */
void vdm_drive_c(Vdm *v, const VdmFs *fs, const char *cwd);
/* REDIR.COM is to run @program (a host path on drive C:) with @args, as
 * COMMAND.COM would, and the machine ends when it does (VDM_ENDED;
 * v->redir.exit_code is its exit code, exec_error EXEC's error if it
 * did not start); 0 if DOS has no name for it */
int vdm_run_program(Vdm *v, const char *program, const char *args);
/* sets or clears flag @f in the FLAGS an interrupt handler returns with */
void vdm_iret_flag(Vdm *v, uint16_t f, int on);
/* the text screen: @cols x @rows cells of character, attribute */
const uint8_t *vdm_text(Vdm *v, int *cols, int *rows);
void vdm_cursor(Vdm *v, int *x, int *y, int *visible);

#endif
