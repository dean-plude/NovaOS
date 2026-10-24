/*
 * settings.c — Settings: live system, display, sound, personalization,
 *               storage, network, Wi-Fi, firewall (settings_firewall.c), accounts, time & language, printers and about pages
 *               (System has Recovery: Reset this PC and Advanced startup)
 *
 * Every page is an entry of g_pages (its name, glyph, ms-settings: names,
 * search words and hooks); one frame draws them all: the sidebar (a search
 * box, then the pages; glyphs only in a narrow window), the page's title,
 * and its content, which scrolls (a scroll bar, the wheel, Page Up/Down)
 * whenever it is taller than the window.  go() is the one way to another
 * page: it closes the page's forms, wipes what was typed and logs
 * "[SETTINGS] Page: NAME".  "settings check" in the Terminal draws every
 * page at three window sizes and checks that each one draws something and
 * scrolls within its range (SettingsCheck).
 */

#include "apps.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../ke/version.h"
#include "../ke/printf.h"
#include "../ke/timezone.h"
#include "../net/net.h"
#include "../nt/wlan.h"
#include "../wm/desktop.h"
#include "../wm/kbdlayout.h"
#include "../fs/persist.h"
#include "../fs/recovery.h"
#include "../fs/reset.h"
#include "../hal/display.h"
#include "../um/um.h"
#include "../drivers/audio.h"
#include "../wm/wallpaper.h"
#include "../ke/scheduler.h"
#include "iconset.h"

#define SIDE_W     200
#define SIDE_SLIM  56                /* the sidebar with glyphs only (a window narrower than SLIM_BELOW) */
#define SLIM_BELOW 680
#define ITEM_H     36
#define HEAD_H     72                /* the page's title: its content starts this far down */
#define FIND_Y     58                /* the search box, in the sidebar */
#define FIND_H     32

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
    int page; UINT32 sig;            /* the page; what it shows, as last drawn (Network, Wi-Fi, Display) */
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
    bool rec_ask;                    /* System, Recovery: "Reset this PC" waits for its confirmation */
    char rec_note[96];               /* ... why a button did nothing */
    UINT32 prn_gen;                  /* Printers: the registry generation shown */
    bool prn_focus;                  /* ... typing an address to add */
    char prn_text[160];              /* ... the address */
    char prn_err[96];                /* ... why it was refused */
    int wifi_job;                    /* Wi-Fi: 0, or the scan (1) or connection (2, 3 disconnect) running */
    bool wifi_scanned;               /* ... a scan has run since the page opened */
    int wifi_n, wifi_sel;            /* ... the networks heard; the one chosen (-1 none) */
    WlanBss wifi_bss[WLAN_MAX_BSS];
    char wifi_ssid[33];              /* ... the network being joined */
    char wifi_key[64];               /* ... its passphrase, as typed (wiped once used) */
    char wifi_note[160];             /* ... what the last connection did */
    WlanInfo wifi_info;              /* ... the adapter, as last seen (WlanPeekInfo: never waits) */
    UiScroll sb;                     /* the page's content: pixels down */
    UiScroll nb;                     /* the sidebar's pages: pixels down (in a short window) */
    UINT32 live;                     /* System, Storage: the second shown (repainted each second) */
    bool find;                       /* the search box has the keyboard */
    char query[32];                  /* ... what is typed in it */
    int check;                       /* SettingsCheck: the step drawn last (0: not checking) */
    int check_drawn, check_bottom;   /* ... the step set_paint drew, and its content's height */
    int check_pass, check_fail;
    GdiRect check_frame;             /* ... the window as it was */
    bool check_close;                /* ... the check opened the window (it closes it again) */
} Settings;

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

static int page_system(int x, int y, int w)
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
    return y + 44;
}

/* System's Recovery, REC_DY below the page's top (set_mouse finds its
 * buttons with the same numbers): Reset this PC (keeping your files,
 * fs/reset.h) and Advanced startup (the boot loader's menu, fs/recovery.h),
 * as in Windows' Settings > System > Recovery */
#define REC_DY 324
#define REC_BTN_W 120

static GdiRect r_rec_reset(int x, int y, int w)   { return RECT(x + w - 8 - REC_BTN_W, y + REC_DY + 28 + 6, REC_BTN_W, 32); }
static GdiRect r_rec_cancel(int x, int y, int w)  { return RECT(x + w - 16 - 2 * REC_BTN_W, y + REC_DY + 28 + 6, REC_BTN_W, 32); }
static GdiRect r_rec_restart(int x, int y, int w) { return RECT(x + w - 8 - REC_BTN_W, y + REC_DY + 28 + 50 + 6, REC_BTN_W, 32); }

static void rec_row(int x, int y, int w, const char *label, const char *about, int buttons)
{
    GdiRoundRect(RECT(x, y, w, 44), 6, UI_CARD, GDI_TRANSPARENT);
    GdiTextT(x + 16, y + 14, label, UI_TEXT);
    int ax = x + 16 + GdiTextW(label) + 16;
    if (ax + GdiTextW(about) < x + w - 16 - buttons * (REC_BTN_W + 8)) GdiTextT(ax, y + 14, about, UI_TEXT2);
}

static int page_recovery(Settings *st, int x, int y, int w)
{
    int ry = y + REC_DY;
    GdiTextT(x, ry, "Recovery", UI_TEXT);
    ry += 28;
    if (st->rec_ask) {
        rec_row(x, ry, w, "Reset this PC?", "Apps and settings are removed; your files and accounts stay.", 2);
        UiButton(r_rec_cancel(x, y, w), "Cancel", false);
        UiButton(r_rec_reset(x, y, w), "Reset", true);
    } else {
        rec_row(x, ry, w, "Reset this PC", "Remove apps and settings, keep your files", 1);
        UiButton(r_rec_reset(x, y, w), "Reset PC", false);
    }
    ry += 50;
    rec_row(x, ry, w, "Advanced startup", "Restart into safe mode, startup repair or the previous version", 1);
    UiButton(r_rec_restart(x, y, w), "Restart now", false);
    if (st->rec_note[0]) { GdiTextT(x, ry + 54, st->rec_note, UI_TEXT2); return ry + 54 + GDI_FONT_H; }
    return ry + 44;
}

static void recovery_click(Settings *st, int x, int y, int px, int py, int pw)
{
    if (UiHit(r_rec_restart(px, py, pw), x, y)) {
        st->rec_ask = false;
        if (RecoveryRequestMenu()) DesktopPowerRequest(POWER_RESTART);
        else ksnprintf(st->rec_note, sizeof(st->rec_note), "Advanced startup needs NovaOS installed on a disk.");
    } else if (UiHit(r_rec_reset(px, py, pw), x, y)) {
        if (!st->rec_ask) { st->rec_ask = true; st->rec_note[0] = '\0'; return; }
        st->rec_ask = false;
        if (ResetRequest()) DesktopPowerRequest(POWER_RESTART);
        else ksnprintf(st->rec_note, sizeof(st->rec_note), "Drive C: is not kept on a disk: there is nothing to reset.");
    } else if (st->rec_ask && UiHit(r_rec_cancel(px, py, pw), x, y)) {
        st->rec_ask = false;
    }
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

static int page_display(Settings *st, int x, int y, int w)
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
            if (st->drag && i == mon) { r.x = st->drag_x; r.y = st->drag_y - st->sb.pos; }
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
    return y + 44;
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

static int page_sound(Settings *st, int x, int y, int w)
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
    return iy + SOUND_HEAD + (d.nin ? d.nin : 1) * SOUND_ROW;
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

static int page_storage(int x, int y, int w)
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
    UINT64 dfree, dtotal;
    bool space = PersistSpace(&dfree, &dtotal);
    if (PersistDirect() && space) {                          /* (C: is the volume itself: docs/design/disk-c.md) */
        char u[24], t[24];
        AppFormatSize(dtotal - dfree, u, sizeof(u));
        AppFormatSize(dtotal, t, sizeof(t));
        ksnprintf(a, sizeof(a), "%s used of %s on %s", u, t, where);
    } else if (PersistActive())
        ksnprintf(a, sizeof(a), "%s in %d file%s; saved to disk %s", b, files, files == 1 ? "" : "s", where);
    else
        ksnprintf(a, sizeof(a), "%s in %d file%s; no disk found, reset on reboot", b, files, files == 1 ? "" : "s");
    GdiTextT(x + 16, y + 38, a, UI_TEXT2);
    if (space) bar(x + 16, y + 66, w - 32, dtotal - dfree, dtotal);   /* the disk's use */
    else bar(x + 16, y + 66, w - 32, bytes, bytes + 1024 * 1024);
    return y + 92;
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

static int net_form(Settings *st, int x, int y, int w)
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
    return ey + 28 + 32;
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

static int page_network(Settings *st, int x, int y, int w)
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
    if (ns.present && st->net_edit) return net_form(st, x, top, w);
    y += 88;
    if (!ns.present) {
        GdiTextT(x, y, "Plug in a network cable, or add a network adapter (Device Manager lists the ones found).",
                 UI_TEXT2);
        return y + GDI_FONT_H;
    }

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
    if (!ns.configured) return y - 6;
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
    return y + 44;
}

