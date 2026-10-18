/*
 * recovery.c — safe mode, startup repair and the failed-start count
 * (see recovery.h and docs/design/recovery.md)
 */
#include "recovery.h"
#include "update.h"
#include "fat.h"
#include "persist.h"
#include "ramfs.h"
#include "setup.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/version.h"
#include "../um/um.h"

#define NOVA_DIR     "\\EFI\\NOVA"
#define BOOT_DIR     "\\EFI\\BOOT"
/* in NOVA_DIR (the boot loader knows these names too) */
#define F_STARTS     "starts.cnt"       /* the failed-start count */
#define F_REQUEST    "recovery.req"     /* show the menu at the next start */
#define F_KERNEL     "kernel.elf"
#define F_OLD        "kernel.old"
#define F_NEW        "kernel.new"
#define F_LOADER_NEW "bootx64.new"
#define F_LOADER_OLD "bootx64.old"
#define F_PENDING    "update.pnd"
#define F_TRYING     "update.try"
#define F_INFO       "update.txt"
#define F_LOADER     "BOOTX64.EFI"      /* in BOOT_DIR */

#define REPORT_DIR   "\\Windows\\System32\\LogFiles\\Srt"
#define REPORT_FILE  "SrtTrail.txt"

static UINT64 g_flags;

void RecoveryBootInfo(const BootInfo *info)
{
    if (!info || info->version < 3) return;
    g_flags = info->boot_flags & (BOOT_FLAG_SAFE_MODE | BOOT_FLAG_STARTUP_REPAIR | BOOT_FLAG_PREVIOUS |
                                  BOOT_FLAG_FAILED_STARTS | BOOT_FLAG_UPDATE_TRIAL);
    if (g_flags & BOOT_FLAG_FAILED_STARTS)
        kprintf("[RECOVERY] The last starts did not reach the desktop: the boot loader showed Advanced startup\n");
    if (g_flags & BOOT_FLAG_PREVIOUS)
        kprintf("[RECOVERY] Started the previous version of NovaOS (%s, from \\EFI\\NOVA\\kernel.old)\n", NovaVersion());
    if (g_flags & BOOT_FLAG_SAFE_MODE)
        kprintf("[RECOVERY] Safe mode: only NovaOS's own drivers and the firmware's framebuffer\n");
    if (g_flags & BOOT_FLAG_STARTUP_REPAIR)
        kprintf("[RECOVERY] Startup repair was chosen: it runs once interrupts are on\n");
}

bool RecoverySafeMode(void) { return (g_flags & BOOT_FLAG_SAFE_MODE) != 0; }

/* ---------------------------------------------------------------------------
 * The failed-start count and the request for the menu
 * ------------------------------------------------------------------------- */
static void good_thread(void *arg)
{
    (void)arg;
    UINT32 dir;
    FatVol *esp = UpdateEspOpen(&dir);
    if (!esp) return;
    FatEntry e;
    /* (a disk whose ESP also keeps drive C:, \NOVA\C: its other FatVol
     * writes there, so this one does not; the boot loader counts no
     * starts on such a volume) */
    if (!PersistOwns(FatDevice(esp), FatStart(esp)) && FatLookup(esp, dir, F_STARTS, &e)) {
        FatDelete(esp, dir, F_STARTS);
        UpdateEspSync(esp);
        kprintf("[RECOVERY] NovaOS reached the desktop: the failed-start count is cleared\n");
    }
    UpdateEspClose(esp);
}

void RecoveryStartGood(void)
{
    static volatile int once;
    if (SetupIsLive() || __atomic_exchange_n(&once, 1, __ATOMIC_SEQ_CST)) return;
    sched_create_thread("start good", good_thread, NULL, PRIO_SERVICE);
}

bool RecoveryRequestMenu(void)
{
    UINT32 dir;
    FatVol *esp = SetupIsLive() ? NULL : UpdateEspOpen(&dir);
    if (!esp) {
        kprintf("[RECOVERY] The EFI System Partition NovaOS started from was not found: no Advanced startup\n");
        return false;
    }
    bool ok = FatWriteFile(esp, dir, F_REQUEST, "advanced startup\n", 17) && UpdateEspSync(esp);
    UpdateEspClose(esp);
    kprintf(ok ? "[RECOVERY] The next start shows Advanced startup\n"
               : "[RECOVERY] Could not ask for Advanced startup on the EFI System Partition\n");
    return ok;
}

