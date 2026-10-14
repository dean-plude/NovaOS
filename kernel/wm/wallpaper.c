/*
 * wallpaper.c — the desktop's picture wallpaper (see wallpaper.h)
 */

#include "wallpaper.h"
#include "../gdi/png.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/spinlock.h"
#include "../um/um.h"
#include "../apps/iconset.h"

#define DESKTOP_KEY  "User\\" UM_USER_SID "\\Control Panel\\Desktop"
#define FILE_MAX     (32u << 20)          /* bigger PNGs are not decoded */
#define THUMB_W      192
#define THUMB_H      108
#define THUMBS_MAX   64

/* The picture the desktop draws (desktop thread) */
static char    g_path[RAMFS_PATH_MAX] = WALLPAPER_DEFAULT;
static UINT32 *g_px;
static int     g_w, g_h;

/* Work for the "wallpaper" thread and what it hands back, under g_lock */
static KSpinLock g_lock;
static char    g_want[RAMFS_PATH_MAX];    /* a picture to decode ("" none) */
static UINT32 *g_new_px;                  /* ... decoded, for the desktop to take */
static int     g_new_w, g_new_h;
static bool    g_new;
static volatile bool g_running;           /* the thread is running */
static volatile bool g_changed;

/* Thumbnails: added, never removed (THUMBS_MAX of them at most) */
typedef struct {
    char    path[RAMFS_PATH_MAX];
    UINT32  size;
    UINT64  mtime;
    UINT32 *px;                           /* THUMB_W x THUMB_H, once made */
    bool    failed;
} Thumb;
static Thumb  g_thumbs[THUMBS_MAX];
static int    g_nthumbs;
static volatile UINT32 g_thumbs_gen;

static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return *a == *b;
}

static bool is_png(const RamNode *f)
{
    size_t n = strlen(f->name);
    return !f->dir && n > 4 && ieq(f->name + n - 4, ".png");
}

/* A copy of the file at @path (the file-system lock is taken) */
static char *snapshot(const char *path, UINT32 *size)
{
    char *copy = NULL;
    FsLock();
    RamNode *f = RamfsResolve(NULL, path);
    if (f && !f->dir && RamfsLoad(f) && f->data && f->size && f->size <= FILE_MAX) {
        copy = kmalloc(f->size);
        if (copy) { memcpy(copy, f->data, f->size); *size = f->size; }
    }
    FsUnlock();
    return copy;
}

static bool decode(const char *path, UINT32 **px, int *w, int *h)
{
    UINT32 size = 0;
    char *copy = snapshot(path, &size);
    if (!copy) return false;
    bool ok = PngDecode(copy, size, px, w, h);
    kfree(copy);
    if (!ok) kprintf("[WALLPAPER] %s: not a picture NovaOS can show\n", path);
    return ok;
}

/* @px (w x h) shrunk into a THUMB_W x THUMB_H picture, its middle cut to that shape */
static UINT32 *thumb_of(const UINT32 *px, int w, int h)
{
    UINT32 *t = kmalloc(THUMB_W * THUMB_H * 4);
    if (!t) return NULL;
    int cw = w, ch = h;                                  /* the part used */
    if ((INT64)w * THUMB_H > (INT64)h * THUMB_W) cw = (int)((INT64)h * THUMB_W / THUMB_H);
    else                                         ch = (int)((INT64)w * THUMB_H / THUMB_W);
    int ox = (w - cw) / 2, oy = (h - ch) / 2;
    for (int y = 0; y < THUMB_H; y++) {
        int y0 = oy + y * ch / THUMB_H, y1 = oy + (y + 1) * ch / THUMB_H;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < THUMB_W; x++) {
            int x0 = ox + x * cw / THUMB_W, x1 = ox + (x + 1) * cw / THUMB_W;
            if (x1 <= x0) x1 = x0 + 1;
            UINT32 r = 0, g = 0, b = 0, n = 0;
            /* (every other pixel of big boxes: plenty for a thumbnail) */
            int step = (x1 - x0) > 8 ? 2 : 1;
            for (int sy = y0; sy < y1; sy += step)
                for (int sx = x0; sx < x1; sx += step) {
                    UINT32 p = px[(size_t)sy * w + sx];
                    r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF; n++;
                }
            t[y * THUMB_W + x] = 0xFF000000u | (r / n) << 16 | (g / n) << 8 | (b / n);
        }
    }
    return t;
}

/* The next thumbnail to make (copied out), or false */
static bool next_thumb(int *idx, char *path)
{
    bool found = false;
    IrqState st = spin_lock_irqsave(&g_lock);
    for (int i = 0; i < g_nthumbs && !found; i++)
        if (!g_thumbs[i].px && !g_thumbs[i].failed) {
            *idx = i;
            strcpy(path, g_thumbs[i].path);
            found = true;
        }
    spin_unlock_irqrestore(&g_lock, st);
    return found;
}

