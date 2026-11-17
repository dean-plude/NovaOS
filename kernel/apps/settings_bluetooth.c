/*
 * settings_bluetooth.c — Settings' Bluetooth page (Windows 11's
 * "Bluetooth & devices")
 *
 * The radio (its name, address and whether it is discoverable), the
 * devices paired with it, and the devices in range that the last search
 * heard.  A search runs when the page opens and on "Add device" (or F5).
 * Choose a device (a click, or Up and Down): one in range gets "Pair"
 * (or Enter), which pairs it and switches its services on as Windows does
 * once a device pairs, so a mouse moves and a headset plays without
 * another step; a paired one gets "Remove device" (or Delete), which
 * forgets it.  The work runs in the background through NovaOS's
 * Bluetooth host (kernel/nt/bthport.c: BthJobStart), the same code
 * bthprops.cpl's BluetoothAuthenticateDeviceEx and BluetoothRemoveDevice
 * reach.  What happens is logged ("[SETTINGS] Bluetooth: ...").
 */

#include "apps.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../nt/bthport.h"

#define CARD_H   72
#define HEAD_GAP 16
#define TITLE_H  28
#define ROW_H    56
#define MAX_SHOWN 16

static BthDevView g_dev[MAX_SHOWN];
static int  g_n, g_paired;              /* the devices shown; how many of them are paired */
static bool g_radio;                    /* a radio is up */
static BthRadioView g_rv;
static bool g_scanned;                  /* a search has run since the page opened */
static int  g_job;                      /* the job running (BTH_JOB_*), or 0 */
static char g_job_name[64];             /* ... the device it is about */
static UINT64 g_sel;                    /* the device chosen (its address), or 0 */
static char g_note[160];                /* what the last job did, when it failed */
static UINT32 g_sig;                    /* what was shown, as last drawn */
static int  g_dy[MAX_SHOWN];            /* each row's top, from the page's (as drawn) */
static int  g_add_dy, g_btn_dy = -1;    /* "Add device"'s top; the chosen row's button's (-1: none) */

static void refresh(void)
{
    g_radio = BthViewRadio(&g_rv);
    g_n = g_radio ? BthViewDevices(g_dev, MAX_SHOWN) : 0;
    g_paired = 0;
    for (int i = 0; i < g_n; i++) g_paired += g_dev[i].paired;
}

static int sel_index(void)
{
    for (int i = 0; i < g_n; i++) if (g_sel && g_dev[i].addr == g_sel) return i;
    return -1;
}

static void dev_name(const BthDevView *d, char *out, int cap)
{
    if (d->name[0]) { strncpy(out, d->name, (size_t)cap - 1); out[cap - 1] = '\0'; return; }
    UINT64 a = d->addr;
    ksnprintf(out, (size_t)cap, "%02X:%02X:%02X:%02X:%02X:%02X", (UINT32)(a >> 40) & 0xFF, (UINT32)(a >> 32) & 0xFF,
              (UINT32)(a >> 24) & 0xFF, (UINT32)(a >> 16) & 0xFF, (UINT32)(a >> 8) & 0xFF, (UINT32)a & 0xFF);
}

/* What its class of device says it is (Assigned Numbers, Baseband) */
static const char *dev_kind(UINT32 cod)
{
    UINT32 major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;
    switch (major) {
    case 0x01: return "Computer";
    case 0x02: return "Phone";
    case 0x04: return minor == 0x01 || minor == 0x02 ? "Headset" : minor == 0x06 ? "Headphones" : "Audio";
    case 0x05: return (minor >> 4) == 1 ? "Keyboard" : (minor >> 4) == 2 ? "Mouse" : (minor >> 4) == 3 ? "Keyboard and mouse"
                    : (minor & 0x0F) == 1 || (minor & 0x0F) == 2 ? "Game controller" : "Input device";
    case 0x06: return "Printer or camera";
    }
    return "Device";
}

static void start(int job, const BthDevView *d)
{
    if (g_job) return;
    if (!BthJobStart(job, d ? d->addr : 0)) { strcpy(g_note, "Bluetooth is busy; try again."); return; }
    g_job = job;
    g_note[0] = '\0';
    if (d) dev_name(d, g_job_name, sizeof(g_job_name));
}

