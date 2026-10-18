/*
 * settings.c — Settings: live system, display, sound, personalization,
 *               storage, network, accounts, time & language and about pages
 */

#include "apps.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../ke/version.h"
#include "../ke/printf.h"
#include "../ke/timezone.h"
#include "../net/net.h"
#include "../wm/desktop.h"
#include "../wm/kbdlayout.h"
#include "../fs/persist.h"
#include "../hal/display.h"
#include "../um/um.h"
#include "../drivers/audio.h"
#include "../wm/wallpaper.h"
#include "iconset.h"

#define SIDE_W 200
#define ITEM_H 36

/* Personalization's clickable parts, recorded as they are drawn (client
 * coordinates) */
typedef enum { PH_TAB, PH_THEME, PH_PICTURE, PH_ADD_PICTURE, PH_ICONSET, PH_ADD_ICONSET,
               /* (and Accounts': i is the account, field or choice) */
               AH_ADD, AH_PASSWORD, AH_PICTURE, AH_TYPE, AH_REMOVE, AH_FIELD, AH_CHIP, AH_OK, AH_CANCEL,
               AH_PIC, AH_PIC_NONE } PHitKind;
typedef struct {
    GdiRect  r;
    PHitKind kind;
    int      i;                      /* the tab or theme */
    char     path[RAMFS_PATH_MAX];   /* the picture, folder or set (C: path without "C:") */
} PHit;
#define PHITS_MAX 48

typedef struct {
    int page; UINT32 net_sig;
    int mon;                         /* Display page: the monitor chosen */
    bool drag, moved;                /* a monitor being dragged in the arrangement */
    int drag_dx, drag_dy;            /* the press inside it (diagram px) */
    int drag_x, drag_y;              /* its top left now (diagram px) */
    char locale[32];                 /* Time & language: the user locale shown */
    int tz;                          /* ... and the time zone */
    int kbd;                         /* ... and the keyboard layout */
    UINT32 sound_sig;                /* Sound: the devices shown (repainted when they change) */
    bool net_edit, net_manual;       /* Network: editing the IP settings; Manual chosen */
    int net_focus;                   /* ... the field typed into */
    char net_text[5][16];            /* ... address, mask, gateway, two DNS servers */
    char net_err[96];                /* ... why Save was refused */
    int ptab;                        /* Personalization: Background (0) or Icons (1) */
    int pscroll, pheight;            /* ... scrolled by, and the height of what it shows */
    UINT32 thumbs_gen;               /* ... the wallpaper thumbnails shown (repainted when more are ready) */
    int nhits;                       /* ... what a click there does (set_paint records it) */
    PHit hits[PHITS_MAX];
    int acc_form;                    /* Accounts: the form open (ACC_*), or 0 */
    UINT32 acc_rid;                  /* ... the account it is about */
    int acc_focus;                   /* ... the field typed into */
    bool acc_admin;                  /* ... the account type chosen */
    char acc_text[3][UM_PASSWORD_MAX + 1];   /* ... its fields (wiped when it closes) */
    char acc_err[96];                /* ... why it was refused */
    char acc_note[96];               /* ... what the last change did */
} Settings;

static const char *g_pages[] = { "System", "Display", "Sound", "Personalization", "Storage", "Network", "Accounts", "Time & language", "About" };
static const Glyph g_page_glyphs[] = { GL_PC, GL_WINDOWS, GL_SPEAKER, GL_PICTURES, GL_FOLDER, GL_NETWORK, GL_PERSON, GL_GEAR, GL_NOVA };
#define N_PAGES ((int)(sizeof(g_pages) / sizeof(g_pages[0])))

/* A labelled row inside a card: "Label ........ value" */
static void row(int x, int y, int w, const char *label, const char *value)
{
    GdiRoundRect(RECT(x, y, w, 44), 6, UI_CARD, GDI_TRANSPARENT);
    GdiTextT(x + 16, y + 14, label, UI_TEXT);
    GdiTextT(x + w - 16 - GdiTextW(value), y + 14, value, UI_TEXT2);
}

static void bar(int x, int y, int w, UINT64 used, UINT64 total)
{
    GdiRoundRect(RECT(x, y, w, 8), 4, UI_HOVER, GDI_TRANSPARENT);
    int fw = total ? (int)((UINT64)w * used / total) : 0;
    if (fw > 0) GdiRoundRect(RECT(x, y, fw < 8 ? 8 : fw, 8), 4, UI_ACCENT, GDI_TRANSPARENT);
}

static void ramdisk_usage(RamNode *d, UINT64 *bytes, int *files)
{
    for (RamNode *c = d->child; c; c = c->next) {
        if (c->dir) ramdisk_usage(c, bytes, files);
        else { *bytes += c->size; (*files)++; }
    }
}

static void page_system(int x, int y, int w)
{
    char cpu[64], up[32], mem[48];
    AppCpuName(cpu, sizeof(cpu));
    AppUptime(up, sizeof(up));
    uint64_t total, free_p, used;
    pmm_stats(&total, &free_p, &used);
    ksnprintf(mem, sizeof(mem), "%u MB (%u MB in use)",
              (unsigned)(total * 4 / 1024), (unsigned)(used * 4 / 1024));

    GdiRoundRect(RECT(x, y, w, 96), 8, UI_CARD, GDI_TRANSPARENT);
    GdiRoundGradV(RECT(x + 20, y + 20, 88, 56), 6, GDI_C(0x3A, 0x8A, 0xF0), GDI_C(0x2A, 0xC8, 0xC8));
    GdiTextLarge(x + 128, y + 20, "NOVA-PC", UI_TEXT);
    char ver[64];
    ksnprintf(ver, sizeof(ver), "NovaOS %s  -  desktop", NovaVersion());
    GdiTextT(x + 128, y + 54, ver, UI_TEXT2);
    y += 112;
    row(x, y, w, "Processor", cpu);           y += 50;
    row(x, y, w, "Installed memory", mem);    y += 50;
    row(x, y, w, "System type", "64-bit operating system, x64 processor"); y += 50;
    row(x, y, w, "Uptime", up);
}

/* Display page: with more than one monitor, an arrangement of them on top
 * (drag one to move it; click one to choose it), then the chosen one's
 * resolution buttons (chips), modes_dy() below the page's top; set_mouse
 * finds them with the same numbers */
#define CHIP_W   116
#define CHIP_H   32
#define CHIP_GAP 8
#define ARR_H    150
#define ARR_DY   26
#define SNAP     48                  /* logical px: edges this close line up */

static bool multi(void) { return GdiMonitorCount() > 1; }

static int modes_dy(void) { return multi() ? ARR_DY + ARR_H + 16 + 26 + 50 + 26 : 26 + 50 + 50 + 26; }

static int chip_cols(int w);
/* The "DPI-aware apps" chips (96 and 192 DPI), on a monitor at scale 2:
 * their top, below the resolution chips (page coordinates) */
static int dpi_chips_dy(int mon, int w)
{
    int cols = chip_cols(w), n = DisplayHeadModeCount(mon);
    return modes_dy() + ((n + cols - 1) / cols) * (CHIP_H + CHIP_GAP) + 18 + 26;
}

static int chip_cols(int w)
{
    int n = (w + CHIP_GAP) / (CHIP_W + CHIP_GAP);
    return n < 1 ? 1 : n;
}

static int chosen(Settings *st) { return st->mon >= 0 && st->mon < GdiMonitorCount() ? st->mon : 0; }

/* The arrangement: the virtual desktop scaled into the card at @top (page
 * coordinates x, w), with room around for a monitor to be dragged */
typedef struct { int ox, oy, num, den; GdiRect v; } Arr;

static Arr arr_of(int x, int top, int w)
{
    Arr a;
    a.v = GdiVirtualRect();
    GdiRect v = a.v;
    int bw = v.w + 2 * 1280 / 2, bh = v.h + 400;             /* the desktop and a margin */
    a.num = 1; a.den = 1;
    int aw = w - 32, ah = ARR_H - 24;
    if ((INT64)bw * ah > (INT64)bh * aw) { a.num = aw; a.den = bw; } else { a.num = ah; a.den = bh; }
    a.ox = x + 16 + (aw - v.w * a.num / a.den) / 2 - v.x * a.num / a.den;
    a.oy = top + 12 + (ah - v.h * a.num / a.den) / 2 - v.y * a.num / a.den;
    return a;
}

static GdiRect arr_rect(const Arr *a, GdiRect m)
{
    return RECT(a->ox + m.x * a->num / a->den, a->oy + m.y * a->num / a->den,
                m.w * a->num / a->den, m.h * a->num / a->den);
}

