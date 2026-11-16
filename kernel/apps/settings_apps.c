/*
 * settings_apps.c — Settings' Apps page (Windows 11's Apps > Installed apps)
 *
 * The app packages installed for the account on the screen, as the
 * package installer (userland/programs/appx.c) records them in its
 * package repository (HKCU\Software\Classes\Local Settings\Software\
 * Microsoft\Windows\CurrentVersion\AppModel\Repository\Packages): each
 * one's tile logo, display name, publisher and version, with an Uninstall
 * button.  Framework packages are left out, as Windows' list leaves them
 * out (they go when nothing uses them; appx refuses to remove one that a
 * package still needs).  Uninstall asks first ("Uninstall" again, or
 * Cancel), then runs "appx remove <full name>" as Windows' deployment
 * service removes a package: as the system, so without UAC's prompt
 * (UmSpawnElevated, as the App Store's installers run).  The keyboard
 * works it too: Up and Down choose an app, Delete asks to uninstall it,
 * Enter confirms and Esc cancels.  Each step is logged as "[SETTINGS]
 * Apps: ...", which the core suite's packaged-apps check
 * (tests/selftest/core/179-appxshell.py) waits for.
 */

#include "apps.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../wm/input.h"
#include "../um/um.h"

#define ROW_H      64
#define APPS_MAX   32
#define APPS_REPO  "Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages"

typedef struct {
    char full[128];                     /* the package's full name */
    char name[64], publisher[64], version[24];
    char logo[RAMFS_PATH_MAX];          /* its tile logo (the first application's), C: path without "C:" */
} PkgRow;

static PkgRow g_rows[APPS_MAX];
static int  g_n;
static int  g_sel = -1;                 /* the app chosen with the keyboard */
static int  g_ask = -1;                 /* the app whose Uninstall waits for its confirmation */
static UINT32 g_gen;                    /* the registry generation listed */
static UmProcess *g_job;                /* appx remove, running */
static char g_job_full[128], g_job_name[64];
static char g_note[160];                /* what the last uninstall did */

static const char *no_drive(const char *p) { return (p[0] | 0x20) == 'c' && p[1] == ':' ? p + 2 : p; }

static void repo_path(char *out, int cap)
{
    char sid[64];
    um_account_sid_string(um_session_rid(), sid, sizeof(sid));
    ksnprintf(out, cap, "User\\%s\\" APPS_REPO, sid);
}

static void ascii(const UINT16 *w, int n, char *out, int cap)
{
    int i = 0;
    for (; i < n && i < cap - 1; i++) out[i] = w[i] < 0x80 ? (char)w[i] : '?';
    out[i] = '\0';
}

/* The packages, again (the registry changed since they were listed) */
static void list(void)
{
    char repo[256];
    repo_path(repo, sizeof(repo));
    g_gen = um_registry_generation();
    g_n = 0;
    for (UINT32 i = 0; g_n < APPS_MAX; i++) {
        UINT16 w[128];
        char key[256], akey[256];
        int n = um_registry_enum_key(repo, i, w, 127, NULL);
        if (n < 0) break;
        PkgRow *r = &g_rows[g_n];
        memset(r, 0, sizeof(*r));
        ascii(w, n, r->full, sizeof(r->full));
        ksnprintf(key, sizeof(key), "%s\\%s", repo, r->full);
        UINT32 fw = 0;
        if (um_registry_get_dword(key, "Framework", &fw) && fw) continue;
        if (!um_registry_get_sz(key, "DisplayName", r->name, sizeof(r->name)) || !r->name[0]) {
            const char *us = strchr(r->full, '_');     /* (its name: Name_Version_...) */
            int len = us ? (int)(us - r->full) : (int)strlen(r->full);
            if (len > (int)sizeof(r->name) - 1) len = (int)sizeof(r->name) - 1;
            memcpy(r->name, r->full, (size_t)len);
            r->name[len] = '\0';
        }
        um_registry_get_sz(key, "PublisherDisplayName", r->publisher, sizeof(r->publisher));
        um_registry_get_sz(key, "Version", r->version, sizeof(r->version));
        /* the tile logo: its first application's, else the package's */
        char logo[RAMFS_PATH_MAX] = "";
        ksnprintf(akey, sizeof(akey), "%s\\Applications", key);
        int m = um_registry_enum_key(akey, 0, w, 63, NULL);
        if (m >= 0) {
            char id[64];
            ascii(w, m, id, sizeof(id));
            ksnprintf(akey, sizeof(akey), "%s\\Applications\\%s", key, id);
            um_registry_get_sz(akey, "Logo", logo, sizeof(logo));
        }
        if (!logo[0]) um_registry_get_sz(key, "Logo", logo, sizeof(logo));
        strncpy(r->logo, no_drive(logo), sizeof(r->logo) - 1);
        g_n++;
    }
    if (g_sel >= g_n) g_sel = g_n - 1;
    if (g_ask >= g_n) g_ask = -1;
}

static GdiRect r_uninstall(int x, int y, int w, int i) { return RECT(x + w - 16 - 104, y + i * ROW_H + 14, 104, 32); }
static GdiRect r_cancel(int x, int y, int w, int i)    { return RECT(x + w - 16 - 104 - 10 - 88, y + i * ROW_H + 14, 88, 32); }

