/*
 * iconset.c — icon sets (see iconset.h)
 */

#include "iconset.h"
#include "../wm/wallpaper.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../ke/printf.h"
#include "../um/um.h"
#include "../gdi/png.h"

/* The icons a set may have, by file name (without .ico or .png) */
enum { K_TERMINAL, K_EXPLORER, K_NOTEPAD, K_SETTINGS, K_CALENDAR, K_WEB, K_STORE, K_PHOTOS,
       K_SETUP, K_WELCOME, K_FOLDER, K_DOCUMENTS, K_DOWNLOADS, K_PICTURES, K_PROJECTS,
       K_FILE, K_PC, K_COUNT };
static const char *const g_names[K_COUNT] = {
    "terminal", "file-explorer", "notepad", "settings", "calendar", "web", "app-store", "photos",
    "install-novaos", "welcome", "folder", "folder-documents", "folder-downloads",
    "folder-pictures", "folder-projects", "file", "this-pc",
};

static char     g_set[RAMFS_NAME_MAX];      /* "" built in */
static GdiIcon *g_icons[K_COUNT];

static int app_key(AppId id)
{
    switch (id) {
    case APP_TERMINAL: return K_TERMINAL;
    case APP_EXPLORER: return K_EXPLORER;
    case APP_NOTEPAD:  return K_NOTEPAD;
    case APP_SETTINGS: return K_SETTINGS;
    case APP_CALENDAR: return K_CALENDAR;
    case APP_NETSURF: case APP_FIREFOX: return K_WEB;
    case APP_STORE:    return K_STORE;
    case APP_PHOTOS:   return K_PHOTOS;
    case APP_SETUP:    return K_SETUP;
    case APP_WELCOME:  return K_WELCOME;
    default:           return -1;
    }
}

static bool ieq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}

/* The key file @f stands for, or -1 */
static int key_of(const RamNode *f)
{
    if (f->dir) return -1;
    size_t n = strlen(f->name);
    if (n < 5 || !(ieq(f->name + n - 4, ".ico", 4) || ieq(f->name + n - 4, ".png", 4))) return -1;
    for (int k = 0; k < K_COUNT; k++)
        if (strlen(g_names[k]) == n - 4 && ieq(f->name, g_names[k], n - 4)) return k;
    return -1;
}

bool IconSetIsSet(const RamNode *d)
{
    if (!d || !d->dir) return false;
    for (RamNode *c = d->child; c; c = c->next) if (key_of(c) >= 0) return true;
    return false;
}

/* @dir's file for key @k (.ico before .png) */
static RamNode *set_file(RamNode *dir, int k)
{
    char name[RAMFS_NAME_MAX];
    ksnprintf(name, sizeof(name), "%s.ico", g_names[k]);
    RamNode *f = RamfsFind(dir, name);
    if (!f) {
        ksnprintf(name, sizeof(name), "%s.png", g_names[k]);
        f = RamfsFind(dir, name);
    }
    return f && !f->dir ? f : NULL;
}

static GdiIcon *load_icon(RamNode *f)
{
    if (!f || !RamfsLoad(f) || !f->data || !f->size) return NULL;
    return PngIsPng(f->data, f->size) ? IconFromPng(f->data, f->size) : IconLoad(f->data, f->size);
}

static void load(const char *name)
{
    for (int k = 0; k < K_COUNT; k++) { IconFree(g_icons[k]); g_icons[k] = NULL; }
    strncpy(g_set, name, sizeof(g_set) - 1);
    g_set[sizeof(g_set) - 1] = '\0';
    if (!g_set[0]) return;
    char path[RAMFS_PATH_MAX];
    ksnprintf(path, sizeof(path), "%s\\%s", ICONSET_DIR, g_set);
    RamNode *dir = RamfsResolve(NULL, path);
    int n = 0;
    if (dir && dir->dir)
        for (int k = 0; k < K_COUNT; k++)
            if ((g_icons[k] = load_icon(set_file(dir, k)))) n++;
    kprintf("[ICONS] Icon set %s: %d of %d icons\n", g_set, n, K_COUNT);
}

