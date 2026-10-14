/*
 * eventvwr.exe — Event Viewer: the Windows logs (Application, Security,
 * System, and any other log a program made), read through the Windows
 * Event Log API as Windows' Event Viewer reads them
 *
 *   - EvtOpenChannelEnum lists the logs for the tree on the left;
 *   - EvtQuery with an XPath filter (Action > Filter Current Log: the
 *     levels to show) and EvtNext give the selected log's events, newest
 *     first; EvtRender with a system render context gives each one's
 *     level, date, source, event ID, and EvtFormatMessage its task
 *     category, for the list;
 *   - the pane below shows the selected event: General (EvtFormatMessage's
 *     message, or Windows' "The description for Event ID ... cannot be
 *     found" with the event's strings, and its fields) and Details (its
 *     XML from EvtRender, indented);
 *   - Action > Clear Log (EvtClearLog, after offering to save it) and
 *     Save All Events As (EvtExportLog for .evtx; the events' XML with
 *     their messages, EvtFormatMessageXml, for .xml).
 *
 *   eventvwr [/c:LOG]                    opens with LOG selected
 *   eventvwr --check SOURCE ID TEXT      (the OS test, core/167-eventlog.py)
 *
 * --check selects the Application log, finds SOURCE's event ID and
 * checks its message has TEXT, that the level filter hides and shows it,
 * that both save formats are written, shows it ("eventvwr: showing", the
 * test's screenshot), then clears the log and checks the System log says
 * so (event 104), logging "eventvwr: N passed, M failed".
 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ---- wevtapi ---- */
typedef struct { ULONGLONG v; DWORD count, type; } EVT_VARIANT;
__declspec(dllimport) HANDLE WINAPI EvtQuery(HANDLE session, LPCWSTR path, LPCWSTR query, DWORD flags);
__declspec(dllimport) BOOL WINAPI EvtNext(HANDLE rs, DWORD n, HANDLE *ev, DWORD timeout, DWORD flags, DWORD *got);
__declspec(dllimport) HANDLE WINAPI EvtCreateRenderContext(DWORD n, LPCWSTR *paths, DWORD flags);
__declspec(dllimport) BOOL WINAPI EvtRender(HANDLE ctx, HANDLE ev, DWORD flags, DWORD size, void *buf, DWORD *used, DWORD *count);
__declspec(dllimport) BOOL WINAPI EvtFormatMessage(HANDLE pub, HANDLE ev, DWORD id, DWORD n, const void *values, DWORD flags,
                                                   DWORD size, LPWSTR buf, DWORD *used);
__declspec(dllimport) BOOL WINAPI EvtClose(HANDLE h);
__declspec(dllimport) HANDLE WINAPI EvtOpenChannelEnum(HANDLE session, DWORD flags);
__declspec(dllimport) BOOL WINAPI EvtNextChannelPath(HANDLE e, DWORD size, LPWSTR buf, DWORD *used);
__declspec(dllimport) HANDLE WINAPI EvtOpenLog(HANDLE session, LPCWSTR path, DWORD flags);
__declspec(dllimport) BOOL WINAPI EvtGetLogInfo(HANDLE log, int id, DWORD size, void *buf, DWORD *used);
__declspec(dllimport) BOOL WINAPI EvtClearLog(HANDLE session, LPCWSTR channel, LPCWSTR target, DWORD flags);
__declspec(dllimport) BOOL WINAPI EvtExportLog(HANDLE session, LPCWSTR path, LPCWSTR query, LPCWSTR target, DWORD flags);

#define EvtQueryChannelPath      0x1
#define EvtQueryReverseDirection 0x200
#define EvtRenderEventValues     0
#define EvtRenderEventXml        1
#define EvtRenderContextSystem   1
#define EvtRenderContextUser     2
#define EvtFormatMessageEvent    1
#define EvtFormatMessageTask     3
#define EvtFormatMessageXml      9
#define EvtExportLogChannelPath  0x1
#define EvtExportLogOverwrite    0x2000
#define EvtLogNumberOfLogRecords 5
#define EvtSystemProviderName    0
#define EvtSystemEventID         2
#define EvtSystemLevel           4
#define EvtSystemTimeCreated     8
#define EvtSystemEventRecordId   9
#define EvtSystemComputer        15
#define EvtSystemUserID          16
#define ERROR_EVT_MESSAGE_NOT_FOUND 15027

/* ---- comdlg32 ---- */
typedef struct {
    DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance; LPCWSTR lpstrFilter; LPWSTR lpstrCustomFilter;
    DWORD nMaxCustFilter, nFilterIndex; LPWSTR lpstrFile; DWORD nMaxFile; LPWSTR lpstrFileTitle; DWORD nMaxFileTitle;
    LPCWSTR lpstrInitialDir, lpstrTitle; DWORD Flags; WORD nFileOffset, nFileExtension; LPCWSTR lpstrDefExt;
    LPARAM lCustData; void *lpfnHook; LPCWSTR lpTemplateName; void *pvReserved; DWORD dwReserved, FlagsEx;
} OFNW;
__declspec(dllimport) BOOL WINAPI GetSaveFileNameW(OFNW *o);
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_PATHMUSTEXIST   0x00000800