static void page_display(Settings *st, int x, int y, int w)
{
    char res[96], scale[48], s1[32];
    int mon = chosen(st);
    DisplayMode cur = DisplayHeadMode(mon);
    int top = y;
    if (multi()) {
        GdiTextBold(x, y, "Arrange displays", UI_TEXT);
        GdiRoundRect(RECT(x, y + ARR_DY, w, ARR_H), 8, UI_CARD, GDI_TRANSPARENT);
        Arr a = arr_of(x, y + ARR_DY, w);
        for (int i = 0; i < GdiMonitorCount(); i++) {
            GdiRect r = arr_rect(&a, GdiMonitorRect(i));
            if (st->drag && i == mon) { r.x = st->drag_x; r.y = st->drag_y; }
            GdiRoundRect(RECT(r.x + 1, r.y + 1, r.w - 2, r.h - 2), 4, i == mon ? UI_ACCENT : UI_HOVER,
                         i == mon ? GDI_WHITE : UI_LINE);
            ksnprintf(s1, sizeof(s1), "%d", i + 1);
            GdiTextLarge(r.x + (r.w - GdiTextLargeW(s1)) / 2, r.y + r.h / 2 - 15, s1, i == mon ? GDI_WHITE : UI_TEXT);
        }
        y += ARR_DY + ARR_H + 16;
        GdiRect m = GdiMonitorRect(mon);
        ksnprintf(s1, sizeof(s1), "Display %d%s", mon + 1, mon == 0 ? " (main display)" : "");
        GdiTextBold(x, y, s1, UI_TEXT);              y += 26;
        ksnprintf(res, sizeof(res), "%d x %d, %d%%, at (%d, %d) - %s", cur.w, cur.h, GdiMonitorScale(mon) * 100,
                  m.x, m.y, DisplayHeadName(mon) ? DisplayHeadName(mon) : "");
        row(x, y, w, "Resolution", res);
    } else {
        int s = GdiScale();
        ksnprintf(res, sizeof(res), "%d x %d", cur.w, cur.h);
        ksnprintf(scale, sizeof(scale), "%d%% (desktop %d x %d)", s * 100, GdiScreenW(), GdiScreenH());
        GdiTextBold(x, y, "Scale & layout", UI_TEXT);   y += 26;
        row(x, y, w, "Display resolution", res);         y += 50;
        row(x, y, w, "Scale", scale);
    }

    GdiTextBold(x, top + modes_dy() - 26, "Resolution", UI_TEXT);
    y = top + modes_dy();
    int cols = chip_cols(w), n = DisplayHeadModeCount(mon);
    for (int i = 0; i < n; i++) {
        DisplayMode m;
        DisplayHeadModeAt(mon, i, &m);
        int cx = x + (i % cols) * (CHIP_W + CHIP_GAP), cy = y + (i / cols) * (CHIP_H + CHIP_GAP);
        bool on = m.w == cur.w && m.h == cur.h;
        GdiRoundRect(RECT(cx, cy, CHIP_W, CHIP_H), 6, on ? UI_ACCENT : UI_CARD, GDI_TRANSPARENT);
        ksnprintf(s1, sizeof(s1), "%d x %d", m.w, m.h);
        GdiTextCenter(cx, cy + 8, CHIP_W, s1, on ? GDI_WHITE : UI_TEXT);
    }
    y += ((n + cols - 1) / cols) * (CHIP_H + CHIP_GAP) + 18;

    if (GdiMonitorScale(mon) >= 2) {                 /* what DPI-aware programs see (user32's dpi.c) */
        GdiTextBold(x, y, "DPI for DPI-aware apps", UI_TEXT);
        y = top + dpi_chips_dy(mon, w);
        for (int i = 0; i < 2; i++) {
            int cx = x + i * (CHIP_W + CHIP_GAP);
            bool on = GdiMonitorDpi(mon) == (i ? 192 : 96);
            GdiRoundRect(RECT(cx, y, CHIP_W, CHIP_H), 6, on ? UI_ACCENT : UI_CARD, GDI_TRANSPARENT);
            GdiTextCenter(cx, y + 8, CHIP_W, i ? "192 (200%)" : "96 (100%)", on ? GDI_WHITE : UI_TEXT);
        }
        y += CHIP_H + CHIP_GAP + 18;
    }

    GdiTextBold(x, y, "Display adapter", UI_TEXT);   y += 26;
    row(x, y, w, "Driver", DisplayDriverName());     y += 50;
    if (DisplayAdapterName()) { row(x, y, w, "Adapter", DisplayAdapterName()); y += 50; }
    row(x, y, w, "Presentation", DisplayCanFlip() ? "Back buffer, page flipping" : "Back buffer, copied to the screen");
}

/* Where a monitor dropped with its top left at (x, y) goes: touching
 * another monitor along the nearest edge, overlapping none, and lined up
 * with an edge of it when that is close */
static void snap_place(int mon, int *px, int *py)
{
    GdiRect me = GdiMonitorRect(mon);
    int bx = *px, by = *py;
    INT64 best = -1;
    for (int j = 0; j < GdiMonitorCount(); j++) {
        if (j == mon) continue;
        GdiRect o = GdiMonitorRect(j);
        for (int side = 0; side < 4; side++) {
            int x = *px, y = *py;
            if (side < 2) {                          /* left or right of it */
                x = side == 0 ? o.x - me.w : o.x + o.w;
                if (y < o.y - me.h + 1) y = o.y - me.h + 1;
                if (y > o.y + o.h - 1) y = o.y + o.h - 1;
                if (y - o.y < SNAP && y - o.y > -SNAP) y = o.y;
                else if (y + me.h - (o.y + o.h) < SNAP && y + me.h - (o.y + o.h) > -SNAP) y = o.y + o.h - me.h;
            } else {                                 /* above or below it */
                y = side == 2 ? o.y - me.h : o.y + o.h;
                if (x < o.x - me.w + 1) x = o.x - me.w + 1;
                if (x > o.x + o.w - 1) x = o.x + o.w - 1;
                if (x - o.x < SNAP && x - o.x > -SNAP) x = o.x;
                else if (x + me.w - (o.x + o.w) < SNAP && x + me.w - (o.x + o.w) > -SNAP) x = o.x + o.w - me.w;
            }
            bool clash = false;
            for (int k = 0; k < GdiMonitorCount() && !clash; k++) {
                GdiRect q = GdiMonitorRect(k);
                if (k != mon && x < q.x + q.w && q.x < x + me.w && y < q.y + q.h && q.y < y + me.h) clash = true;
            }
            if (clash) continue;
            INT64 d = (INT64)(x - *px) * (x - *px) + (INT64)(y - *py) * (y - *py);
            if (best < 0 || d < best) { best = d; bx = x; by = y; }
        }
    }
    *px = bx; *py = by;
}

/* Sound: the outputs, then the inputs, one row each (the default one's
 * circle filled); clicking a row makes that device the default, as
 * Windows' "Choose where to play sound" does.  Programs that chose a
 * device keep it.  Each row has the device's own volume slider at its
 * right (clicking the slider sets that device's level, as each device's
 * slider in Windows' Sound settings does).  The choice and the levels are
 * kept across restarts (drivers/audio.c).  Rows are SOUND_ROW apart;
 * sound_rows() places them for set_paint and set_mouse alike. */
#define SOUND_ROW    50
#define SOUND_HEAD   26
#define SOUND_GAP    16
#define SOUND_SLIDER 160                 /* the slider's track, ending 72 px from the row's right */

static int slider_x(int x, int w) { return x + w - 72 - SOUND_SLIDER; }

typedef struct { AudioDevice out[8], in[8]; int nout, nin; } SoundDevs;

static void sound_devs(SoundDevs *d)
{
    d->nout = AudioDevices(false, d->out, 8);
    d->nin = AudioDevices(true, d->in, 8);
}

/* The top of the input heading, below the outputs (page coordinates) */
static int sound_in_dy(const SoundDevs *d)
{
    return SOUND_HEAD + (d->nout ? d->nout : 1) * SOUND_ROW + SOUND_GAP;
}

static UINT32 sound_sig(const SoundDevs *d)
{
    UINT32 sig = (UINT32)(d->nout * 31 + d->nin);
    for (int i = 0; i < d->nout; i++)
        sig = sig * 131 + d->out[i].id * 2 + d->out[i].is_default + d->out[i].volume * 7 + d->out[i].mute;
    for (int i = 0; i < d->nin; i++)
        sig = sig * 137 + d->in[i].id * 2 + d->in[i].is_default + d->in[i].volume * 7 + d->in[i].mute;
    return sig;
}

static void sound_row(int x, int y, int w, const AudioDevice *a)
{
    GdiRoundRect(RECT(x, y, w, 44), 6, UI_CARD, GDI_TRANSPARENT);
    GdiRoundRect(RECT(x + 16, y + 14, 16, 16), 8, a->is_default ? UI_ACCENT : UI_LINE, GDI_TRANSPARENT);
    GdiRoundRect(RECT(x + 18, y + 16, 12, 12), 6, UI_CARD, GDI_TRANSPARENT);
    if (a->is_default) GdiRoundRect(RECT(x + 20, y + 18, 8, 8), 4, UI_ACCENT, GDI_TRANSPARENT);
    GdiTextT(x + 44, y + 14, a->name, UI_TEXT);
    int sx = slider_x(x, w), fill = (int)((UINT64)SOUND_SLIDER * a->volume / 65536);
    int lx = sx - 20 - GdiTextW("Default");                   /* (left out where a long name reaches it) */
    if (a->is_default && lx >= x + 44 + GdiTextW(a->name) + 16) GdiTextT(lx, y + 14, "Default", UI_TEXT2);
    GdiRoundRect(RECT(sx, y + 20, SOUND_SLIDER, 4), 2, UI_LINE, GDI_TRANSPARENT);
    if (fill > 0) GdiRoundRect(RECT(sx, y + 20, fill, 4), 2, a->mute ? UI_TEXT2 : UI_ACCENT, GDI_TRANSPARENT);
    GdiRoundRect(RECT(sx + fill - 7, y + 15, 14, 14), 7, a->mute ? UI_TEXT2 : UI_ACCENT, GDI_TRANSPARENT);
    char pct[16];
    if (a->mute) strcpy(pct, "Muted");
    else ksnprintf(pct, sizeof(pct), "%u%%", (unsigned)((a->volume * 100ull + 32768) / 65536));
    GdiTextT(x + w - 16 - GdiTextW(pct), y + 14, pct, UI_TEXT2);
}

/* A click at @dx on a row of device @a: on its slider, set its volume
 * there; elsewhere, make it the default */
static void sound_row_click(bool capture, const AudioDevice *a, int dx, int w)
{
    int sx = slider_x(0, w);
    if (dx >= sx - 8 && dx <= sx + SOUND_SLIDER + 8) {
        int v = dx - sx;
        v = v < 0 ? 0 : v > SOUND_SLIDER ? SOUND_SLIDER : v;
        UINT32 vol = (UINT32)((UINT64)v * 65536 / SOUND_SLIDER);
        AudioSetMaster(capture, a->id, vol, vol, false);
        kprintf("[SETTINGS] %s volume %u%%\n", a->name, (unsigned)((vol * 100ull + 32768) / 65536));
        return;
    }
    AudioSetDefault(capture, a->id);
}

static void page_sound(Settings *st, int x, int y, int w)
{
    SoundDevs d;
    sound_devs(&d);
    st->sound_sig = sound_sig(&d);
    GdiTextBold(x, y, "Output: choose where to play sound", UI_TEXT);
    for (int i = 0; i < d.nout; i++) sound_row(x, y + SOUND_HEAD + i * SOUND_ROW, w, &d.out[i]);
    if (!d.nout) row(x, y + SOUND_HEAD, w, "No output devices found", "");
    int iy = y + sound_in_dy(&d);
    GdiTextBold(x, iy, "Input: choose a device for speaking or recording", UI_TEXT);
    for (int i = 0; i < d.nin; i++) sound_row(x, iy + SOUND_HEAD + i * SOUND_ROW, w, &d.in[i]);
    if (!d.nin) row(x, iy + SOUND_HEAD, w, "No input devices found", "");
}

/* A click on the Sound page at page coordinates (@dx, @dy) below its top */
static void sound_click(int dx, int dy, int w)
{
    SoundDevs d;
    sound_devs(&d);
    if (dx < 0 || dx >= w) return;
    int r = (dy - SOUND_HEAD) / SOUND_ROW;
    if (dy >= SOUND_HEAD && (dy - SOUND_HEAD) % SOUND_ROW < 44 && r < d.nout) {
        sound_row_click(false, &d.out[r], dx, w);
        return;
    }
    int iy = dy - sound_in_dy(&d) - SOUND_HEAD;
    r = iy / SOUND_ROW;
    if (iy >= 0 && iy % SOUND_ROW < 44 && r < d.nin) sound_row_click(true, &d.in[r], dx, w);
}

