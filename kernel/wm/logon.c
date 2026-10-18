/*
 * logon.c — the sign-in and lock screen (see logon.h)
 *
 * Drawn by the shell over everything else, Windows 11's way: the screen
 * behind it frosted, the time and date at the top, the account's
 * initial in a circle with its name below, and the password box with
 * its arrow.  A wrong password says so and refuses the next try for a
 * second (um_account_verify does not wait: this is the desktop's thread);
 * the right one signs the account in
 * (um_session_set: its profile and token for what the desktop starts
 * from then on) or, on a locked screen, gives the desktop back.
 *
 * Fast user switching: with more than one account the others are listed
 * in the corner, on the lock screen too.  Signing in as another while one
 * is signed in switches (WmSetSession): the first one's windows are put
 * away with their programs still running, and come back when it signs in
 * again.  Sign-out (Start's account menu, ExitWindowsEx) ends an
 * account's programs (UmEndSession) and closes its windows.
 */

#include "logon.h"
#include "wm.h"
#include "../gdi/gdi.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../ke/timezone.h"
#include "../lib/string.h"
#include "../um/um.h"
#include "../fs/ramfs.h"
#include "wallpaper.h"

enum { MODE_OFF, MODE_BOOT, MODE_LOCK };
#define MAX_TILES 8

static int g_mode;
static volatile bool g_lock_req;
static bool g_first_done;
static UmAccount g_acct[MAX_TILES];
static char g_pic[MAX_TILES][96];           /* their pictures' C: paths ("": the initial) */
static bool g_on[MAX_TILES];                /* signed in already (switched away from) */
static int g_n, g_sel;
static volatile UINT32 g_signout_req;      /* an account to sign out (LogonSignOut) */
static char g_pw[UM_PASSWORD_MAX + 1];
static int g_pwlen;
static const char *g_msg;
static UINT64 g_retry_at;                  /* a wrong password: the next try not before (ticks) */

#define C_TEXT   0xFFFFFF
#define C_DIM    GDI_C(0xC8, 0xD2, 0xE0)
#define C_ACCENT GDI_C(0x00, 0x5A, 0x9E)  /* (the system accent, apps.h's UI_ACCENT_BG) */
#define C_NIGHT  GDI_C(0x0B, 0x1E, 0x3A)
#define C_ERROR  GDI_C(0xFF, 0xB4, 0xA9)

static void wipe(void)
{
    volatile char *p = g_pw;
    for (int i = 0; i < (int)sizeof(g_pw); i++) p[i] = 0;
    g_pwlen = 0;
}

/* Every account that may sign in; @pick (or else the one on the screen)
 * chosen.  Locked, the others are there too: one of them may switch in
 * while the one that locked it stays signed in, as on Windows. */
static void load_accounts(UINT32 pick)
{
    static UmAccount all[32];                                      /* (the desktop thread's) */
    int n = um_accounts_list(all, 32);
    UINT32 me = pick ? pick : um_session_rid();
    g_n = 0;
    g_sel = 0;
    for (int i = 0; i < n && g_n < MAX_TILES; i++) {
        if (all[i].flags & UM_ACCT_DISABLED) continue;
        if (all[i].rid == me) g_sel = g_n;
        um_account_picture(all[i].rid, g_pic[g_n], sizeof(g_pic[0]));
        g_on[g_n] = um_session_signed_in(all[i].rid);
        g_acct[g_n++] = all[i];
    }
}

static void show_for(int mode, UINT32 pick)
{
    g_mode = mode;
    wipe();
    g_msg = NULL;
    load_accounts(pick);
    if (!g_n) { g_mode = MODE_OFF; return; }                       /* (nothing to sign in as) */
    kprintf("[LOGON] %s screen (%s)\n", mode == MODE_LOCK ? "Lock" : "Sign-in", g_acct[g_sel].name);
    WmInvalidate();
}

static void show(int mode) { show_for(mode, 0); }