static void scan(void)                 { if (g_radio) start(BTH_JOB_SCAN, NULL); }
static void pair_chosen(void)
{
    int i = sel_index();
    if (i >= 0 && !g_dev[i].paired) start(BTH_JOB_PAIR, &g_dev[i]);
}
static void remove_chosen(void)
{
    int i = sel_index();
    if (i >= 0 && g_dev[i].paired) start(BTH_JOB_REMOVE, &g_dev[i]);
}

/* A background job finished: true if the page changed */
static bool poll(void)
{
    NT_STATUS st;
    UINT32 n;
    if (!g_job || !BthJobDone(&st, &n)) return false;
    int job = g_job;
    g_job = 0;
    if (job == BTH_JOB_SCAN) {
        g_scanned = true;
        if (st < 0) ksnprintf(g_note, sizeof(g_note), "The search for devices failed (0x%08x).", (UINT32)st);
        kprintf("[SETTINGS] Bluetooth: %u device(s) found\n", n);
    } else if (st >= 0) {
        kprintf("[SETTINGS] Bluetooth: %s %s\n", job == BTH_JOB_PAIR ? "paired with" : "removed", g_job_name);
        if (job == BTH_JOB_REMOVE) g_sel = 0;
    } else {
        ksnprintf(g_note, sizeof(g_note), job == BTH_JOB_PAIR ? "Couldn't pair with %s (0x%08x). Make sure it is on and "
                  "ready to pair, then try again." : "Couldn't remove %s (0x%08x).", g_job_name, (UINT32)st);
        kprintf("[SETTINGS] Bluetooth: %s\n", g_note);
    }
    return true;
}

static UINT32 view_sig(void)
{
    UINT32 h = 2166136261u ^ (g_radio ? 1u : 0) ^ (g_rv.discoverable ? 2u : 0) ^ (UINT32)g_job << 4 ^ (g_scanned ? 8u : 0);
    for (int i = 0; i < g_n; i++) {
        const BthDevView *d = &g_dev[i];
        h = (h ^ (UINT32)d->addr ^ (UINT32)(d->addr >> 32)) * 16777619u;
        h = (h ^ (d->paired | d->connected << 1 | d->hid << 2 | d->audio << 3) ^ (UINT32)d->name[0] << 8) * 16777619u;
    }
    return h;
}

static GdiRect r_add(int x, int y, int w) { return RECT(x + w - 16 - 120, y + 20, 120, 32); }
static GdiRect r_btn(int x, int y, int w) { return RECT(x + w - 16 - 136, y, 136, 32); }

static int row(int x, int y, int w, int i)
{
    const BthDevView *d = &g_dev[i];
    bool sel = g_sel && d->addr == g_sel;
    char name[64], line[128];
    dev_name(d, name, sizeof(name));
    g_dy[i] = y;
    GdiRoundRect(RECT(x, y, w, ROW_H - 8), 8, sel ? UI_HOVER : UI_CARD, GDI_TRANSPARENT);
    AppDrawGlyph(GL_BLUETOOTH, x + 14, y + 12, 22, d->connected ? UI_ACCENT : UI_TEXT2);
    GdiTextBold(x + 52, y + 6, name, UI_TEXT);
    const char *state = g_job && g_job != BTH_JOB_SCAN && !strcmp(g_job_name, name)
                        ? (g_job == BTH_JOB_PAIR ? "Pairing..." : "Removing...")
                        : d->connected ? "Connected" : d->paired ? "Paired" : "Not paired";
    ksnprintf(line, sizeof(line), "%s  -  %s%s", dev_kind(d->cod), state,
              d->paired && d->audio ? "  -  Audio" : d->paired && d->hid ? "  -  Input" : "");
    GdiTextT(x + 52, y + 26, line, UI_TEXT2);
    if (sel && !g_job) {
        g_btn_dy = y + 8;
        UiButton(r_btn(x, y + 8, w), d->paired ? "Remove device" : "Pair", !d->paired);
    }
    return y + ROW_H;
}