static void page_storage(int x, int y, int w)
{
    uint64_t total, free_p, used;
    pmm_stats(&total, &free_p, &used);
    UINT64 bytes = 0;
    int files = 0;
    ramdisk_usage(RamfsRoot(), &bytes, &files);
    char a[64], b[64];

    GdiRoundRect(RECT(x, y, w, 92), 8, UI_CARD, GDI_TRANSPARENT);
    GdiTextBold(x + 16, y + 14, "Memory (RAM)", UI_TEXT);
    ksnprintf(a, sizeof(a), "%u MB used of %u MB", (unsigned)(used * 4 / 1024),
              (unsigned)(total * 4 / 1024));
    GdiTextT(x + 16, y + 38, a, UI_TEXT2);
    bar(x + 16, y + 66, w - 32, used, total);
    y += 108;

    GdiRoundRect(RECT(x, y, w, 92), 8, UI_CARD, GDI_TRANSPARENT);
    GdiTextBold(x + 16, y + 14, "Local Disk (C:)", UI_TEXT);
    AppFormatSize(bytes, b, sizeof(b));
    char where[40];
    PersistWhere(where, sizeof(where));
    if (PersistActive())
        ksnprintf(a, sizeof(a), "%s in %d file%s; saved to disk %s", b, files, files == 1 ? "" : "s", where);
    else
        ksnprintf(a, sizeof(a), "%s in %d file%s; no disk found, reset on reboot", b, files, files == 1 ? "" : "s");
    GdiTextT(x + 16, y + 38, a, UI_TEXT2);
    UINT64 dfree, dtotal;
    if (PersistSpace(&dfree, &dtotal)) bar(x + 16, y + 66, w - 32, dtotal - dfree, dtotal);   /* the disk's use */
    else bar(x + 16, y + 66, w - 32, bytes, bytes + 1024 * 1024);
}

/* Network: the Edit button on the Ethernet card, and the IP settings form
 * (layout shared by set_paint + set_mouse) */
#define NET_FIELDS  5
#define NET_CHIP_W  168
#define NET_BOX_W   200
static const char *const g_net_labels[NET_FIELDS] = {
    "IP address", "Subnet mask", "Gateway", "Preferred DNS", "Alternate DNS"
};
static GdiRect r_net_edit(int x, int y, int w) { return RECT(x + w - 16 - 96, y + 20, 96, 32); }
static GdiRect r_net_chip(int x, int y, int i) { return RECT(x + i * (NET_CHIP_W + CHIP_GAP), y + 88 + 26, NET_CHIP_W, CHIP_H); }
/* field @f's row (manual: all five; automatic: the two DNS servers only) */
static bool net_shown(const Settings *st, int f) { return st->net_manual || f >= 3; }
static int net_row_y(const Settings *st, int y, int f)
{
    int r = 0;
    for (int i = 0; i < f; i++) r += net_shown(st, i);
    return y + 88 + 26 + CHIP_H + 16 + r * 50;
}
static GdiRect r_net_box(const Settings *st, int x, int y, int w, int f)
{
    return RECT(x + w - 16 - NET_BOX_W, net_row_y(st, y, f) + 6, NET_BOX_W, 32);
}
static int net_end_y(const Settings *st, int y) { return net_row_y(st, y, NET_FIELDS); }
static GdiRect r_net_save(const Settings *st, int x, int y)   { return RECT(x, net_end_y(st, y) + 28, 100, 32); }
static GdiRect r_net_cancel(const Settings *st, int x, int y) { return RECT(x + 112, net_end_y(st, y) + 28, 100, 32); }

static void net_form(Settings *st, int x, int y, int w)
{
    GdiTextBold(x, y + 88, "IP assignment", UI_TEXT);
    for (int i = 0; i < 2; i++) {
        GdiRect r = r_net_chip(x, y, i);
        bool on = st->net_manual == (i == 1);
        GdiRoundRect(r, 6, on ? UI_ACCENT : UI_CARD, GDI_TRANSPARENT);
        GdiTextCenter(r.x, r.y + 8, r.w, i ? "Manual" : "Automatic (DHCP)", on ? GDI_WHITE : UI_TEXT);
    }
    for (int f = 0; f < NET_FIELDS; f++) {
        if (!net_shown(st, f)) continue;
        int ry = net_row_y(st, y, f);
        GdiRoundRect(RECT(x, ry, w, 44), 6, UI_CARD, GDI_TRANSPARENT);
        GdiTextT(x + 16, ry + 14, g_net_labels[f], UI_TEXT);
        GdiRect b = r_net_box(st, x, y, w, f);
        GdiRoundRect(b, 6, UI_BG, GDI_TRANSPARENT);
        GdiRoundBorderAlpha(b, 6, f == st->net_focus ? UI_ACCENT : GDI_WHITE, f == st->net_focus ? 200 : 30);
        GdiTextT(b.x + 10, b.y + (b.h - GDI_FONT_H) / 2, st->net_text[f], UI_TEXT);
        if (f == st->net_focus)                                            /* the caret */
            GdiFillRect(RECT(b.x + 10 + GdiTextW(st->net_text[f]) + 1, b.y + 7, 2, b.h - 14), UI_ACCENT);
    }
    int ey = net_end_y(st, y);
    GdiTextT(x, ey, st->net_manual ? "Leave the DNS servers blank for none." :
                                     "Leave the DNS servers blank to get them from DHCP.", UI_TEXT2);
    UiButton(r_net_save(st, x, y), "Save", true);
    UiButton(r_net_cancel(st, x, y), "Cancel", false);
    if (st->net_err[0]) GdiTextT(x + 228, ey + 36, st->net_err, GDI_C(0xE0, 0x60, 0x60));
}

static void net_begin_edit(Settings *st)
{
    NetStatus ns;
    NetConfig c;
    NetGetStatus(&ns);
    NetGetConfig(&c);
    memset(st->net_text, 0, sizeof(st->net_text));
    st->net_manual = c.manual_ip;
    UINT32 ip = c.manual_ip ? c.ip : ns.configured ? ns.ip : 0;      /* (start from what is in use) */
    UINT32 v[NET_FIELDS] = { ip, c.manual_ip ? c.mask : ns.configured ? ns.mask : 0,
                             c.manual_ip ? c.gw : ns.configured ? ns.gw : 0,
                             c.manual_dns ? c.dns[0] : 0, c.manual_dns ? c.dns[1] : 0 };
    for (int f = 0; f < NET_FIELDS; f++)
        if (v[f]) NetFormatIp(v[f], st->net_text[f], sizeof(st->net_text[f]));
    st->net_focus = st->net_manual ? 0 : 3;
    st->net_err[0] = '\0';
    st->net_edit = true;
}

static void net_save(Settings *st)
{
    NetConfig c = { 0 };
    UINT32 v[NET_FIELDS] = { 0 };
    for (int f = 0; f < NET_FIELDS; f++) {
        if (!net_shown(st, f) || !st->net_text[f][0]) continue;
        if (!NetParseIp(st->net_text[f], &v[f])) {
            ksnprintf(st->net_err, sizeof(st->net_err), "The %s is not a valid IPv4 address.", g_net_labels[f]);
            st->net_focus = f;
            return;
        }
    }
    c.manual_ip = st->net_manual;
    c.ip = v[0]; c.mask = v[1]; c.gw = v[2];
    c.dns[0] = v[3]; c.dns[1] = v[4];
    c.manual_dns = v[3] || v[4] || c.manual_ip;
    if (NetSetConfig(&c, st->net_err, sizeof(st->net_err))) st->net_edit = false;
}

static bool net_mouse(Settings *st, int x, int y, int w, int mx, int my)
{
    if (!st->net_edit) {
        NetStatus ns;
        NetGetStatus(&ns);
        if (ns.present && UiHit(r_net_edit(x, y, w), mx, my)) { net_begin_edit(st); return true; }
        return false;
    }
    for (int i = 0; i < 2; i++)
        if (UiHit(r_net_chip(x, y, i), mx, my)) {
            st->net_manual = i == 1;
            if (!net_shown(st, st->net_focus)) st->net_focus = 3;
            return true;
        }
    for (int f = 0; f < NET_FIELDS; f++)
        if (net_shown(st, f) && UiHit(r_net_box(st, x, y, w, f), mx, my)) { st->net_focus = f; return true; }
    if (UiHit(r_net_save(st, x, y), mx, my)) { net_save(st); return true; }
    if (UiHit(r_net_cancel(st, x, y), mx, my)) { st->net_edit = false; return true; }
    return false;
}

static void net_key(Settings *st, const KeyEvent *k)
{
    if (k->scancode == KEY_ESC) { st->net_edit = false; return; }
    if (k->scancode == KEY_ENTER && !k->extended) { net_save(st); return; }
    if (k->scancode == KEY_TAB || (k->extended && (k->scancode == KEY_UP || k->scancode == KEY_DOWN))) {
        int d = (k->scancode == KEY_UP || (k->scancode == KEY_TAB && k->shift)) ? -1 : 1, f = st->net_focus;
        do f = (f + d + NET_FIELDS) % NET_FIELDS; while (!net_shown(st, f));
        st->net_focus = f;
        return;
    }
    char *t = st->net_text[st->net_focus];
    size_t n = strlen(t);
    if (k->ch == '\b') { if (n) t[n - 1] = '\0'; }
    else if (!k->ctrl && !k->alt && ((k->ch >= '0' && k->ch <= '9') || k->ch == '.') && n < sizeof(st->net_text[0]) - 1) {
        t[n] = k->ch;
        t[n + 1] = '\0';
    }
}

