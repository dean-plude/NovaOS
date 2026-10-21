/*
 * settings_firewall.c — Settings' Firewall page and the new-network prompt
 *
 * The page (Windows' "Firewall & network protection" with Network &
 * internet's "Network profile type"): the network the PC is on and whether
 * it is Public or Private, the firewall on or off for the Domain, Private
 * and Public profiles (the one in force marked active), and the rules,
 * each switched on or off where it is listed.  Changing them needs an
 * administrator, as on Windows.  The firewall is kernel/net/firewall.c.
 *
 * The prompt: a network the PC joins for the first time is Public, and,
 * as in Windows 11, nothing pops up by itself: the question whether the
 * PC may be discoverable there waits until the person opens the network
 * icon in the tray, which then shows it in a small window at the bottom
 * right (or they pick Public or Private on this page).  Yes makes the
 * network Private, No (or no answer within a minute) leaves it Public.
 * The window never takes the keyboard focus, not even when the window
 * under it closes.  The NewNetworkWindowOff key
 * (HKLM\SYSTEM\CurrentControlSet\Control\Network) turns the question off.
 */

#include "apps.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../mm/vmm.h"
#include "../net/wfp.h"
#include "../um/um.h"

#define PROF_H   48                     /* a profile's row */
#define RULE_H   40                     /* a rule's row */
#define RULES_SHOWN 6
#define NET_CARD_H 92

static char g_ids[RULES_SHOWN][48];     /* the rules shown (their ids), for clicks */
static int  g_nshown;
static char g_note[96];                 /* why the last click did nothing */

static bool can_change(void)
{
    UmAccount me;
    return um_account_get(um_session_rid(), &me) && (me.flags & UM_ACCT_ADMIN);
}

static UINT32 fw(UINT32 op, void *req, INT64 *result)
{
    INT64 r = 0;
    UINT32 hr = FwCtl(true, op, 0, req, &r);
    if (result) *result = r;
    return hr;
}

/* A Windows 11 toggle switch, 40 x 20 */
static void toggle(int x, int y, bool on)
{
    if (on) GdiRoundRect(RECT(x, y, 40, 20), 10, UI_ACCENT, GDI_TRANSPARENT);
    else {
        GdiRoundRect(RECT(x, y, 40, 20), 10, UI_BG, GDI_TRANSPARENT);
        GdiRoundBorderAlpha(RECT(x, y, 40, 20), 10, UI_TEXT2, 255);
    }
    GdiFillCircle(on ? x + 30 : x + 10, y + 10, on ? 6 : 5, on ? GDI_C(0, 0, 0) : UI_TEXT2);
}

/* The page's parts (the painter and the mouse use the same numbers) */
static GdiRect r_category(int px, int py, int pw, int i)        /* 0 Public, 1 Private */
{
    return RECT(px + pw - 16 - (2 - i) * 104, py + 46, 96, 30);
}
static int prof_y(int py, int i) { return py + NET_CARD_H + 32 + i * (PROF_H + 6); }
static GdiRect r_prof_toggle(int px, int py, int pw, int i) { return RECT(px + pw - 16 - 40, prof_y(py, i) + 14, 40, 20); }
static int rules_y(int py) { return prof_y(py, 3) + 34; }
static GdiRect r_rule_toggle(int px, int py, int pw, int i) { return RECT(px + pw - 16 - 40, rules_y(py) + i * (RULE_H + 4) + 10, 40, 20); }
static GdiRect r_restore(int px, int py, int pw) { return RECT(px + pw - 150, rules_y(py) - 34, 150, 28); }

static const char *const g_prof_names[3] = { "Domain network", "Private network", "Public network" };