/* Wi-Fi: the adapter's state, the networks its last scan heard (a scan
 * runs when the page opens and on Refresh), and joining one: choose it
 * (a click, or Up and Down), type its passphrase if it is secured, then
 * Connect (or Enter).  Scans and connections run in the background
 * through NovaOS's WDI upper edge (kernel/nt/wdi.c), as netsh wlan's do.
 * Layout shared by set_paint + set_mouse. */
#define WIFI_ROW_H   56
#define WIFI_SHOWN   6
#define WIFI_FORM_H  52              /* (below the chosen row: the passphrase and Connect) */
static GdiRect r_wifi_refresh(int x, int y, int w)    { return RECT(x + w - 16 - 96, y + 20, 96, 32); }
static GdiRect r_wifi_disconnect(int x, int y, int w) { return RECT(x + w - 16 - 96 - 12 - 120, y + 20, 120, 32); }
static int wifi_row_y(const Settings *st, int y, int i)
{
    return y + 88 + i * WIFI_ROW_H + (st->wifi_sel >= 0 && i > st->wifi_sel ? WIFI_FORM_H : 0);
}
static bool wifi_secured(const WlanBss *b) { return b->privacy || b->rsn_len; }
static GdiRect r_wifi_key(const Settings *st, int x, int y, int w)
{
    return RECT(x + 16, wifi_row_y(st, y, st->wifi_sel) + WIFI_ROW_H - 4, w - 32 - 112, 32);
}
static GdiRect r_wifi_connect(const Settings *st, int x, int y, int w)
{
    return RECT(x + w - 16 - 100, wifi_row_y(st, y, st->wifi_sel) + WIFI_ROW_H - 4, 100, 32);
}

static void wifi_scan(Settings *st)
{
    if (st->wifi_job || !WlanCount()) return;
    if (WlanScanStart(0)) st->wifi_job = 1;
}

static void wifi_connect(Settings *st)
{
    if (st->wifi_job || st->wifi_sel < 0 || st->wifi_sel >= st->wifi_n) return;
    const WlanBss *b = &st->wifi_bss[st->wifi_sel];
    if (wifi_secured(b) && !st->wifi_key[0]) {
        ksnprintf(st->wifi_note, sizeof(st->wifi_note), "Type the network security key for \"%s\".", b->ssid);
        return;
    }
    strcpy(st->wifi_ssid, b->ssid);
    if (!WlanConnectStart(0, st->wifi_ssid, wifi_secured(b) ? st->wifi_key : NULL)) {
        strcpy(st->wifi_note, "The wireless adapter is busy; try again.");
        return;
    }
    memset(st->wifi_key, 0, sizeof(st->wifi_key));
    st->wifi_note[0] = '\0';
    st->wifi_job = 2;
}

static void wifi_disconnect(Settings *st)
{
    if (st->wifi_job) return;
    if (WlanConnectStart(0, NULL, NULL)) { st->wifi_job = 3; st->wifi_note[0] = '\0'; }
}

/* A background scan or connection finished: true if the page changed */
static bool wifi_poll(Settings *st)
{
    if (st->wifi_job == 1) {
        int n;
        UINT32 status;
        if (!WlanScanDone(st->wifi_bss, WLAN_MAX_BSS, &n, &status)) return false;
        char chosen[33] = "";
        if (st->wifi_sel >= 0 && st->wifi_sel < st->wifi_n) strcpy(chosen, st->wifi_bss[st->wifi_sel].ssid);
        st->wifi_n = n < 0 ? 0 : n;
        st->wifi_sel = -1;
        for (int i = 0; i < st->wifi_n; i++) if (chosen[0] && !strcmp(st->wifi_bss[i].ssid, chosen)) st->wifi_sel = i;
        if (n < 0) ksnprintf(st->wifi_note, sizeof(st->wifi_note), "The scan failed (0x%08x).", status);
        kprintf("[SETTINGS] Wi-Fi: %d network(s) found\n", st->wifi_n);
        st->wifi_job = 0;
        st->wifi_scanned = true;
        return true;
    }
    if (st->wifi_job == 2 || st->wifi_job == 3) {
        int r;
        UINT32 status;
        if (!WlanConnectDone(&r, &status)) return false;
        const char *s = st->wifi_ssid;
        switch (r) {
        case WLAN_OK:
            if (st->wifi_job == 2) kprintf("[SETTINGS] Wi-Fi: connected to %s\n", s);
            else kprintf("[SETTINGS] Wi-Fi: disconnected\n");
            st->wifi_note[0] = '\0';
            st->wifi_sel = -1;
            break;
        case WLAN_NO_NETWORK:  ksnprintf(st->wifi_note, sizeof(st->wifi_note), "\"%s\" is not in range now.", s); break;
        case WLAN_BAD_KEY:     ksnprintf(st->wifi_note, sizeof(st->wifi_note),
                                         "The network security key isn't correct: \"%s\" did not accept it.", s); break;
        case WLAN_KEY_FORMAT:  strcpy(st->wifi_note, "A WPA2 passphrase is 8 to 63 characters, or 64 hexadecimal "
                                                     "digits; a WPA3 password 8 to 63 characters."); break;
        case WLAN_NEEDS_KEY:   ksnprintf(st->wifi_note, sizeof(st->wifi_note), "Type the network security key for \"%s\".", s);
                               break;
        case WLAN_UNSUPPORTED: ksnprintf(st->wifi_note, sizeof(st->wifi_note), "\"%s\" uses security NovaOS does not "
                                         "support yet (WEP, WPA or Enterprise).", s); break;
        default:               ksnprintf(st->wifi_note, sizeof(st->wifi_note), "Can't connect to \"%s\" (0x%08x).", s, status);
        }
        if (r != WLAN_OK) kprintf("[SETTINGS] Wi-Fi: %s\n", st->wifi_note);
        st->wifi_job = 0;
        return true;
    }
    return false;
}

/* Four signal bars, @q (0-100) of them lit */
static void wifi_bars(int x, int y, UINT32 q)
{
    for (int i = 0; i < 4; i++) {
        int h = 5 + i * 4;
        bool on = q > (UINT32)(i * 25);
        GdiFillRect(RECT(x + i * 6, y + 18 - h, 4, h), on ? UI_TEXT : UI_LINE);
    }
}

static int page_wifi(Settings *st, int x, int y, int w)
{
    bool have = WlanCount() > 0;
    if (have) WlanPeekInfo(0, &st->wifi_info);
    const WlanInfo info = st->wifi_info;
    GdiRoundRect(RECT(x, y, w, 72), 8, UI_CARD, GDI_TRANSPARENT);
    GdiTextBold(x + 16, y + 14, have && info.description[0] ? info.description : "Wi-Fi", UI_TEXT);
    char state[96];
    if (!have) strcpy(state, "No Wi-Fi adapter found");
    else if (st->wifi_job == 2) ksnprintf(state, sizeof(state), "Connecting to %s...", st->wifi_ssid);
    else if (st->wifi_job == 3) strcpy(state, "Disconnecting...");
    else if (info.connected) ksnprintf(state, sizeof(state), "Connected to %s (%s)", info.bss.ssid, info.bss.auth);
    else if (st->wifi_job == 1) strcpy(state, "Looking for networks...");
    else strcpy(state, "Not connected");
    GdiColor dot = !have ? GDI_C(0xD0, 0x40, 0x40) : info.connected ? GDI_C(0x2E, 0xB8, 0x5C) : GDI_C(0xE8, 0xA8, 0x20);
    GdiRoundRect(RECT(x + 16, y + 44, 10, 10), 5, dot, GDI_TRANSPARENT);
    GdiTextT(x + 34, y + 40, state, UI_TEXT2);
    if (!have) {
        GdiTextT(x, y + 88, "Wi-Fi needs a wireless adapter with a Windows driver (Device Manager lists the "
                            "devices found).", UI_TEXT2);
        return y + 88 + GDI_FONT_H;
    }
    UiButton(r_wifi_refresh(x, y, w), "Refresh", false);
    if (info.connected) UiButton(r_wifi_disconnect(x, y, w), "Disconnect", false);
    int n = st->wifi_n < WIFI_SHOWN ? st->wifi_n : WIFI_SHOWN;
    if (!n) {
        GdiTextT(x, y + 96, st->wifi_scanned ? "No networks in range." : "Looking for networks...", UI_TEXT2);
        if (!st->wifi_note[0]) return y + 96 + GDI_FONT_H;
    }
    for (int i = 0; i < n; i++) {
        const WlanBss *b = &st->wifi_bss[i];
        int ry = wifi_row_y(st, y, i);
        bool sel = i == st->wifi_sel, here = info.connected && !strcmp(info.bss.ssid, b->ssid);
        GdiRoundRect(RECT(x, ry, w, WIFI_ROW_H - 8 + (sel ? WIFI_FORM_H : 0)), 8, sel ? UI_HOVER : UI_CARD, GDI_TRANSPARENT);
        wifi_bars(x + 16, ry + 12, b->quality);
        GdiTextBold(x + 52, ry + 6, b->ssid[0] ? b->ssid : "(hidden network)", UI_TEXT);
        char line[128];
        ksnprintf(line, sizeof(line), "%s%s  -  Signal %u%%  -  %s GHz, channel %u", here ? "Connected  -  " : "",
                  wifi_secured(b) ? b->auth : "Open", b->quality,
                  b->band == 1 ? "2.4" : b->band == 2 ? "5" : "6", b->channel);
        GdiTextT(x + 52, ry + 26, line, UI_TEXT2);
        if (!sel || here) continue;
        if (wifi_secured(b)) {
            GdiRect k = r_wifi_key(st, x, y, w);
            GdiRoundRect(k, 6, UI_BG, GDI_TRANSPARENT);
            GdiRoundBorderAlpha(k, 6, UI_ACCENT, 200);
            char dots[64];
            size_t kl = strlen(st->wifi_key);
            for (size_t c = 0; c < kl; c++) dots[c] = '*';
            dots[kl] = '\0';
            if (kl) GdiTextT(k.x + 10, k.y + (k.h - GDI_FONT_H) / 2, dots, UI_TEXT);
            else GdiTextT(k.x + 10, k.y + (k.h - GDI_FONT_H) / 2, "Network security key", UI_TEXT2);
            GdiFillRect(RECT(k.x + 10 + GdiTextW(dots) + 1, k.y + 7, 2, k.h - 14), UI_ACCENT);
        }
        UiButton(r_wifi_connect(st, x, y, w), "Connect", true);
    }
    int ny = n ? wifi_row_y(st, y, n) + 4 : y + 96 + 24;
    if (st->wifi_n > WIFI_SHOWN) {
        char more[64];
        ksnprintf(more, sizeof(more), "and %d more", st->wifi_n - WIFI_SHOWN);
        GdiTextT(x + 16, ny, more, UI_TEXT2);
        ny += 24;
    }
    if (st->wifi_note[0]) { GdiTextT(x, ny, st->wifi_note, GDI_C(0xE0, 0x60, 0x60)); ny += 24; }
    return ny;
}