static void page_network(Settings *st, int x, int y, int w)
{
    NetStatus ns;
    NetGetStatus(&ns);
    char mac[24], ip[20], mask[20], gw[20], dns[48], d2[20];
    int top = y;

    GdiRoundRect(RECT(x, y, w, 72), 8, UI_CARD, GDI_TRANSPARENT);
    GdiTextBold(x + 16, y + 14, "Ethernet", UI_TEXT);
    const char *state = !ns.present ? "No network adapter found" :
                        !ns.link ? "Disconnected (no link)" :
                        !ns.configured && ns.dhcp6 ? "Connected (IPv6, DHCPv6)" :
                        !ns.configured ? "Connecting (waiting for DHCP)..." : "Connected";
    GdiColor dot = ns.configured || ns.dhcp6 ? GDI_C(0x2E, 0xB8, 0x5C) :
                   ns.link ? GDI_C(0xE8, 0xA8, 0x20) : GDI_C(0xD0, 0x40, 0x40);
    GdiRoundRect(RECT(x + 16, y + 44, 10, 10), 5, dot, GDI_TRANSPARENT);
    GdiTextT(x + 34, y + 40, state, UI_TEXT2);
    if (ns.present && !st->net_edit) UiButton(r_net_edit(x, y, w), "Edit", false);
    if (ns.present && st->net_edit) { net_form(st, x, top, w); return; }
    y += 88;
    if (!ns.present) return;

    ksnprintf(mac, sizeof(mac), "%02X-%02X-%02X-%02X-%02X-%02X", ns.mac[0], ns.mac[1],
              ns.mac[2], ns.mac[3], ns.mac[4], ns.mac[5]);
    row(x, y, w, "Adapter", ns.adapter);          y += 50;
    row(x, y, w, "Physical address (MAC)", mac);  y += 50;
    for (int i = 0; i < ns.ip6_count; i++) {                  /* the DHCPv6 lease (RFC 8415) */
        if (!ns.ip6_dhcp[i]) continue;
        NetIp a6 = { .v6 = true };
        char text[48];
        memcpy(a6.a, ns.ip6[i], 16);
        NetFormatAddr(&a6, text, sizeof(text));
        row(x, y, w, "IPv6 address (DHCPv6)", text); y += 50;
    }
    if (ns.dhcp6) {
        char lease[64];
        if (ns.dhcp6_valid == 0xFFFFFFFFu) strcpy(lease, "Does not expire");
        else {
            UINT32 left = ns.dhcp6_valid > ns.dhcp6_age ? ns.dhcp6_valid - ns.dhcp6_age : 0;
            ksnprintf(lease, sizeof(lease), "Expires in %u min %u s (renewed before then)", (unsigned)(left / 60), (unsigned)(left % 60));
        }
        row(x, y, w, "DHCPv6 lease", lease); y += 50;
    }
    if (ns.dns_suffix[0]) { row(x, y, w, "DNS suffix search list", ns.dns_suffix); y += 50; }
    if (!ns.configured) return;
    NetFormatIp(ns.ip, ip, sizeof(ip));
    NetFormatIp(ns.mask, mask, sizeof(mask));
    NetFormatIp(ns.gw, gw, sizeof(gw));
    if (ns.dns[0]) NetFormatIp(ns.dns[0], dns, sizeof(dns));
    else strcpy(dns, "None");
    if (ns.dns[1]) {
        NetFormatIp(ns.dns[1], d2, sizeof(d2));
        strcat(dns, ", ");
        strcat(dns, d2);
    }
    row(x, y, w, ns.manual_ip ? "IPv4 address (manual)" : "IPv4 address (DHCP)", ip);  y += 50;
    row(x, y, w, "Subnet mask", mask);            y += 50;
    row(x, y, w, "Default gateway", ns.gw ? gw : "None"); y += 50;
    row(x, y, w, ns.manual_dns ? "DNS servers (manual)" : "DNS servers", dns); y += 50;
    char roots[48];
    int imported, n = NetRootCount(&imported);
    if (imported) ksnprintf(roots, sizeof(roots), "%d (%d imported)", n, imported);
    else          ksnprintf(roots, sizeof(roots), "%d (Mozilla CA list)", n);
    row(x, y, w, "Trusted root certificates (HTTPS)", roots);
}

static void current_locale(char *out, int cap);

/* Repaint the Network page when the connection state changes (and Time &
 * language when the user locale does) */
static bool set_tick(WND *w)
{
    Settings *st = w->user;
    if (st && st->page == SETTINGS_TIME_LANGUAGE) {           /* intl.exe has written the choice */
        char cur[32];
        current_locale(cur, sizeof(cur));
        int tz = TzCurrent(), kbd = KbdCurrent();
        if (!strcmp(cur, st->locale) && tz == st->tz && kbd == st->kbd) return false;
        strcpy(st->locale, cur);
        st->tz = tz;
        st->kbd = kbd;
        return true;
    }
    if (st && (st->page == SETTINGS_PERSONALIZE || st->page == SETTINGS_ACCOUNTS)) {   /* wallpaper thumbnails made */
        UINT32 g = WallpaperThumbsGen();
        if (g == st->thumbs_gen) return false;
        st->thumbs_gen = g;
        return true;
    }
    if (st && st->page == SETTINGS_SOUND) {                   /* a device plugged in or out */
        SoundDevs d;
        sound_devs(&d);
        return sound_sig(&d) != st->sound_sig;
    }
    if (!st || st->page != SETTINGS_NETWORK) return false;
    NetStatus ns;
    NetGetStatus(&ns);
    UINT32 sig = ns.ip ^ ns.gw ^ ns.dns[0] ^ (ns.link ? 1u : 0) ^ (ns.configured ? 2u : 0) ^ (ns.dhcp6 ? 4u : 0) ^
                 ((UINT32)ns.ip6_count << 3) ^ (ns.dhcp6_valid - ns.dhcp6_age) / 60 << 6;
    if (sig == st->net_sig) return false;
    st->net_sig = sig;
    return true;
}

/* Personalization, in two tabs:
 *   Background: one card per wallpaper theme, the pictures in
 *     C:\Windows\Web\Wallpaper (choosing one draws it in the Aurora theme),
 *     and PNG pictures in the user's folders to add to them;
 *   Icons: the built-in icons and the icon sets in C:\Windows\Web\Icons,
 *     and folders of icons in the user's folders to add as sets.
 * The page scrolls with the wheel below its tabs; clicks go by st->hits. */
#define THEME_CARD_H 96
#define PIC_COLS     4             /* (at least; more on a wide window) */
#define ADD_ROW_H    44
#define SET_CARD_H   76
static const AppId g_preview_apps[] = { APP_EXPLORER, APP_TERMINAL, APP_SETTINGS, APP_FIREFOX, APP_PHOTOS, APP_NOTEPAD };

static void hit(Settings *st, GdiRect r, GdiRect c, PHitKind kind, int i, RamNode *node)
{
    if (st->nhits >= PHITS_MAX) return;
    PHit *h = &st->hits[st->nhits++];
    h->r = RECT(r.x - c.x, r.y - c.y, r.w, r.h);
    h->kind = kind;
    h->i = i;
    h->path[0] = '\0';
    if (node) {
        char p[RAMFS_PATH_MAX];
        RamfsPath(node, p, sizeof(p));
        strncpy(h->path, p[1] == ':' ? p + 2 : p, sizeof(h->path) - 1);
        h->path[sizeof(h->path) - 1] = '\0';
    }
}

/* A card's frame: the active one outlined in the accent colour */
static void card(GdiRect r, bool active)
{
    GdiRoundRect(r, 8, UI_CARD, active ? UI_ACCENT : GDI_TRANSPARENT);
    if (active) GdiRoundRect(RECT(r.x + 1, r.y + 1, r.w - 2, r.h - 2), 7, GDI_TRANSPARENT, UI_ACCENT);
}

/* "name ...... in Downloads  [Add]" */
static void add_row(Settings *st, GdiRect c, int x, int y, int w, RamNode *n, PHitKind kind)
{
    GdiRoundRect(RECT(x, y, w, ADD_ROW_H - 6), 6, UI_CARD, GDI_TRANSPARENT);
    char where[RAMFS_PATH_MAX];
    RamfsPath(n->parent, where, sizeof(where));
    GdiTextT(x + 16, y + 11, n->name, UI_TEXT);
    GdiRect b = RECT(x + w - 8 - 80, y + 4, 80, ADD_ROW_H - 14);
    int ww = GdiTextW(where);
    if (x + 16 + GdiTextW(n->name) + 24 < b.x - 16 - ww) GdiTextT(b.x - 16 - ww, y + 11, where, UI_TEXT2);
    UiButton(b, "Add", true);
    hit(st, b, c, kind, 0, n);
}