#define ID_TREE      100
#define ID_LIST      101
#define ID_TAB       102
#define ID_MSG       103
#define ID_FIELDS    104
#define ID_XML       105
#define ID_HEADER    106
#define IDM_EXIT     200
#define IDM_REFRESH  201
#define IDM_CLEAR    202
#define IDM_SAVE     203
#define IDM_LEVEL    210                 /* + level 1..5 */
#define IDM_NOFILTER 216
#define IDM_GENERAL  220
#define IDM_DETAILS  221
#define TIMER_CLEAR  1
#define TIMER_CLOSE  2
#define MAX_EVENTS   5000
#define MAX_LOGS     32

typedef struct {
    HANDLE    h;
    BYTE      level;
    ULONGLONG time;
    DWORD     id;
    ULONGLONG record;
    WCHAR     source[128];
    WCHAR     task[64];
} Row;

static HWND g_wnd, g_tree, g_list, g_header, g_tab, g_msg, g_fields, g_xml;
static HMENU g_action;
static HFONT g_mono;
static Row *g_rows;
static int g_nrows, g_sel = -1;
static ULONGLONG g_total;
static WCHAR g_log[64] = L"Application";
static WCHAR g_logs[MAX_LOGS][64];
static int g_nlogs;
static BOOL g_levels[6];                 /* the levels the filter shows (none: no filter) */
static HANDLE g_ctx;
static BOOL g_check;
static WCHAR g_want_source[128], g_want_text[256];
static DWORD g_want_id;
static int g_pass, g_fail;

static void say(const char *fmt, ...)
{
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

#define CHECK(what, cond) do { if (cond) g_pass++; else { g_fail++; say("FAIL: eventvwr: %s\n", what); } } while (0)

/* ---- the events ---- */

static const WCHAR *level_name(BYTE level)
{
    static const WCHAR *const names[] = { L"Information", L"Critical", L"Error", L"Warning", L"Information", L"Verbose" };
    return level < 6 ? names[level] : L"Information";
}

static void format_time(ULONGLONG t, WCHAR *out, int cap)
{
    FILETIME ft = { (DWORD)t, (DWORD)(t >> 32) }, lt;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &lt);
    FileTimeToSystemTime(&lt, &st);
    int h = st.wHour % 12 ? st.wHour % 12 : 12;
    swprintf(out, cap, L"%u/%u/%04u %d:%02u:%02u %ls", st.wMonth, st.wDay, st.wYear, h, st.wMinute, st.wSecond,
             st.wHour < 12 ? L"AM" : L"PM");
}

/* The XPath the level filter makes, as Windows' Filter Current Log writes it */
static void filter_xpath(WCHAR *out, int cap)
{
    WCHAR terms[200] = L"";
    int n = 0;
    for (int l = 1; l <= 5; l++) {
        if (!g_levels[l]) continue;
        WCHAR t[40];
        swprintf(t, 40, l == 4 ? L"%lsLevel=4 or Level=0" : L"%lsLevel=%d", n ? L" or " : L"", l);
        wcscat(terms, t);
        n++;
    }
    if (!n || n == 5) wcscpy(out, L"*");
    else swprintf(out, cap, L"*[System[(%ls)]]", terms);
}

static void free_rows(void)
{
    for (int i = 0; i < g_nrows; i++) EvtClose(g_rows[i].h);
    g_nrows = 0;
    g_sel = -1;
}

/* The selected log's events (as the filter has them), newest first */
static void load_events(void)
{
    free_rows();
    if (!g_rows) g_rows = calloc(MAX_EVENTS, sizeof(Row));
    if (!g_ctx) g_ctx = EvtCreateRenderContext(0, NULL, EvtRenderContextSystem);
    WCHAR xpath[300];
    filter_xpath(xpath, 300);
    HANDLE q = EvtQuery(NULL, g_log, xpath, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!q) say("eventvwr: EvtQuery(%ls, %ls) failed (error %lu)\n", g_log, xpath, GetLastError());
    static BYTE buf[8192];
    HANDLE got[64];
    DWORD n;
    while (q && g_rows && g_nrows < MAX_EVENTS && EvtNext(q, 64, got, INFINITE, 0, &n)) {
        for (DWORD i = 0; i < n; i++) {
            DWORD used, count;
            if (g_nrows >= MAX_EVENTS || !EvtRender(g_ctx, got[i], EvtRenderEventValues, sizeof(buf), buf, &used, &count)) {
                EvtClose(got[i]);
                continue;
            }
            EVT_VARIANT *v = (EVT_VARIANT *)buf;
            Row *r = &g_rows[g_nrows++];
            memset(r, 0, sizeof(*r));
            r->h = got[i];
            r->level = (BYTE)v[EvtSystemLevel].v;
            r->id = (DWORD)(v[EvtSystemEventID].v & 0xFFFF);
            r->time = v[EvtSystemTimeCreated].v;
            r->record = v[EvtSystemEventRecordId].v;
            if (v[EvtSystemProviderName].type == 1 && v[EvtSystemProviderName].v)
                wcsncpy(r->source, (const WCHAR *)(ULONG_PTR)v[EvtSystemProviderName].v, 127);
            if (!EvtFormatMessage(NULL, got[i], 0, 0, NULL, EvtFormatMessageTask, 64, r->task, &used)) wcscpy(r->task, L"None");
        }
    }
    if (q) EvtClose(q);
    g_total = 0;
    HANDLE lg = EvtOpenLog(NULL, g_log, EvtQueryChannelPath);
    if (lg) {
        EVT_VARIANT v;
        DWORD used;
        if (EvtGetLogInfo(lg, EvtLogNumberOfLogRecords, sizeof(v), &v, &used)) g_total = v.v;
        EvtClose(lg);
    }
}