static void rule_detail(const NovaFwRule *r, char *out, int cap)
{
    const char *proto = r->protocol == 6 ? "TCP" : r->protocol == 17 ? "UDP" : r->protocol == 1 ? "ICMPv4" :
                        r->protocol == 58 ? "ICMPv6" : r->protocol == 256 ? "" : "IP";
    char ports[64] = "";
    const char *pl = r->dir == 1 ? r->lports : r->rports;
    if (pl[0] && strcmp(pl, "*")) ksnprintf(ports, sizeof(ports), " %s", pl);
    char profs[40] = "";
    if ((r->profiles & 7) == 7) strcpy(profs, "All profiles");
    else {
        if (r->profiles & NOVAFW_DOMAIN) strcat(profs, "Domain ");
        if (r->profiles & NOVAFW_PRIVATE) strcat(profs, "Private ");
        if (r->profiles & NOVAFW_PUBLIC) strcat(profs, "Public ");
    }
    const char *app = r->app[0] ? r->app : "";
    const char *base = app;
    for (const char *s = app; *s; s++) if (*s == '\\' || *s == '/') base = s + 1;
    ksnprintf(out, (size_t)cap, "%s  -  %s  -  %s%s%s%s", r->dir == 1 ? "Inbound" : "Outbound",
              r->action ? "Allow" : "Block", proto[0] ? proto : "Any protocol", ports,
              base[0] ? "  -  " : "", base);
    int n = (int)strlen(out);
    if (n < cap - 8) ksnprintf(out + n, (size_t)(cap - n), "  -  %s", profs);
}

int FirewallPagePaint(int x, int y, int w)
{
    NovaFwNetwork net;
    bool connected = FwGetNetwork(&net);
    UINT32 cur = FwCurrentProfiles();

    /* the network and its profile type */
    GdiRoundRect(RECT(x, y, w, NET_CARD_H), 8, UI_CARD, GDI_TRANSPARENT);
    AppDrawGlyph(GL_NETWORK, x + 16, y + 16, 20, UI_TEXT);
    GdiTextBold(x + 48, y + 16, connected ? net.name : "Not connected", UI_TEXT);
    char line[128];
    if (connected) ksnprintf(line, sizeof(line), "%s network",
                             net.category == 2 ? "Domain" : net.category == 1 ? "Private" : "Public");
    else strcpy(line, "Public network rules apply until the PC joins a network");
    GdiTextT(x + 48, y + 40, line, UI_TEXT2);
    if (connected && net.category != 2) {
        GdiTextT(x + 16, y + 66, "Network profile type", UI_TEXT3);
        for (int i = 0; i < 2; i++) {
            GdiRect r = r_category(x, y, w, i);
            bool on = net.category == i;
            GdiRoundRect(r, 6, on ? UI_ACCENT_BG : UI_HOVER, GDI_TRANSPARENT);
            const char *t = i ? "Private" : "Public";
            GdiTextT(r.x + (r.w - GdiTextW(t)) / 2, r.y + (r.h - GDI_FONT_H) / 2, t, on ? UI_TEXT : UI_TEXT2);
        }
    }

    /* the three profiles */
    GdiTextBold(x, y + NET_CARD_H + 8, "Firewall", UI_TEXT);
    for (int i = 0; i < 3; i++) {
        NovaFwProfile p = { .profile = 1u << i };
        fw(NOVAFWC_PROFILE_GET, &p, NULL);
        int ry = prof_y(y, i);
        GdiRoundRect(RECT(x, ry, w, PROF_H), 8, UI_CARD, GDI_TRANSPARENT);
        char name[48];
        ksnprintf(name, sizeof(name), "%s%s", g_prof_names[i], (cur & (1u << i)) ? "  (active)" : "");
        GdiTextT(x + 16, ry + 6, name, UI_TEXT);
        const char *state = !p.enabled ? "Off: this PC is not protected on these networks" :
                            p.block_all ? "On, blocking all incoming connections" :
                            p.default_in ? "On, incoming connections allowed" :
                            "On, incoming connections blocked unless a rule allows them";
        GdiTextT(x + 16, ry + 26, state, p.enabled ? UI_TEXT2 : GDI_C(0xE8, 0xA8, 0x20));
        toggle(r_prof_toggle(x, y, w, i).x, r_prof_toggle(x, y, w, i).y, p.enabled != 0);
    }

    /* the rules */
    int ry = rules_y(y);
    INT64 count = 0;
    fw(NOVAFWC_RULE_COUNT, NULL, &count);
    char head[48];
    ksnprintf(head, sizeof(head), "Allowed apps and rules (%d)", (int)count);
    GdiTextBold(x, ry - 28, head, UI_TEXT);
    UiButton(r_restore(x, y, w), "Restore defaults", false);
    g_nshown = 0;
    NovaFwRule *r = kmalloc(sizeof(*r));
    for (int i = 0; r && i < RULES_SHOWN && i < count; i++) {
        memset(r, 0, sizeof(*r));
        r->index = i;
        if (fw(NOVAFWC_RULE_AT, r, NULL)) break;
        int yy = ry + i * (RULE_H + 4);
        GdiRoundRect(RECT(x, yy, w, RULE_H), 6, UI_CARD, GDI_TRANSPARENT);
        GdiTextT(x + 16, yy + 2, r->name[0] ? r->name : r->id, r->enabled ? UI_TEXT : UI_TEXT3);
        rule_detail(r, line, sizeof(line));
        GdiTextT(x + 16, yy + 20, line, UI_TEXT3);
        GdiRect t = r_rule_toggle(x, y, w, i);
        toggle(t.x, t.y, r->enabled != 0);
        strcpy(g_ids[i], r->id);
        g_nshown = i + 1;
    }
    kfree(r);
    int ny = ry + g_nshown * (RULE_H + 4) + 4;
    if (!count) { GdiTextT(x + 16, ny, "No rules: only what this PC starts is let through.", UI_TEXT2); ny += 22; }
    else if (count > g_nshown) {
        char more[96];
        ksnprintf(more, sizeof(more), "and %d more (netsh advfirewall firewall show rule name=all)", (int)count - g_nshown);
        GdiTextT(x + 16, ny, more, UI_TEXT2);
        ny += 22;
    }
    if (g_note[0]) { GdiTextT(x, ny, g_note, GDI_C(0xE0, 0x60, 0x60)); ny += 22; }
    return ny;
}