bool LogonStart(void)
{
    if (!um_account_signin_required()) {
        um_session_set(um_session_rid());
        WmSetSession(um_session_rid());
        g_first_done = true;
        return true;
    }
    show(MODE_BOOT);
    if (g_mode == MODE_OFF) { g_first_done = true; return true; }
    return false;
}

bool LogonTakeFirstSignIn(void)
{
    static bool taken;
    if (!g_first_done || taken) return false;
    taken = true;
    return true;
}

bool LogonActive(void) { return g_mode != MODE_OFF; }

void LogonLock(void) { __atomic_store_n(&g_lock_req, true, __ATOMIC_RELEASE); }

void LogonSwitchTo(UINT32 rid)
{
    if (g_mode == MODE_OFF) show_for(MODE_BOOT, rid);
}

void LogonSignOut(UINT32 rid) { __atomic_store_n(&g_signout_req, rid, __ATOMIC_RELEASE); }

/* Sign-out: the account's programs are ended and its windows closed;
 * the sign-in screen follows when it was the one on the screen */
static void sign_out(UINT32 rid)
{
    UmAccount a;
    if (!um_session_signed_in(rid) || !um_account_get(rid, &a)) return;
    int n = UmEndSession(rid);
    WmCloseSession(rid);
    um_session_end(rid);
    kprintf("[LOGON] %s signed out (%d programs ended)\n", a.name, n);
    if (rid == um_session_rid() && g_mode == MODE_OFF) show(MODE_BOOT);
    else if (g_mode != MODE_OFF) load_accounts(g_acct[g_sel].rid);
    WmInvalidate();
}

void LogonPoll(void)
{
    UINT32 out = __atomic_exchange_n(&g_signout_req, 0, __ATOMIC_ACQ_REL);
    if (out) sign_out(out);
    if (!__atomic_exchange_n(&g_lock_req, false, __ATOMIC_ACQ_REL)) return;
    if (g_mode == MODE_OFF) show(MODE_LOCK);
}

/* -----------------------------------------------------------------------
 * Layout
 * ----------------------------------------------------------------------- */
#define FIELD_W 296
#define FIELD_H 36
#define TILE_W  232
#define TILE_H  48