/* The source as Event Viewer shows it ("Kernel-General", not
 * "Microsoft-Windows-Kernel-General") */
static const WCHAR *short_source(const WCHAR *s)
{
    return !wcsncmp(s, L"Microsoft-Windows-", 18) ? s + 18 : s;
}

static void fill_list(void)
{
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LVM_DELETEALLITEMS, 0, 0);
    for (int i = 0; i < g_nrows; i++) {
        Row *r = &g_rows[i];
        WCHAR when[64], id[16];
        format_time(r->time, when, 64);
        swprintf(id, 16, L"%lu", (unsigned long)r->id);
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        it.iItem = i;
        it.pszText = (LPWSTR)level_name(r->level);
        it.iImage = r->level == 1 || r->level == 2 ? 2 : r->level == 3 ? 1 : 0;
        it.lParam = i;
        SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        const WCHAR *cols[4] = { when, short_source(r->source), id, r->task };
        for (int c = 0; c < 4; c++) {
            it.mask = LVIF_TEXT;
            it.iSubItem = c + 1;
            it.pszText = (LPWSTR)cols[c];
            SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&it);
        }
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
    WCHAR head[300];
    BOOL filtered = FALSE;
    for (int l = 1; l <= 5; l++) filtered |= g_levels[l];
    if (filtered)
        swprintf(head, 300, L"  %ls    Number of events: %llu    Filtered: showing %d of %llu events", g_log,
                 (unsigned long long)g_total, g_nrows, (unsigned long long)g_total);
    else
        swprintf(head, 300, L"  %ls    Number of events: %llu", g_log, (unsigned long long)g_total);
    SetWindowTextW(g_header, head);
}

/* "<a><b>x</b></a>" with each element on its own line, indented */
static WCHAR *indent_xml(const WCHAR *x)
{
    size_t cap = wcslen(x) * 3 + 256, o = 0;
    WCHAR *out = malloc(cap * sizeof(WCHAR));
    if (!out) return NULL;
    int depth = 0;
    for (const WCHAR *p = x; *p && o + 128 < cap;) {
        const WCHAR *end = wcschr(p, '>');
        if (*p != '<' || !end) { out[o++] = *p++; continue; }
        BOOL close = p[1] == '/', empty = end[-1] == '/';
        if (close) depth--;
        if (o) { out[o++] = '\r'; out[o++] = '\n'; }
        for (int i = 0; i < depth * 2 && o + 2 < cap; i++) out[o++] = ' ';
        /* <x>text</x> stays on one line */
        const WCHAR *after = end + 1, *lt = wcschr(after, '<');
        size_t n = (size_t)(end - p + 1);
        if (!close && !empty && lt && lt[1] == '/') {
            const WCHAR *cend = wcschr(lt, '>');
            if (cend) n = (size_t)(cend - p + 1);
        } else if (!close && !empty) depth++;
        if (o + n + 4 >= cap) break;
        memcpy(out + o, p, n * sizeof(WCHAR));
        o += n;
        p += n;
    }
    out[o] = 0;
    return out;
}

/* The selected event's message: its source's, or what Windows says when
 * the source has none, with the event's strings */