bool FirewallPageMouse(int px, int py, int pw, int x, int y)
{
    g_note[0] = 0;
    bool hit = false;
    NovaFwNetwork net;
    bool connected = FwGetNetwork(&net);
    for (int i = 0; i < 2 && connected && net.category != 2; i++) if (UiHit(r_category(px, py, pw, i), x, y)) hit = true;
    for (int i = 0; i < 3; i++) if (UiHit(r_prof_toggle(px, py, pw, i), x, y)) hit = true;
    for (int i = 0; i < g_nshown; i++) if (UiHit(r_rule_toggle(px, py, pw, i), x, y)) hit = true;
    if (UiHit(r_restore(px, py, pw), x, y)) hit = true;
    if (!hit) return false;
    if (!can_change()) {
        strcpy(g_note, "Only an administrator can change the firewall.");
        return true;
    }
    for (int i = 0; i < 2 && connected && net.category != 2; i++)
        if (UiHit(r_category(px, py, pw, i), x, y)) { FwSetCategory(i); FwPromptAnswered(); return true; }
    for (int i = 0; i < 3; i++) {
        if (!UiHit(r_prof_toggle(px, py, pw, i), x, y)) continue;
        NovaFwProfile p = { .profile = 1u << i };
        if (!fw(NOVAFWC_PROFILE_GET, &p, NULL)) {
            p.enabled = !p.enabled;
            fw(NOVAFWC_PROFILE_SET, &p, NULL);
        }
        return true;
    }
    for (int i = 0; i < g_nshown; i++) {
        if (!UiHit(r_rule_toggle(px, py, pw, i), x, y)) continue;
        NovaFwRule *r = kzalloc(sizeof(*r));
        if (!r) return true;
        strcpy(r->id, g_ids[i]);
        if (!fw(NOVAFWC_RULE_GET, r, NULL)) {
            r->enabled = !r->enabled;
            if (fw(NOVAFWC_RULE_SET, r, NULL)) ksnprintf(g_note, sizeof(g_note), "The rule could not be changed: %s", r->desc);
        }
        kfree(r);
        return true;
    }
    fw(NOVAFWC_RESTORE_DEFAULTS, NULL, NULL);
    return true;
}

/* Something to show again: a change in the registry (where the firewall
 * keeps its state) or in the network */