void IconSetInit(void)
{
    char name[RAMFS_NAME_MAX];
    if (!um_registry_get_sz(PERSONALIZE_KEY, "IconSet", name, sizeof(name))) return;
    FsLock();
    load(name);
    FsUnlock();
}

const char *IconSetCurrent(void) { return g_set; }

void IconSetChoose(const char *name)
{
    if (!name || strlen(name) >= sizeof(g_set)) return;
    load(name);
    um_registry_set_sz(PERSONALIZE_KEY, "IconSet", g_set);
    WmInvalidateBackground();                   /* (the desktop's icons) */
}

GdiIcon *IconSetApp(AppId id)       { int k = app_key(id); return k >= 0 ? g_icons[k] : NULL; }
GdiIcon *IconSetFile(void)          { return g_icons[K_FILE]; }
GdiIcon *IconSetPc(void)            { return g_icons[K_PC]; }
GdiIcon *IconSetFolder(FolderKind k)
{
    static const int keys[] = { K_FOLDER, K_DOCUMENTS, K_DOWNLOADS, K_PICTURES, K_PROJECTS };
    return (int)k >= 0 && (int)k < 5 ? g_icons[keys[k]] : NULL;
}

/* -----------------------------------------------------------------------
 * The sets, and folders to add as sets
 * ----------------------------------------------------------------------- */
int IconSetList(RamNode **out, int max)
{
    RamNode *d = RamfsResolve(NULL, ICONSET_DIR);
    int n = 0;
    if (d && d->dir)
        for (RamNode *c = d->child; c && n < max; c = c->next)   /* (dirs come first, by name) */
            if (IconSetIsSet(c)) out[n++] = c;
    return n;
}

int IconSetCandidates(RamNode **out, int max)
{
    static const char *const places[] = { "\\Downloads", "\\Documents", "\\Desktop", "\\Pictures" };
    RamNode *sets = RamfsResolve(NULL, ICONSET_DIR);
    int n = 0;
    for (int p = 0; p < 4; p++) {
        RamNode *d = RamfsResolve(NULL, places[p]);
        if (!d || !d->dir) continue;
        for (RamNode *c = d->child; c && n < max; c = c->next) {
            if (!c->dir) continue;
            if (IconSetIsSet(c) && !(sets && RamfsFind(sets, c->name))) out[n++] = c;
            for (RamNode *g = c->child; g && n < max; g = g->next)           /* one folder down */
                if (IconSetIsSet(g) && !(sets && RamfsFind(sets, g->name))) out[n++] = g;
        }
    }
    return n;
}

bool IconSetInstall(RamNode *src)
{
    if (!IconSetIsSet(src)) return false;
    RamNode *web = RamfsCreate(RamfsCreate(RamfsRoot(), "Windows", true), "Web", true);
    RamNode *sets = web ? RamfsCreate(web, "Icons", true) : NULL;
    if (!sets || RamfsFind(sets, src->name)) return false;
    RamNode *dst = RamfsCreate(sets, src->name, true);
    if (!dst) return false;
    int copied = 0;
    for (RamNode *c = src->child; c; c = c->next) {
        if (key_of(c) < 0 || !RamfsLoad(c) || (!c->data && c->size)) continue;
        RamNode *f = RamfsCreate(dst, c->name, false);
        if (f && RamfsWrite(f, c->data, c->size)) copied++;
    }
    kprintf("[ICONS] Added the icon set %s (%d icons)\n", dst->name, copied);
    if (!copied) return false;
    IconSetChoose(dst->name);
    return true;
}

void IconSetDrawPreview(RamNode *dir, AppId id, int x, int y, int size)
{
    int k = app_key(id);
    RamNode *f = dir && k >= 0 ? set_file(dir, k) : NULL;
    if (f && IconDraw(AppFileIcon(f), x, y, size)) return;
    /* (built in: what AppDrawIcon draws without a set) */
    GdiIcon *saved = k >= 0 ? g_icons[k] : NULL;
    if (k >= 0) g_icons[k] = NULL;
    AppDrawIcon(id, x, y, size);
    if (k >= 0) g_icons[k] = saved;
}