static WCHAR *message_of(HANDLE ev, const Row *r)
{
    DWORD used = 0;
    if (!EvtFormatMessage(NULL, ev, 0, 0, NULL, EvtFormatMessageEvent, 0, NULL, &used) && used) {
        WCHAR *m = malloc((used + 1) * sizeof(WCHAR));
        if (m && EvtFormatMessage(NULL, ev, 0, 0, NULL, EvtFormatMessageEvent, used + 1, m, &used)) return m;
        free(m);
    }
    size_t cap = 4096;
    WCHAR *m = malloc(cap * sizeof(WCHAR));
    if (!m) return NULL;
    swprintf(m, cap, L"The description for Event ID %lu from source %ls cannot be found. Either the component that raises "
             L"this event is not installed on your local computer or the installation is corrupted. You can install or "
             L"repair the component on the local computer.\r\n\r\nIf the event originated on another computer, the display "
             L"information had to be saved with the event.", (unsigned long)r->id, r->source);
    HANDLE user = EvtCreateRenderContext(0, NULL, EvtRenderContextUser);
    static BYTE buf[16384];
    DWORD count = 0;
    if (user && EvtRender(user, ev, EvtRenderEventValues, sizeof(buf), buf, &used, &count) && count) {
        wcscat(m, L"\r\n\r\nThe following information was included with the event: \r\n");
        EVT_VARIANT *v = (EVT_VARIANT *)buf;
        for (DWORD i = 0; i < count; i++)
            if (v[i].type == 1 && v[i].v && wcslen(m) + wcslen((const WCHAR *)(ULONG_PTR)v[i].v) + 4 < cap) {
                wcscat(m, L"\r\n");
                wcscat(m, (const WCHAR *)(ULONG_PTR)v[i].v);
            }
    }
    if (user) EvtClose(user);
    return m;
}

static void show_event(int i)
{
    g_sel = i;
    if (i < 0 || i >= g_nrows) {
        SetWindowTextW(g_msg, L"");
        SetWindowTextW(g_fields, L"");
        SetWindowTextW(g_xml, L"");
        return;
    }
    Row *r = &g_rows[i];
    WCHAR *m = message_of(r->h, r);
    SetWindowTextW(g_msg, m ? m : L"");
    free(m);
    static BYTE buf[8192];
    DWORD used, count;
    WCHAR computer[64] = L"", user[80] = L"N/A", when[64], fields[1024];
    if (EvtRender(g_ctx, r->h, EvtRenderEventValues, sizeof(buf), buf, &used, &count)) {
        EVT_VARIANT *v = (EVT_VARIANT *)buf;
        if (v[EvtSystemComputer].type == 1 && v[EvtSystemComputer].v)
            wcsncpy(computer, (const WCHAR *)(ULONG_PTR)v[EvtSystemComputer].v, 63);
        if (v[EvtSystemUserID].type == 19 && v[EvtSystemUserID].v) {
            WCHAR name[64], dom[64];
            DWORD nl = 64, dl = 64;
            SID_NAME_USE use;
            if (LookupAccountSidW(NULL, (PSID)(ULONG_PTR)v[EvtSystemUserID].v, name, &nl, dom, &dl, &use))
                swprintf(user, 80, L"%ls\\%ls", dom, name);
        }
    }
    format_time(r->time, when, 64);
    swprintf(fields, 1024,
             L"Log Name:\t%ls\r\nSource:\t%ls\r\nLogged:\t%ls\r\nEvent ID:\t%lu\r\nTask Category:\t%ls\r\nLevel:\t%ls\r\n"
             L"Keywords:\tClassic\r\nUser:\t%ls\r\nComputer:\t%ls\r\nRecord:\t%llu",
             g_log, short_source(r->source), when, (unsigned long)r->id, r->task, level_name(r->level), user, computer,
             (unsigned long long)r->record);
    SetWindowTextW(g_fields, fields);
    used = 0;
    EvtRender(NULL, r->h, EvtRenderEventXml, 0, NULL, &used, &count);
    WCHAR *x = used ? malloc(used + 2) : NULL;
    if (x && EvtRender(NULL, r->h, EvtRenderEventXml, used, x, &used, &count)) {
        WCHAR *pretty = indent_xml(x);
        SetWindowTextW(g_xml, pretty ? pretty : x);
        free(pretty);
    } else SetWindowTextW(g_xml, L"");
    free(x);
}

static void select_row(int i)
{
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    it.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(g_list, LVM_SETITEMSTATE, i, (LPARAM)&it);
    SendMessageW(g_list, LVM_ENSUREVISIBLE, i, FALSE);
    show_event(i);
}

static void refresh(void)
{
    load_events();
    fill_list();
    if (g_nrows) select_row(0);
    else show_event(-1);
}

/* ---- the logs ---- */

