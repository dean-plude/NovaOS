/*
 * consent.c — User Account Control's prompt (see consent.h)
 *
 * One request at a time: the thread asking (a program's CreateProcess, or
 * the Terminal's worker starting a program) leaves it here and sleeps;
 * the desktop loop shows it (ConsentPoll), draws it over everything and
 * hands it the keyboard and the mouse, and wakes the asker with the
 * answer.  The "[UAC]" line is logged when the dialog is on the screen,
 * not before, so a key pressed after it reaches the dialog.
 *
 * Windows 11's layout: the screen behind dimmed, a dialog with "User
 * Account Control" over "Do you want to allow this app to make changes to
 * your device?", the program's name, its publisher (NovaOS checks no
 * signature here yet: "Unknown") and where it is, then Yes and No, or the
 * user name and password boxes.  Alt+Y and Alt+N answer, Esc is No,
 * Enter presses the focused button (Yes at first) or, with the boxes,
 * Yes; Tab moves between the boxes and the buttons.  A wrong password
 * says "The user name or password is incorrect." and refuses the next try
 * for a second, as the sign-in screen does.
 */

#include "consent.h"
#include "desktop.h"
#include "logon.h"
#include "wm.h"
#include "../gdi/gdi.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/smp.h"
#include "../lib/string.h"
#include "../um/um.h"

/* The request (one at a time: g_busy) */
static volatile UINT32 g_busy;
static volatile UINT32 g_pending;          /* waiting for the desktop to show it */
static volatile UINT32 g_done;             /* answered: g_answer */
static UINT32 g_answer, g_self;
static bool g_cred;
static char g_path[160], g_name[64];

/* The dialog, while it shows (the desktop thread's) */
static bool g_on;
static int g_focus;                        /* 0 user name, 1 password, 2 Yes, 3 No */
static char g_user[UM_ACCOUNT_NAME_MAX + 1], g_pw[UM_PASSWORD_MAX + 1];
static int g_userlen, g_pwlen;
static const char *g_msg;
static UINT64 g_retry_at;

#define C_SURFACE GDI_C(0x2B, 0x2B, 0x2B)
#define C_FOOT    GDI_C(0x20, 0x20, 0x20)
#define C_TEXT    0xFFFFFF
#define C_DIM     GDI_C(0xC5, 0xC5, 0xC5)
#define C_ACCENT  GDI_C(0x00, 0x5A, 0x9E)  /* (the system accent, apps.h's UI_ACCENT_BG) */
#define C_FIELD   GDI_C(0x1F, 0x1F, 0x1F)
#define C_ERROR   GDI_C(0xFF, 0x99, 0xA4)

static void wipe(void)
{
    volatile char *p = g_pw;
    for (int i = 0; i < (int)sizeof(g_pw); i++) p[i] = 0;
    g_pwlen = 0;
}