static int background_tab(Settings *st, GdiRect c, int x, int y, int w)
{
    int top = y;
    GdiTextBold(x, y, "Theme", UI_TEXT);
    y += 26;
    int n = DesktopThemeCount(), cw = (w - (n - 1) * 12) / n;
    const char *cur = WallpaperCurrent();
    for (int i = 0; i < n; i++) {
        GdiRect r = RECT(x + i * (cw + 12), y, cw, THEME_CARD_H + 30);
        card(r, i == DesktopTheme());
        DesktopDrawThemePreview(i, RECT(r.x + 6, r.y + 6, cw - 12, THEME_CARD_H - 12));
        GdiTextT(r.x + 10, r.y + THEME_CARD_H + 2, DesktopThemeName(i), UI_TEXT);
        hit(st, r, c, PH_THEME, i, NULL);
    }
    y += THEME_CARD_H + 30 + 22;

    GdiTextBold(x, y, "Picture", UI_TEXT);
    GdiTextT(x + 80, y, "The Aurora theme with your picture  -  C:\\Windows\\Web\\Wallpaper", UI_TEXT2);
    y += 26;
    RamNode *pics[64];
    int np = WallpaperList(pics, 64);
    int cols = w / 180 > PIC_COLS ? w / 180 : PIC_COLS;
    int pw = (w - (cols - 1) * 12) / cols, ph = pw * 9 / 16;
    for (int i = 0; i < np; i++) {
        GdiRect r = RECT(x + (i % cols) * (pw + 12), y + (i / cols) * (ph + 42), pw, ph + 30);
        char path[RAMFS_PATH_MAX];
        RamfsPath(pics[i], path, sizeof(path));
        bool active = DesktopAurora() && !strcmp(path + 2, cur);
        card(r, active);
        GdiRect t = RECT(r.x + 5, r.y + 5, pw - 10, ph - 6);
        if (!WallpaperDrawThumb(pics[i], t)) {
            GdiRoundRect(t, 4, UI_HOVER, GDI_TRANSPARENT);
            GdiTextCenter(t.x, t.y + t.h / 2 - 8, t.w, "...", UI_TEXT2);
        }
        char label[48];
        strncpy(label, pics[i]->name, sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        char *dot = strrchr(label, '.');
        if (dot) *dot = '\0';
        GdiTextT(r.x + 10, r.y + ph + 4, label, active ? UI_ACCENT : UI_TEXT);
        hit(st, r, c, PH_PICTURE, 0, pics[i]);
    }
    if (!np) {
        GdiTextT(x, y, "No pictures in C:\\Windows\\Web\\Wallpaper.", UI_TEXT2);
        y += 26;
    }
    y += ((np + cols - 1) / cols) * (ph + 42) + 10;

    GdiTextBold(x, y, "Add a picture", UI_TEXT);
    y += 26;
    RamNode *cand[16];
    int nc = WallpaperCandidates(cand, 16);
    for (int i = 0; i < nc; i++, y += ADD_ROW_H) add_row(st, c, x, y, w, cand[i], PH_ADD_PICTURE);
    if (!nc) {
        GdiTextT(x, y, "Save a PNG picture in Downloads, Pictures, Documents or Desktop to add it here.", UI_TEXT2);
        y += 26;
    }
    return y - top;
}

static int icons_tab(Settings *st, GdiRect c, int x, int y, int w)
{
    int top = y;
    GdiTextBold(x, y, "Icon set", UI_TEXT);
    GdiTextT(x + 80, y, "Apps, folders and files  -  C:\\Windows\\Web\\Icons", UI_TEXT2);
    y += 26;
    RamNode *sets[ICONSET_MAX];
    int ns = IconSetList(sets, ICONSET_MAX);
    int napps = (int)(sizeof(g_preview_apps) / sizeof(g_preview_apps[0]));
    for (int i = -1; i < ns; i++, y += SET_CARD_H + 10) {
        RamNode *d = i < 0 ? NULL : sets[i];
        const char *name = d ? d->name : "Built-in";
        bool active = d ? !strcmp(IconSetCurrent(), d->name) : !IconSetCurrent()[0];
        GdiRect r = RECT(x, y, w, SET_CARD_H);
        card(r, active);
        GdiTextBold(r.x + 16, r.y + 12, name, UI_TEXT);
        if (active) GdiTextT(r.x + 16, r.y + 36, "Active", UI_ACCENT);
        for (int k = 0; k < napps; k++)
            IconSetDrawPreview(d, g_preview_apps[k], r.x + 160 + k * 56, r.y + 14, 48);
        hit(st, r, c, PH_ICONSET, 0, d);
    }
    y += 12;
    GdiTextBold(x, y, "Add an icon set", UI_TEXT);
    y += 26;
    RamNode *cand[12];
    int nc = IconSetCandidates(cand, 12);
    for (int i = 0; i < nc; i++, y += ADD_ROW_H) add_row(st, c, x, y, w, cand[i], PH_ADD_ICONSET);
    if (!nc) {
        GdiTextT(x, y, "Save a folder of icons (terminal.ico, folder.ico, this-pc.ico, ...) in Downloads,", UI_TEXT2);
        GdiTextT(x, y + 20, "Documents, Desktop or Pictures to add it here.  docs/design/brand lists the names.", UI_TEXT2);
        y += 46;
    }
    return y - top;
}

static void page_personalize(Settings *st, GdiRect c, int x, int y, int w)
{
    st->nhits = 0;
    static const char *const tabs[] = { "Background", "Icons" };
    for (int i = 0; i < 2; i++) {
        GdiRect r = RECT(x + i * 128, y, 120, CHIP_H);
        GdiRoundRect(r, 6, i == st->ptab ? UI_ACCENT : UI_CARD, GDI_TRANSPARENT);
        GdiTextCenter(r.x, r.y + 8, r.w, tabs[i], i == st->ptab ? GDI_WHITE : UI_TEXT);
        hit(st, r, c, PH_TAB, i, NULL);
    }
    y += CHIP_H + 16;
    /* the rest scrolls, clipped below the tabs */
    int view = c.y + c.h - y;
    GdiRect old = GdiClipRect();
    int y0 = y > old.y ? y : old.y, y1 = c.y + c.h < old.y + old.h ? c.y + c.h : old.y + old.h;
    if (y1 <= y0) return;
    GdiSetClip(RECT(old.x, y0, old.w, y1 - y0));
    int first = st->nhits;
    st->pheight = st->ptab ? icons_tab(st, c, x, y - st->pscroll, w)
                           : background_tab(st, c, x, y - st->pscroll, w);
    GdiSetClip(old);
    /* (parts scrolled out of view are not clickable) */
    int n = first;
    for (int i = first; i < st->nhits; i++) {
        PHit *h = &st->hits[i];
        if (h->r.y + c.y < y0 || h->r.y + h->r.h + c.y > y1) {
            int vy = h->r.y + c.y, vb = vy + h->r.h;
            if (vb <= y0 || vy >= y1) continue;
            if (vy < y0) { h->r.h -= y0 - vy; h->r.y += y0 - vy; }
            if (vb > y1) h->r.h -= vb - y1;
        }
        if (n != i) st->hits[n] = *h;
        n++;
    }
    st->nhits = n;
    int max = st->pheight - view + 16;
    if (st->pscroll > (max > 0 ? max : 0)) st->pscroll = max > 0 ? max : 0;
}

static void personalize_click(Settings *st, int x, int y)
{
    for (int i = 0; i < st->nhits; i++) {
        PHit *h = &st->hits[i];
        if (!UiHit(h->r, x, y)) continue;
        RamNode *n = h->path[0] ? RamfsResolve(NULL, h->path) : NULL;
        switch (h->kind) {
        case PH_TAB:         if (st->ptab != h->i) { st->ptab = h->i; st->pscroll = 0; } break;
        case PH_THEME:       DesktopSetTheme(h->i); break;
        case PH_PICTURE:     DesktopSetPicture(h->path); break;
        case PH_ADD_PICTURE: {
            char path[RAMFS_PATH_MAX];
            if (n && WallpaperInstall(n, path, sizeof(path))) DesktopSetPicture(path);
            break; }
        case PH_ICONSET:     IconSetChoose(n ? n->name : ""); break;
        case PH_ADD_ICONSET: if (n) IconSetInstall(n); break;
        default: break;                                  /* (Accounts' kinds) */
        }
        return;
    }
}

/* Time & language: the user's regional format (the user locale).  A
 * choice runs intl.exe, which writes HKCU\Control Panel\International
 * (LocaleName and the values beside it) from kernel32's locale data;
 * programs started afterwards format dates, times, numbers and money that
 * way.  "intl NAME" in the Terminal sets any of the Windows locales.
 * The time zone row's Change opens the first-boot setup's time zone page
 * (welcome.c); tzutil /s NAME in the Terminal sets it too.  The keyboard
 * row's Change opens its keyboard layout page (wm/kbdlayout.c). */
#define INTL_KEY "User\\" UM_USER_SID "\\Control Panel\\International"
#define LOC_CHIP_W 184
#define TZ_ROW_DY  (26 + 50 + 34)
#define KBD_ROW_DY (TZ_ROW_DY + 52)
#define LOCALES_DY (KBD_ROW_DY + 54 + 26)
#define TZ_BTN_W   96
static GdiRect r_tz_change(int x, int y, int w) { return RECT(x + w - 8 - TZ_BTN_W, y + TZ_ROW_DY + 6, TZ_BTN_W, 32); }
static GdiRect r_kbd_change(int x, int y, int w) { return RECT(x + w - 8 - TZ_BTN_W, y + KBD_ROW_DY + 6, TZ_BTN_W, 32); }
static const struct { const char *name, *label; } g_formats[] = {
    { "en-US", "English (United States)" }, { "en-GB", "English (United Kingdom)" },
    { "de-DE", "German (Germany)" },        { "fr-FR", "French (France)" },
    { "es-ES", "Spanish (Spain)" },         { "it-IT", "Italian (Italy)" },
    { "nl-NL", "Dutch (Netherlands)" },     { "pt-BR", "Portuguese (Brazil)" },
    { "pl-PL", "Polish (Poland)" },         { "sv-SE", "Swedish (Sweden)" },
    { "ru-RU", "Russian (Russia)" },        { "tr-TR", "Turkish (Turkey)" },
    { "ja-JP", "Japanese (Japan)" },        { "zh-CN", "Chinese (China)" },
    { "ko-KR", "Korean (Korea)" },
};
#define N_FORMATS ((int)(sizeof(g_formats) / sizeof(g_formats[0])))

static void current_locale(char *out, int cap)
{
    if (!um_registry_get_sz(INTL_KEY, "LocaleName", out, cap) || !out[0]) strcpy(out, "en-US");
}

static int loc_cols(int w)
{
    int n = (w + CHIP_GAP) / (LOC_CHIP_W + CHIP_GAP);
    return n < 1 ? 1 : n;
}

static void page_time_language(int x, int y, int w)
{
    char cur[32], value[64];
    current_locale(cur, sizeof(cur));
    const char *label = cur;
    for (int i = 0; i < N_FORMATS; i++) if (!strcmp(g_formats[i].name, cur)) label = g_formats[i].label;
    if (label != cur) ksnprintf(value, sizeof(value), "%s  (%s)", label, cur);
    else ksnprintf(value, sizeof(value), "%s", cur);
    int top = y;
    GdiTextBold(x, y, "Region", UI_TEXT);              y += 26;
    row(x, y, w, "Regional format", value);            y += 50;
    GdiTextT(x, y, "Dates, times, numbers and currency in programs started from now on", UI_TEXT2);
    GdiRoundRect(RECT(x, top + TZ_ROW_DY, w, 44), 6, UI_CARD, GDI_TRANSPARENT);
    GdiTextT(x + 16, top + TZ_ROW_DY + 14, "Time zone", UI_TEXT);
    const char *zone = TzAt(TzCurrent())->display;
    GdiRect b = r_tz_change(x, top, w);
    GdiTextT(b.x - 16 - GdiTextW(zone), top + TZ_ROW_DY + 14, zone, UI_TEXT2);
    UiButton(b, "Change", false);
    GdiRoundRect(RECT(x, top + KBD_ROW_DY, w, 44), 6, UI_CARD, GDI_TRANSPARENT);
    GdiTextT(x + 16, top + KBD_ROW_DY + 14, "Keyboard", UI_TEXT);
    const char *layout = KbdName(KbdCurrent());
    b = r_kbd_change(x, top, w);
    GdiTextT(b.x - 16 - GdiTextW(layout), top + KBD_ROW_DY + 14, layout, UI_TEXT2);
    UiButton(b, "Change", false);

    GdiTextBold(x, top + LOCALES_DY - 26, "Choose a format", UI_TEXT);
    y = top + LOCALES_DY;
    int cols = loc_cols(w);
    for (int i = 0; i < N_FORMATS; i++) {
        int cx = x + (i % cols) * (LOC_CHIP_W + CHIP_GAP), cy = y + (i / cols) * (CHIP_H + CHIP_GAP);
        bool on = !strcmp(g_formats[i].name, cur);
        GdiRoundRect(RECT(cx, cy, LOC_CHIP_W, CHIP_H), 6, on ? UI_ACCENT : UI_CARD, GDI_TRANSPARENT);
        GdiTextCenter(cx, cy + 8, LOC_CHIP_W, g_formats[i].label, on ? GDI_WHITE : UI_TEXT);
    }
    y += ((N_FORMATS + cols - 1) / cols) * (CHIP_H + CHIP_GAP) + 10;
    GdiTextT(x, y, "Other locales: intl NAME in the Terminal (intl /list shows them)", UI_TEXT2);
}

static void set_locale(const char *name)
{
    RamNode *exe = RamfsResolve(NULL, "\\Windows\\System32\\intl.exe");
    char cmd[48];
    ksnprintf(cmd, sizeof(cmd), "intl %s", name);
    if (!exe || !UmSpawnDetached(exe, cmd, exe->parent))
        kprintf("[SETTINGS] Cannot start intl.exe: %s\n", exe ? "out of memory" : "not installed");
}

/* -----------------------------------------------------------------------
 * Accounts: Windows 11's Accounts page.  "Your info" (the account signed
 * in: its picture, type, password), then "Other users": each with
 * "Change account type" and "Remove", and "Add account" (Windows' "Create
 * a user for this PC": name, password twice, Standard user or
 * Administrator).  Only an administrator changes other accounts (step
 * 33.2 asks for consent first).  The forms take the keyboard: Tab between
 * the fields, Enter to go on, Esc to cancel.  Clicks go by st->hits.
 * ----------------------------------------------------------------------- */
enum { ACC_NONE, ACC_ADD, ACC_PASSWORD, ACC_PICTURE, ACC_TYPE, ACC_REMOVE };
#define ACC_ROW_H 60

static bool acc_me(UmAccount *a) { return um_account_get(um_session_rid(), a); }
static bool acc_can_manage(void)
{
    UmAccount me;
    return acc_me(&me) && (me.flags & UM_ACCT_ADMIN);
}

static const char *acc_type(const UmAccount *a) { return (a->flags & UM_ACCT_ADMIN) ? "Administrator" : "Standard user"; }

/* The account's picture (a PNG's thumbnail) or its initial in a circle */
static void acc_avatar(const UmAccount *a, int x, int y, int size)
{
    char pic[96];
    RamNode *f = um_account_picture(a->rid, pic, sizeof(pic)) ? RamfsResolve(NULL, pic + 2) : NULL;
    if (f && !f->dir && WallpaperDrawThumb(f, RECT(x, y, size, size))) return;
    GdiFillCircle(x + size / 2, y + size / 2, size / 2, UI_ACCENT_BG);
    char ini[2] = { a->name[0] >= 'a' && a->name[0] <= 'z' ? (char)(a->name[0] - 32) : a->name[0], 0 };
    if (size >= 48) GdiTextLarge(x + (size - GdiTextLargeW(ini)) / 2, y + (size - GDI_LARGE_H) / 2, ini, GDI_WHITE);
    else GdiTextCenter(x, y + (size - GDI_FONT_H) / 2, size, ini, GDI_WHITE);
}

static void acc_wipe(Settings *st)
{
    volatile char *p = &st->acc_text[0][0];
    for (size_t i = 0; i < sizeof(st->acc_text); i++) p[i] = 0;
}

static void acc_open(Settings *st, int form, UINT32 rid)
{
    acc_wipe(st);
    st->acc_form = form;
    st->acc_rid = rid;
    st->acc_focus = 0;
    st->acc_err[0] = 0;
    st->acc_note[0] = 0;
    UmAccount a;
    st->acc_admin = form == ACC_TYPE && um_account_get(rid, &a) && (a.flags & UM_ACCT_ADMIN);
    if (form == ACC_PASSWORD && um_account_get(rid, &a) && !a.password) st->acc_focus = 1;   /* (no current one to type) */
}

static void acc_close(Settings *st, const char *note)
{
    acc_wipe(st);
    st->acc_form = ACC_NONE;
    st->acc_err[0] = 0;
    ksnprintf(st->acc_note, sizeof(st->acc_note), "%s", note ? note : "");
}

/* The form's text fields: their labels, and whether one is shown */
static const char *acc_label(const Settings *st, int f)
{
    if (st->acc_form == ACC_ADD) return f == 0 ? "User name" : f == 1 ? "Password" : "Confirm password";
    return f == 0 ? "Current password" : f == 1 ? "New password" : "Confirm new password";
}
static bool acc_field_shown(const Settings *st, int f)
{
    UmAccount a;
    if (st->acc_form == ACC_ADD) return true;
    if (st->acc_form != ACC_PASSWORD) return false;
    return f || (um_account_get(st->acc_rid, &a) && a.password);
}
static GdiRect acc_box(int x, int y, int w, int f) { return RECT(x + w - 16 - 260, y + 40 + f * 50 + 6, 260, 32); }

static void acc_status(UINT32 st, char *out, int cap, const char *name)
{
    const char *why = st == 0xC0000062u ? "That user name isn't allowed (1-20 characters, none of \" / \\ [ ] : ; | = , + * ? < > @)." :
                      st == 0xC0000063u ? "There is already an account with that name." :
                      st == 0xC0000069u ? "This is the last administrator: another account has to be one first." :
                      st == 0xC0000022u ? "That account is signed in: sign it out first." :
                      st == 0xC000006Cu ? "The password is too long." : NULL;
    if (why) ksnprintf(out, (size_t)cap, "%s", why);
    else ksnprintf(out, (size_t)cap, "Couldn't change %s (status 0x%08X).", name, st);
}

static void acc_submit(Settings *st)
{
    char (*t)[UM_PASSWORD_MAX + 1] = st->acc_text;
    UmAccount a;
    if (st->acc_form == ACC_ADD) {
        if (!um_account_name_ok(t[0])) { acc_status(0xC0000062u, st->acc_err, sizeof(st->acc_err), t[0]); st->acc_focus = 0; return; }
        if (strcmp(t[1], t[2])) { strcpy(st->acc_err, "The passwords don't match."); st->acc_focus = 2; return; }
        UINT32 rid = 0, r = um_account_add(t[0], t[1], st->acc_admin ? UM_ACCT_ADMIN : 0, &rid);
        if (r) { acc_status(r, st->acc_err, sizeof(st->acc_err), t[0]); return; }
        kprintf("[SETTINGS] Added account %s (%s)\n", t[0], st->acc_admin ? "administrator" : "standard user");
        char note[96];
        ksnprintf(note, sizeof(note), "%s can sign in now: Start, your account, %s.", t[0], t[0]);
        acc_close(st, note);
        return;
    }
    if (st->acc_form == ACC_PASSWORD) {
        if (!um_account_get(st->acc_rid, &a)) { acc_close(st, NULL); return; }
        if (a.password && !um_account_check(a.rid, t[0])) { strcpy(st->acc_err, "The current password is incorrect."); st->acc_focus = 0; t[0][0] = 0; return; }
        if (strcmp(t[1], t[2])) { strcpy(st->acc_err, "The passwords don't match."); st->acc_focus = 2; return; }
        UINT32 r = um_account_set_password(a.rid, t[1]);
        if (r) { acc_status(r, st->acc_err, sizeof(st->acc_err), a.name); return; }
        kprintf("[SETTINGS] %s's password %s\n", a.name, t[1][0] ? "changed" : "removed");
        acc_close(st, t[1][0] ? "Your password was changed." : "Your password was removed.");
        return;
    }
    if (st->acc_form == ACC_TYPE) {
        if (!um_account_get(st->acc_rid, &a)) { acc_close(st, NULL); return; }
        UINT32 r = um_account_set_flags(a.rid, (a.flags & ~UM_ACCT_ADMIN) | (st->acc_admin ? UM_ACCT_ADMIN : 0));
        if (r) { acc_status(r, st->acc_err, sizeof(st->acc_err), a.name); return; }
        char note[96];
        ksnprintf(note, sizeof(note), "%s is now %s.", a.name, st->acc_admin ? "an administrator" : "a standard user");
        acc_close(st, note);
        return;
    }
    if (st->acc_form == ACC_REMOVE) {
        if (!um_account_get(st->acc_rid, &a)) { acc_close(st, NULL); return; }
        UINT32 r = um_account_delete(a.rid);
        if (r) { acc_status(r, st->acc_err, sizeof(st->acc_err), a.name); return; }
        um_account_delete_profile(a.rid);                           /* ("Delete account and data") */
        kprintf("[SETTINGS] Removed account %s\n", a.name);
        char note[96];
        ksnprintf(note, sizeof(note), "%s's account and files were removed.", a.name);
        acc_close(st, note);
    }
}

static void acc_buttons(Settings *st, GdiRect c, int x, int y, const char *ok)
{
    GdiRect b = RECT(x, y, GdiTextW(ok) + 40 > 100 ? GdiTextW(ok) + 40 : 100, 32);
    UiButton(b, ok, true);
    hit(st, b, c, AH_OK, 0, NULL);
    GdiRect k = RECT(b.x + b.w + 12, y, 100, 32);
    UiButton(k, "Cancel", false);
    hit(st, k, c, AH_CANCEL, 0, NULL);
    if (st->acc_err[0]) GdiTextT(x, y + 44, st->acc_err, GDI_C(0xE0, 0x60, 0x60));
}

static void acc_chips(Settings *st, GdiRect c, int x, int y)
{
    static const char *const kinds[2] = { "Standard user", "Administrator" };
    for (int i = 0; i < 2; i++) {
        GdiRect r = RECT(x + i * (NET_CHIP_W + CHIP_GAP), y, NET_CHIP_W, CHIP_H);
        bool on = st->acc_admin == (i == 1);
        GdiRoundRect(r, 6, on ? UI_ACCENT : UI_CARD, GDI_TRANSPARENT);
        GdiTextCenter(r.x, r.y + 8, r.w, kinds[i], on ? GDI_WHITE : UI_TEXT);
        hit(st, r, c, AH_CHIP, i, NULL);
    }
}

static void acc_form(Settings *st, GdiRect c, int x, int y, int w)
{
    UmAccount a;
    bool known = um_account_get(st->acc_rid, &a);
    if (st->acc_form == ACC_ADD || st->acc_form == ACC_PASSWORD) {
        GdiTextBold(x, y, st->acc_form == ACC_ADD ? "Create a user for this PC" : "Change your password", UI_TEXT);
        for (int f = 0; f < 3; f++) {
            if (!acc_field_shown(st, f)) continue;
            int ry = y + 40 + f * 50;
            GdiRoundRect(RECT(x, ry, w, 44), 6, UI_CARD, GDI_TRANSPARENT);
            GdiTextT(x + 16, ry + 14, acc_label(st, f), UI_TEXT);
            GdiRect b = acc_box(x, y, w, f);
            GdiRoundRect(b, 6, UI_BG, GDI_TRANSPARENT);
            GdiRoundBorderAlpha(b, 6, f == st->acc_focus ? UI_ACCENT : GDI_WHITE, f == st->acc_focus ? 200 : 30);
            int n = (int)strlen(st->acc_text[f]), tx = b.x + 10;
            if (f == 0 && st->acc_form == ACC_ADD) { GdiTextT(tx, b.y + (b.h - GDI_FONT_H) / 2, st->acc_text[f], UI_TEXT); tx += GdiTextW(st->acc_text[f]); }
            else { for (int i = 0; i < n && i < 28; i++) GdiFillCircle(b.x + 14 + i * 8, b.y + b.h / 2, 3, UI_TEXT); tx = b.x + 10 + (n < 28 ? n : 28) * 8; }
            if (f == st->acc_focus) GdiFillRect(RECT(tx + 1, b.y + 7, 2, b.h - 14), UI_ACCENT);
            hit(st, b, c, AH_FIELD, f, NULL);
        }
        int ey = y + 40 + 3 * 50 + 8;
        if (st->acc_form == ACC_ADD) {
            GdiTextBold(x, ey, "Account type", UI_TEXT);
            acc_chips(st, c, x, ey + 26);
            ey += 26 + CHIP_H + 16;
            GdiTextT(x, ey, "Leave the password blank to sign in without one.", UI_TEXT2);
        } else GdiTextT(x, ey, "Leave the new password blank to sign in without one.", UI_TEXT2);
        acc_buttons(st, c, x, ey + 30, st->acc_form == ACC_ADD ? "Create" : "Save");
        return;
    }
    if (!known) { st->acc_form = ACC_NONE; return; }
    if (st->acc_form == ACC_TYPE) {
        char t[64];
        ksnprintf(t, sizeof(t), "Change account type: %s", a.full[0] ? a.full : a.name);
        GdiTextBold(x, y, t, UI_TEXT);
        acc_chips(st, c, x, y + 32);
        GdiTextT(x, y + 32 + CHIP_H + 16, "An administrator can change settings, install programs and manage other accounts.", UI_TEXT2);
        acc_buttons(st, c, x, y + 32 + CHIP_H + 48, "OK");
        return;
    }
    if (st->acc_form == ACC_REMOVE) {
        char t[64], l[160];
        ksnprintf(t, sizeof(t), "Delete account and data? (%s)", a.name);
        GdiTextBold(x, y, t, UI_TEXT);
        ksnprintf(l, sizeof(l), "Deleting %s's account removes their files from this PC: %s.", a.name, a.profile[0] ? a.profile : "C:\\Users\\...");
        GdiTextT(x, y + 32, l, UI_TEXT2);
        GdiTextT(x, y + 56, "Back up anything you want to keep before you delete it.", UI_TEXT2);
        acc_buttons(st, c, x, y + 92, "Delete account and data");
        return;
    }
    /* ACC_PICTURE: the pictures in C:\Windows\Web\Wallpaper, or none */
    GdiTextBold(x, y, "Choose your picture", UI_TEXT);
    GdiTextT(x, y + 22, "PNG pictures in C:\\Windows\\Web\\Wallpaper (add more in Personalization)", UI_TEXT2);
    y += 52;
    int sz = 88, cols = (w + 12) / (sz + 12);
    if (cols < 1) cols = 1;
    GdiRect none = RECT(x, y, sz, sz);
    GdiRoundRect(none, 8, UI_CARD, GDI_TRANSPARENT);
    UmAccount plain = a;
    char pic[96];
    um_account_picture(a.rid, pic, sizeof(pic));
    GdiFillCircle(x + sz / 2, y + sz / 2 - 6, 26, UI_ACCENT_BG);
    char ini[2] = { plain.name[0] >= 'a' && plain.name[0] <= 'z' ? (char)(plain.name[0] - 32) : plain.name[0], 0 };
    GdiTextCenter(x, y + sz / 2 - 14, sz, ini, GDI_WHITE);
    GdiTextCenter(x, y + sz - 20, sz, "None", UI_TEXT2);
    if (!pic[0]) GdiRoundBorderAlpha(none, 8, UI_ACCENT, 255);
    hit(st, none, c, AH_PIC_NONE, 0, NULL);
    RamNode *pics[24];
    int np = WallpaperList(pics, 24);
    for (int i = 0; i < np; i++) {
        int k = i + 1;
        GdiRect r = RECT(x + (k % cols) * (sz + 12), y + (k / cols) * (sz + 12), sz, sz);
        if (!WallpaperDrawThumb(pics[i], r)) GdiRoundRect(r, 8, UI_HOVER, GDI_TRANSPARENT);
        char path[RAMFS_PATH_MAX];
        RamfsPath(pics[i], path, sizeof(path));
        if (pic[0] && !strcmp(path, pic)) GdiRoundBorderAlpha(r, 8, UI_ACCENT, 255);
        hit(st, r, c, AH_PIC, 0, pics[i]);
    }
    int ey = y + ((np + 1 + cols - 1) / cols) * (sz + 12) + 8;
    GdiRect k = RECT(x, ey, 100, 32);
    UiButton(k, "Cancel", false);
    hit(st, k, c, AH_CANCEL, 0, NULL);
}

static void page_accounts(Settings *st, GdiRect c, int x, int y, int w)
{
    st->nhits = 0;
    if (st->acc_form) { acc_form(st, c, x, y, w); return; }
    UmAccount me;
    if (!acc_me(&me)) return;
    bool admin = me.flags & UM_ACCT_ADMIN;

    /* Your info */
    GdiRoundRect(RECT(x, y, w, 104), 8, UI_CARD, GDI_TRANSPARENT);
    acc_avatar(&me, x + 20, y + 20, 64);
    GdiTextLarge(x + 104, y + 20, me.full[0] ? me.full : me.name, UI_TEXT);
    char sub[96];
    ksnprintf(sub, sizeof(sub), "Local account  -  %s  -  %s", acc_type(&me), me.password ? "Password set" : "No password");
    GdiTextT(x + 104, y + 58, sub, UI_TEXT2);
    GdiRect bp = RECT(x + w - 16 - 150, y + 16, 150, 32), bi = RECT(x + w - 16 - 150, y + 56, 150, 32);
    UiButton(bp, me.password ? "Change password" : "Add a password", false);
    hit(st, bp, c, AH_PASSWORD, (int)me.rid, NULL);
    UiButton(bi, "Change picture", false);
    hit(st, bi, c, AH_PICTURE, (int)me.rid, NULL);
    y += 104 + 14;
    if (st->acc_note[0]) { GdiTextT(x, y, st->acc_note, UI_ACCENT); y += 24; }

    /* Other users */
    GdiTextBold(x, y, "Other users", UI_TEXT);
    y += 26;
    GdiRoundRect(RECT(x, y, w, 52), 6, UI_CARD, GDI_TRANSPARENT);
    GdiTextT(x + 16, y + 17, "Add other user", UI_TEXT);
    if (admin) {
        GdiRect b = RECT(x + w - 16 - 130, y + 10, 130, 32);
        UiButton(b, "Add account", true);
        hit(st, b, c, AH_ADD, 0, NULL);
    } else GdiTextT(x + w - 16 - GdiTextW("An administrator adds accounts"), y + 17, "An administrator adds accounts", UI_TEXT3);
    y += 58;
    static UmAccount all[16];
    int n = um_accounts_list(all, 16), shown = 0;
    for (int i = 0; i < n; i++) {
        const UmAccount *a = &all[i];
        if (a->rid == me.rid) continue;
        shown++;
        GdiRoundRect(RECT(x, y, w, ACC_ROW_H - 6), 6, UI_CARD, GDI_TRANSPARENT);
        acc_avatar(a, x + 14, y + 9, 36);
        GdiTextT(x + 62, y + 8, a->full[0] ? a->full : a->name, UI_TEXT);
        ksnprintf(sub, sizeof(sub), "%s%s%s", acc_type(a), (a->flags & UM_ACCT_DISABLED) ? "  -  Disabled" : "",
                  um_session_signed_in(a->rid) ? "  -  Signed in" : "");
        GdiTextT(x + 62, y + 28, sub, UI_TEXT2);
        if (admin) {
            GdiRect rm = RECT(x + w - 16 - 90, y + 11, 90, 32), ty = RECT(rm.x - 12 - 190, y + 11, 190, 32);
            UiButton(ty, "Change account type", false);
            hit(st, ty, c, AH_TYPE, (int)a->rid, NULL);
            UiButton(rm, "Remove", false);
            hit(st, rm, c, AH_REMOVE, (int)a->rid, NULL);
        }
        y += ACC_ROW_H;
    }
    if (!shown) GdiTextT(x, y + 4, "No other accounts on this PC yet.", UI_TEXT2);
}

static void accounts_click(Settings *st, int x, int y)
{
    for (int i = st->nhits - 1; i >= 0; i--) {
        PHit *h = &st->hits[i];
        if (!UiHit(h->r, x, y)) continue;
        switch (h->kind) {
        case AH_ADD:      acc_open(st, ACC_ADD, 0); break;
        case AH_PASSWORD: acc_open(st, ACC_PASSWORD, (UINT32)h->i); break;
        case AH_PICTURE:  acc_open(st, ACC_PICTURE, (UINT32)h->i); break;
        case AH_TYPE:     acc_open(st, ACC_TYPE, (UINT32)h->i); break;
        case AH_REMOVE:   acc_open(st, ACC_REMOVE, (UINT32)h->i); break;
        case AH_FIELD:    st->acc_focus = h->i; break;
        case AH_CHIP:     st->acc_admin = h->i == 1; break;
        case AH_OK:       acc_submit(st); break;
        case AH_CANCEL:   acc_close(st, NULL); break;
        case AH_PIC_NONE: um_account_set_picture(st->acc_rid, ""); acc_close(st, "Your picture was removed."); break;
        case AH_PIC: {
            char path[RAMFS_PATH_MAX + 2];
            ksnprintf(path, sizeof(path), "C:%s", h->path);
            um_account_set_picture(st->acc_rid, path);
            kprintf("[SETTINGS] Account picture %s\n", path);
            acc_close(st, "Your picture was changed.");
            break; }
        default: break;
        }
        return;
    }
}

static void accounts_key(Settings *st, const KeyEvent *k)
{
    if (k->scancode == KEY_ESC) { acc_close(st, NULL); return; }
    if (k->scancode == KEY_ENTER && !k->extended) { acc_submit(st); return; }
    if (st->acc_form != ACC_ADD && st->acc_form != ACC_PASSWORD) return;
    if (k->scancode == KEY_TAB || (k->extended && (k->scancode == KEY_UP || k->scancode == KEY_DOWN))) {
        int d = (k->scancode == KEY_UP || (k->scancode == KEY_TAB && k->shift)) ? -1 : 1, f = st->acc_focus;
        do f = (f + d + 3) % 3; while (!acc_field_shown(st, f));
        st->acc_focus = f;
        return;
    }
    char *t = st->acc_text[st->acc_focus];
    size_t n = strlen(t), cap = st->acc_form == ACC_ADD && st->acc_focus == 0 ? UM_ACCOUNT_NAME_MAX : UM_PASSWORD_MAX;
    if (k->ch == '\b') { if (n) t[n - 1] = '\0'; }
    else if (!k->ctrl && !k->alt && (UINT8)k->ch >= 0x20 && (UINT8)k->ch < 0x7F && n < cap) { t[n] = k->ch; t[n + 1] = '\0'; }
    st->acc_err[0] = 0;
}

static void page_about(int x, int y, int w)
{
    GdiTextLarge(x, y, "NovaOS", UI_TEXT);             y += 40;
    char ver[96];
    ksnprintf(ver, sizeof(ver), "Version %s  -  a Windows-compatible OS research project", NovaVersion());
    GdiTextT(x, y, ver, UI_TEXT2); y += 34;
    row(x, y, w, "Kernel", "Nova, NT-style syscalls (Win10 1903 ABI)"); y += 50;
    row(x, y, w, "Desktop", "Kernel GDI + window manager");            y += 50;
    row(x, y, w, "UI font", "Inter 4.1 (SIL OFL 1.1)");                 y += 50;
    row(x, y, w, "Monospace font", "Cascadia Mono (SIL OFL 1.1)");
}

static void set_paint(WND *w)
{
    Settings *st = w->user;
    GdiRect c = WmClientRect(w);

    GdiFillRect(RECT(c.x, c.y, SIDE_W, c.h), UI_PANEL);
    GdiFillRect(RECT(c.x + SIDE_W - 1, c.y, 1, c.h), UI_LINE);
    GdiTextLarge(c.x + 20, c.y + 16, "Settings", UI_TEXT);
    for (int i = 0; i < N_PAGES; i++) {
        int y = c.y + 64 + i * ITEM_H;
        if (i == st->page) {
            GdiRoundRect(RECT(c.x + 8, y, SIDE_W - 16, ITEM_H - 4), 4, UI_HOVER, GDI_TRANSPARENT);
            GdiRoundRect(RECT(c.x + 8, y + 9, 3, 14), 1, UI_ACCENT, GDI_TRANSPARENT);
        }
        AppDrawGlyph(g_page_glyphs[i], c.x + 22, y + 8, 16, i == st->page ? UI_TEXT : UI_TEXT2);
        GdiTextT(c.x + 48, y + 8, g_pages[i], i == st->page ? UI_TEXT : UI_TEXT2);
    }

    int x = c.x + SIDE_W + 28, y = c.y + 20, w2 = c.w - SIDE_W - 56;
    GdiTextLarge(x, y, g_pages[st->page], UI_TEXT);
    y += 52;
    switch (st->page) {
    case SETTINGS_SYSTEM:      page_system(x, y, w2);     break;
    case SETTINGS_DISPLAY:     page_display(st, x, y, w2); break;
    case SETTINGS_SOUND:       page_sound(st, x, y, w2);   break;
    case SETTINGS_PERSONALIZE: page_personalize(st, c, x, y, w2); break;
    case SETTINGS_STORAGE:     page_storage(x, y, w2);    break;
    case SETTINGS_NETWORK:     page_network(st, x, y, w2); break;
    case SETTINGS_ACCOUNTS:    page_accounts(st, c, x, y, w2); break;
    case SETTINGS_TIME_LANGUAGE: page_time_language(x, y, w2); break;
    case SETTINGS_ABOUT:       page_about(x, y, w2);      break;
    }
}

static void set_mouse(WND *w, WmMouseMsg msg, int x, int y)
{
    Settings *st = w->user;
    GdiRect c = WmClientRect(w);
    if (st->page == SETTINGS_DISPLAY && multi()) {
        /* the arrangement (layout matches set_paint + page_display) */
        Arr a = arr_of(SIDE_W + 28, 20 + 52 + ARR_DY, c.w - SIDE_W - 56);
        if (msg == WM_MOUSE_DOWN) {
            for (int i = GdiMonitorCount() - 1; i >= 0; i--) {
                GdiRect r = arr_rect(&a, GdiMonitorRect(i));
                if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) continue;
                st->mon = i;
                st->drag = i > 0;                   /* the main display stays at (0, 0) */
                st->moved = false;
                st->drag_dx = x - r.x; st->drag_dy = y - r.y;
                st->drag_x = r.x + c.x; st->drag_y = r.y + c.y;
                return;
            }
        } else if (msg == WM_MOUSE_MOVE && st->drag) {
            st->drag_x = x - st->drag_dx + c.x;
            st->drag_y = y - st->drag_dy + c.y;
            st->moved = true;
            return;
        } else if (msg == WM_MOUSE_UP && st->drag) {
            st->drag = false;
            if (!st->moved) return;                 /* a click: just chose it */
            int mx = (x - st->drag_dx - a.ox) * a.den / a.num, my = (y - st->drag_dy - a.oy) * a.den / a.num;
            snap_place(st->mon, &mx, &my);
            GdiRect m = GdiMonitorRect(st->mon);
            if (mx != m.x || my != m.y) DesktopSetMonitorOrigin(st->mon, mx, my, true);
            return;
        }
    }
    if (msg == WM_MOUSE_WHEEL && st->page == SETTINGS_PERSONALIZE) {
        st->pscroll -= WmWheelDelta() * 48;                    /* (page_personalize keeps it in range) */
        if (st->pscroll < 0) st->pscroll = 0;
        return;
    }
    if (msg != WM_MOUSE_DOWN) return;
    if (x >= SIDE_W && st->page == SETTINGS_NETWORK) {
        net_mouse(st, SIDE_W + 28, 20 + 52, c.w - SIDE_W - 56, x, y);
        return;
    }
    if (x >= SIDE_W) {
        if (st->page == SETTINGS_DISPLAY) {
            /* resolution chips (layout matches set_paint + page_display) */
            int px = SIDE_W + 28, py = 20 + 52 + modes_dy(), pw = c.w - SIDE_W - 56;
            int cols = chip_cols(pw);
            int mon0 = chosen(st), dy = 20 + 52 + dpi_chips_dy(mon0, pw);
            if (GdiMonitorScale(mon0) >= 2 && y >= dy && y < dy + CHIP_H && x >= px &&
                (x - px) % (CHIP_W + CHIP_GAP) < CHIP_W && (x - px) / (CHIP_W + CHIP_GAP) < 2) {
                DesktopSetMonitorDpi(mon0, (x - px) / (CHIP_W + CHIP_GAP) ? 192 : 96, true);
                return;
            }
            if (x < px || y < py) return;
            int col = (x - px) / (CHIP_W + CHIP_GAP), r = (y - py) / (CHIP_H + CHIP_GAP);
            if (col >= cols || (x - px) % (CHIP_W + CHIP_GAP) >= CHIP_W ||
                (y - py) % (CHIP_H + CHIP_GAP) >= CHIP_H) return;
            DisplayMode m;
            int mon = chosen(st);
            if (!DisplayHeadModeAt(mon, r * cols + col, &m)) return;
            if (DesktopSetHeadMode(mon, m.w, m.h)) DesktopSaveHeadMode(mon, m.w, m.h);
            return;
        }
        if (st->page == SETTINGS_SOUND) {
            /* device rows (layout matches set_paint + page_sound) */
            sound_click(x - (SIDE_W + 28), y - (20 + 52), c.w - SIDE_W - 56);
            return;
        }
        if (st->page == SETTINGS_TIME_LANGUAGE) {
            /* the time zone's Change, format chips (layout matches set_paint + page_time_language) */
            int px = SIDE_W + 28, py = 20 + 52 + LOCALES_DY, pw = c.w - SIDE_W - 56;
            if (UiHit(r_tz_change(px, 20 + 52, pw), x, y)) { WelcomeTimeZone(); return; }
            if (UiHit(r_kbd_change(px, 20 + 52, pw), x, y)) { WelcomeKeyboard(); return; }
            int cols = loc_cols(pw);
            if (x < px || y < py) return;
            int col = (x - px) / (LOC_CHIP_W + CHIP_GAP), r = (y - py) / (CHIP_H + CHIP_GAP), i = r * cols + col;
            if (col >= cols || (x - px) % (LOC_CHIP_W + CHIP_GAP) >= LOC_CHIP_W ||
                (y - py) % (CHIP_H + CHIP_GAP) >= CHIP_H || i >= N_FORMATS) return;
            set_locale(g_formats[i].name);
            return;
        }
        if (st->page == SETTINGS_PERSONALIZE) personalize_click(st, x, y);
        if (st->page == SETTINGS_ACCOUNTS) accounts_click(st, x, y);
        return;
    }
    int i = (y - 64) / ITEM_H;
    if (y >= 64 && i >= 0 && i < N_PAGES) { st->page = i; st->net_edit = false; if (st->acc_form) acc_close(st, NULL); }
}

