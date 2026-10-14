/*
 * bootfiles.c — the files beside the kernel (see bootfiles.h)
 */

#include "bootfiles.h"
#include "ramfs.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../ke/printf.h"

static UINT64  g_table, g_count;          /* from the bootloader (physical BootFile[]) */
static char  **g_paths;                   /* what was installed */
static int     g_installed;
static UINT64  g_bytes;

void BootFilesInfo(const BootInfo *info)
{
    if (!info || info->version < 5 || !info->files_base || !info->files_count) return;
    g_table = info->files_base;
    g_count = info->files_count > BOOT_FILES_MAX ? BOOT_FILES_MAX : info->files_count;
}

/* The directory @path's file goes in ("\A\B\file" -> \A\B, made as needed) */
static RamNode *parent_of(const char *path, const char **name)
{
    char dir[RAMFS_PATH_MAX];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '\\');
    if (!slash || !slash[1]) return NULL;
    *name = path + (slash - dir) + 1;
    *slash = '\0';
    RamNode *d = RamfsRoot();
    for (char *part = dir + 1, *next; d && part && *part; part = next) {
        next = strchr(part, '\\');
        if (next) *next++ = '\0';
        d = RamfsCreate(d, part, true);
    }
    return d;
}

int BootFilesInstall(void)
{
    if (!g_table || !g_count) return 0;
    BootFile *t = (BootFile *)(uintptr_t)(PHYSMAP_BASE + g_table);
    g_paths = kzalloc(sizeof(char *) * (size_t)g_count);
    for (UINT64 i = 0; i < g_count; i++) {
        BootFile *b = &t[i];
        b->path[BOOT_FILE_PATH_MAX - 1] = '\0';
        const char *name = NULL;
        RamNode *d = b->path[0] == '\\' && b->size <= 0xFFFFFFFFu ? parent_of(b->path, &name) : NULL;
        RamNode *f = d ? RamfsCreate(d, name, false) : NULL;
        if (f && RamfsWrite(f, (const char *)(uintptr_t)(PHYSMAP_BASE + b->base), (UINT32)b->size)) {
            size_t n = strlen(b->path) + 1;
            char *p = g_paths ? kmalloc(n) : NULL;
            if (p) { memcpy(p, b->path, n); g_paths[g_installed++] = p; }
            g_bytes += b->size;
        } else {
            kprintf("[FILES] %s: could not install\n", b->path);
        }
        UINT64 pages = (b->size + 4095) / 4096;
        pmm_free_pages((uintptr_t)b->base, pages ? pages : 1);
    }
    pmm_free_pages((uintptr_t)g_table, (BOOT_FILES_MAX * sizeof(BootFile) + 4095) / 4096);
    kprintf("[FILES] Installed %d files from %s (%u KB): wallpapers, icon sets, pictures\n",
            g_installed, BOOT_FILES_DIR, (unsigned)(g_bytes >> 10));
    g_table = g_count = 0;
    return g_installed;
}

int         BootFilesCount(void)     { return g_installed; }
const char *BootFilesPath(int i)     { return i >= 0 && i < g_installed ? g_paths[i] : NULL; }
UINT64      BootFilesBytes(void)     { return g_bytes; }