UINT32 ConsentAsk(const char *path, bool credentials, UINT32 self)
{
    if (!DesktopAvailable()) {
        kprintf("[UAC] No desktop to ask on: %s does not run as administrator\n", path);
        return 0;
    }
    while (__atomic_exchange_n(&g_busy, 1u, __ATOMIC_ACQUIRE)) sched_sleep_tick();
    strncpy(g_path, path, sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = 0;
    const char *base = g_path;
    for (const char *c = g_path; *c; c++) if (*c == '\\' || *c == '/') base = c + 1;
    strncpy(g_name, base, sizeof(g_name) - 1);
    g_name[sizeof(g_name) - 1] = 0;
    g_cred = credentials;
    g_self = self;
    g_answer = 0;
    __atomic_store_n(&g_done, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pending, 1u, __ATOMIC_RELEASE);
    WmInvalidate();
    UINT32 depth = bkl_drop();                 /* (the desktop needs it to draw) */
    while (!__atomic_load_n(&g_done, __ATOMIC_ACQUIRE)) sched_sleep_until(&g_done, sched_ticks() + 10);
    bkl_restore(depth);
    UINT32 a = g_answer;
    __atomic_store_n(&g_busy, 0u, __ATOMIC_RELEASE);
    return a;
}

static UINT32 g_serial;                     /* prompts shown so far */

bool ConsentActive(void) { return g_on; }
UINT32 ConsentSerial(void) { return g_serial; }

void ConsentPoll(void)
{
    if (g_on || !__atomic_load_n(&g_pending, __ATOMIC_ACQUIRE) || LogonActive()) return;
    g_pending = 0;
    g_serial++;
    g_on = true;
    g_user[0] = 0;
    g_userlen = 0;
    wipe();
    g_msg = NULL;
    g_focus = g_cred ? 0 : 2;
    kprintf("[UAC] %s prompt for %s\n", g_cred ? "Credential" : "Consent", g_path);
    WmInvalidate();
}

static void answer(UINT32 rid)
{
    wipe();
    g_on = false;
    g_answer = rid;
    kprintf("[UAC] %s: %s\n", g_name, rid ? "allowed" : "refused");
    __atomic_store_n(&g_done, 1u, __ATOMIC_RELEASE);
    WmInvalidateBackground();
    WmInvalidate();
}

/* -----------------------------------------------------------------------
 * Layout
 * ----------------------------------------------------------------------- */
#define DLG_W   456
#define FIELD_H 32
#define BTN_H   32

static int dlg_h(void) { return g_cred ? 376 : 300; }
static GdiRect r_dlg(void) { return RECT(GdiScreenW() / 2 - DLG_W / 2, GdiScreenH() / 2 - dlg_h() / 2, DLG_W, dlg_h()); }
static GdiRect r_field(int i)
{
    GdiRect d = r_dlg();
    return RECT(d.x + 24, d.y + 196 + i * (FIELD_H + 10), DLG_W - 48, FIELD_H);
}
static GdiRect r_button(int i)                 /* 0 Yes, 1 No */
{
    GdiRect d = r_dlg();
    int w = (DLG_W - 48 - 8) / 2;
    return RECT(d.x + 24 + i * (w + 8), d.y + d.h - 16 - BTN_H, w, BTN_H);
}
static bool in(GdiRect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static void field(int i, const char *hint)
{
    GdiRect f = r_field(i);
    GdiRoundRect(f, 4, C_FIELD, g_focus == i ? C_ACCENT : GDI_C(0x60, 0x60, 0x60));
    if (g_focus == i) GdiFillRect(RECT(f.x + 4, f.y + f.h - 2, f.w - 8, 2), C_ACCENT);
    int ty = f.y + (FIELD_H - GDI_FONT_H) / 2;
    if (i == 0 && g_userlen) GdiTextT(f.x + 10, ty, g_user, C_TEXT);
    else if (i == 1 && g_pwlen) {
        int max = (f.w - 20) / 10;
        for (int k = 0; k < g_pwlen && k < max; k++) GdiFillCircle(f.x + 14 + k * 10, f.y + FIELD_H / 2, 3, C_TEXT);
    } else GdiTextT(f.x + 10, ty, hint, C_DIM);
}

static void button(int i)
{
    GdiRect b = r_button(i);
    bool yes = i == 0, focus = g_focus == 2 + i;
    GdiRoundRect(b, 4, yes ? C_ACCENT : GDI_C(0x3C, 0x3C, 0x3C), focus ? C_TEXT : GDI_TRANSPARENT);
    GdiTextCenter(b.x, b.y + (BTN_H - GDI_FONT_H) / 2, b.w, yes ? "Yes" : "No", C_TEXT);
}

void ConsentDraw(void)
{
    if (!g_on) return;
    GdiAlphaFill(RECT(0, 0, GdiScreenW(), GdiScreenH()), GDI_BLACK, 150);    /* (the desktop behind, dimmed) */
    for (int i = 1; i < GdiMonitorCount(); i++) GdiAlphaFill(GdiMonitorRect(i), GDI_BLACK, 150);
    GdiRect d = r_dlg();
    GdiDropShadow(d, 8, 24, 120);
    GdiRoundRect(d, 8, C_SURFACE, GDI_C(0x45, 0x45, 0x45));
    int x = d.x + 24, y = d.y + 20;
    GdiTextT(x, y, "User Account Control", C_DIM);
    GdiTextBold(x, y + 30, "Do you want to allow this app to make", C_TEXT);
    GdiTextBold(x, y + 50, "changes to your device?", C_TEXT);

    /* the program: its icon's place, its name, publisher and location */
    GdiRoundRect(RECT(x, y + 86, 32, 32), 4, C_ACCENT, GDI_TRANSPARENT);
    char ini[2] = { g_name[0] >= 'a' && g_name[0] <= 'z' ? (char)(g_name[0] - 32) : g_name[0], 0 };
    GdiTextCenter(x, y + 86 + (32 - GDI_FONT_H) / 2, 32, ini, C_TEXT);
    GdiTextBold(x + 44, y + 86, g_name, C_TEXT);
    GdiTextT(x + 44, y + 104, "Publisher: Unknown", C_DIM);
    char loc[200];
    ksnprintf(loc, sizeof(loc), "Program location: %s", g_path);
    int max = DLG_W - 48;
    while (GdiTextW(loc) > max && strlen(loc) > 24) {   /* (a long path: the end kept) */
        size_t n = strlen(loc);
        memmove(loc + 18, loc + 22, n - 21);
        memcpy(loc + 18, "...", 3);
    }
    GdiTextT(x, y + 136, loc, C_DIM);

    if (g_cred) {
        GdiTextT(x, y + 160, "To continue, enter an admin user name and password.", C_TEXT);
        field(0, "User name");
        field(1, "Password");
        if (g_msg) GdiTextT(x, r_field(1).y + FIELD_H + 6, g_msg, C_ERROR);
    }
    GdiFillRect(RECT(d.x + 1, r_button(0).y - 16, d.w - 2, d.y + d.h - (r_button(0).y - 16) - 8), C_FOOT);
    button(0);
    button(1);
}

/* -----------------------------------------------------------------------
 * Input
 * ----------------------------------------------------------------------- */
static void yes(void)
{
    if (!g_cred) { answer(g_self); return; }
    if (sched_ticks() < g_retry_at) return;                        /* (a second after a wrong one) */
    UmAccount a;
    bool ok = g_userlen && um_account_find(g_user, &a) && (a.flags & UM_ACCT_ADMIN) && !(a.flags & UM_ACCT_DISABLED) &&
              um_account_verify(a.rid, g_pw);
    if (!ok) {
        kprintf("[UAC] Wrong administrator name or password for %s\n", g_name);
        wipe();
        g_msg = "The user name or password is incorrect.";
        g_focus = 1;
        g_retry_at = sched_ticks() + 100;
        WmInvalidate();
        return;
    }
    kprintf("[UAC] %s approved %s\n", a.name, g_name);
    answer(a.rid);
}

void ConsentKey(const KeyEvent *k)
{
    if (!k->pressed || !g_on) return;
    if (k->alt && k->scancode == 0x15) { yes(); return; }          /* Alt+Y */
    if ((k->alt && k->scancode == 0x31) || k->scancode == KEY_ESC) { answer(0); return; }   /* Alt+N, Esc */
    if (k->scancode == KEY_ENTER) {
        if (g_focus == 3) answer(0);
        else yes();
        return;
    }
    if (k->scancode == KEY_TAB || (k->extended && (k->scancode == KEY_LEFT || k->scancode == KEY_RIGHT))) {
        int first = g_cred ? 0 : 2;
        bool back = k->shift || (k->extended && k->scancode == KEY_LEFT);
        g_focus = first + ((g_focus - first + (back ? -1 : 1)) % (4 - first) + (4 - first)) % (4 - first);
        WmInvalidate();
        return;
    }
    if (g_focus > 1 || k->ctrl || k->alt) return;
    if (k->scancode == KEY_BACKSPACE) {
        if (g_focus == 0 && g_userlen) g_user[--g_userlen] = 0;
        if (g_focus == 1 && g_pwlen) g_pw[--g_pwlen] = 0;
        WmInvalidate();
        return;
    }
    char c = k->ch;
    if ((UINT8)c < 0x20 || (UINT8)c >= 0x7F) return;
    if (g_focus == 0 && g_userlen < UM_ACCOUNT_NAME_MAX) { g_user[g_userlen++] = c; g_user[g_userlen] = 0; }
    if (g_focus == 1 && g_pwlen < UM_PASSWORD_MAX) { g_pw[g_pwlen++] = c; g_pw[g_pwlen] = 0; }
    g_msg = NULL;
    WmInvalidate();
}

void ConsentPress(int x, int y)
{
    if (!g_on) return;
    if (in(r_button(0), x, y)) { yes(); return; }
    if (in(r_button(1), x, y)) { answer(0); return; }
    for (int i = 0; g_cred && i < 2; i++)
        if (in(r_field(i), x, y)) { g_focus = i; WmInvalidate(); }
}