static void worker(void *arg)
{
    (void)arg;
    char path[RAMFS_PATH_MAX];
    for (;;) {
        IrqState st = spin_lock_irqsave(&g_lock);
        strcpy(path, g_want);
        g_want[0] = '\0';
        spin_unlock_irqrestore(&g_lock, st);
        if (path[0]) {                              /* the wallpaper first */
            UINT32 *px;
            int w, h;
            if (decode(path, &px, &w, &h)) {
                st = spin_lock_irqsave(&g_lock);
                UINT32 *old = g_new ? g_new_px : NULL;
                g_new_px = px; g_new_w = w; g_new_h = h; g_new = true;
                spin_unlock_irqrestore(&g_lock, st);
                kfree(old);
                g_changed = true;
            }
            continue;
        }
        int i;
        if (next_thumb(&i, path)) {
            UINT32 *px, *t = NULL;
            int w, h;
            if (decode(path, &px, &w, &h)) {
                t = thumb_of(px, w, h);
                kfree(px);
            }
            st = spin_lock_irqsave(&g_lock);
            g_thumbs[i].px = t;
            g_thumbs[i].failed = !t;
            spin_unlock_irqrestore(&g_lock, st);
            __atomic_add_fetch(&g_thumbs_gen, 1, __ATOMIC_RELEASE);
            continue;
        }
        /* nothing left: stop, unless work came in meanwhile */
        __atomic_store_n(&g_running, false, __ATOMIC_SEQ_CST);
        st = spin_lock_irqsave(&g_lock);
        bool more = g_want[0] != '\0';
        for (int k = 0; k < g_nthumbs && !more; k++) more = !g_thumbs[k].px && !g_thumbs[k].failed;
        spin_unlock_irqrestore(&g_lock, st);
        if (!more || __atomic_exchange_n(&g_running, true, __ATOMIC_SEQ_CST)) return;
    }
}

static void kick(void)
{
    if (__atomic_exchange_n(&g_running, true, __ATOMIC_SEQ_CST)) return;
    if (!sched_create_thread("wallpaper", worker, NULL, PRIO_SERVICE))
        __atomic_store_n(&g_running, false, __ATOMIC_SEQ_CST);
}

/* -----------------------------------------------------------------------
 * The wallpaper
 * ----------------------------------------------------------------------- */
