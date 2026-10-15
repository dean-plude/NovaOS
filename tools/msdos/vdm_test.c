/* vdm_test.c — boots a DOS floppy image on NovaOS's real-mode CPU and
 * BIOS (userland/programs/ntvdm/cpu86.c, pcbios.c) on the build machine,
 * outside NovaOS, and drives it from the command line:
 *
 *     vdm_test IMAGE [wait=TEXT | type=TEXT]... [--timeout SECONDS]
 *              [--drive-c=DIR [--cwd=PATH] [--run=PATH [--args=TEXT] --exit=CODE]]
 *     vdm_test --put-dosrw=DIR
 *
 * wait=TEXT runs DOS until it writes TEXT to the screen (through the
 * BIOS, after what the last wait matched); type=TEXT types it ("\r" is
 * Enter).  --drive-c gives DOS directory DIR as drive C: (redir.h), with
 * PATH under it as the current directory; --run has DOS run the program
 * PATH (under DIR) as ntvdm.exe does for CreateProcess, and DOS must end
 * when it does with exit code CODE.  --put-dosrw writes the test program
 * DOSRW (ntvdm/dosrw.h) into DIR, as DOSRW.COM and DOSRW.EXE.  At the end, or when a wait times
 * out, it prints the screen; the exit status is 0 when every wait matched
 * (and the program ended as it should).  vdm_test.py builds and runs it
 * (CI's MS-DOS job), so the CPU, BIOS and drive C: are tested on every
 * change without booting NovaOS. */
#include "cpu86.h"
#include "pcbios.h"
#include "dosrw.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

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

/* ---- drive C: on a directory of the build machine ---- */

static char root[4096];

static void host_path(const char *path, char *out)
{
    snprintf(out, 4096 + VDM_PATH, "%s/%s", root, path);
    for (char *p = out + strlen(root); *p; p++) if (*p == '\\') *p = '/';
}

static int dos_error(void)
{
    return errno == ENOENT ? 2 : errno == ENOTDIR ? 3 : errno == EEXIST ? 0x50 : errno == EMFILE ? 4 : 5;
}

static void file_info(const char *name, const struct stat *st, VdmFile *f)
{
    memset(f, 0, sizeof(*f));
    snprintf(f->name, sizeof(f->name), "%s", name);
    f->size = S_ISDIR(st->st_mode) ? 0 : (uint32_t)st->st_size;
    f->attr = S_ISDIR(st->st_mode) ? 0x10 : 0x20;
    if (!(st->st_mode & S_IWUSR)) f->attr |= 0x01;
    struct tm tm;
    localtime_r(&st->st_mtime, &tm);
    f->time = (uint16_t)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
    f->date = (uint16_t)((tm.tm_year - 80) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
}

static int names_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int fs_list(void *ctx, const char *dir, int (*fn)(void *arg, const VdmFile *f), void *arg)
{
    (void)ctx;
    char path[4096 + VDM_PATH];
    host_path(dir, path);
    DIR *d = opendir(path);
    if (!d) return 3;
    char *names[512];
    int n = 0;
    for (struct dirent *e; (e = readdir(d)) && n < 512;)
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names[n++] = strdup(e->d_name);
    closedir(d);
    qsort(names, (size_t)n, sizeof(names[0]), names_cmp);    /* (the same order every time) */
    int stop = 0;
    for (int i = 0; i < n; i++) {
        char p[4096 + VDM_PATH + 260];
        struct stat st;
        snprintf(p, sizeof(p), "%s/%s", path, names[i]);
        if (!stop && !stat(p, &st)) {
            VdmFile f;
            file_info(names[i], &st, &f);
            stop = fn(arg, &f);
        }
        free(names[i]);
    }
    return 0;
}

static int fs_stat(void *ctx, const char *path, VdmFile *f)
{
    (void)ctx;
    char p[4096 + VDM_PATH];
    struct stat st;
    host_path(path, p);
    if (stat(p, &st)) return dos_error();
    const char *base = strrchr(path, '\\');
    file_info(base ? base + 1 : path, &st, f);
    return 0;
}

static int fs_open(void *ctx, const char *path, int how, void **file)
{
    (void)ctx;
    char p[4096 + VDM_PATH];
    host_path(path, p);
    int flags = (how & 3) == VDMFS_READ ? O_RDONLY : (how & 3) == VDMFS_WRITE ? O_WRONLY : O_RDWR;
    if (how & VDMFS_CREATE) flags |= O_CREAT | O_TRUNC;
    if (how & VDMFS_NEW) flags |= O_CREAT | O_EXCL;
    int fd = open(p, flags, 0644);
    if (fd < 0) return dos_error();
    *file = (void *)(intptr_t)(fd + 1);
    return 0;
}

static void fs_close(void *ctx, void *file) { (void)ctx; close((int)(intptr_t)file - 1); }

static int fs_read(void *ctx, void *file, uint32_t pos, void *buf, uint32_t n, uint32_t *got)
{
    (void)ctx;
    ssize_t r = pread((int)(intptr_t)file - 1, buf, n, pos);
    if (r < 0) return 5;
    *got = (uint32_t)r;
    return 0;
}

static int fs_write(void *ctx, void *file, uint32_t pos, const void *buf, uint32_t n)
{
    (void)ctx;
    int fd = (int)(intptr_t)file - 1;
    if (!n) return ftruncate(fd, pos) ? 5 : 0;
    return pwrite(fd, buf, n, pos) == (ssize_t)n ? 0 : 5;
}

static int fs_remove(void *ctx, const char *path)
{
    (void)ctx;
    char p[4096 + VDM_PATH];
    host_path(path, p);
    return unlink(p) ? dos_error() : 0;
}

static int fs_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    char a[4096 + VDM_PATH], b[4096 + VDM_PATH];
    host_path(from, a);
    host_path(to, b);
    return rename(a, b) ? dos_error() : 0;
}