/* ---------------------------------------------------------------------------
 * Startup repair
 * ------------------------------------------------------------------------- */
typedef struct {
    char  text[2048];
    int   len, found, fixed;
} Report;

static void note(Report *r, const char *fmt, ...)
{
    char line[200];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(line, sizeof(line), fmt, ap);
    __builtin_va_end(ap);
    kprintf("[REPAIR] %s\n", line);
    int n = (int)strlen(line);
    if (r->len + n + 3 < (int)sizeof(r->text)) {
        memcpy(r->text + r->len, line, (size_t)n);
        r->len += n;
        r->text[r->len++] = '\r';
        r->text[r->len++] = '\n';
        r->text[r->len] = '\0';
    }
}

/* Whether @name in @dir is there and starts as a NovaOS kernel would (an
 * x86-64 ELF executable of at least a megabyte) */
static bool kernel_good(FatVol *v, UINT32 dir, const char *name)
{
    FatEntry e;
    UINT8 h[64];
    if (!FatLookup(v, dir, name, &e) || e.dir || e.size < (1u << 20)) return false;
    return FatReadHead(v, &e, h, sizeof(h)) == sizeof(h) && h[0] == 0x7F && h[1] == 'E' && h[2] == 'L' &&
           h[3] == 'F' && h[4] == 2 && h[5] == 1 && h[16] == 2 && h[18] == 0x3E && h[19] == 0;
}

/* ... and a boot loader as an EFI program ("MZ") */
static bool loader_good(FatVol *v, UINT32 dir, const char *name)
{
    FatEntry e;
    UINT8 h[2];
    return FatLookup(v, dir, name, &e) && !e.dir && e.size >= 4096 && FatReadHead(v, &e, h, 2) == 2 &&
           h[0] == 'M' && h[1] == 'Z';
}

static bool has(FatVol *v, UINT32 dir, const char *name)
{
    FatEntry e;
    return FatLookup(v, dir, name, &e) && !e.dir;
}

/* The boot loader: \EFI\BOOT\BOOTX64.EFI, put back from the known-good
 * copy when it is missing or not an EFI program */
static void repair_loader(Report *r, FatVol *esp, UINT32 dir)
{
    FatEntry b;
    if (!FatLookupPath(esp, BOOT_DIR, &b) || !b.dir) {
        UINT32 c;
        if (!FatMkdirPath(esp, BOOT_DIR, &c)) { r->found++; note(r, "The folder \\EFI\\BOOT is missing and could not be made."); return; }
        b.cluster = c;
    }
    if (loader_good(esp, b.cluster, F_LOADER)) {
        note(r, "The boot loader \\EFI\\BOOT\\BOOTX64.EFI: no problem found.");
        return;
    }
    r->found++;
    FatEntry e;
    UINT8 *buf = NULL;
    if (loader_good(esp, dir, F_LOADER_OLD) && FatLookup(esp, dir, F_LOADER_OLD, &e) && (buf = kmalloc(e.size)) &&
        FatRead(esp, &e, buf) && FatWriteFile(esp, b.cluster, F_LOADER, buf, e.size) && UpdateEspSync(esp)) {
        r->fixed++;
        note(r, "The boot loader \\EFI\\BOOT\\BOOTX64.EFI was damaged or missing: restored from the previous version's (bootx64.old).");
    } else {
        note(r, "The boot loader \\EFI\\BOOT\\BOOTX64.EFI is damaged or missing, and there is no copy to restore it from.");
    }
    kfree(buf);
}

/* The kernel: kernel.elf, replaced by the previous one when it is not a
 * kernel (the boot loader then started kernel.old to get here) */
static void repair_kernel(Report *r, FatVol *esp, UINT32 dir)
{
    if (kernel_good(esp, dir, F_KERNEL)) {
        note(r, "The kernel \\EFI\\NOVA\\kernel.elf: no problem found.");
        return;
    }
    r->found++;
    if (kernel_good(esp, dir, F_OLD)) {
        FatDelete(esp, dir, F_KERNEL);
        if (FatRename(esp, dir, F_OLD, F_KERNEL) && UpdateEspSync(esp)) {
            r->fixed++;
            note(r, "The kernel \\EFI\\NOVA\\kernel.elf was damaged or missing: the previous version (kernel.old) took its place.");
            return;
        }
    }
    note(r, "The kernel \\EFI\\NOVA\\kernel.elf is damaged or missing, and there is no previous version to put in its place.");
}