int BluetoothPagePaint(int x, int y, int w)
{
    refresh();
    g_sig = view_sig();
    int y0 = y;
    g_btn_dy = -1;
    GdiRoundRect(RECT(x, y, w, CARD_H), 8, UI_CARD, GDI_TRANSPARENT);
    AppDrawGlyph(GL_BLUETOOTH, x + 14, y + 14, 22, g_radio ? UI_ACCENT : UI_TEXT3);
    GdiTextBold(x + 46, y + 14, "Bluetooth", UI_TEXT);
    char state[128];
    if (!g_radio) strcpy(state, "No Bluetooth radio found");
    else ksnprintf(state, sizeof(state), "On  -  %s as \"%s\" (%s)", g_rv.discoverable ? "Discoverable" : "Connectable",
                   g_rv.name, g_rv.addr);
    GdiColor dot = g_radio ? GDI_C(0x2E, 0xB8, 0x5C) : GDI_C(0xD0, 0x40, 0x40);
    GdiRoundRect(RECT(x + 46, y + 44, 10, 10), 5, dot, GDI_TRANSPARENT);
    GdiTextT(x + 64, y + 40, state, UI_TEXT2);
    if (!g_radio) {
        GdiTextT(x, y + CARD_H + HEAD_GAP, "Bluetooth needs a radio with a Windows driver, such as Intel's from the "
                                           "Update Catalog (Device Manager lists the devices found).", UI_TEXT2);
        return y + CARD_H + HEAD_GAP + GDI_FONT_H;
    }
    g_add_dy = 20;
    UiButton(r_add(x, y, w), g_job == BTH_JOB_SCAN ? "Searching..." : "Add device", true);
    y += CARD_H + HEAD_GAP;
    GdiTextBold(x, y, "Paired devices", UI_TEXT);
    y += TITLE_H;
    if (!g_paired) { GdiTextT(x + 16, y, "No devices paired yet.", UI_TEXT2); y += TITLE_H; }
    for (int i = 0; i < g_paired; i++) y = row(x, y, w, i);
    y += 8;
    GdiTextBold(x, y, "Devices in range", UI_TEXT);
    y += TITLE_H;
    if (g_n == g_paired) {
        GdiTextT(x + 16, y, g_job == BTH_JOB_SCAN || !g_scanned ? "Looking for devices..."
                            : "No other devices found. Make sure yours is on and ready to pair, then Add device.", UI_TEXT2);
        y += TITLE_H;
    }
    for (int i = g_paired; i < g_n; i++) y = row(x, y, w, i);
    if (g_note[0]) { GdiTextT(x, y + 4, g_note, GDI_C(0xE0, 0x60, 0x60)); y += 28; }
    for (int i = 0; i < g_n; i++) g_dy[i] -= y0;
    if (g_btn_dy >= 0) g_btn_dy -= y0;
    return y;
}

bool BluetoothPageMouse(int px, int py, int pw, int x, int y)
{
    if (!g_radio) return false;
    if (UiHit(r_add(px, py, pw), x, y)) { scan(); return true; }
    int s = sel_index();
    if (s >= 0 && g_btn_dy >= 0 && UiHit(r_btn(px, py + g_btn_dy, pw), x, y)) {
        if (g_dev[s].paired) remove_chosen(); else pair_chosen();
        return true;
    }
    for (int i = 0; i < g_n; i++)
        if (UiHit(RECT(px, py + g_dy[i], pw, ROW_H - 8), x, y)) {
            g_sel = g_sel == g_dev[i].addr ? 0 : g_dev[i].addr;
            g_note[0] = '\0';
            return true;
        }
    return false;
}

/* Up and Down choose a device, Enter pairs the one chosen, Delete removes
 * it, F5 searches again, Esc lets go (then Up and Down move between pages
 * again); false: not the page's key */
bool BluetoothPageKey(const KeyEvent *k)
{
    int s = sel_index();
    if (k->extended && k->scancode == KEY_DOWN && g_n) { g_sel = g_dev[s + 1 < g_n ? s + 1 : g_n - 1].addr; g_note[0] = '\0'; return true; }
    if (k->extended && k->scancode == KEY_UP && s >= 0) { g_sel = g_dev[s > 0 ? s - 1 : 0].addr; g_note[0] = '\0'; return true; }
    if (k->scancode == 0x3F && !k->extended) { scan(); return true; }   /* (F5) */
    if (s < 0) return false;
    if (k->scancode == KEY_ESC) { g_sel = 0; return true; }
    if (k->scancode == KEY_ENTER && !k->extended) { pair_chosen(); return true; }
    if (k->scancode == 0x53 && k->extended) { remove_chosen(); return true; }   /* (Delete) */
    return false;
}

bool BluetoothPageTick(void)
{
    bool changed = poll();
    refresh();
    if (g_radio && !g_scanned && !g_job) scan();
    return changed || view_sig() != g_sig;
}

void BluetoothPageReset(void)
{
    g_scanned = false;                  /* (it searches again when it opens) */
    g_sel = 0;
    g_note[0] = '\0';
}