bool FirewallPageTick(void)
{
    static UINT32 sig;
    NovaFwNetwork net;
    bool c = FwGetNetwork(&net);
    UINT32 s = um_registry_generation() ^ (c ? 0x80000000u : 0) ^ ((UINT32)net.category << 28) ^ FwCurrentProfiles() << 24;
    if (s == sig) return false;
    sig = s;
    return true;
}

/* ---------------------------------------------------------------------------
 * The new-network prompt
 * ------------------------------------------------------------------------- */
#define PROMPT_W 360
#define PROMPT_H 176
static WND *g_prompt;
static UINT64 g_prompt_until;

static GdiRect r_yes(GdiRect c) { return RECT(c.x + 16, c.y + c.h - 44, (c.w - 40) / 2, 30); }
static GdiRect r_no(GdiRect c)  { return RECT(c.x + 24 + (c.w - 40) / 2, c.y + c.h - 44, (c.w - 40) / 2, 30); }

static void prompt_paint(WND *w)
{
    GdiRect c = WmClientRect(w);
    NovaFwNetwork net;
    FwGetNetwork(&net);
    GdiTextBold(c.x + 16, c.y + 12, net.name[0] ? net.name : "Network", UI_TEXT);
    GdiTextT(c.x + 16, c.y + 38, "Do you want to allow your PC to be", UI_TEXT);
    GdiTextT(c.x + 16, c.y + 56, "discoverable by other PCs and devices", UI_TEXT);
    GdiTextT(c.x + 16, c.y + 74, "on this network?", UI_TEXT);
    GdiTextT(c.x + 16, c.y + 98, "Yes on your home and work networks,", UI_TEXT3);
    GdiTextT(c.x + 16, c.y + 114, "not on public ones.", UI_TEXT3);
    UiButton(r_yes(c), "Yes", true);
    UiButton(r_no(c), "No", false);
}

static void prompt_answer(int category)
{
    if (category >= 0) {
        if (can_change()) FwSetCategory(category);
        else kprintf("[FIREWALL] the network stays Public: only an administrator can change it\n");
    }
    FwPromptAnswered();
    if (g_prompt) { WND *w = g_prompt; g_prompt = NULL; WmDestroyWindow(w); }
}

static void prompt_mouse(WND *w, WmMouseMsg msg, int x, int y)
{
    if (msg != WM_MOUSE_DOWN) return;
    GdiRect c = WmClientRect(w);
    c.x = c.y = 0;
    if (UiHit(r_yes(c), x, y)) prompt_answer(1);
    else if (UiHit(r_no(c), x, y)) prompt_answer(0);
}

static bool prompt_tick(WND *w)
{
    (void)w;
    if (sched_ticks() >= g_prompt_until || !FwPromptPending()) prompt_answer(-1);   /* (stays as it is) */
    return false;
}

static void prompt_close(WND *w)
{
    (void)w;
    if (g_prompt) { g_prompt = NULL; FwPromptAnswered(); }
}

void NetworkPromptPoll(void)
{
    FwPoll();
}

bool NetworkPromptOpen(void)
{
    if (g_prompt) return true;
    if (!FwPromptPending()) return false;
    GdiRect work = WmWorkArea();
    int fw_ = PROMPT_W + 2, fh = PROMPT_H + WM_TITLEBAR_H + 1;
    GdiRect frame = RECT(work.x + work.w - fw_ - 12, work.y + work.h - fh - 12, fw_, fh);
    g_prompt = WmCreateWindowEx("Networks", frame, WS_TOOLWINDOW, UI_BG, UI_ACCENT, prompt_paint, NULL, false);
    if (!g_prompt) return false;
    g_prompt->no_activate = true;
    g_prompt->on_mouse = prompt_mouse;
    g_prompt->on_tick = prompt_tick;
    g_prompt->on_close = prompt_close;
    g_prompt_until = sched_ticks() + 60 * 100;
    NovaFwNetwork net;
    FwGetNetwork(&net);
    kprintf("[FIREWALL] asking whether this PC may be discoverable on \"%s\"\n", net.name);
    return true;
}