void WallpaperInit(void)
{
    char v[RAMFS_PATH_MAX + 2];
    /* "C:\Windows\Web\Wallpaper\Beam.png": a picture on drive C: */
    if (um_registry_get_sz(DESKTOP_KEY, "Wallpaper", v, sizeof(v)) &&
        (v[0] == 'C' || v[0] == 'c') && v[1] == ':' && v[2] == '\\') {
        strncpy(g_path, v + 2, sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
    }
    if (!decode(g_path, &g_px, &g_w, &g_h) && strcmp(g_path, WALLPAPER_DEFAULT)) {
        strcpy(g_path, WALLPAPER_DEFAULT);         /* (gone: the default) */
        decode(g_path, &g_px, &g_w, &g_h);
    }
}

const char *WallpaperCurrent(void) { return g_path; }

void WallpaperSet(const char *path)
{
    if (!path || !path[0] || strlen(path) >= sizeof(g_path)) return;
    strcpy(g_path, path);
    char v[RAMFS_PATH_MAX + 2];
    ksnprintf(v, sizeof(v), "C:%s", path);
    um_registry_set_sz(DESKTOP_KEY, "Wallpaper", v);
    IrqState st = spin_lock_irqsave(&g_lock);
    strcpy(g_want, path);
    spin_unlock_irqrestore(&g_lock, st);
    kick();
}

bool WallpaperChanged(void)
{
    return __atomic_exchange_n(&g_changed, false, __ATOMIC_ACQ_REL);
}

/* Take a picture the thread finished (desktop thread) */
static void adopt(void)
{
    IrqState st = spin_lock_irqsave(&g_lock);
    UINT32 *px = g_new ? g_new_px : NULL, *old = NULL;
    if (g_new) {
        old = g_px;
        g_px = g_new_px; g_w = g_new_w; g_h = g_new_h;
        g_new = false;
        g_new_px = NULL;
    }
    spin_unlock_irqrestore(&g_lock, st);
    if (px) kfree(old);
}

/* @px (w x h) filling @r, its middle cut to @r's shape */
static void draw_cover(GdiRect r, const UINT32 *px, int w, int h)
{
    int dw = r.w, dh = r.h;
    if ((INT64)w * r.h > (INT64)h * r.w) dw = (int)((INT64)w * r.h / h);
    else                                 dh = (int)((INT64)h * r.w / w);
    GdiRect old = GdiClipRect();
    int x0 = r.x > old.x ? r.x : old.x, y0 = r.y > old.y ? r.y : old.y;
    int x1 = r.x + r.w < old.x + old.w ? r.x + r.w : old.x + old.w;
    int y1 = r.y + r.h < old.y + old.h ? r.y + r.h : old.y + old.h;
    if (x1 <= x0 || y1 <= y0) return;
    GdiSetClip(RECT(x0, y0, x1 - x0, y1 - y0));
    GdiDrawImage(RECT(r.x - (dw - r.w) / 2, r.y - (dh - r.h) / 2, dw, dh), px, w, h);
    GdiSetClip(old);
}

bool WallpaperDraw(GdiRect r)
{
    adopt();
    if (!g_px || r.w <= 0 || r.h <= 0) return false;
    draw_cover(r, g_px, g_w, g_h);
    return true;
}

/* -----------------------------------------------------------------------
 * The folder, and pictures to add to it
 * ----------------------------------------------------------------------- */
static int add_sorted(RamNode **out, int n, int max, RamNode *f)
{
    if (n >= max) return n;
    int i = n;
    while (i > 0 && strcmp(out[i - 1]->name, f->name) > 0) { out[i] = out[i - 1]; i--; }
    out[i] = f;
    return n + 1;
}

int WallpaperList(RamNode **out, int max)
{
    RamNode *d = RamfsResolve(NULL, WALLPAPER_DIR);
    int n = 0;
    if (d && d->dir)
        for (RamNode *c = d->child; c; c = c->next)
            if (is_png(c)) n = add_sorted(out, n, max, c);
    return n;
}

static bool installed(RamNode *wall, const char *name)
{
    return wall && RamfsFind(wall, name) != NULL;
}

int WallpaperCandidates(RamNode **out, int max)
{
    static const char *const places[] = { "\\Downloads", "\\Pictures", "\\Documents", "\\Desktop" };
    RamNode *wall = RamfsResolve(NULL, WALLPAPER_DIR);
    int n = 0;
    for (int p = 0; p < 4; p++) {
        RamNode *d = RamfsResolve(NULL, places[p]);
        if (!d || !d->dir) continue;
        for (RamNode *c = d->child; c && n < max; c = c->next) {
            if (is_png(c) && !installed(wall, c->name)) out[n++] = c;
            if (!c->dir || IconSetIsSet(c)) continue;         /* (an icon set's pictures are icons) */
            for (RamNode *g = c->child; g && n < max; g = g->next)       /* one folder down */
                if (is_png(g) && !installed(wall, g->name)) out[n++] = g;
        }
    }
    return n;
}

bool WallpaperInstall(RamNode *src, char *path, int cap)
{
    if (!src || src->dir || !RamfsLoad(src) || (!src->data && src->size)) return false;
    RamNode *web = RamfsCreate(RamfsCreate(RamfsRoot(), "Windows", true), "Web", true);
    RamNode *wall = web ? RamfsCreate(web, "Wallpaper", true) : NULL;
    if (!wall || installed(wall, src->name)) return false;
    RamNode *f = RamfsCreate(wall, src->name, false);
    if (!f || !RamfsWrite(f, src->data, src->size)) {
        if (f) RamfsDelete(f);
        return false;
    }
    ksnprintf(path, cap, "%s\\%s", WALLPAPER_DIR, f->name);
    kprintf("[WALLPAPER] Added %s\n", path);
    return true;
}

/* -----------------------------------------------------------------------
 * Thumbnails
 * ----------------------------------------------------------------------- */
bool WallpaperDrawThumb(RamNode *f, GdiRect r)
{
    char path[RAMFS_PATH_MAX];
    RamfsPath(f, path, sizeof(path));             /* "C:\..." */
    const char *p = path[1] == ':' ? path + 2 : path;
    UINT32 *px = NULL, *stale = NULL;
    bool queued = false;
    IrqState st = spin_lock_irqsave(&g_lock);
    int i;
    for (i = 0; i < g_nthumbs; i++) if (!strcmp(g_thumbs[i].path, p)) break;
    if (i < g_nthumbs && (g_thumbs[i].size != f->size || g_thumbs[i].mtime != f->mtime) &&
        (g_thumbs[i].px || g_thumbs[i].failed)) {
        /* (the file changed: make it again; only the desktop thread, this
         * one, draws thumbnails) */
        stale = g_thumbs[i].px;
        g_thumbs[i].px = NULL;
        g_thumbs[i].failed = false;
        g_thumbs[i].size = f->size;
        g_thumbs[i].mtime = f->mtime;
        queued = true;
    } else if (i == g_nthumbs && g_nthumbs < THUMBS_MAX) {
        Thumb *t = &g_thumbs[g_nthumbs++];
        strncpy(t->path, p, sizeof(t->path) - 1);
        t->size = f->size;
        t->mtime = f->mtime;
        queued = true;
    }
    if (i < g_nthumbs) px = g_thumbs[i].px;
    spin_unlock_irqrestore(&g_lock, st);
    kfree(stale);
    if (queued) kick();
    if (!px) return false;
    draw_cover(r, px, THUMB_W, THUMB_H);
    return true;
}

UINT32 WallpaperThumbsGen(void) { return __atomic_load_n(&g_thumbs_gen, __ATOMIC_ACQUIRE); }