static void set_key(WND *w, const KeyEvent *k)
{
    Settings *st = w->user;
    if (st->page == SETTINGS_NETWORK && st->net_edit) { if (k->pressed) net_key(st, k); return; }
    if (st->page == SETTINGS_ACCOUNTS && st->acc_form) { if (k->pressed) accounts_key(st, k); return; }
    if (!k->extended) return;
    if (k->scancode == KEY_UP && st->page > 0) st->page--;
    if (k->scancode == KEY_DOWN && st->page < N_PAGES - 1) st->page++;
}

static void set_close(WND *w)
{
    Settings *st = w->user;
    if (st) acc_wipe(st);                     /* (passwords typed) */
    kfree(w->user);
    w->user = NULL;
}

void SettingsOpen(void)
{
    Settings *st = kzalloc(sizeof(Settings));
    if (!st) return;
    WND *w = AppCreateWindow(APP_SETTINGS, "Settings", 860, 580, UI_BG);
    if (!w) { kfree(st); return; }
    w->user     = st;
    w->on_paint = set_paint;
    w->on_mouse = set_mouse;
    w->on_key   = set_key;
    w->on_close = set_close;
    w->on_tick  = set_tick;
    w->rbutton  = true;                       /* (the wheel scrolls Personalization) */
}

void SettingsOpenPage(int page)
{
    WND *w = WmFindApp(APP_SETTINGS);
    if (!w) {
        SettingsOpen();
        w = WmFindApp(APP_SETTINGS);
        if (!w) return;
    }
    Settings *st = w->user;
    if (st && page == SETTINGS_ACCOUNTS_ADD) {                /* (Start's "Add an account") */
        st->page = SETTINGS_ACCOUNTS;
        if (acc_can_manage()) acc_open(st, ACC_ADD, 0);
    } else if (st && page >= 0 && page < N_PAGES) st->page = page;
    WmSetActive(w);
    WmInvalidate();
}