static void add_logs(void)
{
    TreeView_DeleteAllItems(g_tree);
    g_nlogs = 0;
    HANDLE e = EvtOpenChannelEnum(NULL, 0);
    WCHAR name[64];
    DWORD used;
    while (e && g_nlogs < MAX_LOGS && EvtNextChannelPath(e, 64, name, &used)) wcscpy(g_logs[g_nlogs++], name);
    if (e) EvtClose(e);
    TVINSERTSTRUCTW ins;
    memset(&ins, 0, sizeof(ins));
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM;
    ins.item.pszText = L"Event Viewer (Local)";
    ins.item.lParam = -1;
    HTREEITEM root = (HTREEITEM)SendMessageW(g_tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
    ins.hParent = root;
    ins.item.pszText = L"Windows Logs";
    HTREEITEM win = (HTREEITEM)SendMessageW(g_tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
    ins.item.pszText = L"Applications and Services Logs";
    HTREEITEM apps = (HTREEITEM)SendMessageW(g_tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
    static const WCHAR *const windows[] = { L"Application", L"Security", L"System" };
    HTREEITEM sel = NULL;
    for (int i = 0; i < g_nlogs; i++) {
        BOOL is_win = FALSE;
        for (int k = 0; k < 3; k++) is_win |= !_wcsicmp(g_logs[i], windows[k]);
        ins.hParent = is_win ? win : apps;
        ins.hInsertAfter = is_win ? TVI_SORT : TVI_LAST;
        ins.item.pszText = g_logs[i];
        ins.item.lParam = i;
        HTREEITEM it = (HTREEITEM)SendMessageW(g_tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
        if (!_wcsicmp(g_logs[i], g_log)) sel = it;
    }
    TreeView_Expand(g_tree, root, TVE_EXPAND);
    TreeView_Expand(g_tree, win, TVE_EXPAND);
    TreeView_Expand(g_tree, apps, TVE_EXPAND);
    if (sel) TreeView_SelectItem(g_tree, sel);
}

static void select_log(const WCHAR *log)
{
    wcsncpy(g_log, log, 63);
    say("eventvwr: log %ls\n", g_log);
    refresh();
}

/* ---- saving and clearing ---- */

static BOOL write_utf8(HANDLE f, const WCHAR *s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char *b = n > 0 ? malloc((size_t)n) : NULL;
    DWORD w = 0;
    BOOL ok = b && WideCharToMultiByte(CP_UTF8, 0, s, -1, b, n, NULL, NULL) && WriteFile(f, b, (DWORD)n - 1, &w, NULL);
    free(b);
    return ok;
}

/* The log's events as the filter shows them: .evtx (EvtExportLog) or
 * .xml (each event's XML with its message, in an <Events> list) */
static BOOL save_events(const WCHAR *path)
{
    WCHAR xpath[300];
    filter_xpath(xpath, 300);
    size_t n = wcslen(path);
    if (n > 4 && !_wcsicmp(path + n - 4, L".xml")) {
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) return FALSE;
        BOOL ok = write_utf8(f, L"<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\r\n<Events>");
        HANDLE q = EvtQuery(NULL, g_log, xpath, EvtQueryChannelPath);
        HANDLE got[32];
        DWORD k;
        while (ok && q && EvtNext(q, 32, got, INFINITE, 0, &k))
            for (DWORD i = 0; i < k; i++) {
                DWORD used = 0;
                EvtFormatMessage(NULL, got[i], 0, 0, NULL, EvtFormatMessageXml, 0, NULL, &used);
                WCHAR *x = used ? malloc((used + 1) * sizeof(WCHAR)) : NULL;
                if (x && EvtFormatMessage(NULL, got[i], 0, 0, NULL, EvtFormatMessageXml, used + 1, x, &used))
                    ok = write_utf8(f, x) && write_utf8(f, L"\r\n");
                free(x);
                EvtClose(got[i]);
            }
        if (q) EvtClose(q);
        ok = ok && write_utf8(f, L"</Events>\r\n");
        CloseHandle(f);
        return ok;
    }
    return EvtExportLog(NULL, g_log, xpath, path, EvtExportLogChannelPath | EvtExportLogOverwrite);
}

static BOOL ask_path(WCHAR *path, const WCHAR *title)
{
    swprintf(path, MAX_PATH, L"%ls.evtx", g_log);
    OFNW o;
    memset(&o, 0, sizeof(o));
    o.lStructSize = sizeof(o);
    o.hwndOwner = g_wnd;
    o.lpstrFilter = L"Event Files (*.evtx)\0*.evtx\0XML (*.xml)\0*.xml\0";
    o.nFilterIndex = 1;
    o.lpstrFile = path;
    o.nMaxFile = MAX_PATH;
    o.lpstrTitle = title;
    o.lpstrDefExt = L"evtx";
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return GetSaveFileNameW(&o);
}

static void save_as(void)
{
    WCHAR path[MAX_PATH];
    if (!ask_path(path, L"Save As")) return;
    if (!save_events(path)) {
        WCHAR m[400];
        swprintf(m, 400, L"The events could not be saved to %ls (error %lu).", path, GetLastError());
        MessageBoxW(g_wnd, m, L"Event Viewer", MB_OK | MB_ICONERROR);
    }
}

static void clear_log(void)
{
    WCHAR q[200], path[MAX_PATH];
    swprintf(q, 200, L"Do you want to save \"%ls\" before clearing it?", g_log);
    int r = MessageBoxW(g_wnd, q, L"Event Viewer", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return;
    BOOL ok;
    if (r == IDYES) {
        if (!ask_path(path, L"Save and Clear")) return;
        ok = EvtClearLog(NULL, g_log, path, 0);
    } else ok = EvtClearLog(NULL, g_log, NULL, 0);
    if (!ok) {
        WCHAR m[200];
        swprintf(m, 200, L"The %ls log could not be cleared (error %lu).", g_log, GetLastError());
        MessageBoxW(g_wnd, m, L"Event Viewer", MB_OK | MB_ICONERROR);
    }
    refresh();
}

/* ---- the check (the OS test) ---- */

static int find_wanted(void)
{
    for (int i = 0; i < g_nrows; i++)
        if (!_wcsicmp(g_rows[i].source, g_want_source) && g_rows[i].id == g_want_id) return i;
    return -1;
}

static void set_levels(int only)
{
    for (int l = 0; l < 6; l++) g_levels[l] = only && l == only;
    for (int l = 1; l <= 5; l++) CheckMenuItem(g_action, IDM_LEVEL + l, g_levels[l] ? MF_CHECKED : MF_UNCHECKED);
}

static void check(void)
{
    select_log(L"Application");
    CHECK("the Application log has events", g_nrows > 0);
    int i = find_wanted();
    say("eventvwr: %d events in Application, %ls's event %lu %s\n", g_nrows, g_want_source, (unsigned long)g_want_id,
        i >= 0 ? "found" : "not found");
    CHECK("the test's event is listed", i >= 0);
    if (i < 0) return;
    BYTE level = g_rows[i].level;
    select_row(i);
    WCHAR msg[4096] = L"", xml[8192] = L"";
    GetWindowTextW(g_msg, msg, 4096);
    GetWindowTextW(g_xml, xml, 8192);
    CHECK("the details pane has the event's text", wcsstr(msg, g_want_text) != NULL);
    CHECK("the XML view has the event's text", wcsstr(xml, g_want_text) != NULL);
    CHECK("the XML view names the provider", wcsstr(xml, L"<Provider Name=") != NULL);
    say("eventvwr: details: %ls\n", msg);

    /* the level filter: the event's own level shows it, another hides it */
    set_levels(level == 2 ? 3 : 2);
    refresh();
    BOOL others = TRUE;
    for (int k = 0; k < g_nrows; k++) others &= g_rows[k].level == (level == 2 ? 3 : 2);
    CHECK("filtering on another level hides the event", find_wanted() < 0 && others);
    say("eventvwr: filter %ls: %d events\n", level_name(level == 2 ? 3 : 2), g_nrows);
    set_levels(level ? level : 4);
    refresh();
    CHECK("filtering on its level shows the event", find_wanted() >= 0);
    set_levels(0);
    refresh();

    /* saving: .evtx and .xml */
    WCHAR tmp[MAX_PATH], evtx[MAX_PATH], xmlp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf(evtx, MAX_PATH, L"%lseventvwr-check.evtx", tmp);
    swprintf(xmlp, MAX_PATH, L"%lseventvwr-check.xml", tmp);
    CHECK("Save All Events As .evtx", save_events(evtx));
    CHECK("Save All Events As .xml", save_events(xmlp));
    WIN32_FILE_ATTRIBUTE_DATA fa;
    BOOL ok = GetFileAttributesExW(evtx, GetFileExInfoStandard, &fa) && fa.nFileSizeLow >= 4096 + 65536;
    char head[8] = { 0 };
    HANDLE f = CreateFileW(evtx, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD got = 0;
    if (f != INVALID_HANDLE_VALUE) { ReadFile(f, head, 8, &got, NULL); CloseHandle(f); }
    CHECK("the .evtx file has a header and a chunk", ok && !memcmp(head, "ElfFile", 8));
    say("eventvwr: saved %ls (%lu bytes)\n", evtx, ok ? (unsigned long)fa.nFileSizeLow : 0ul);
    static char xb[1 << 16];
    f = CreateFileW(xmlp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    got = 0;
    if (f != INVALID_HANDLE_VALUE) { ReadFile(f, xb, sizeof(xb) - 1, &got, NULL); CloseHandle(f); }
    xb[got] = 0;
    CHECK("the .xml file lists the events with their messages", strstr(xb, "<Events>") && strstr(xb, "<RenderingInfo"));
    say("eventvwr: saved %ls (%lu bytes)\n", xmlp, (unsigned long)got);

    select_row(find_wanted());
}

/* After the screenshot: clear the log and look for event 104 in System */
static void check_clear(void)
{
    WCHAR tmp[MAX_PATH], backup[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf(backup, MAX_PATH, L"%lseventvwr-cleared.evtx", tmp);
    DeleteFileW(backup);
    BOOL ok = EvtClearLog(NULL, L"Application", backup, 0);
    CHECK("EvtClearLog clears the Application log (saving it first)", ok);
    refresh();
    say("eventvwr: cleared Application: %d events, %llu in the log\n", g_nrows, (unsigned long long)g_total);
    CHECK("the cleared log is empty", g_nrows == 0 && g_total == 0);
    CHECK("the backup was written", GetFileAttributesW(backup) != INVALID_FILE_ATTRIBUTES);
    HANDLE q = EvtQuery(NULL, L"System", L"*[System[Provider[@Name='Microsoft-Windows-Eventlog'] and EventID=104]]",
                        EvtQueryChannelPath | EvtQueryReverseDirection);
    HANDLE ev;
    DWORD n = 0;
    BOOL found = FALSE;
    if (q && EvtNext(q, 1, &ev, INFINITE, 0, &n) && n) {
        WCHAR m[256];
        DWORD used;
        found = EvtFormatMessage(NULL, ev, 0, 0, NULL, EvtFormatMessageEvent, 256, m, &used) &&
                wcsstr(m, L"The Application log file was cleared.") != NULL;
        say("eventvwr: System: %ls\n", found ? m : L"(event 104 without its message)");
        EvtClose(ev);
    }
    if (q) EvtClose(q);
    CHECK("the System log has event 104 for the clearing", found);
    say("eventvwr: %d passed, %d failed\n", g_pass, g_fail);
}

/* ---- the window ---- */

static void show_tab(int t)
{
    TabCtrl_SetCurSel(g_tab, t);
    ShowWindow(g_msg, t == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_fields, t == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_xml, t == 1 ? SW_SHOW : SW_HIDE);
}

static void layout(void)
{
    RECT r;
    GetClientRect(g_wnd, &r);
    int tw = 230, x = tw + 4, w = r.right - x, hh = 24;
    int lh = (r.bottom - hh) * 45 / 100, ty = hh + lh + 4, th = r.bottom - ty;
    MoveWindow(g_tree, 0, 0, tw, r.bottom, TRUE);
    MoveWindow(g_header, x, 0, w, hh, TRUE);
    MoveWindow(g_list, x, hh, w, lh, TRUE);
    MoveWindow(g_tab, x, ty, w, th, TRUE);
    RECT in = { 0, 0, w, th };
    TabCtrl_AdjustRect(g_tab, FALSE, &in);
    int ix = x + in.left, iy = ty + in.top, iw = in.right - in.left, ih = in.bottom - in.top;
    int mh = ih * 45 / 100;
    MoveWindow(g_msg, ix, iy, iw, mh, TRUE);
    MoveWindow(g_fields, ix, iy + mh + 4, iw, ih - mh - 4, TRUE);
    MoveWindow(g_xml, ix, iy, iw, ih, TRUE);
    int cw[5] = { 110, 170, 220, 80, 0 };
    cw[4] = w - cw[0] - cw[1] - cw[2] - cw[3] - 24;
    if (cw[4] < 100) cw[4] = 100;
    for (int c = 0; c < 5; c++) SendMessageW(g_list, LVM_SETCOLUMNWIDTH, c, cw[c]);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (g_tree) layout();
        return 0;
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        if (n->idFrom == ID_TREE && n->code == TVN_SELCHANGEDW) {
            NMTREEVIEWW *t = (NMTREEVIEWW *)lp;
            int i = (int)t->itemNew.lParam;
            if (i >= 0 && i < g_nlogs && _wcsicmp(g_logs[i], g_log)) select_log(g_logs[i]);
        } else if (n->idFrom == ID_LIST && n->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *l = (NMLISTVIEW *)lp;
            if ((l->uNewState & LVIS_SELECTED) && l->iItem != g_sel) show_event(l->iItem);
        } else if (n->idFrom == ID_TAB && n->code == TCN_SELCHANGE) {
            show_tab(TabCtrl_GetCurSel(g_tab));
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id > IDM_LEVEL && id <= IDM_LEVEL + 5) {
            g_levels[id - IDM_LEVEL] = !g_levels[id - IDM_LEVEL];
            CheckMenuItem(g_action, id, g_levels[id - IDM_LEVEL] ? MF_CHECKED : MF_UNCHECKED);
            refresh();
            return 0;
        }
        switch (id) {
        case IDM_EXIT: DestroyWindow(h); break;
        case IDM_REFRESH: refresh(); break;
        case IDM_NOFILTER: set_levels(0); refresh(); break;
        case IDM_CLEAR: clear_log(); break;
        case IDM_SAVE: save_as(); break;
        case IDM_GENERAL: show_tab(0); break;
        case IDM_DETAILS: show_tab(1); break;
        }
        return 0;
    }
    case WM_TIMER:
        KillTimer(h, wp);
        if (wp == TIMER_CLEAR) { check_clear(); SetTimer(h, TIMER_CLOSE, 3000, NULL); }
        else if (wp == TIMER_CLOSE) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        say("eventvwr: closed\n");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HMENU make_menu(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), filter = CreatePopupMenu(), view = CreatePopupMenu();
    g_action = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"&Save All Events As...");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    static const WCHAR *const levels[] = { NULL, L"&Critical", L"&Error", L"&Warning", L"&Information", L"&Verbose" };
    for (int l = 1; l <= 5; l++) AppendMenuW(filter, MF_STRING, IDM_LEVEL + l, levels[l]);
    AppendMenuW(filter, MF_SEPARATOR, 0, NULL);
    AppendMenuW(filter, MF_STRING, IDM_NOFILTER, L"&Clear Filter");
    AppendMenuW(g_action, MF_STRING, IDM_REFRESH, L"&Refresh\tF5");
    AppendMenuW(g_action, MF_POPUP, (UINT_PTR)filter, L"&Filter Current Log");
    AppendMenuW(g_action, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_action, MF_STRING, IDM_CLEAR, L"C&lear Log...");
    AppendMenuW(g_action, MF_STRING, IDM_SAVE, L"&Save All Events As...");
    AppendMenuW(view, MF_STRING, IDM_GENERAL, L"&General");
    AppendMenuW(view, MF_STRING, IDM_DETAILS, L"&Details (XML)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)g_action, L"&Action");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    return bar;
}

static HWND edit(HINSTANCE inst, int id)
{
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                             ES_AUTOVSCROLL, 0, 0, 10, 10, g_wnd, (HMENU)(INT_PTR)id, inst, NULL);
    SendMessageW(e, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);
    return e;
}

int main(int argc, char **argv)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check") && i + 3 < argc) {
            g_check = TRUE;
            MultiByteToWideChar(CP_ACP, 0, argv[i + 1], -1, g_want_source, 128);
            g_want_id = (DWORD)strtoul(argv[i + 2], NULL, 10);
            MultiByteToWideChar(CP_ACP, 0, argv[i + 3], -1, g_want_text, 256);
            i += 3;
        } else if (!_strnicmp(argv[i], "/c:", 3) || !_strnicmp(argv[i], "-c:", 3)) {
            MultiByteToWideChar(CP_ACP, 0, argv[i] + 3, -1, g_log, 64);
        }
    }
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NovaEventViewer";
    RegisterClassExW(&wc);
    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"Event Viewer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700,
                            NULL, make_menu(), inst, NULL);
    if (!g_wnd) { say("eventvwr: no window\n"); return 1; }
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES |
                             TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 10, 10, g_wnd, (HMENU)ID_TREE, inst, NULL);
    g_header = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 10, 10, g_wnd,
                               (HMENU)ID_HEADER, inst, NULL);
    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT |
                             LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, 0, 0, 10, 10, g_wnd, (HMENU)ID_LIST, inst, NULL);
    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 10, 10, g_wnd,
                            (HMENU)ID_TAB, inst, NULL);
    g_msg = edit(inst, ID_MSG);
    g_fields = edit(inst, ID_FIELDS);
    int stop = 64;                                  /* (one column for the field names) */
    SendMessageW(g_fields, EM_SETTABSTOPS, 1, (LPARAM)&stop);
    g_xml = edit(inst, ID_XML);
    SendMessageW(g_tree, WM_SETFONT, (WPARAM)font, 0);
    SendMessageW(g_header, WM_SETFONT, (WPARAM)font, 0);
    SendMessageW(g_list, WM_SETFONT, (WPARAM)font, 0);
    SendMessageW(g_tab, WM_SETFONT, (WPARAM)font, 0);
    g_mono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, FIXED_PITCH, L"Consolas");
    if (g_mono) SendMessageW(g_xml, WM_SETFONT, (WPARAM)g_mono, 0);
    SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
    /* the level icons: information, warning, error */
    HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 3, 0);
    if (il) {
        ImageList_AddIcon(il, LoadIconW(NULL, (LPCWSTR)IDI_INFORMATION));
        ImageList_AddIcon(il, LoadIconW(NULL, (LPCWSTR)IDI_WARNING));
        ImageList_AddIcon(il, LoadIconW(NULL, (LPCWSTR)IDI_ERROR));
        SendMessageW(g_list, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)il);
    }
    static const WCHAR *const cols[] = { L"Level", L"Date and Time", L"Source", L"Event ID", L"Task Category" };
    LVCOLUMNW col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    for (int c = 0; c < 5; c++) {
        col.cx = 100;
        col.pszText = (LPWSTR)cols[c];
        SendMessageW(g_list, LVM_INSERTCOLUMNW, c, (LPARAM)&col);
    }
    TCITEMW tab;
    memset(&tab, 0, sizeof(tab));
    tab.mask = TCIF_TEXT;
    tab.pszText = L"General";
    SendMessageW(g_tab, TCM_INSERTITEMW, 0, (LPARAM)&tab);
    tab.pszText = L"Details";
    SendMessageW(g_tab, TCM_INSERTITEMW, 1, (LPARAM)&tab);
    show_tab(0);
    layout();
    if (g_check) wcscpy(g_log, L"Application");
    add_logs();
    refresh();
    say("eventvwr: showing %d events in %ls\n", g_nrows, g_log);
    ShowWindow(g_wnd, SW_SHOWNORMAL);
    UpdateWindow(g_wnd);
    if (g_check) {
        check();
        UpdateWindow(g_wnd);
        say("eventvwr: showing %ls event %lu\n", g_want_source, (unsigned long)g_want_id);
        SetTimer(g_wnd, TIMER_CLEAR, 6000, NULL);
    }
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_F5) { refresh(); continue; }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return g_check && g_fail ? 1 : 0;
}