/* An update left half-way: staged files without their mark, a trying
 * mark without its kernel, or a staged update, which a start that needed
 * repair should not try next (it can be installed again) */
static void repair_update(Report *r, FatVol *esp, UINT32 dir)
{
    if (g_flags & BOOT_FLAG_UPDATE_TRIAL) {
        note(r, "An update is being tried in this start: it is left to finish or to roll back.");
        return;
    }
    bool pending = has(esp, dir, F_PENDING), trying = has(esp, dir, F_TRYING);
    bool left = has(esp, dir, F_NEW) || has(esp, dir, F_LOADER_NEW) || has(esp, dir, F_INFO);
    if (!pending && !trying && !left) {
        note(r, "Updates: nothing unfinished.");
        return;
    }
    r->found++;
    FatDelete(esp, dir, F_PENDING);
    FatDelete(esp, dir, F_TRYING);
    FatDelete(esp, dir, F_NEW);
    FatDelete(esp, dir, F_LOADER_NEW);
    FatDelete(esp, dir, F_INFO);
    if (UpdateEspSync(esp)) r->fixed++;
    note(r, pending ? "A staged update was removed (install it again from Settings once NovaOS starts well)."
                    : "The files of an unfinished update were removed.");
}

static void write_report(Report *r)
{
    FsLock();
    RamNode *d = RamfsResolve(NULL, "\\");
    char path[] = REPORT_DIR;
    for (char *part = path + 1, *next; d && part && *part; part = next) {
        next = strchr(part, '\\');
        if (next) *next++ = '\0';
        d = RamfsCreate(d, part, true);
    }
    RamNode *f = d ? RamfsCreate(d, REPORT_FILE, false) : NULL;
    bool ok = f && RamfsWrite(f, r->text, (UINT32)r->len);
    FsUnlock();
    if (!ok) kprintf("[REPAIR] Could not write C:" REPORT_DIR "\\" REPORT_FILE "\n");
}

static void repair_thread(void *arg)
{
    (void)arg;
    static Report r;
    memset(&r, 0, sizeof(r));
    note(&r, "Startup Repair diagnosis and repair log (NovaOS %s)", NovaVersion());
    note(&r, "---------------------------");
    UINT32 dir;
    FatVol *esp = UpdateEspOpen(&dir);
    if (!esp) {
        r.found++;
        note(&r, "The EFI System Partition NovaOS started from was not found.");
    } else {
        FatReclaimInfo ri;
        if (PersistOwns(FatDevice(esp), FatStart(esp))) {
            note(&r, "The EFI System Partition is drive C:'s volume too: it was checked as it was mounted.");
        } else if (!FatReclaim(esp, true, &ri)) {
            r.found++;
            note(&r, "The EFI System Partition's file system could not be checked.");
        } else if (ri.reclaimed || ri.crossed) {
            r.found++;
            r.fixed += ri.reclaimed ? 1 : 0;
            note(&r, "The EFI System Partition's file system: %u lost cluster(s) freed, %u crossed chain(s) left alone.",
                 ri.reclaimed, ri.crossed);
        } else {
            note(&r, "The EFI System Partition's file system: no problem found.");
        }
        repair_loader(&r, esp, dir);
        repair_kernel(&r, esp, dir);
        repair_update(&r, esp, dir);
        FatDelete(esp, dir, F_STARTS);
        FatDelete(esp, dir, F_REQUEST);
        UpdateEspSync(esp);
        note(&r, "The failed-start count was cleared.");
        UpdateEspClose(esp);
    }
    char where[128];
    PersistDescribe(where, sizeof(where));
    if (PersistActive()) {
        note(&r, "Drive C: is kept on %s: it was checked as it was mounted.", where);
    } else {
        note(&r, "Drive C: is not kept on a disk (%s): files saved now are lost at the next restart.", where);
    }
    note(&r, "Startup repair finished: %d problem(s) found, %d repaired.", r.found, r.fixed);
    write_report(&r);
}

void RecoveryBootDone(void)
{
    if (!(g_flags & BOOT_FLAG_STARTUP_REPAIR) || SetupIsLive()) return;
    sched_create_thread("startup repair", repair_thread, NULL, PRIO_SERVICE);
}
