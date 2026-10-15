/* redir.h — drive C: for DOS: the host's drive through MS-DOS's network
 * redirector interface (INT 2Fh, AH=11h), as NTVDM's REDIR did.
 *
 * DOS hands every operation on a drive whose current directory structure
 * (CDS) is marked "network" to INT 2Fh: open, read, write, find first,
 * make directory and the rest, with the file's full name ("C:\DIR\X.TXT")
 * in its swappable data area and the open file's system file table entry
 * (SFT) in ES:DI.  REDIR.COM, which the boot disk's AUTOEXEC.BAT runs,
 * hands the host DOS's list of lists and data area (BOP 60h); the host
 * then marks C: as a network drive and puts the BIOS's INT 2Fh handler
 * (BOP 2Fh) in front of DOS's.  The names DOS sees are 8.3: a host name
 * that does not fit gets an alias as Windows makes them (LONGNA~1.TXT).
 *
 * It knows nothing of the host either: a VdmFs (ntvdm.c: NovaOS's files;
 * tools/msdos/vdm_test.c: a directory on the build machine) reads and
 * writes the files.  Paths given to it are relative to the drive's root,
 * components joined with '\', "" for the root itself. */
#ifndef NOVA_REDIR_H
#define NOVA_REDIR_H

#include <stdint.h>

/* a file or directory as the host has it */
typedef struct VdmFile {
    char name[256];                             /* its own (long) name */
    uint32_t size;
    uint8_t attr;                               /* DOS's: 01h read-only 02h hidden 04h system 10h directory 20h archive */
    uint16_t time, date;                        /* DOS's format */
} VdmFile;

enum { VDMFS_READ = 0, VDMFS_WRITE = 1, VDMFS_RW = 2,
       VDMFS_CREATE = 0x10,                     /* create it, or empty it if it is there */
       VDMFS_NEW = 0x20 };                      /* create it; error 50h if it is there */

/* each returns 0 or a DOS error code (2 file not found, 3 path not found,
 * 5 access denied, 50h file exists, ...) */
typedef struct VdmFs {
    void *ctx;
    /* the entries of directory @dir (without "." and ".."), in an order
     * that stays the same while the directory does; @fn returns nonzero to
     * stop */
    int (*list)(void *ctx, const char *dir, int (*fn)(void *arg, const VdmFile *f), void *arg);
    int (*stat)(void *ctx, const char *path, VdmFile *f);
    int (*open)(void *ctx, const char *path, int how, void **file);
    void (*close)(void *ctx, void *file);
    int (*read)(void *ctx, void *file, uint32_t pos, void *buf, uint32_t n, uint32_t *got);
    int (*write)(void *ctx, void *file, uint32_t pos, const void *buf, uint32_t n);   /* n = 0: the file ends at @pos */
    int (*remove)(void *ctx, const char *path);
    int (*rename)(void *ctx, const char *from, const char *to);
    int (*mkdir)(void *ctx, const char *path);
    int (*rmdir)(void *ctx, const char *path);
    void (*space)(void *ctx, uint64_t *total, uint64_t *free);
} VdmFs;

#define VDM_FILES 64
#define VDM_SEARCHES 16
#define VDM_PATH 520

typedef struct VdmRedir {
    VdmFs fs;
    int on;                                     /* a VdmFs is there */
    int hooked;                                 /* INT 2Fh is ours (REDIR.COM ran) */
    uint16_t sda_seg, sda_off;                  /* DOS's swappable data area */
    void *files[VDM_FILES];                     /* open host files: SFT field 19h is the index + 1 */
    struct { char dir[VDM_PATH]; uint16_t tag; } search[VDM_SEARCHES];
    uint16_t search_tag;                        /* (find next knows its directory by slot and tag) */
    char cwd[68];                               /* DOS's current directory on C: to start with */
    char run[80], args[128];                    /* the program REDIR.COM runs ("" none) */
    int ended, exit_code, exec_error;           /* it ended: its exit code, or EXEC's error (0: none) */
} VdmRedir;

struct Vdm;

/* the INT 2Fh redirector call in AX (AH = 11h); 0 if it is not ours (the
 * BIOS passes it on to DOS's handler) */
int vdm_redir_call(struct Vdm *v);
/* REDIR.COM's BOP 60h: ES:BX DOS's list of lists, DS:SI its data area */
int vdm_redir_install(struct Vdm *v);
/* a host path ("Users\Dean\a long name") as DOS sees it ("C:\USERS\DEAN\ALONGN~1"),
 * in @out (67 bytes with the NUL, DOS's limit); 0 if it has no such name */
int vdm_redir_dos_path(struct Vdm *v, const char *host, char *out, int cap);

#endif