static int fs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    char p[4096 + VDM_PATH];
    host_path(path, p);
    return mkdir(p, 0755) ? dos_error() : 0;
}

static int fs_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    char p[4096 + VDM_PATH];
    host_path(path, p);
    return rmdir(p) ? dos_error() : 0;
}

static void fs_space(void *ctx, uint64_t *total, uint64_t *avail)
{
    (void)ctx;
    struct statvfs s;
    if (statvfs(root, &s)) { *total = *avail = 0; return; }
    *total = (uint64_t)s.f_blocks * s.f_frsize;
    *avail = (uint64_t)s.f_bavail * s.f_frsize;
}

static const VdmFs host_fs = { NULL, fs_list, fs_stat, fs_open, fs_close, fs_read, fs_write,
                               fs_remove, fs_rename, fs_mkdir, fs_rmdir, fs_space };

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
    if (argc == 2 && !strncmp(argv[1], "--put-dosrw=", 12)) {
        static unsigned char exe[sizeof(dosrw_com) + 32];
        unsigned n = dosrw_exe(exe);
        char path[4096];
        snprintf(path, sizeof(path), "%s/DOSRW.COM", argv[1] + 12);
        FILE *f = fopen(path, "wb");
        if (!f || fwrite(dosrw_com, 1, sizeof(dosrw_com), f) != sizeof(dosrw_com) || fclose(f)) { perror(path); return 2; }
        snprintf(path, sizeof(path), "%s/DOSRW.EXE", argv[1] + 12);
        f = fopen(path, "wb");
        if (!f || fwrite(exe, 1, n, f) != n || fclose(f)) { perror(path); return 2; }
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: vdm_test IMAGE [wait=TEXT | type=TEXT]... [--timeout SECONDS]\n");
        return 2;
    }
    const char *drive = NULL, *cwd = "", *run = NULL, *args = "";
    int want_exit = -1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout = atoi(argv[i + 1]);
        else if (!strncmp(argv[i], "--drive-c=", 10)) drive = argv[i] + 10;
        else if (!strncmp(argv[i], "--cwd=", 6)) cwd = argv[i] + 6;
        else if (!strncmp(argv[i], "--run=", 6)) run = argv[i] + 6;
        else if (!strncmp(argv[i], "--args=", 7)) args = argv[i] + 7;
        else if (!strncmp(argv[i], "--exit=", 7)) want_exit = atoi(argv[i] + 7);
    }
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
    if (drive) {
        snprintf(root, sizeof(root), "%s", drive);
        vdm_drive_c(&vdm, &host_fs, cwd);
        if (run && !vdm_run_program(&vdm, run, args)) {
            fprintf(stderr, "vdm_test: DOS has no name for %s\n", run);
            return 2;
        }
    }
    uint64_t deadline = host_ms(NULL) + (uint64_t)timeout * 1000;
    int ok = 1;
    size_t from = 0;
    for (int i = 2; i < argc && ok; i++) {
        if (!strcmp(argv[i], "--timeout")) { i++; continue; }
        if (!strncmp(argv[i], "--", 2)) continue;
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
    while (ok && run) {                          /* the program runs to its end */
        int r = vdm_run(&vdm, 200000);
        if (r == VDM_ENDED) {
            printf("vdm_test: %s ended with exit code %d after %llu instructions%s\n", run, vdm.redir.exit_code,
                   (unsigned long long)vdm.cpu.count, vdm.redir.exec_error ? " (EXEC failed)" : "");
            if (vdm.redir.exec_error || vdm.redir.exit_code != want_exit) {
                printf("vdm_test: wanted exit code %d (EXEC error %d)\n", want_exit, vdm.redir.exec_error);
                ok = 0;
            }
            break;
        }
        if (host_ms(NULL) > deadline) {
            printf("vdm_test: %s did not end in %d s\n", run, timeout);
            ok = 0;
            break;
        }
        if (r != VDM_RAN) {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }
    read_screen(&vdm);
    printf("---- screen ----\n%s----------------\n", screen);
    return ok ? 0 : 1;
}