static void wifi_choose(Settings *st, int i)
{
    if (i != st->wifi_sel) memset(st->wifi_key, 0, sizeof(st->wifi_key));
    st->wifi_sel = i;
    st->wifi_note[0] = '\0';
}

static void wifi_mouse(Settings *st, int x, int y, int w, int mx, int my)
{
    if (!WlanCount()) return;
    if (UiHit(r_wifi_refresh(x, y, w), mx, my)) { wifi_scan(st); return; }
    bool connected = st->wifi_info.connected;
    if (connected && UiHit(r_wifi_disconnect(x, y, w), mx, my)) { wifi_disconnect(st); return; }
    if (st->wifi_sel >= 0 && UiHit(r_wifi_connect(st, x, y, w), mx, my)) { wifi_connect(st); return; }
    if (st->wifi_sel >= 0 && UiHit(r_wifi_key(st, x, y, w), mx, my)) return;
    int n = st->wifi_n < WIFI_SHOWN ? st->wifi_n : WIFI_SHOWN;
    for (int i = 0; i < n; i++)
        if (UiHit(RECT(x, wifi_row_y(st, y, i), w, WIFI_ROW_H - 8), mx, my)) { wifi_choose(st, i == st->wifi_sel ? -1 : i); return; }
}

/* Up and Down choose a network, typing goes to its passphrase, Enter
 * connects, F5 scans again, Esc lets go (then Up and Down move between pages
 * again); false: not the page's key */
static bool wifi_key(Settings *st, const KeyEvent *k)
{
    int n = st->wifi_n < WIFI_SHOWN ? st->wifi_n : WIFI_SHOWN;
    if (k->extended && k->scancode == KEY_DOWN && n) { wifi_choose(st, st->wifi_sel + 1 < n ? st->wifi_sel + 1 : n - 1); return true; }
    if (k->extended && k->scancode == KEY_UP && st->wifi_sel >= 0) { wifi_choose(st, st->wifi_sel > 0 ? st->wifi_sel - 1 : 0); return true; }
    if (k->scancode == 0x3F && !k->extended) { wifi_scan(st); return true; }   /* (F5) */
    if (st->wifi_sel < 0) return false;
    if (k->scancode == KEY_ESC) { wifi_choose(st, -1); return true; }
    if (k->scancode == KEY_ENTER && !k->extended) { wifi_connect(st); return true; }
    size_t kl = strlen(st->wifi_key);
    if (k->ch == '\b') { if (kl) st->wifi_key[kl - 1] = '\0'; return true; }
    if (!k->ctrl && !k->alt && k->ch >= ' ' && k->ch < 0x7F && kl < sizeof(st->wifi_key) - 1) {
        st->wifi_key[kl] = k->ch;
        st->wifi_key[kl + 1] = '\0';
        return true;
    }
    return false;
}

static void current_locale(char *out, int cap);

/* Each page's on_tick part: true when what it shows changed (a repaint) */
static bool tick_time_language(Settings *st)                  /* intl.exe has written the choice */
{
    char cur[32];
    current_locale(cur, sizeof(cur));
    int tz = TzCurrent(), kbd = KbdCurrent();
    if (!strcmp(cur, st->locale) && tz == st->tz && kbd == st->kbd) return false;
    strcpy(st->locale, cur);
    st->tz = tz;
    st->kbd = kbd;
    return true;
}

static bool tick_thumbs(Settings *st)                         /* wallpaper thumbnails made */
{
    UINT32 g = WallpaperThumbsGen();
    if (g == st->thumbs_gen) return false;
    st->thumbs_gen = g;
    return true;
}

static bool tick_printers(Settings *st) { return um_registry_generation() != st->prn_gen; }   /* added, named, removed... */

static bool tick_sound(Settings *st)                          /* a device plugged in or out */
{
    SoundDevs d;
    sound_devs(&d);
    return sound_sig(&d) != st->sound_sig;
}

static bool tick_wifi(Settings *st)                           /* the first scan; the adapter's state */
{
    if (!st->wifi_scanned && !st->wifi_job) { wifi_scan(st); return st->wifi_job != 0; }
    UINT32 sig = WlanCount() ? 1u : 0;
    if (sig && WlanPeekInfo(0, &st->wifi_info)) {
        const WlanInfo *info = &st->wifi_info;
        sig |= (info->connected ? 2u : 0) ^ ((UINT32)info->bss.quality << 2) ^ ((UINT32)info->bss.ssid[0] << 9);
    } else if (sig) sig = st->sig;                            /* (busy: nothing new to show) */
    if (sig == st->sig) return false;
    st->sig = sig;
    return true;
}

static bool tick_network(Settings *st)                        /* the connection's state */
{
    NetStatus ns;
    NetGetStatus(&ns);
    UINT32 sig = ns.ip ^ ns.gw ^ ns.dns[0] ^ (ns.link ? 1u : 0) ^ (ns.configured ? 2u : 0) ^ (ns.dhcp6 ? 4u : 0) ^
                 ((UINT32)ns.ip6_count << 3) ^ (ns.dhcp6_valid - ns.dhcp6_age) / 60 << 6 ^ (ns.present ? 1u << 30 : 0);
    if (sig == st->sig) return false;
    st->sig = sig;
    return true;
}

static bool tick_display(Settings *st)                        /* a monitor plugged in or out, a mode changed */
{
    int mon = chosen(st);
    DisplayMode m = DisplayHeadMode(mon);
    UINT32 sig = (UINT32)GdiMonitorCount() ^ (UINT32)m.w << 4 ^ (UINT32)m.h << 17 ^ (UINT32)GdiMonitorScale(mon) << 28 ^
                 (UINT32)GdiMonitorDpi(mon) << 20;
    if (sig == st->sig) return false;
    st->sig = sig;
    return true;
}

/* System (its uptime and memory) each second; Storage every five */
static bool tick_live(Settings *st, UINT32 every)
{
    UINT32 now = (UINT32)(sched_ticks() / 100 / every);
    if (now == st->live) return false;
    st->live = now;
    return true;
}
static bool tick_system(Settings *st)  { return tick_live(st, 1); }
static bool tick_storage(Settings *st) { return tick_live(st, 5); }

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
    if (st->nhits >= PHITS_MAX || !GdiInClip(r)) return;     /* (parts scrolled out of view are not clickable) */
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

static int page_personalize(Settings *st, GdiRect c, int x, int y, int w)
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
    return y + (st->ptab ? icons_tab(st, c, x, y, w) : background_tab(st, c, x, y, w));
}

