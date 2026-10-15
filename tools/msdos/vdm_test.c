/* vdm_test.c — boots a DOS floppy image on NovaOS's real-mode CPU and
 * BIOS (userland/programs/ntvdm/cpu86.c, pcbios.c) on the build machine,
 * outside NovaOS, and drives it from the command line:
 *
 *     vdm_test IMAGE [wait=TEXT | type=TEXT]... [--timeout SECONDS]
 *
 * wait=TEXT runs DOS until it writes TEXT to the screen (through the
 * BIOS, after what the last wait matched); type=TEXT types it ("\r" is
 * Enter).  At the end, or when a wait times out, it prints the screen;
 * the exit status is 0 when every wait matched.  vdm_test.py builds and
 * runs it (CI's MS-DOS job), so the CPU and BIOS are tested on every
 * change without booting NovaOS. */
#include "cpu86.h"
#include "pcbios.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t host_ms(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void host_localtime(void *ctx, int t[6])
{
    (void)ctx;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    t[0] = tm.tm_year + 1900; t[1] = tm.tm_mon + 1; t[2] = tm.tm_mday;
    t[3] = tm.tm_hour; t[4] = tm.tm_min; t[5] = tm.tm_sec;
}

static void host_serial(void *ctx, uint8_t ch)
{
    (void)ctx;
    fputc(ch, stderr);
}

static char *said;                              /* everything DOS wrote to the screen */
static size_t said_len, said_cap;

static void host_tty(void *ctx, uint8_t ch)
{
    (void)ctx;
    if (said_len + 2 > said_cap) {
        said_cap = said_cap ? said_cap * 2 : 65536;
        said = realloc(said, said_cap);
        if (!said) exit(2);
    }
    said[said_len++] = ch >= 32 && ch < 127 ? (char)ch : ch == '\n' ? '\n' : ' ';
    said[said_len] = 0;
}

static char screen[25 * 81 + 1];

static void read_screen(Vdm *v)
{
    int cols, rows;
    const uint8_t *t = vdm_text(v, &cols, &rows);
    char *p = screen;
    for (int y = 0; y < rows && y < 25; y++) {
        for (int x = 0; x < cols && x < 80; x++) {
            uint8_t ch = t[(y * cols + x) * 2];
            *p++ = ch >= 32 && ch < 127 ? (char)ch : ' ';
        }
        *p++ = '\n';
    }
    *p = 0;
}

int main(int argc, char **argv)
{
    int timeout = 120;
    if (argc < 2) {
        fprintf(stderr, "usage: vdm_test IMAGE [wait=TEXT | type=TEXT]... [--timeout SECONDS]\n");
        return 2;
    }
    for (int i = 2; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--timeout")) timeout = atoi(argv[i + 1]);
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static uint8_t disk[2949120];
    uint32_t size = (uint32_t)fread(disk, 1, sizeof(disk), f);
    fclose(f);
    uint8_t *mem = calloc(1, CPU86_MEM_SIZE);
    static Vdm vdm;
    VdmHost host = { NULL, host_ms, host_localtime, host_serial, host_tty };
    if (!mem || !vdm_init(&vdm, mem, disk, size, &host)) {
        fprintf(stderr, "vdm_test: %s is not a floppy image (%u bytes)\n", argv[1], size);
        return 2;
    }
    uint64_t deadline = host_ms(NULL) + (uint64_t)timeout * 1000;
    int ok = 1;
    size_t from = 0;
    for (int i = 2; i < argc && ok; i++) {
        if (!strcmp(argv[i], "--timeout")) { i++; continue; }
        if (!strncmp(argv[i], "type=", 5)) {
            for (const char *s = argv[i] + 5; *s && ok; s++) {
                uint16_t k = s[0] == '\\' && s[1] == 'r' ? (s++, vdm_char_key('\r')) : vdm_char_key((uint8_t)*s);
                while (!vdm_key(&vdm, k)) {         /* (as fast as DOS takes them) */
                    vdm_run(&vdm, 20000);
                    if (host_ms(NULL) > deadline) { printf("vdm_test: DOS takes no keys\n"); ok = 0; break; }
                }
            }
            continue;
        }
        if (strncmp(argv[i], "wait=", 5)) { fprintf(stderr, "vdm_test: what is %s?\n", argv[i]); return 2; }
        const char *want = argv[i] + 5;
        for (;;) {
            int r = vdm_run(&vdm, 200000);
            const char *hit = said ? strstr(said + from, want) : NULL;
            if (hit) {
                from = (size_t)(hit - said) + strlen(want);
                printf("vdm_test: \"%s\" after %llu instructions\n", want, (unsigned long long)vdm.cpu.count);
                break;
            }
            if (host_ms(NULL) > deadline) {
                printf("vdm_test: no \"%s\" on the screen in %d s (%llu instructions, CS:IP %04X:%04X)\n",
                       want, timeout, (unsigned long long)vdm.cpu.count, vdm.cpu.sreg[CPU86_CS], vdm.cpu.ip);
                ok = 0;
                break;
            }
            if (r != VDM_RAN) {
                struct timespec ts = { 0, 1000000 };
                nanosleep(&ts, NULL);
            }
        }
    }
    read_screen(&vdm);
    printf("---- screen ----\n%s----------------\n", screen);
    return ok ? 0 : 1;
}