static int cx(void) { return GdiScreenW() / 2; }
static int cy(void) { return GdiScreenH() / 2; }
static GdiRect r_field(void)  { return RECT(cx() - FIELD_W / 2, cy() + 52, FIELD_W, FIELD_H); }
static GdiRect r_arrow(void)  { GdiRect f = r_field(); return RECT(f.x + f.w - FIELD_H, f.y, FIELD_H, FIELD_H); }
static GdiRect r_button(void) { return RECT(cx() - 80, cy() + 52, 160, FIELD_H); }
static GdiRect r_tile(int i)  { return RECT(24, GdiScreenH() - 24 - (g_n - i) * (TILE_H + 8), TILE_W, TILE_H); }
static bool in(GdiRect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static char initial(const UmAccount *a)
{
    char c = a->name[0];
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

static void avatar(int x, int y, int rad, int i, bool large)
{
    const UmAccount *a = &g_acct[i];
    if (g_pic[i][0]) {                                             /* its picture (Settings, Accounts) */
        FsLock();
        RamNode *f = RamfsResolve(NULL, g_pic[i] + 2);
        bool drawn = f && !f->dir && WallpaperDrawThumb(f, RECT(x - rad, y - rad, 2 * rad, 2 * rad));
        FsUnlock();
        if (drawn) { GdiRoundBorderAlpha(RECT(x - rad, y - rad, 2 * rad, 2 * rad), rad / 4, 0xFFFFFF, 140); return; }
    }
    GdiFillCircle(x, y, rad, C_ACCENT);
    char s[2] = { initial(a), 0 };
    if (large) GdiTextLarge(x - GdiTextLargeW(s) / 2, y - GDI_LARGE_H / 2, s, C_TEXT);
    else GdiTextCenter(x - rad, y - GDI_FONT_H / 2, 2 * rad, s, C_TEXT);
}

static const char *shown_name(const UmAccount *a) { return a->full[0] ? a->full : a->name; }

void LogonDraw(void)
{
    if (g_mode == MODE_OFF) return;
    int w = GdiScreenW(), h = GdiScreenH();
    GdiRect all = RECT(0, 0, w, h);
    GdiBackdrop(all, 0, 28, C_NIGHT, 150);
    for (int i = 1; i < GdiMonitorCount(); i++) GdiFillRect(GdiMonitorRect(i), C_NIGHT);   /* (the others: dark) */

    /* the time and date */
    RtcTime t;
    TzLocalNow(&t);
    static const char *const months[12] = { "January", "February", "March", "April", "May", "June", "July",
                                            "August", "September", "October", "November", "December" };
    static const char *const days[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    static const int off[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int m = t.month >= 1 && t.month <= 12 ? t.month : 1, yy = t.year - (m < 3);
    int dow = (yy + yy / 4 - yy / 100 + yy / 400 + off[m - 1] + t.day) % 7;
    char clock[16], date[48];
    int h12 = t.hour % 12 ? t.hour % 12 : 12;
    ksnprintf(clock, sizeof(clock), "%d:%02u", h12, t.minute);
    ksnprintf(date, sizeof(date), "%s, %s %u", days[dow], months[m - 1], t.day);
    int top = h / 8;
    GdiTextLarge(cx() - GdiTextLargeW(clock) / 2, top, clock, C_TEXT);
    GdiTextShadowCenter(0, top + GDI_LARGE_H + 4, w, date, C_TEXT, 120);

    /* the chosen account */
    const UmAccount *a = &g_acct[g_sel];
    avatar(cx(), cy() - 64, 56, g_sel, true);
    const char *nm = shown_name(a);
    GdiTextLarge(cx() - GdiTextLargeW(nm) / 2, cy() + 6, nm, C_TEXT);
    if (a->password) {
        GdiRect f = r_field();
        GdiRoundAlpha(f, 6, 0xFFFFFF, 40);
        GdiRoundBorderAlpha(f, 6, 0xFFFFFF, 110);
        if (!g_pwlen) GdiTextT(f.x + 12, f.y + (FIELD_H - GDI_FONT_H) / 2, "Password", C_DIM);
        int max = (f.w - FIELD_H - 16) / 12;
        for (int i = 0; i < g_pwlen && i < max; i++) GdiFillCircle(f.x + 16 + i * 12, f.y + FIELD_H / 2, 4, C_TEXT);
        GdiRect ar = r_arrow();
        GdiRoundAlpha(RECT(ar.x + 4, ar.y + 4, ar.w - 8, ar.h - 8), 4, C_ACCENT, 255);
        int ax = ar.x + ar.w / 2, ay = ar.y + ar.h / 2;
        GdiPoint tri[3] = { { (ax - 4) * 16, (ay - 7) * 16 }, { (ax + 5) * 16, ay * 16 }, { (ax - 4) * 16, (ay + 7) * 16 } };   /* (1/16 px) */
        GdiFillPolygon(tri, 3, C_TEXT);
    } else {
        GdiRect b = r_button();
        GdiRoundAlpha(b, 6, 0xFFFFFF, 46);
        GdiRoundBorderAlpha(b, 6, 0xFFFFFF, 110);
        GdiTextCenter(b.x, b.y + (FIELD_H - GDI_FONT_H) / 2, b.w, g_mode == MODE_LOCK && a->rid == um_session_rid() ? "Unlock" : "Sign in", C_TEXT);
    }
    if (g_msg) GdiTextShadowCenter(0, r_field().y + FIELD_H + 16, w, g_msg, C_ERROR, 140);
    bool mine = a->rid == um_session_rid();
    if (g_mode == MODE_LOCK && mine) GdiTextShadowCenter(0, r_field().y + FIELD_H + 40, w, "Locked", C_DIM, 120);
    else if (g_on[g_sel] && !mine) GdiTextShadowCenter(0, r_field().y + FIELD_H + 40, w, "Signed in", C_DIM, 120);

    /* the accounts to choose from (another one switches in: its own
     * desktop, the others' programs still running behind it) */
    if (g_n > 1)
        for (int i = 0; i < g_n; i++) {
            GdiRect r = r_tile(i);
            GdiRoundAlpha(r, 6, 0xFFFFFF, i == g_sel ? 60 : 22);
            avatar(r.x + 24, r.y + TILE_H / 2, 16, i, false);
            int ty = g_on[i] ? r.y + 6 : r.y + (TILE_H - GDI_FONT_H) / 2;
            GdiTextT(r.x + 52, ty, shown_name(&g_acct[i]), C_TEXT);
            if (g_on[i]) GdiTextT(r.x + 52, ty + GDI_FONT_H + 1, "Signed in", C_DIM);
        }
}

/* -----------------------------------------------------------------------
 * Input
 * ----------------------------------------------------------------------- */
static void submit(void)
{
    const UmAccount *a = &g_acct[g_sel];
    if (sched_ticks() < g_retry_at) return;                         /* (a second after a wrong one) */
    if (a->password && !um_account_verify(a->rid, g_pw)) {
        wipe();
        g_retry_at = sched_ticks() + 100;
        g_msg = "The password is incorrect. Try again.";
        kprintf("[LOGON] Wrong password for %s\n", a->name);
        WmInvalidate();
        return;
    }
    wipe();
    UINT32 was = um_session_rid();
    if (g_mode == MODE_LOCK && a->rid == was && um_session_signed_in(was)) kprintf("[LOGON] %s unlocked the screen\n", a->name);
    else {
        bool other = g_first_done && a->rid != was && um_session_signed_in(was);
        um_session_set(a->rid);
        WmSetSession(a->rid);
        if (other) {
            UmAccount o;
            kprintf("[LOGON] Switched to %s (%s stays signed in)\n", a->name, um_account_get(was, &o) ? o.name : "?");
        }
    }
    if (g_mode == MODE_BOOT) g_first_done = true;
    g_mode = MODE_OFF;
    g_msg = NULL;
    WmInvalidateBackground();
    WmInvalidate();
}

static void choose(int i)
{
    if (i < 0 || i >= g_n || i == g_sel) return;
    g_sel = i;
    wipe();
    g_msg = NULL;
    WmInvalidate();
}

void LogonKey(const KeyEvent *k)
{
    if (!k->pressed || g_mode == MODE_OFF) return;
    if (k->scancode == KEY_ENTER && !k->alt) { submit(); return; }
    if (k->scancode == KEY_ESC) { wipe(); g_msg = NULL; WmInvalidate(); return; }
    if (k->scancode == KEY_BACKSPACE) { if (g_pwlen) g_pw[--g_pwlen] = 0; WmInvalidate(); return; }
    if (g_n > 1 && ((k->extended && (k->scancode == KEY_UP || k->scancode == KEY_DOWN)) || k->scancode == KEY_TAB)) {
        bool up = k->extended && k->scancode == KEY_UP;
        choose(((g_sel + (up ? -1 : 1)) % g_n + g_n) % g_n);
        return;
    }
    if (!g_acct[g_sel].password || k->ctrl || k->alt) return;
    char c = k->ch;
    if ((UINT8)c >= 0x20 && (UINT8)c < 0x7F && g_pwlen < UM_PASSWORD_MAX) {
        g_pw[g_pwlen++] = c;
        g_pw[g_pwlen] = 0;
        g_msg = NULL;
        WmInvalidate();
    }
}

void LogonPress(int x, int y)
{
    if (g_mode == MODE_OFF) return;
    if (g_n > 1)
        for (int i = 0; i < g_n; i++) if (in(r_tile(i), x, y)) { choose(i); return; }
    if (g_acct[g_sel].password ? in(r_arrow(), x, y) : in(r_button(), x, y)) submit();
}