static void personalize_click(Settings *st, int x, int y)
{
    for (int i = 0; i < st->nhits; i++) {
        PHit *h = &st->hits[i];
        if (!UiHit(h->r, x, y)) continue;
        RamNode *n = h->path[0] ? RamfsResolve(NULL, h->path) : NULL;
        switch (h->kind) {
        case PH_TAB:         if (st->ptab != h->i) { st->ptab = h->i; UiScrollTo(&st->sb, 0); } break;
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

static int page_time_language(int x, int y, int w)
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
    return y + GDI_FONT_H;
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

static int acc_form(Settings *st, GdiRect c, int x, int y, int w)
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
        return ey + 30 + 32 + (st->acc_err[0] ? 12 + GDI_FONT_H : 0);
    }
    if (!known) {                                             /* (removed meanwhile) */
        st->acc_form = ACC_NONE;
        GdiTextT(x, y, "That account is no longer on this PC.", UI_TEXT2);
        return y + GDI_FONT_H;
    }
    if (st->acc_form == ACC_TYPE) {
        char t[64];
        ksnprintf(t, sizeof(t), "Change account type: %s", a.full[0] ? a.full : a.name);
        GdiTextBold(x, y, t, UI_TEXT);
        acc_chips(st, c, x, y + 32);
        GdiTextT(x, y + 32 + CHIP_H + 16, "An administrator can change settings, install programs and manage other accounts.", UI_TEXT2);
        acc_buttons(st, c, x, y + 32 + CHIP_H + 48, "OK");
        return y + 32 + CHIP_H + 48 + 32 + (st->acc_err[0] ? 12 + GDI_FONT_H : 0);
    }
    if (st->acc_form == ACC_REMOVE) {
        char t[64], l[160];
        ksnprintf(t, sizeof(t), "Delete account and data? (%s)", a.name);
        GdiTextBold(x, y, t, UI_TEXT);
        ksnprintf(l, sizeof(l), "Deleting %s's account removes their files from this PC: %s.", a.name, a.profile[0] ? a.profile : "C:\\Users\\...");
        GdiTextT(x, y + 32, l, UI_TEXT2);
        GdiTextT(x, y + 56, "Back up anything you want to keep before you delete it.", UI_TEXT2);
        acc_buttons(st, c, x, y + 92, "Delete account and data");
        return y + 92 + 32 + (st->acc_err[0] ? 12 + GDI_FONT_H : 0);
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
    return ey + 32;
}

static int page_accounts(Settings *st, GdiRect c, int x, int y, int w)
{
    st->nhits = 0;
    if (st->acc_form) return acc_form(st, c, x, y, w);
    UmAccount me;
    if (!acc_me(&me)) {
        GdiTextT(x, y, "The account signed in could not be read; sign out and in again.", UI_TEXT2);
        return y + GDI_FONT_H;
    }
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
    if (!shown) { GdiTextT(x, y + 4, "No other accounts on this PC yet.", UI_TEXT2); y += 4 + GDI_FONT_H; }
    return y;
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

/* Printers: the printers (the keys under HKLM\SYSTEM\CurrentControlSet\
 * Control\Print\Printers, which winspool.drv and the Print Spooler keep),
 * the default (HKCU ...\Windows NT\CurrentVersion\Windows "Device"), and
 * a network printer added by its address: its key, on an IPP port with
 * the Microsoft IPP Class Driver and "Pending Setup", after which the
 * spooler asks the printer for its make and model, names it after them and
 * records where it is (userland/programs/spoolsv.c).  Layout shared by
 * set_paint + set_mouse. */
#define PRN_KEY    "Machine\\SYSTEM\\CurrentControlSet\\Control\\Print\\Printers"
#define PRN_DEVICE "User\\" UM_USER_SID "\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows"
#define PRN_MAX    32
#define PRN_SHOWN  5                 /* (the rows that fit above the Add form) */
#define PRN_ROW_H  60
typedef struct { char name[128], port[160], location[96]; UINT32 status; bool pending; } Prn;
static Prn g_prn[PRN_MAX];

static int prn_list(Prn *p, int cap)
{
    int n = 0;
    for (UINT32 i = 0; n < cap; i++) {
        UINT16 w[128];
        int len = um_registry_enum_key(PRN_KEY, i, w, 127, NULL);
        if (len < 0) break;
        if (len > 127) continue;
        Prn *q = &p[n];
        memset(q, 0, sizeof(*q));
        bool ascii = true;
        for (int k = 0; k < len; k++) { if (w[k] < 0x20 || w[k] > 0x7E) ascii = false; q->name[k] = (char)w[k]; }
        if (!ascii) continue;                   /* (the kernel's registry paths are ASCII) */
        char path[300];
        ksnprintf(path, sizeof(path), PRN_KEY "\\%s", q->name);
        um_registry_get_sz(path, "Port", q->port, sizeof(q->port));
        um_registry_get_sz(path, "Location", q->location, sizeof(q->location));
        UINT32 v = 0;
        if (um_registry_get_dword(path, "Status", &v)) q->status = v;
        q->pending = um_registry_get_dword(path, "Pending Setup", &v) && v;
        n++;
    }
    return n;
}

/* The default printer's name (as winspool.drv picks it: "Device", else the first printer) */
static void prn_default(const Prn *p, int n, char *out, int cap)
{
    char dev[300];
    out[0] = '\0';
    if (um_registry_get_sz(PRN_DEVICE, "Device", dev, sizeof(dev))) {
        char *comma = strchr(dev, ',');
        if (comma) *comma = '\0';
        for (int i = 0; i < n; i++) if (!strcmp(p[i].name, dev)) { strncpy(out, dev, (size_t)cap - 1); out[cap - 1] = '\0'; return; }
    }
    if (n) { strncpy(out, p[0].name, (size_t)cap - 1); out[cap - 1] = '\0'; }
}

static GdiRect r_prn_remove(int x, int y, int w, int i)  { return RECT(x + w - 16 - 96, y + i * PRN_ROW_H + 12, 96, 32); }
static GdiRect r_prn_default(int x, int y, int w, int i) { return RECT(x + w - 16 - 96 - 12 - 136, y + i * PRN_ROW_H + 12, 136, 32); }
static int prn_add_y(int y, int n) { return y + (n < 1 ? 1 : n > PRN_SHOWN ? PRN_SHOWN + 1 : n) * PRN_ROW_H + 8; }
static GdiRect r_prn_box(int x, int ay, int w) { return RECT(x, ay + 70, w - 112, 32); }
static GdiRect r_prn_add(int x, int ay, int w) { return RECT(x + w - 100, ay + 70, 100, 32); }

static int page_printers(Settings *st, int x, int y, int w)
{
    Prn *p = g_prn;
    int n = prn_list(p, PRN_MAX);
    char def[128];
    prn_default(p, n, def, sizeof(def));
    st->prn_gen = um_registry_generation();
    for (int i = 0; i < n && i < PRN_SHOWN; i++) {
        int ry = y + i * PRN_ROW_H;
        bool is_def = !strcmp(p[i].name, def);
        GdiRoundRect(RECT(x, ry, w, PRN_ROW_H - 8), 8, UI_CARD, GDI_TRANSPARENT);
        AppDrawGlyph(GL_PRINTER, x + 16, ry + 14, 24, UI_TEXT2);
        GdiTextBold(x + 52, ry + 8, p[i].name, UI_TEXT);
        char line[300];
        const char *state = p[i].pending ? "Setting up..." : (p[i].status & 0x80) ? "Offline" :
                            (p[i].status & 0x1) ? "Paused" : "Ready";
        /* "Default - Ready - where it is" (or its port when it says nowhere) */
        const char *where = p[i].location[0] ? p[i].location : strcmp(p[i].port, "PORTPROMPT:") ? p[i].port : "";
        ksnprintf(line, sizeof(line), "%s%s%s%s", is_def ? "Default  -  " : "", state, where[0] ? "  -  " : "", where);
        GdiTextT(x + 52, ry + 30, line, UI_TEXT2);
        if (!is_def) UiButton(r_prn_default(x, y, w, i), "Set as default", false);
        UiButton(r_prn_remove(x, y, w, i), "Remove", false);
    }
    if (!n) GdiTextT(x, y + 8, "No printers.", UI_TEXT2);
    if (n > PRN_SHOWN) {
        char more[64];
        ksnprintf(more, sizeof(more), "and %d more", n - PRN_SHOWN);
        GdiTextT(x + 16, y + PRN_SHOWN * PRN_ROW_H + 12, more, UI_TEXT2);
    }
    int ay = prn_add_y(y, n);
    GdiTextBold(x, ay, "Add a network printer", UI_TEXT);
    GdiTextT(x, ay + 24, "Its IP address or host name, or an ipp://, ipps:// or http:// address.", UI_TEXT2);
    GdiTextT(x, ay + 44, "Printers that support IPP Everywhere (Mopria) print without a driver.", UI_TEXT2);
    GdiRect b = r_prn_box(x, ay, w);
    GdiRoundRect(b, 6, UI_BG, GDI_TRANSPARENT);
    GdiRoundBorderAlpha(b, 6, st->prn_focus ? UI_ACCENT : GDI_WHITE, st->prn_focus ? 200 : 30);
    if (st->prn_text[0] || st->prn_focus) GdiTextT(b.x + 10, b.y + (b.h - GDI_FONT_H) / 2, st->prn_text, UI_TEXT);
    else GdiTextT(b.x + 10, b.y + (b.h - GDI_FONT_H) / 2, "192.168.1.20", UI_TEXT2);
    if (st->prn_focus) GdiFillRect(RECT(b.x + 10 + GdiTextW(st->prn_text) + 1, b.y + 7, 2, b.h - 14), UI_ACCENT);
    UiButton(r_prn_add(x, ay, w), "Add", true);
    if (st->prn_err[0]) { GdiTextT(x, ay + 112, st->prn_err, GDI_C(0xE0, 0x60, 0x60)); return ay + 112 + GDI_FONT_H; }
    return ay + 102;
}

/* Add the printer whose address was typed */
static void prn_add(Settings *st)
{
    char *t = st->prn_text;
    while (*t == ' ') t++;
    size_t tl = strlen(t);
    while (tl && t[tl - 1] == ' ') t[--tl] = '\0';
    st->prn_err[0] = '\0';
    if (!*t) { strcpy(st->prn_err, "Type the printer's address."); return; }
    char url[200];
    const char *sep = strstr(t, "://");
    if (sep) {
        if (strncmp(t, "ipp://", 6) && strncmp(t, "ipps://", 7) && strncmp(t, "http://", 7) && strncmp(t, "https://", 8)) {
            strcpy(st->prn_err, "The address must start with ipp://, ipps://, http:// or https://.");
            return;
        }
        ksnprintf(url, sizeof(url), "%s", t);
    } else {
        ksnprintf(url, sizeof(url), strchr(t, '/') ? "ipp://%s" : "ipp://%s/ipp/print", t);
    }
    const char *host = strstr(url, "://") + 3;
    char h[96];
    int hn = 0;
    while (host[hn] && host[hn] != '/' && host[hn] != ':' && hn < 95) { h[hn] = host[hn]; hn++; }
    h[hn] = '\0';
    if (!hn) { strcpy(st->prn_err, "The address has no host name."); return; }
    for (const char *c = url; *c; c++)
        if (*c == '\\' || *c == ',' || *c == ' ' || *c < 0x20 || *c > 0x7E) {
            strcpy(st->prn_err, "The address cannot have spaces, commas or backslashes.");
            return;
        }
    Prn *p = g_prn;
    int n = prn_list(p, PRN_MAX);
    for (int i = 0; i < n; i++)
        if (!strcmp(p[i].port, url)) { strcpy(st->prn_err, "That printer is already added."); return; }
    char name[128], path[300];
    for (int k = 1; k < 10; k++) {
        if (k == 1) ksnprintf(name, sizeof(name), "Printer at %s", h);
        else ksnprintf(name, sizeof(name), "Printer at %s (%d)", h, k);
        ksnprintf(path, sizeof(path), PRN_KEY "\\%s", name);
        if (!um_registry_key_exists(path)) break;
    }
    um_registry_set_sz(path, "Name", name);
    um_registry_set_sz(path, "Port", url);
    um_registry_set_sz(path, "Printer Driver", "Microsoft IPP Class Driver");
    um_registry_set_sz(path, "Print Processor", "winprint");
    um_registry_set_sz(path, "Datatype", "RAW");
    um_registry_set_sz(path, "Description", "");
    um_registry_set_sz(path, "Location", "");
    um_registry_set_dword(path, "Attributes", 0x10);         /* PRINTER_ATTRIBUTE_NETWORK */
    um_registry_set_dword(path, "Priority", 1);
    um_registry_set_dword(path, "Pending Setup", 1);         /* (the spooler names it) */
    kprintf("[SETTINGS] Added printer \"%s\" on %s\n", name, url);
    st->prn_text[0] = '\0';
    st->prn_focus = false;
}

static void prn_mouse(Settings *st, int x, int y, int w, int mx, int my)
{
    Prn *p = g_prn;
    int n = prn_list(p, PRN_MAX);
    char def[128];
    prn_default(p, n, def, sizeof(def));
    int ay = prn_add_y(y, n);
    st->prn_focus = UiHit(r_prn_box(x, ay, w), mx, my);
    if (UiHit(r_prn_add(x, ay, w), mx, my)) { prn_add(st); return; }
    for (int i = 0; i < n && i < PRN_SHOWN; i++) {
        char path[300];
        ksnprintf(path, sizeof(path), PRN_KEY "\\%s", p[i].name);
        bool is_def = !strcmp(p[i].name, def);
        if (!is_def && UiHit(r_prn_default(x, y, w, i), mx, my)) {
            char dev[300];
            ksnprintf(dev, sizeof(dev), "%s,winspool,%s", p[i].name, p[i].port);
            um_registry_set_sz(PRN_DEVICE, "Device", dev);
            return;
        }
        if (UiHit(r_prn_remove(x, y, w, i), mx, my)) {
            um_registry_delete_tree(path);
            if (is_def) um_registry_delete_value(PRN_DEVICE, "Device");
            kprintf("[SETTINGS] Removed printer \"%s\"\n", p[i].name);
            return;
        }
    }
}

static void prn_key(Settings *st, const KeyEvent *k)
{
    if (k->scancode == KEY_ESC) { st->prn_focus = false; return; }
    if (k->scancode == KEY_ENTER && !k->extended) { prn_add(st); return; }
    size_t n = strlen(st->prn_text);
    if (k->ch == '\b') { if (n) st->prn_text[n - 1] = '\0'; }
    else if (!k->ctrl && !k->alt && k->ch > ' ' && k->ch < 0x7F && n < sizeof(st->prn_text) - 1) {
        st->prn_text[n] = k->ch;
        st->prn_text[n + 1] = '\0';
    }
}

static int page_about(int x, int y, int w)
{
    GdiTextLarge(x, y, "NovaOS", UI_TEXT);             y += 40;
    char ver[96];
    ksnprintf(ver, sizeof(ver), "Version %s  -  a Windows-compatible OS research project", NovaVersion());
    GdiTextT(x, y, ver, UI_TEXT2); y += 34;
    row(x, y, w, "Kernel", "Nova, NT-style syscalls (Win10 1903 ABI)"); y += 50;
    row(x, y, w, "Desktop", "Kernel GDI + window manager");            y += 50;
    row(x, y, w, "UI font", "Inter 4.1 (SIL OFL 1.1)");                 y += 50;
    row(x, y, w, "Monospace font", "Cascadia Mono (SIL OFL 1.1)");
    return y + 44;
}

/* -----------------------------------------------------------------------
 * The pages, and the frame that shows them
 * ----------------------------------------------------------------------- */

/* Mouse hooks get client coordinates (x, y) and the page's origin (px, py:
 * its content's top left, client-relative) and width; for a page with
 * .hits, y is where the pointer is, for the others where it is in the
 * unscrolled page, which the page's layout functions compute against */
typedef struct {
    const char *name;
    Glyph glyph;
    const char *uri;                 /* its ms-settings: names (space separated) */
    const char *keys;                /* what the search box and Start's search find it by */
    int  (*paint)(Settings *st, GdiRect c, int x, int y, int w);   /* the content's bottom */
    void (*mouse)(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw);
    bool (*key)(Settings *st, const KeyEvent *k);                  /* (pressed keys) true: taken */
    bool (*tick)(Settings *st);
    bool hits;                       /* the page's clicks go by st->hits (recorded as drawn) */
} SetPage;

static int paint_system(Settings *st, GdiRect c, int x, int y, int w)
{
    (void)c;
    page_system(x, y, w);
    return page_recovery(st, x, y, w);
}
static int paint_display(Settings *st, GdiRect c, int x, int y, int w)  { (void)c; return page_display(st, x, y, w); }
static int paint_sound(Settings *st, GdiRect c, int x, int y, int w)    { (void)c; return page_sound(st, x, y, w); }
static int paint_storage(Settings *st, GdiRect c, int x, int y, int w)  { (void)c; (void)st; return page_storage(x, y, w); }
static int paint_network(Settings *st, GdiRect c, int x, int y, int w)  { (void)c; return page_network(st, x, y, w); }
static int paint_wifi(Settings *st, GdiRect c, int x, int y, int w)     { (void)c; return page_wifi(st, x, y, w); }
static int paint_time(Settings *st, GdiRect c, int x, int y, int w)     { (void)c; (void)st; return page_time_language(x, y, w); }
static int paint_firewall(Settings *st, GdiRect c, int x, int y, int w) { (void)c; (void)st; return FirewallPagePaint(x, y, w); }
static int paint_printers(Settings *st, GdiRect c, int x, int y, int w) { (void)c; return page_printers(st, x, y, w); }
static int paint_about(Settings *st, GdiRect c, int x, int y, int w)    { (void)c; (void)st; return page_about(x, y, w); }

static void mouse_system(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    (void)c;
    if (msg == WM_MOUSE_DOWN) recovery_click(st, x, y, px, py, pw);
}

static void mouse_display(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    if (multi()) {
        /* the arrangement (layout matches page_display) */
        Arr a = arr_of(px, py + ARR_DY, pw);
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
    if (msg != WM_MOUSE_DOWN) return;
    /* resolution chips, then the DPI chips (layout matches page_display) */
    int cols = chip_cols(pw);
    int mon0 = chosen(st), dy = py + dpi_chips_dy(mon0, pw);
    if (GdiMonitorScale(mon0) >= 2 && y >= dy && y < dy + CHIP_H && x >= px &&
        (x - px) % (CHIP_W + CHIP_GAP) < CHIP_W && (x - px) / (CHIP_W + CHIP_GAP) < 2) {
        DesktopSetMonitorDpi(mon0, (x - px) / (CHIP_W + CHIP_GAP) ? 192 : 96, true);
        return;
    }
    int my = py + modes_dy();
    if (x < px || y < my) return;
    int col = (x - px) / (CHIP_W + CHIP_GAP), r = (y - my) / (CHIP_H + CHIP_GAP);
    if (col >= cols || (x - px) % (CHIP_W + CHIP_GAP) >= CHIP_W || (y - my) % (CHIP_H + CHIP_GAP) >= CHIP_H) return;
    DisplayMode m;
    int mon = chosen(st);
    if (!DisplayHeadModeAt(mon, r * cols + col, &m)) return;
    if (DesktopSetHeadMode(mon, m.w, m.h)) DesktopSaveHeadMode(mon, m.w, m.h);
}

static void mouse_sound(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    (void)st; (void)c;
    if (msg == WM_MOUSE_DOWN) sound_click(x - px, y - py, pw);   /* device rows (layout matches page_sound) */
}

static void mouse_personalize(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    (void)c; (void)px; (void)py; (void)pw;
    if (msg == WM_MOUSE_DOWN) personalize_click(st, x, y);
}

static void mouse_network(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    (void)c;
    if (msg == WM_MOUSE_DOWN) net_mouse(st, px, py, pw, x, y);
}

static void mouse_wifi(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    (void)c;
    if (msg == WM_MOUSE_DOWN) wifi_mouse(st, px, py, pw, x, y);
}

static void mouse_accounts(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    (void)c; (void)px; (void)py; (void)pw;
    if (msg == WM_MOUSE_DOWN) accounts_click(st, x, y);
}

static void mouse_time(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    (void)st; (void)c;
    if (msg != WM_MOUSE_DOWN) return;
    /* the time zone's and keyboard's Change, format chips (layout matches page_time_language) */
    if (UiHit(r_tz_change(px, py, pw), x, y)) { WelcomeTimeZone(); return; }
    if (UiHit(r_kbd_change(px, py, pw), x, y)) { WelcomeKeyboard(); return; }
    int cols = loc_cols(pw), ly = py + LOCALES_DY;
    if (x < px || y < ly) return;
    int col = (x - px) / (LOC_CHIP_W + CHIP_GAP), r = (y - ly) / (CHIP_H + CHIP_GAP), i = r * cols + col;
    if (col >= cols || (x - px) % (LOC_CHIP_W + CHIP_GAP) >= LOC_CHIP_W ||
        (y - ly) % (CHIP_H + CHIP_GAP) >= CHIP_H || i >= N_FORMATS) return;
    set_locale(g_formats[i].name);
}

static void mouse_printers(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    (void)c;
    if (msg == WM_MOUSE_DOWN) prn_mouse(st, px, py, pw, x, y);
}

static void mouse_firewall(Settings *st, GdiRect c, WmMouseMsg msg, int x, int y, int px, int py, int pw)
{
    (void)st; (void)c;
    if (msg == WM_MOUSE_DOWN) FirewallPageMouse(px, py, pw, x, y);
}

static bool tick_firewall(Settings *st) { (void)st; return FirewallPageTick(); }   /* a profile, rule or network changed */

static bool key_network(Settings *st, const KeyEvent *k)  { if (!st->net_edit) return false; net_key(st, k); return true; }
static bool key_accounts(Settings *st, const KeyEvent *k) { if (!st->acc_form) return false; accounts_key(st, k); return true; }
static bool key_printers(Settings *st, const KeyEvent *k) { if (!st->prn_focus) return false; prn_key(st, k); return true; }

/* (Start's search lists these too: desktop.c asks SettingsPageName/Keys) */
static const SetPage g_pages[] = {
    [SETTINGS_SYSTEM] = { "System", GL_PC, "system recovery",
        "system about pc processor memory uptime recovery reset advanced startup safe mode",
        paint_system, mouse_system, NULL, tick_system, false },
    [SETTINGS_DISPLAY] = { "Display", GL_WINDOWS, "display screenrotation",
        "display screen resolution scale monitor dpi arrange multiple displays adapter",
        paint_display, mouse_display, NULL, tick_display, false },
    [SETTINGS_SOUND] = { "Sound", GL_SPEAKER, "sound sound-devices apps-volume",
        "sound audio speakers headphones headset microphone output input device usb volume",
        paint_sound, mouse_sound, NULL, tick_sound, false },
    [SETTINGS_PERSONALIZE] = { "Personalization", GL_PICTURES,
        "personalization personalization-background personalization-colors themes",
        "personalization wallpaper background theme colors icons icon set picture",
        page_personalize, mouse_personalize, NULL, tick_thumbs, true },
    [SETTINGS_STORAGE] = { "Storage", GL_FOLDER, "storagesense savelocations",
        "storage disk drive space memory ram",
        paint_storage, NULL, NULL, tick_storage, false },
    [SETTINGS_NETWORK] = { "Network", GL_NETWORK, "network network-status network-ethernet network-advancedsettings",
        "network ethernet internet ip dns certificates dhcp gateway adapter",
        paint_network, mouse_network, key_network, tick_network, false },
    [SETTINGS_WIFI] = { "Wi-Fi", GL_NETWORK, "network-wifi network-wifisettings availablenetworks",
        "wi-fi wifi wireless wlan networks ssid connect join",
        paint_wifi, mouse_wifi, wifi_key, tick_wifi, false },
    [SETTINGS_FIREWALL] = { "Firewall", GL_SHIELD, "windowsdefender network-firewall",
        "firewall network security protection public private profile allow app port rule",
        paint_firewall, mouse_firewall, NULL, tick_firewall, false },
    [SETTINGS_ACCOUNTS] = { "Accounts", GL_PERSON, "accounts yourinfo otherusers signinoptions",
        "accounts users user add other family password sign in picture administrator switch",
        page_accounts, mouse_accounts, key_accounts, tick_thumbs, true },
    [SETTINGS_TIME_LANGUAGE] = { "Time & language", GL_GEAR, "dateandtime regionformatting regionlanguage keyboard",
        "time language region regional format locale date number currency zone keyboard layout",
        paint_time, mouse_time, NULL, tick_time_language, false },
    [SETTINGS_PRINTERS] = { "Printers", GL_PRINTER, "printers",
        "printers printing printer scanner ipp default add network",
        paint_printers, mouse_printers, key_printers, tick_printers, false },
    [SETTINGS_ABOUT] = { "About", GL_NOVA, "about",
        "about version fonts licence",
        paint_about, NULL, NULL, NULL, false },
};
#define N_PAGES ((int)(sizeof(g_pages) / sizeof(g_pages[0])))

int SettingsPageCount(void) { return N_PAGES; }
const char *SettingsPageName(int page) { return page >= 0 && page < N_PAGES ? g_pages[page].name : ""; }
const char *SettingsPageKeys(int page) { return page >= 0 && page < N_PAGES ? g_pages[page].keys : ""; }

/* Case-insensitive: @needle (its first @n characters) in @hay */
static bool ci_has(const char *hay, const char *needle, size_t n)
{
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && (hay[i] | 0x20) == (needle[i] | 0x20)) i++;
        if (i == n) return true;
    }
    return !n;
}

/* Every word of @q is in the page's name or search words */
static bool page_matches(int page, const char *q)
{
    while (*q) {
        while (*q == ' ') q++;
        size_t n = 0;
        while (q[n] && q[n] != ' ') n++;
        if (n && !ci_has(g_pages[page].name, q, n) && !ci_has(g_pages[page].keys, q, n)) return false;
        q += n;
    }
    return true;
}

/* The pages the sidebar lists (those the search matches); how many */
static int nav_list(const Settings *st, int *vis)
{
    int n = 0;
    for (int i = 0; i < N_PAGES; i++) if (page_matches(i, st->query)) vis[n++] = i;
    return n;
}

/* The page named by @uri ("ms-settings:display", "display", "Wi-Fi"), or -1 */
static int page_by_uri(const char *uri)
{
    if (!strncmp(uri, "ms-settings:", 12) || !strncmp(uri, "MS-SETTINGS:", 12)) uri += 12;
    size_t n = 0;
    while (uri[n] && uri[n] != '?' && uri[n] != '#' && uri[n] != ' ') n++;
    if (!n) return SETTINGS_SYSTEM;                           /* (ms-settings: alone: Windows' home page) */
    for (int i = 0; i < N_PAGES; i++) {
        const char *u = g_pages[i].uri;
        while (*u) {                                          /* one of its ms-settings: names */
            size_t k = 0;
            while (u[k] && u[k] != ' ') k++;
            bool same = k == n;
            for (size_t j = 0; same && j < n; j++) same = (u[j] | 0x20) == (uri[j] | 0x20);
            if (same) return i;
            u += k;
            while (*u == ' ') u++;
        }
        const char *nm = g_pages[i].name;                     /* its name */
        bool same = strlen(nm) == n;
        for (size_t j = 0; same && j < n; j++) same = (nm[j] | 0x20) == (uri[j] | 0x20);
        if (same) return i;
    }
    return -1;
}

/* The frame's parts, client-relative: the sidebar's width, the page's
 * origin and width, and the view its content scrolls in */
typedef struct { int side, px, py, pw; GdiRect view; bool slim; } Frame;

static Frame frame_of(GdiRect c)
{
    Frame f;
    f.slim = c.w < SLIM_BELOW;
    f.side = f.slim ? SIDE_SLIM : SIDE_W;
    f.px = f.side + 28;
    f.py = HEAD_H;
    f.pw = c.w - f.side - 56;
    if (f.pw < 160) f.pw = 160;                               /* (narrower: cut at the right, not squeezed) */
    f.view = RECT(f.side, HEAD_H - 8, c.w - f.side, c.h > HEAD_H - 8 ? c.h - (HEAD_H - 8) : 0);
    return f;
}

static int nav_top(const Frame *f) { return f->slim ? 12 : FIND_Y + FIND_H + 14; }

/* Go to @page: what the page had open (forms, typed text, a pending
 * Reset) is closed, the content scrolls to its top */
static void go(Settings *st, int page, bool log)
{
    if (page < 0 || page >= N_PAGES) return;
    if (page != st->page) {
        st->net_edit = false; st->net_err[0] = '\0';
        st->prn_focus = false; st->prn_err[0] = '\0';
        st->rec_ask = false; st->rec_note[0] = '\0';
        st->drag = false;
        st->wifi_scanned = false;                             /* (Wi-Fi scans again when it opens) */
        wifi_choose(st, -1);
        if (st->acc_form) acc_close(st, NULL);
        st->acc_note[0] = '\0';
        st->page = page;
        st->sig = 0;
        UiScrollSet(&st->sb, 0, st->sb.page);
        UiScrollTo(&st->sb, 0);
    }
    if (log) kprintf("[SETTINGS] Page: %s\n", g_pages[page].name);
}

static void sidebar(Settings *st, GdiRect c, const Frame *f)
{
    GdiFillRect(RECT(c.x, c.y, f->side, c.h), UI_PANEL);
    GdiFillRect(RECT(c.x + f->side - 1, c.y, 1, c.h), UI_LINE);
    if (!f->slim) {
        GdiTextLarge(c.x + 20, c.y + 16, "Settings", UI_TEXT);
        GdiRect b = RECT(c.x + 12, c.y + FIND_Y, f->side - 24, FIND_H);  /* the search box */
        GdiRoundRect(b, 6, UI_BG, GDI_TRANSPARENT);
        GdiRoundBorderAlpha(b, 6, st->find ? UI_ACCENT : GDI_WHITE, st->find ? 200 : 30);
        AppDrawGlyph(GL_SEARCH, b.x + b.w - 26, b.y + 8, 16, UI_TEXT2);
        int ty = b.y + (b.h - GDI_FONT_H) / 2;
        if (st->query[0] || st->find) GdiTextT(b.x + 10, ty, st->query, UI_TEXT);
        else GdiTextT(b.x + 10, ty, "Find a setting", UI_TEXT3);
        if (st->find) GdiFillRect(RECT(b.x + 10 + GdiTextW(st->query) + 1, b.y + 7, 2, b.h - 14), UI_ACCENT);
    }
    int vis[N_PAGES], n = nav_list(st, vis), top = nav_top(f);
    UiScrollSet(&st->nb, n * ITEM_H + 8, c.h - top);
    GdiRect old = GdiClipRect(), list = RECT(c.x, c.y + top, f->side - 1, c.h - top);
    if (GdiInClip(list)) {
        GdiSetClip(list);
        for (int i = 0; i < n; i++) {
            int p = vis[i], y = c.y + top + i * ITEM_H - st->nb.pos;
            bool on = p == st->page;
            if (on) {
                GdiRoundRect(RECT(c.x + 8, y, f->side - 16, ITEM_H - 4), 4, UI_HOVER, GDI_TRANSPARENT);
                GdiRoundRect(RECT(c.x + 8, y + 9, 3, 14), 1, UI_ACCENT, GDI_TRANSPARENT);
            }
            AppDrawGlyph(g_pages[p].glyph, f->slim ? c.x + (f->side - 16) / 2 : c.x + 22, y + 8, 16, on ? UI_TEXT : UI_TEXT2);
            if (!f->slim) GdiTextT(c.x + 48, y + 8, g_pages[p].name, on ? UI_TEXT : UI_TEXT2);
        }
        if (!n) GdiTextT(c.x + 20, c.y + top + 8, "No results", UI_TEXT3);
        GdiSetClip(old);
    }
    UiScrollDraw(&st->nb, RECT(f->side - 1 - UI_SB_W, top, UI_SB_W, c.h - top), c);
}

static void set_paint(WND *w)
{
    Settings *st = w->user;
    GdiRect c = WmClientRect(w);
    Frame f = frame_of(c);
    if (st->page < 0 || st->page >= N_PAGES) st->page = SETTINGS_SYSTEM;
    const SetPage *pg = &g_pages[st->page];

    sidebar(st, c, &f);
    GdiTextLarge(c.x + f.px, c.y + 20, pg->name, UI_TEXT);

    /* the content, clipped to its view and scrolled (the scroll range is
     * the height it had when last drawn) */
    GdiRect old = GdiClipRect(), view = RECT(c.x + f.view.x, c.y + f.view.y, f.view.w, f.view.h);
    UiScrollSet(&st->sb, st->sb.max, f.view.h);
    int pos = st->sb.pos, top = c.y + f.py - pos, bottom = top;
    if (GdiInClip(view)) {
        GdiSetClip(view);
        bottom = pg->paint(st, c, c.x + f.px, top, f.pw);
        GdiSetClip(old);
    }
    int height = (f.py - f.view.y) + bottom - top + 24;       /* (from the view's top; a margin below) */
    UiScrollSet(&st->sb, height > 0 ? height : 0, f.view.h);
    if (st->sb.pos != pos) WmInvalidate();                    /* (it got shorter: drawn again in range) */
    UiScrollDraw(&st->sb, RECT(c.w - UI_SB_W, f.view.y, UI_SB_W, f.view.h), c);
    if (st->check) { st->check_drawn = st->check; st->check_bottom = bottom - top; }
}

static void set_mouse(WND *w, WmMouseMsg msg, int x, int y)
{
    Settings *st = w->user;
    if (st->check) return;
    GdiRect c = WmClientRect(w);
    Frame f = frame_of(c);
    if (UiScrollMouse(&st->sb, msg, x, y) || UiScrollMouse(&st->nb, msg, x, y)) return;
    const SetPage *pg = &g_pages[st->page];
    if (msg == WM_MOUSE_WHEEL) {
        UiScroll *s = x < f.side ? &st->nb : &st->sb;
        UiScrollTo(s, s->pos - WmWheelDelta() * 48);
        return;
    }
    if (x >= f.side || (msg != WM_MOUSE_DOWN && st->drag)) {
        if (msg == WM_MOUSE_DOWN) {
            st->find = false;
            if (y < f.view.y) return;                         /* (the title, and parts scrolled under it) */
        }
        if (pg->mouse) pg->mouse(st, c, msg, x, pg->hits ? y : y + st->sb.pos, f.px, f.py, f.pw);
        return;
    }
    if (msg != WM_MOUSE_DOWN) return;
    if (!f.slim && UiHit(RECT(12, FIND_Y, f.side - 24, FIND_H), x, y)) { st->find = true; return; }
    st->find = false;
    int vis[N_PAGES], n = nav_list(st, vis), top = nav_top(&f);
    int i = (y - top + st->nb.pos) / ITEM_H;
    if (y < top || i < 0 || i >= n) return;
    st->query[0] = '\0';
    go(st, vis[i], true);
}

/* The search box has the keyboard: typing filters the pages, Up and Down
 * go through the ones found, Enter opens the first one (or the one shown),
 * Esc clears it */
static void find_key(Settings *st, const KeyEvent *k)
{
    int vis[N_PAGES], n = nav_list(st, vis), at = -1;
    for (int i = 0; i < n; i++) if (vis[i] == st->page) at = i;
    size_t len = strlen(st->query);
    if (k->scancode == KEY_ESC || k->scancode == KEY_TAB) { st->query[0] = '\0'; st->find = false; return; }
    if (k->scancode == KEY_ENTER && !k->extended) {
        if (n) go(st, vis[at >= 0 ? at : 0], true);
        st->query[0] = '\0';
        st->find = false;
        return;
    }
    if (k->extended && (k->scancode == KEY_UP || k->scancode == KEY_DOWN)) {
        if (!n) return;
        int to = k->scancode == KEY_UP ? (at > 0 ? at - 1 : 0) : (at + 1 < n ? at + 1 : n - 1);
        go(st, vis[to], true);
        return;
    }
    if (k->ch == '\b') { if (len) st->query[len - 1] = '\0'; }
    else if (!k->ctrl && !k->alt && k->ch >= ' ' && k->ch < 0x7F && len < sizeof(st->query) - 1) {
        st->query[len] = k->ch;
        st->query[len + 1] = '\0';
    } else return;
    UiScrollTo(&st->nb, 0);
}

static void set_key(WND *w, const KeyEvent *k)
{
    Settings *st = w->user;
    if (!k->pressed || st->check) return;
    if (st->find) { find_key(st, k); return; }
    Frame f = frame_of(WmClientRect(w));
    if (k->ctrl && !k->alt && !k->extended && k->scancode == 0x21 && !f.slim) { st->find = true; return; }   /* Ctrl+F */
    const SetPage *pg = &g_pages[st->page];
    if (pg->key && pg->key(st, k)) return;
    if (!k->extended) return;
    switch (k->scancode) {
    case KEY_UP:   go(st, st->page > 0 ? st->page - 1 : 0, true); break;
    case KEY_DOWN: go(st, st->page < N_PAGES - 1 ? st->page + 1 : N_PAGES - 1, true); break;
    case KEY_PGUP: UiScrollTo(&st->sb, st->sb.pos - st->sb.page + 48); break;
    case KEY_PGDN: UiScrollTo(&st->sb, st->sb.pos + st->sb.page - 48); break;
    case KEY_HOME: UiScrollTo(&st->sb, 0); break;
    case KEY_END:  UiScrollTo(&st->sb, st->sb.max); break;
    }
}

static bool check_step(WND *w, Settings *st);

static bool set_tick(WND *w)
{
    Settings *st = w->user;
    if (!st) return false;
    bool moved = UiScrollTick(&st->sb);
    moved |= UiScrollTick(&st->nb);
    if (st->check) return check_step(w, st) || moved;
    if (st->wifi_job && wifi_poll(st)) return true;           /* (a Wi-Fi scan or connection finished, on any page) */
    const SetPage *pg = &g_pages[st->page];
    return (pg->tick && pg->tick(st)) || moved;
}

static int g_check_state, g_check_pass, g_check_fail;     /* SettingsCheck: 0 none, 1 running, 2 done */

static void set_close(WND *w)
{
    Settings *st = w->user;
    if (st && st->check) {                    /* (closed half-way through "settings check") */
        kprintf("[SETTINGS] check: FAILED: the window was closed\n");
        g_check_pass = st->check_pass;
        g_check_fail = st->check_fail + 1;
        g_check_state = 2;
    }
    if (st) acc_wipe(st);                     /* (passwords typed) */
    if (st) memset(st->wifi_key, 0, sizeof(st->wifi_key));
    kfree(w->user);
    w->user = NULL;
}

void SettingsOpen(void)
{
    if (WmFindApp(APP_SETTINGS)) { WmSetActive(WmFindApp(APP_SETTINGS)); return; }
    Settings *st = kzalloc(sizeof(Settings));
    if (!st) return;
    WND *w = AppCreateWindow(APP_SETTINGS, "Settings", 860, 580, UI_BG);
    if (!w) { kfree(st); return; }
    st->wifi_sel = -1;
    st->sb.vert = st->nb.vert = true;
    st->sb.line = st->nb.line = 48;
    w->user     = st;
    w->on_paint = set_paint;
    w->on_mouse = set_mouse;
    w->on_key   = set_key;
    w->on_close = set_close;
    w->on_tick  = set_tick;
    w->rbutton  = true;                       /* (the wheel scrolls the page and the sidebar) */
}

static Settings *settings_window(WND **out)
{
    WND *w = WmFindApp(APP_SETTINGS);
    if (!w) {
        SettingsOpen();
        w = WmFindApp(APP_SETTINGS);
        if (!w) return NULL;
    }
    if (out) *out = w;
    return w->user;
}

void SettingsOpenPage(int page)
{
    WND *w;
    Settings *st = settings_window(&w);
    if (!st) return;
    if (page == SETTINGS_ACCOUNTS_ADD) {                     /* (Start's "Add an account") */
        go(st, SETTINGS_ACCOUNTS, true);
        if (acc_can_manage()) acc_open(st, ACC_ADD, 0);
    } else if (page >= 0 && page < N_PAGES) {
        go(st, page, true);
    }
    WmSetActive(w);
    WmInvalidate();
}

bool SettingsOpenUri(const char *uri)
{
    int page = page_by_uri(uri ? uri : "");
    if (page < 0) kprintf("[SETTINGS] No page \"%s\": opened System\n", uri);
    SettingsOpenPage(page < 0 ? SETTINGS_SYSTEM : page);
    return page >= 0;
}

/* -----------------------------------------------------------------------
 * SettingsCheck ("settings check" in the Terminal): every page drawn at
 * three window sizes (the narrowest and shortest a window gets, with the
 * slim sidebar; the default; a wide one), at its top and scrolled to its
 * end, through the real on_paint.  Each must draw something, and its
 * end must come into view; every page must be found by its ms-settings:
 * name and by searching for its name.  One step per tick; the summary is
 * "[SETTINGS] check: N passed, M failed".
 * ----------------------------------------------------------------------- */
static const struct { int w, h; } g_check_sizes[] = { { 400, 300 }, { 860, 580 }, { 1200, 720 } };
#define CHECK_SIZES ((int)(sizeof(g_check_sizes) / sizeof(g_check_sizes[0])))
#define CHECK_STEPS (CHECK_SIZES * N_PAGES * 2)

static void check_result(Settings *st, bool ok, const char *what, int step)
{
    if (ok) { st->check_pass++; return; }
    st->check_fail++;
    int s = (step - 1) / (N_PAGES * 2), p = (step - 1) / 2 % N_PAGES;
    kprintf("[SETTINGS] check: FAILED %s at %d x %d: %s\n", g_pages[p].name, g_check_sizes[s].w, g_check_sizes[s].h, what);
}

/* Set up step @k (1..CHECK_STEPS): its window size, page and scroll */
static void check_setup(WND *w, Settings *st, int k)
{
    int s = (k - 1) / (N_PAGES * 2), p = (k - 1) / 2 % N_PAGES;
    bool end = (k - 1) % 2;
    if ((k - 1) % (N_PAGES * 2) == 0) {                       /* a new size */
        GdiRect fr = w->frame, c = WmClientRect(w);
        WmSetFrame(w, RECT(fr.x, fr.y, g_check_sizes[s].w + fr.w - c.w, g_check_sizes[s].h + fr.h - c.h));
    }
    if (end) UiScrollTo(&st->sb, st->sb.max);
    else { go(st, p, false); st->sb.max = 0; UiScrollTo(&st->sb, 0); }
    st->check = k;
}

static bool check_step(WND *w, Settings *st)
{
    int k = st->check;
    if (st->check_drawn != k) return true;                    /* (not drawn yet) */
    GdiRect c = WmClientRect(w);
    Frame f = frame_of(c);
    bool end = (k - 1) % 2;
    if (!end) {
        check_result(st, st->check_bottom > GDI_FONT_H, "nothing drawn", k);
        check_result(st, f.slim == (c.w < SLIM_BELOW), "wrong sidebar", k);
    } else {
        /* scrolled to the end: the content's bottom is in view */
        int seen = st->sb.pos + st->sb.page;
        check_result(st, !UiScrollNeeded(&st->sb) || seen >= st->sb.max, "its end cannot be scrolled into view", k);
    }
    if (k == 1)                                               /* (once: names and searches) */
        for (int i = 0; i < N_PAGES; i++) {
            char uri[64];
            const char *u = g_pages[i].uri;
            size_t n = 0;
            while (u[n] && u[n] != ' ' && n < 40) n++;
            strcpy(uri, "ms-settings:");
            memcpy(uri + 12, u, n);                           /* (its first name) */
            uri[12 + n] = '\0';
            check_result(st, page_by_uri(uri) == i, "not found by its ms-settings: name", 1 + i * 2);
            check_result(st, page_matches(i, g_pages[i].name), "not found by searching for its name", 1 + i * 2);
        }
    if (k < CHECK_STEPS) { check_setup(w, st, k + 1); return true; }
    WmSetFrame(w, st->check_frame);
    kprintf("[SETTINGS] check: %d pages at %d window sizes\n", N_PAGES, CHECK_SIZES);
    kprintf("[SETTINGS] check: %d passed, %d failed\n", st->check_pass, st->check_fail);
    g_check_pass = st->check_pass;
    g_check_fail = st->check_fail;
    g_check_state = 2;
    st->check = 0;
    go(st, SETTINGS_SYSTEM, false);
    if (st->check_close) WmRequestClose(w);
    return true;
}

bool SettingsCheck(void)
{
    bool open = WmFindApp(APP_SETTINGS) != NULL;
    WND *w;
    Settings *st = settings_window(&w);
    if (!st || st->check) return false;
    st->check_close = !open;
    WmSetActive(w);
    if (w->maximized) WmSnap(w, WM_SNAP_RESTORE);
    st->check_frame = w->frame;
    st->check_pass = st->check_fail = 0;
    st->check_drawn = 0;
    st->query[0] = '\0';
    st->find = false;
    check_setup(w, st, 1);
    g_check_state = 1;
    WmInvalidate();
    return true;
}

bool SettingsCheckDone(int *pass, int *fail)
{
    if (g_check_state != 2) return false;
    *pass = g_check_pass;
    *fail = g_check_fail;
    g_check_state = 0;
    return true;
}
