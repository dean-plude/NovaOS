/*
 * reset.c — Reset this PC, keeping your files (see reset.h)
 */
#include "reset.h"
#include "persist.h"
#include "recovery.h"
#include "ramfs.h"
#include "setup.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/version.h"
#include "../um/um.h"
#include "../wm/desktop.h"

#define REQUEST_DIR  "\\Windows\\System32\\Recovery"
#define REQUEST_FILE "ResetPC.req"
#define REPORT_DIR   "\\Windows\\Logs\\SystemReset"
#define REPORT_FILE  "ResetReport.txt"
#define PROFILE_LIST "Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList"

/* What stays on C: (paths from its root) */
#define KEEP_MAX 24
static char g_keep[KEEP_MAX][RAMFS_PATH_MAX];
static int  g_nkeep;
static int  g_removed, g_kept_users;
static bool g_done;

/* The first account's folders when its profile is C:\ itself */
static const char *const g_root_profile[] = {
    "Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos", "AppData",
    "Favorites", "Links", "Contacts", "Saved Games", "Searches",
};

static void keep(const char *path)
{
    if (g_nkeep < KEEP_MAX) ksnprintf(g_keep[g_nkeep++], RAMFS_PATH_MAX, "%s", path);
}

static char up(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

/* @path ("C:\..." or "\...") is a kept folder or inside one */
static bool kept(const char *path)
{
    if (path[0] && path[1] == ':') path += 2;
    for (int i = 0; i < g_nkeep; i++) {
        const char *a = path, *k = g_keep[i];
        while (*k && up(*a) == up(*k)) { a++; k++; }
        if (!*k && (!*a || *a == '\\')) return true;
    }
    return false;
}

bool ResetRequest(void)
{
    if (SetupIsLive() || !PersistActive()) {
        kprintf("[RESET] Drive C: is not kept on a disk: there is nothing to reset\n");
        return false;
    }
    FsLock();
    RamNode *d = RamfsResolve(NULL, "\\");
    char path[] = REQUEST_DIR;
    for (char *part = path + 1, *next; d && part && *part; part = next) {
        next = strchr(part, '\\');
        if (next) *next++ = '\0';
        d = RamfsCreate(d, part, true);
    }
    RamNode *f = d ? RamfsCreate(d, REQUEST_FILE, false) : NULL;
    bool ok = f && RamfsWrite(f, "keep my files\r\n", 15);
    FsUnlock();
    kprintf(ok ? "[RESET] The next start resets this PC, keeping your files\n"
               : "[RESET] Could not ask for a reset on drive C:\n");
    return ok;
}

/* Remove what is below @n (a folder) and isn't kept; then @n itself when
 * it isn't the root and comes to be empty.  NovaOS's own files (sealed:
 * from the OS image) stay; starter files go without being remembered as
 * deleted, so the next start brings them back as they were. */
static void sweep(RamNode *n, bool root)
{
    char path[RAMFS_PATH_MAX];
    RamfsPath(n, path, sizeof(path));
    if (!root && kept(path)) {
        if (n->dir) g_kept_users++;
        return;
    }
    if (n->dir && RamfsLoad(n))
        for (RamNode *c = n->child, *next; c; c = next) {
            next = c->next;
            sweep(c, false);
        }
    if (root || (n->pflags & RAMFS_F_SEALED) || (n->dir && n->child)) return;
    n->pflags &= (UINT8)~RAMFS_F_SEED;
    if (RamfsDelete(n)) g_removed++;
}

static void report(void)
{
    char text[512];
    int len = ksnprintf(text, sizeof(text),
                        "Reset this PC (keep my files), NovaOS %s\r\n"
                        "Removed: %d file(s) and folder(s): the apps and drivers installed, and the settings.\r\n"
                        "Kept: %d profile folder(s) of the accounts, and the accounts.\r\n",
                        NovaVersion(), g_removed, g_kept_users);
    FsLock();
    RamNode *d = RamfsResolve(NULL, "\\");
    char path[] = REPORT_DIR;
    for (char *part = path + 1, *next; d && part && *part; part = next) {
        next = strchr(part, '\\');
        if (next) *next++ = '\0';
        d = RamfsCreate(d, part, true);
    }
    RamNode *f = d ? RamfsCreate(d, REPORT_FILE, false) : NULL;
    if (f) RamfsWrite(f, text, (UINT32)len);
    FsUnlock();
}

void ResetBoot(void)
{
    if (SetupIsLive() || !PersistActive() || !RamfsResolve(NULL, REQUEST_DIR "\\" REQUEST_FILE)) return;
    kprintf("[RESET] Resetting this PC: removing the apps and settings, keeping the accounts and their files\n");

    /* the profiles: C:\Users, and each account's (the first's may be C:\) */
    keep("\\Users");
    keep("\\Windows\\System32\\config");                 /* (the registry's file: written again below) */
    for (UINT32 i = 0; g_nkeep < KEEP_MAX - (int)(sizeof(g_root_profile) / sizeof(g_root_profile[0])); i++) {
        UINT16 w[64];
        int n = um_registry_enum_key(PROFILE_LIST, i, w, 63, NULL);
        if (n < 0) break;
        char key[192], sid[64], prof[RAMFS_PATH_MAX];
        int m = 0;
        for (int k = 0; k < n && k < 63; k++) sid[m++] = (char)w[k];
        sid[m] = '\0';
        ksnprintf(key, sizeof(key), PROFILE_LIST "\\%s", sid);
        if (!um_registry_get_sz(key, "ProfileImagePath", prof, sizeof(prof))) continue;
        if (up(prof[0]) != 'C' || prof[1] != ':') continue;
        if (!prof[2] || (prof[2] == '\\' && !prof[3])) {
            for (unsigned k = 0; k < sizeof(g_root_profile) / sizeof(g_root_profile[0]); k++) {
                char p[64];
                ksnprintf(p, sizeof(p), "\\%s", g_root_profile[k]);
                keep(p);
            }
        } else if (!kept(prof)) {
            keep(prof + 2);
        }
    }

    FsLock();
    sweep(RamfsResolve(NULL, "\\"), true);
    FsUnlock();
    PersistForgetDeleted(kept);

    static const char *const keys[] = {
        "Machine\\SAM",
        PROFILE_LIST,
        "Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\LogonUI",
        UM_SETUP_KEY,
    };
    um_registry_reset(keys, (int)(sizeof(keys) / sizeof(keys[0])));
    report();
    g_done = true;
    kprintf("[RESET] Removed %d file(s) and folder(s); kept %d profile folder(s): restarting once interrupts are on\n",
            g_removed, g_kept_users);
}

static void restart_thread(void *arg)
{
    (void)arg;
    RecoveryClearFailedStarts();                         /* (this start restarts on purpose before the desktop) */
    kprintf("[RESET] Reset this PC finished: restarting\n");
    DesktopPowerRequest(POWER_RESTART);
}

void ResetBootDone(void)
{
    if (g_done) sched_create_thread("reset", restart_thread, NULL, PRIO_SERVICE);
}