/* Run "appx remove" for row @i */
static void uninstall(int i)
{
    g_ask = -1;
    if (i < 0 || i >= g_n || g_job) return;
    RamNode *appx = RamfsResolve(NULL, "\\Windows\\System32\\appx.exe");
    char cmd[200], err[160];
    if (!appx) { ksnprintf(g_note, sizeof(g_note), "The package installer (appx.exe) is missing."); return; }
    ksnprintf(cmd, sizeof(cmd), "appx remove %s", g_rows[i].full);
    g_job = UmSpawnElevated(appx, cmd, appx->parent, NULL, err, sizeof(err));
    if (!g_job) {
        ksnprintf(g_note, sizeof(g_note), "%s could not be uninstalled: %s", g_rows[i].name, err);
        kprintf("[SETTINGS] Apps: %s could not be uninstalled: %s\n", g_rows[i].full, err);
        return;
    }
    strcpy(g_job_full, g_rows[i].full);
    strcpy(g_job_name, g_rows[i].name);
    ksnprintf(g_note, sizeof(g_note), "Uninstalling %s...", g_rows[i].name);
    kprintf("[SETTINGS] Apps: uninstalling %s\n", g_job_full);
}

int AppsPagePaint(int x, int y, int w)
{
    if (um_registry_generation() != g_gen) list();
    GdiTextT(x, y, "Apps installed from app packages (.msix, .appx) for this account.", UI_TEXT2);
    y += 32;
    for (int i = 0; i < g_n; i++) {
        PkgRow *r = &g_rows[i];
        int ry = y + i * ROW_H;
        GdiRoundRect(RECT(x, ry, w, ROW_H - 8), 8, i == g_sel ? UI_HOVER : UI_CARD, GDI_TRANSPARENT);
        RamNode *logo = r->logo[0] ? RamfsResolve(NULL, r->logo) : NULL;
        if (logo && !logo->dir) AppDrawNodeIcon(logo, x + 14, ry + 12, 32);
        else AppDrawGlyph(GL_WINDOWS, x + 18, ry + 16, 24, UI_TEXT2);
        GdiTextBold(x + 58, ry + 10, r->name, UI_TEXT);
        char sub[120];
        if (i == g_ask) ksnprintf(sub, sizeof(sub), "This app and its info will be uninstalled.");
        else ksnprintf(sub, sizeof(sub), "%s%s%s", r->publisher, r->publisher[0] && r->version[0] ? "  -  " : "",
                       r->version);
        GdiTextT(x + 58, ry + 32, sub, UI_TEXT2);
        if (g_job && !strcmp(g_job_full, r->full)) continue;    /* (being uninstalled: no buttons) */
        if (i == g_ask) UiButton(r_cancel(x, y, w, i), "Cancel", false);
        UiButton(r_uninstall(x, y, w, i), "Uninstall", i == g_ask);
    }
    int by = y + (g_n ? g_n * ROW_H : 0);
    if (!g_n) { GdiTextT(x, by + 8, "No apps are installed from packages.", UI_TEXT2); by += 40; }
    if (g_note[0]) { GdiTextT(x, by + 8, g_note, UI_TEXT2); by += 40; }
    return by;
}

bool AppsPageMouse(int px, int py, int pw, int mx, int my)
{
    int y = py + 32;
    for (int i = 0; i < g_n; i++) {
        if (g_job && !strcmp(g_job_full, g_rows[i].full)) continue;
        if (UiHit(r_uninstall(px, y, pw, i), mx, my)) {
            g_sel = i;
            if (g_ask == i) uninstall(i);
            else g_ask = i;
            return true;
        }
        if (g_ask == i && UiHit(r_cancel(px, y, pw, i), mx, my)) { g_ask = -1; return true; }
    }
    return false;
}

bool AppsPageKey(const KeyEvent *k)
{
    if (k->scancode == KEY_ESC && g_ask >= 0) { g_ask = -1; return true; }
    if (k->scancode == KEY_ENTER && !k->extended && g_ask >= 0) { uninstall(g_ask); return true; }
    if (!k->extended || !g_n) return false;
    if (k->scancode == KEY_DOWN) { g_sel = g_sel + 1 < g_n ? g_sel + 1 : g_n - 1; g_ask = -1; return true; }
    if (k->scancode == KEY_UP && g_sel > 0) { g_sel--; g_ask = -1; return true; }
    if (k->scancode == KEY_DELETE && g_sel >= 0) {
        g_ask = g_sel;
        kprintf("[SETTINGS] Apps: uninstall %s?\n", g_rows[g_sel].full);
        return true;
    }
    return false;
}

/* The uninstall finished, or the packages changed: repaint */
bool AppsPageTick(void)
{
    if (g_job) {
        UINT32 status = 0;
        if (!UmHasExited(g_job, &status, NULL, 0)) return false;
        UmRelease(g_job);
        g_job = NULL;
        if (status) ksnprintf(g_note, sizeof(g_note), "%s could not be uninstalled (appx: %u).", g_job_name, status);
        else ksnprintf(g_note, sizeof(g_note), "%s was uninstalled.", g_job_name);
        kprintf("[SETTINGS] Apps: %s %s (%u)\n", g_job_full, status ? "not uninstalled" : "uninstalled", status);
        return true;
    }
    return um_registry_generation() != g_gen;
}
