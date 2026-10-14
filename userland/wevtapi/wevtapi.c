/*
 * wevtapi.dll — the Windows Event Log API (EvtQuery, EvtNext, EvtRender,
 * EvtFormatMessage, EvtOpenLog, EvtClearLog, EvtExportLog, EvtSubscribe...)
 *
 * The logs are the EventLog service's (eventlog.exe, novaevt.h): this
 * reads them through advapi32's classic API (OpenEventLog, ReadEventLog),
 * as an event log reader on Windows sees classic events.  A query loads
 * the channel's events (Application, System, Security or any log with a
 * key under HKLM\SYSTEM\CurrentControlSet\Services\EventLog; another
 * channel name has no events) or a saved .evt file, keeps the ones its
 * XPath filter selects (the subset Event Viewer and programs use:
 * Level, EventID, EventRecordID, Task, Keywords and band(), Provider
 * [@Name=...], TimeCreated[timediff(@SystemTime) <= ms], with and, or
 * and brackets; a <QueryList> works too) and hands them out with EvtNext.
 * An event renders as Windows renders a classic one (EvtRenderEventXml,
 * the system values, the user values: its strings).  EvtFormatMessage
 * gives its message from the source's EventMessageFile, or NovaOS's own
 * sources' texts (novaevt.h).  EvtExportLog writes .evtx (evtx.c).  A
 * subscription checks for new events twice a second.
 */
#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "wevtapi.h"

/* advapi32 */
__declspec(dllimport) BOOL WINAPI ConvertSidToStringSidW(PSID sid, LPWSTR *out);
__declspec(dllimport) HANDLE WINAPI OpenEventLogW(LPCWSTR server, LPCWSTR source);
__declspec(dllimport) HANDLE WINAPI OpenBackupEventLogW(LPCWSTR server, LPCWSTR file);
__declspec(dllimport) BOOL WINAPI ReadEventLogW(HANDLE h, DWORD flags, DWORD offset, LPVOID buf, DWORD n, DWORD *read, DWORD *need);
__declspec(dllimport) BOOL WINAPI CloseEventLog(HANDLE h);
__declspec(dllimport) BOOL WINAPI ClearEventLogW(HANDLE h, LPCWSTR backup);
__declspec(dllimport) BOOL WINAPI GetNumberOfEventLogRecords(HANDLE h, PDWORD n);
__declspec(dllimport) BOOL WINAPI GetOldestEventLogRecord(HANDLE h, PDWORD n);

#define EvtQueryChannelPath         0x1
#define EvtQueryFilePath            0x2
#define EvtQueryReverseDirection    0x200
#define EvtQueryTolerateQueryErrors 0x1000

enum { H_QUERY = 0x51455645, H_RENDER = 0x52455645, H_BOOKMARK = 0x42455645, H_LOG = 0x4C455645,
       H_ENUM = 0x4E455645, H_CONFIG = 0x43455645, H_EVENT = 0x56455645, H_PUBLISHER = 0x50455645,
       H_SUB = 0x53555645 };

typedef struct { DWORD magic; } EvtObj;

/* ---- small helpers ---- */

void *evt_alloc(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
void evt_free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static void wcpy(WCHAR *d, size_t cap, const WCHAR *s)
{
    size_t i = 0;
    for (; s && s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}

static const WCHAR *wfind(const WCHAR *h, const WCHAR *n)
{
    int k = lstrlenW(n);
    for (; h && *h; h++) if (!wcsncmp(h, n, (size_t)k)) return h;
    return NULL;
}

static BOOL wsame(const WCHAR *a, const WCHAR *b) { return !lstrcmpiW(a ? a : L"", b ? b : L""); }

static EvtObj *obj_of(HANDLE h, DWORD magic)
{
    EvtObj *o = h;
    if (!o || o->magic != magic) { SetLastError(ERROR_INVALID_HANDLE); return NULL; }
    return o;
}

static HANDLE make(DWORD magic, size_t size)
{
    EvtObj *o = evt_alloc(size);
    if (!o) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    o->magic = magic;
    return o;
}

/* A growing UTF-16 string */
void sb_put(Sb *b, const WCHAR *s, int n)
{
    if (n < 0) n = s ? lstrlenW(s) : 0;
    if (b->len + n + 1 > b->cap) {
        int cap = (b->cap ? b->cap * 2 : 256) + n;
        WCHAR *p = evt_alloc(sizeof(WCHAR) * (size_t)cap);
        if (!p) { b->oom = TRUE; return; }
        if (b->p) memcpy(p, b->p, sizeof(WCHAR) * (size_t)b->len);
        evt_free(b->p);
        b->p = p;
        b->cap = cap;
    }
    if (n) memcpy(b->p + b->len, s, sizeof(WCHAR) * (size_t)n);
    b->len += n;
    b->p[b->len] = 0;
}

void sb_esc(Sb *b, const WCHAR *s)
{
    for (; s && *s; s++) {
        if (*s == '<') sb_put(b, L"&lt;", 4);
        else if (*s == '>') sb_put(b, L"&gt;", 4);
        else if (*s == '&') sb_put(b, L"&amp;", 5);
        else if (*s == '\'') sb_put(b, L"&apos;", 6);
        else if (*s == '"') sb_put(b, L"&quot;", 6);
        else sb_put(b, s, 1);
    }
}

void sb_num(Sb *b, ULONGLONG v, int hex, int width)
{
    WCHAR t[24];
    int i = 23;
    t[i] = 0;
    do { int d = (int)(v % (hex ? 16 : 10)); t[--i] = (WCHAR)(d < 10 ? '0' + d : 'A' + d - 10); v /= hex ? 16 : 10; } while (v);
    while (23 - i < width) t[--i] = '0';
    sb_put(b, t + i, 23 - i);
}

/* ---- events ---- */

static Event *event_of(HANDLE h) { return (Event *)obj_of(h, H_EVENT); }

static Event *copy_event(const WCHAR *channel, const EVENTLOGRECORD *r)
{
    Event *e = evt_alloc(sizeof(Event) - sizeof(EVENTLOGRECORD) + r->Length);
    if (!e) return NULL;
    e->magic = H_EVENT;
    wcpy(e->channel, NOVA_EVT_LOG_CHARS, channel);
    memcpy(&e->rec, r, r->Length);
    return e;
}

static const WCHAR *ev_source(const Event *e) { return nova_evt_source(&e->rec); }
static const WCHAR *ev_string(const Event *e, int i)
{
    if (i >= e->rec.NumStrings) return NULL;
    const WCHAR *s = (const WCHAR *)((const BYTE *)&e->rec + e->rec.StringOffset);
    while (i--) s += lstrlenW(s) + 1;
    return s;
}
static ULONGLONG ev_filetime(DWORD unix) { return (ULONGLONG)unix * 10000000ULL + 116444736000000000ULL; }
static ULONGLONG ev_keywords(const Event *e)
{
    return e->rec.EventType == EVENTLOG_AUDIT_SUCCESS ? 0x8020000000000000ULL :
           e->rec.EventType == EVENTLOG_AUDIT_FAILURE ? 0x8010000000000000ULL : 0x80000000000000ULL;
}

/* The system time as Event XML writes it */
static void sb_time(Sb *b, DWORD unix)
{
    ULONGLONG t = ev_filetime(unix);
    FILETIME ft = { (DWORD)t, (DWORD)(t >> 32) };
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    sb_num(b, st.wYear, 0, 4); sb_put(b, L"-", 1); sb_num(b, st.wMonth, 0, 2); sb_put(b, L"-", 1); sb_num(b, st.wDay, 0, 2);
    sb_put(b, L"T", 1); sb_num(b, st.wHour, 0, 2); sb_put(b, L":", 1); sb_num(b, st.wMinute, 0, 2); sb_put(b, L":", 1);
    sb_num(b, st.wSecond, 0, 2); sb_put(b, L".0000000Z", -1);
}

/* ---- loading a log ---- */

typedef struct { Event **v; DWORD n, cap; } EvList;

static void list_add(EvList *l, Event *e)
{
    if (l->n == l->cap) {
        DWORD cap = l->cap ? l->cap * 2 : 64;
        Event **v = evt_alloc(sizeof(Event *) * cap);
        if (!v) { evt_free(e); return; }
        if (l->v) memcpy(v, l->v, sizeof(Event *) * l->n);
        evt_free(l->v);
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = e;
}

static void list_free(EvList *l)
{
    for (DWORD i = 0; i < l->n; i++) evt_free(l->v[i]);
    evt_free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

/* Does the channel have a log? */
static BOOL channel_known(const WCHAR *channel)
{
    WCHAR path[200];
    HKEY k;
    if (!channel || !channel[0] || lstrlenW(channel) >= NOVA_EVT_LOG_CHARS) return FALSE;
    for (const WCHAR *c = channel; *c; c++) if (*c == '\\' || *c == '/') return FALSE;
    wcpy(path, 200, NOVA_EVT_KEY L"\\");
    wcpy(path + lstrlenW(path), 200 - (size_t)lstrlenW(path), channel);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k)) return FALSE;
    RegCloseKey(k);
    return TRUE;
}

/* The events of a channel (@file FALSE) or of a saved .evt file, oldest
 * first, after record @after; FALSE (last error set) if the file cannot
 * be read */
BOOL evt_load(const WCHAR *path, BOOL file, DWORD after, EvList *out)
{
    WCHAR channel[NOVA_EVT_LOG_CHARS] = L"";
    HANDLE h;
    if (file) {
        h = OpenBackupEventLogW(NULL, path);
        if (!h) return FALSE;
    } else {
        if (!channel_known(path)) return TRUE;               /* (a channel NovaOS has no log for: no events) */
        wcpy(channel, NOVA_EVT_LOG_CHARS, path);
        h = OpenEventLogW(NULL, path);
        if (!h) return FALSE;
    }
    DWORD size = 0x10000, read, need;
    BYTE *buf = evt_alloc(size);
    DWORD oldest = 0, count = 0;
    GetOldestEventLogRecord(h, &oldest);
    GetNumberOfEventLogRecords(h, &count);
    BOOL seek = after >= oldest && after < oldest + count;
    while (buf) {
        BOOL ok = seek ? ReadEventLogW(h, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, after + 1, buf, size, &read, &need)
                       : ReadEventLogW(h, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, 0, buf, size, &read, &need);
        if (!ok && GetLastError() == ERROR_INSUFFICIENT_BUFFER && need > size) {
            evt_free(buf);
            size = need;
            buf = evt_alloc(size);
            continue;
        }
        if (!ok) break;
        seek = FALSE;
        for (DWORD off = 0; off + sizeof(EVENTLOGRECORD) <= read;) {
            const EVENTLOGRECORD *r = (const EVENTLOGRECORD *)(buf + off);
            if (r->RecordNumber > after) {
                Event *e = copy_event(channel, r);
                if (e) list_add(out, e);
            }
            off += r->Length;
        }
    }
    evt_free(buf);
    CloseEventLog(h);
    return TRUE;
}

/* ---- the XPath filter ---- */

typedef struct { const WCHAR *p; BOOL bad; const Event *e; } Xp;

static void xp_space(Xp *x) { while (*x->p == ' ' || *x->p == '\t' || *x->p == '\r' || *x->p == '\n') x->p++; }

static BOOL xp_word(Xp *x, const WCHAR *w)
{
    xp_space(x);
    int n = lstrlenW(w);
    for (int i = 0; i < n; i++) if (x->p[i] != w[i]) return FALSE;
    WCHAR c = x->p[n];
    if (((w[0] >= 'a' && w[0] <= 'z') || (w[0] >= 'A' && w[0] <= 'Z')) &&
        ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
        return FALSE;
    x->p += n;
    return TRUE;
}

static void xp_need(Xp *x, const WCHAR *w) { if (!xp_word(x, w)) x->bad = TRUE; }

static LONGLONG xp_number(Xp *x)
{
    xp_space(x);
    LONGLONG v = 0;
    BOOL hex = x->p[0] == '0' && (x->p[1] == 'x' || x->p[1] == 'X'), any = FALSE;
    if (hex) x->p += 2;
    for (;; x->p++) {
        WCHAR c = *x->p;
        int d = c >= '0' && c <= '9' ? c - '0' : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) break;
        v = v * (hex ? 16 : 10) + d;
        any = TRUE;
    }
    if (!any) x->bad = TRUE;
    return v;
}

/* '...' or "...": TRUE if it equals @s (ignoring case) */
static BOOL xp_string_is(Xp *x, const WCHAR *s)
{
    xp_space(x);
    WCHAR q = *x->p;
    if (q != '\'' && q != '"') { x->bad = TRUE; return FALSE; }
    const WCHAR *start = ++x->p;
    while (*x->p && *x->p != q) x->p++;
    if (!*x->p) { x->bad = TRUE; return FALSE; }
    int n = (int)(x->p - start);
    x->p++;
    return lstrlenW(s) == n && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, start, n, s, n) == CSTR_EQUAL;
}

static int xp_op(Xp *x)
{
    xp_space(x);
    if (x->p[0] == '!' && x->p[1] == '=') { x->p += 2; return 1; }
    if (x->p[0] == '<' && x->p[1] == '=') { x->p += 2; return 2; }
    if (x->p[0] == '>' && x->p[1] == '=') { x->p += 2; return 3; }
    if (x->p[0] == '&' && x->p[1] == 'l' && x->p[2] == 't' && x->p[3] == ';' && x->p[4] == '=') { x->p += 5; return 2; }
    if (x->p[0] == '&' && x->p[1] == 'g' && x->p[2] == 't' && x->p[3] == ';' && x->p[4] == '=') { x->p += 5; return 3; }
    if (x->p[0] == '&' && x->p[1] == 'l' && x->p[2] == 't' && x->p[3] == ';') { x->p += 4; return 4; }
    if (x->p[0] == '&' && x->p[1] == 'g' && x->p[2] == 't' && x->p[3] == ';') { x->p += 4; return 5; }
    if (x->p[0] == '=') { x->p++; return 0; }
    if (x->p[0] == '<') { x->p++; return 4; }
    if (x->p[0] == '>') { x->p++; return 5; }
    x->bad = TRUE;
    return 0;
}

static BOOL xp_cmp(LONGLONG a, int op, LONGLONG b)
{
    switch (op) {
    case 0: return a == b;
    case 1: return a != b;
    case 2: return a <= b;
    case 3: return a >= b;
    case 4: return a < b;
    default: return a > b;
    }
}

static BOOL xp_or(Xp *x);

/* @Name='a' or @Name='b' inside Provider[...] */
static BOOL xp_provider(Xp *x)
{
    BOOL any = FALSE;
    do {
        xp_need(x, L"@Name");
        xp_need(x, L"=");
        if (xp_string_is(x, x->e ? ev_source(x->e) : L"")) any = TRUE;
    } while (!x->bad && xp_word(x, L"or"));
    return any;
}

static BOOL xp_term(Xp *x)
{
    const Event *e = x->e;
    if (xp_word(x, L"(")) {
        BOOL v = xp_or(x);
        xp_need(x, L")");
        return v;
    }
    if (xp_word(x, L"Provider")) {
        xp_need(x, L"[");
        BOOL v = xp_provider(x);
        xp_need(x, L"]");
        return v;
    }
    if (xp_word(x, L"TimeCreated")) {                  /* TimeCreated[timediff(@SystemTime) <= ms] */
        xp_need(x, L"["); xp_need(x, L"timediff"); xp_need(x, L"("); xp_need(x, L"@SystemTime"); xp_need(x, L")");
        int op = xp_op(x);
        LONGLONG ms = xp_number(x);
        xp_need(x, L"]");
        if (!e) return FALSE;
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULONGLONG n = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime, t = ev_filetime(e->rec.TimeGenerated);
        return xp_cmp(n > t ? (LONGLONG)((n - t) / 10000) : 0, op, ms);
    }
    if (xp_word(x, L"band")) {                         /* band(Keywords, n) */
        xp_need(x, L"("); xp_need(x, L"Keywords"); xp_need(x, L",");
        LONGLONG m = xp_number(x);
        xp_need(x, L")");
        return e && (ev_keywords(e) & (ULONGLONG)m) != 0;
    }
    static const WCHAR *const names[] = { L"Level", L"EventID", L"EventRecordID", L"Task", L"Keywords", L"Opcode", L"Version" };
    for (int i = 0; i < 7; i++)
        if (xp_word(x, names[i])) {
            int op = xp_op(x);
            LONGLONG v = xp_number(x);
            if (!e) return FALSE;
            LONGLONG have = i == 0 ? nova_evt_level(e->rec.EventType) : i == 1 ? (LONGLONG)(e->rec.EventID & 0xFFFF) :
                            i == 2 ? (LONGLONG)e->rec.RecordNumber : i == 3 ? e->rec.EventCategory :
                            i == 4 ? (LONGLONG)ev_keywords(e) : 0;
            return xp_cmp(have, op, v);
        }
    x->bad = TRUE;
    return FALSE;
}

static BOOL xp_and(Xp *x)
{
    BOOL v = xp_term(x);
    while (!x->bad && xp_word(x, L"and")) { BOOL w = xp_term(x); v = v && w; }
    return v;
}

static BOOL xp_or(Xp *x)
{
    BOOL v = xp_and(x);
    while (!x->bad && xp_word(x, L"or")) { BOOL w = xp_and(x); v = v || w; }
    return v;
}

/* Does @e match the select expression @q ("*", "*[System[...]]",
 * "Event/System[...]")?  @bad: the expression is not one this reads */
static BOOL xp_match(const WCHAR *q, const Event *e, BOOL *bad)
{
    Xp x = { q, FALSE, e };
    xp_space(&x);
    if (!*x.p) return TRUE;
    BOOL v = TRUE;
    if (xp_word(&x, L"*")) {
        if (xp_word(&x, L"[")) {
            xp_need(&x, L"System");
            xp_need(&x, L"[");
            v = xp_or(&x);
            xp_need(&x, L"]");
            xp_need(&x, L"]");
        }
    } else if (xp_word(&x, L"Event")) {
        xp_need(&x, L"/");
        xp_need(&x, L"System");
        if (xp_word(&x, L"[")) { v = xp_or(&x); xp_need(&x, L"]"); }
    } else {
        x.bad = TRUE;
    }
    xp_space(&x);
    if (*x.p) x.bad = TRUE;
    if (bad) *bad = x.bad;
    return v && !x.bad;
}

/* A <QueryList>'s first <Select Path="...">...</Select>: its path and expression */
static BOOL query_list(const WCHAR *q, WCHAR *path, WCHAR *expr, int cap)
{
    const WCHAR *s = q;
    while (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t') s++;
    if (wcsncmp(s, L"<QueryList", 10)) return FALSE;
    const WCHAR *sel = wfind(s, L"<Select");
    if (!sel) return FALSE;
    const WCHAR *pa = wfind(sel, L"Path=");
    const WCHAR *gt = wcschr(sel, '>');
    if (!gt) return FALSE;
    path[0] = 0;
    if (pa && pa < gt) {
        WCHAR qc = pa[5];
        const WCHAR *st = pa + 6, *en = wcschr(st, qc);
        if (en && en - st < cap) { memcpy(path, st, sizeof(WCHAR) * (size_t)(en - st)); path[en - st] = 0; }
    }
    const WCHAR *end = wfind(gt, L"</Select>");
    if (!end || end - gt - 1 >= cap) return FALSE;
    memcpy(expr, gt + 1, sizeof(WCHAR) * (size_t)(end - gt - 1));
    expr[end - gt - 1] = 0;
    return TRUE;
}

/* ---- queries ---- */

typedef struct {
    DWORD  magic;
    EvList l;
    DWORD  pos;
} Query;

/* The events @path and @query select, in @out (oldest first) */
static BOOL run_query(LPCWSTR path, LPCWSTR query, DWORD flags, EvList *out, DWORD after)
{
    DWORD kind = flags & 3;
    WCHAR qpath[MAX_PATH], qexpr[1024];
    const WCHAR *expr = query ? query : L"*";
    if (query && query_list(query, qpath, qexpr, 1024)) {
        if (qpath[0]) path = qpath;
        expr = qexpr;
    }
    if ((kind != EvtQueryChannelPath && kind != EvtQueryFilePath) || !path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL bad = FALSE;
    xp_match(expr, NULL, &bad);
    if (bad && !(flags & EvtQueryTolerateQueryErrors)) { SetLastError(ERROR_EVT_INVALID_QUERY); return FALSE; }
    EvList all = { 0 };
    if (!evt_load(path, kind == EvtQueryFilePath, after, &all)) {
        if (kind == EvtQueryFilePath && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) SetLastError(ERROR_FILE_NOT_FOUND);
        else if (kind == EvtQueryFilePath) return TRUE;  /* (a file that is not a .evt: no events NovaOS can read) */
        return FALSE;
    }
    for (DWORD i = 0; i < all.n; i++) {
        if (bad || xp_match(expr, all.v[i], NULL)) list_add(out, all.v[i]);
        else evt_free(all.v[i]);
    }
    evt_free(all.v);
    return TRUE;
}

WEVTAPI HANDLE WINAPI EvtQuery(HANDLE session, LPCWSTR path, LPCWSTR query, DWORD flags)
{
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return NULL; }      /* (remote sessions) */
    Query *q = make(H_QUERY, sizeof(Query));
    if (!q) return NULL;
    if (!run_query(path, query, flags, &q->l, 0)) { evt_free(q); return NULL; }
    if (flags & EvtQueryReverseDirection)
        for (DWORD i = 0; i < q->l.n / 2; i++) { Event *t = q->l.v[i]; q->l.v[i] = q->l.v[q->l.n - 1 - i]; q->l.v[q->l.n - 1 - i] = t; }
    return q;
}

/* A subscription: events delivered to a callback, or queued for EvtNext
 * with @signal set */
typedef struct {
    DWORD  magic;
    EvList l;                       /* (queued, for EvtNext) */
    DWORD  pos;
    WCHAR  path[MAX_PATH];
    WCHAR *query;
    DWORD  after;                   /* the newest record seen */
    HANDLE signal;
    EVT_SUBSCRIBE_CALLBACK cb;
    void  *ctx;
    volatile LONG stop;
    CRITICAL_SECTION lock;
    HANDLE thread;
} Sub;

WEVTAPI BOOL WINAPI EvtNext(HANDLE results, DWORD n, HANDLE *events, DWORD timeout, DWORD flags, DWORD *returned)
{
    (void)timeout; (void)flags;
    EvtObj *o = results;
    if (!o || (o->magic != H_QUERY && o->magic != H_SUB && o->magic != H_ENUM)) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (!n || !events || !returned) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *returned = 0;
    if (o->magic == H_ENUM) { SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
    Query *q = (Query *)o;
    Sub *s = o->magic == H_SUB ? (Sub *)o : NULL;
    if (s) EnterCriticalSection(&s->lock);
    EvList *l = s ? &s->l : &q->l;
    DWORD *pos = s ? &s->pos : &q->pos;
    while (*returned < n && *pos < l->n) {
        Event *e = copy_event(l->v[*pos]->channel, &l->v[*pos]->rec);
        if (!e) break;
        events[(*returned)++] = e;
        (*pos)++;
    }
    if (s) LeaveCriticalSection(&s->lock);
    if (!*returned) { SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
    return TRUE;
}

WEVTAPI BOOL WINAPI EvtSeek(HANDLE results, LONGLONG position, HANDLE bookmark, DWORD timeout, DWORD flags)
{
    (void)timeout;
    Query *q = (Query *)obj_of(results, H_QUERY);
    if (!q) return FALSE;
    LONGLONG at;
    switch (flags & 7) {
    case 1: at = position; break;                                   /* EvtSeekRelativeToFirst */
    case 2: at = (LONGLONG)q->l.n - 1 + position; break;            /* EvtSeekRelativeToLast */
    case 3: at = (LONGLONG)q->pos + position; break;                /* EvtSeekRelativeToCurrent */
    case 4: {                                                       /* EvtSeekRelativeToBookmark */
        Bookmark *b = (Bookmark *)obj_of(bookmark, H_BOOKMARK);
        if (!b) return FALSE;
        at = -1;
        for (DWORD i = 0; i < q->l.n && at < 0; i++)
            if (q->l.v[i]->rec.RecordNumber == b->record && wsame(q->l.v[i]->channel, b->channel)) at = i;
        if (at < 0) { SetLastError(ERROR_NOT_FOUND); return FALSE; }
        at += position;
        break;
    }
    default: SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    if (at < 0 || at > (LONGLONG)q->l.n) { SetLastError(ERROR_NOT_FOUND); return FALSE; }
    q->pos = (DWORD)at;
    return TRUE;
}

/* ---- rendering ---- */

typedef struct { DWORD magic, kind, n; WCHAR **paths; } Render;

WEVTAPI HANDLE WINAPI EvtCreateRenderContext(DWORD n, LPCWSTR *paths, DWORD flags)
{
    if (flags > 2 || (flags == 0 && n && !paths)) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    Render *r = make(H_RENDER, sizeof(Render));
    if (!r) return NULL;
    r->kind = flags;
    if (flags == 0 && n) {
        r->paths = evt_alloc(sizeof(WCHAR *) * n);
        for (DWORD i = 0; r->paths && i < n; i++) {
            r->paths[i] = evt_alloc(sizeof(WCHAR) * (size_t)(lstrlenW(paths[i]) + 1));
            if (r->paths[i]) wcpy(r->paths[i], (size_t)lstrlenW(paths[i]) + 1, paths[i]);
        }
        r->n = r->paths ? n : 0;
    }
    return r;
}

/* The Event XML of a classic event, as Windows writes it */
void evt_xml(const Event *e, Sb *b, BOOL rendering)
{
    const EVENTLOGRECORD *r = &e->rec;
    sb_put(b, L"<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System><Provider Name='", -1);
    sb_esc(b, ev_source(e));
    sb_put(b, L"'/><EventID Qualifiers='", -1);
    sb_num(b, r->EventID >> 16, 0, 0);
    sb_put(b, L"'>", -1);
    sb_num(b, r->EventID & 0xFFFF, 0, 0);
    sb_put(b, L"</EventID><Version>0</Version><Level>", -1);
    sb_num(b, nova_evt_level(r->EventType), 0, 0);
    sb_put(b, L"</Level><Task>", -1);
    sb_num(b, r->EventCategory, 0, 0);
    sb_put(b, L"</Task><Opcode>0</Opcode><Keywords>0x", -1);
    WCHAR kw[20];
    Sb t = { 0 };
    sb_num(&t, ev_keywords(e), 1, 0);
    wcpy(kw, 20, t.p);
    evt_free(t.p);
    for (WCHAR *c = kw; *c; c++) if (*c >= 'A' && *c <= 'F') *c = (WCHAR)(*c - 'A' + 'a');
    sb_put(b, kw, -1);
    sb_put(b, L"</Keywords><TimeCreated SystemTime='", -1);
    sb_time(b, r->TimeGenerated);
    sb_put(b, L"'/><EventRecordID>", -1);
    sb_num(b, r->RecordNumber, 0, 0);
    sb_put(b, L"</EventRecordID><Correlation/><Execution ProcessID='0' ThreadID='0'/><Channel>", -1);
    sb_esc(b, e->channel);
    sb_put(b, L"</Channel><Computer>", -1);
    sb_esc(b, nova_evt_computer(r));
    sb_put(b, L"</Computer>", -1);
    LPWSTR sid = NULL;
    if (r->UserSidLength && ConvertSidToStringSidW((PSID)((const BYTE *)r + r->UserSidOffset), &sid)) {
        sb_put(b, L"<Security UserID='", -1);
        sb_esc(b, sid);
        sb_put(b, L"'/>", -1);
        LocalFree(sid);
    } else {
        sb_put(b, L"<Security/>", -1);
    }
    sb_put(b, L"</System><EventData>", -1);
    for (int i = 0; i < r->NumStrings; i++) {
        sb_put(b, L"<Data>", -1);
        sb_esc(b, ev_string(e, i));
        sb_put(b, L"</Data>", -1);
    }
    if (r->DataLength) {
        sb_put(b, L"<Binary>", -1);
        const BYTE *d = (const BYTE *)r + r->DataOffset;
        for (DWORD i = 0; i < r->DataLength; i++) sb_num(b, d[i], 1, 2);
        sb_put(b, L"</Binary>", -1);
    }
    sb_put(b, L"</EventData>", -1);
    if (rendering) {
        WCHAR *msg = evt_message(e);
        WCHAR lvl[32];
        evt_level_name(e, lvl, 32);
        sb_put(b, L"<RenderingInfo Culture='en-US'><Message>", -1);
        sb_esc(b, msg ? msg : L"");
        sb_put(b, L"</Message><Level>", -1);
        sb_esc(b, lvl);
        sb_put(b, L"</Level><Task>", -1);
        WCHAR task[32];
        evt_task_name(e, task, 32);
        sb_esc(b, task);
        sb_put(b, L"</Task><Opcode>Info</Opcode><Channel>", -1);
        sb_esc(b, e->channel);
        sb_put(b, L"</Channel><Provider>", -1);
        sb_esc(b, ev_source(e));
        sb_put(b, L"</Provider><Keywords><Keyword>Classic</Keyword></Keywords></RenderingInfo>", -1);
        evt_free(msg);
    }
    sb_put(b, L"</Event>", -1);
}

/* Variants into the caller's buffer: @n of them, then the strings they point to */
typedef struct { BYTE *buf; DWORD size, used, strings; EvtVariant *v; DWORD n; } Vb;

static void vb_string(Vb *vb, DWORD i, const WCHAR *s)
{
    DWORD len = (DWORD)(s ? lstrlenW(s) + 1 : 0) * sizeof(WCHAR);
    EvtVariant *v = &vb->v[i];
    v->type = s ? EvtVarTypeString : EvtVarTypeNull;
    v->count = 0;
    v->v = 0;
    if (!s) return;
    if (vb->used + len <= vb->size && vb->buf) {
        memcpy(vb->buf + vb->used, s, len);
        v->v = (ULONGLONG)(ULONG_PTR)(vb->buf + vb->used);
    }
    vb->used += len;
}

static void vb_value(Vb *vb, DWORD i, DWORD type, ULONGLONG val)
{
    vb->v[i].type = type;
    vb->v[i].count = 0;
    vb->v[i].v = val;
}

/* One value of the event by its XPath, for a values context */
static void path_value(Vb *vb, DWORD i, const Event *e, const WCHAR *p)
{
    const EVENTLOGRECORD *r = &e->rec;
    if (!wcsncmp(p, L"Event/", 6)) p += 6;
    else if (!wcsncmp(p, L"/Event/", 7)) p += 7;
    if (wsame(p, L"System/Provider/@Name")) vb_string(vb, i, ev_source(e));
    else if (wsame(p, L"System/EventID")) vb_value(vb, i, EvtVarTypeUInt16, r->EventID & 0xFFFF);
    else if (wsame(p, L"System/EventID/@Qualifiers")) vb_value(vb, i, EvtVarTypeUInt16, r->EventID >> 16);
    else if (wsame(p, L"System/Level")) vb_value(vb, i, EvtVarTypeByte, nova_evt_level(r->EventType));
    else if (wsame(p, L"System/Task")) vb_value(vb, i, EvtVarTypeUInt16, r->EventCategory);
    else if (wsame(p, L"System/Keywords")) vb_value(vb, i, EvtVarTypeHexInt64, ev_keywords(e));
    else if (wsame(p, L"System/TimeCreated/@SystemTime")) vb_value(vb, i, EvtVarTypeFileTime, ev_filetime(r->TimeGenerated));
    else if (wsame(p, L"System/EventRecordID")) vb_value(vb, i, EvtVarTypeUInt64, r->RecordNumber);
    else if (wsame(p, L"System/Channel")) vb_string(vb, i, e->channel);
    else if (wsame(p, L"System/Computer")) vb_string(vb, i, nova_evt_computer(r));
    else if (!wcsncmp(p, L"EventData/Data[", 15)) {               /* EventData/Data[n] (from 1) */
        int k = 0;
        for (const WCHAR *c = p + 15; *c >= '0' && *c <= '9'; c++) k = k * 10 + (*c - '0');
        const WCHAR *s = k >= 1 ? ev_string(e, k - 1) : NULL;
        vb_string(vb, i, s);
    } else vb_value(vb, i, EvtVarTypeNull, 0);
}

static BOOL render_values(Render *ctx, Event *e, DWORD size, void *buf, DWORD *used, DWORD *count)
{
    DWORD n = ctx->kind == 1 ? 18 : ctx->kind == 2 ? e->rec.NumStrings : ctx->n;
    EvtVariant scratch[64];
    Vb vb = { buf, size, n * sizeof(EvtVariant), 0, NULL, n };
    vb.v = buf && size >= n * sizeof(EvtVariant) ? buf : n <= 64 ? scratch : NULL;
    if (!vb.v) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    if (!(buf && size >= n * sizeof(EvtVariant))) vb.buf = NULL;
    const EVENTLOGRECORD *r = &e->rec;
    if (ctx->kind == 1) {                                          /* EvtRenderContextSystem: EVT_SYSTEM_PROPERTY_ID order */
        vb_string(&vb, 0, ev_source(e));                           /* EvtSystemProviderName */
        vb_value(&vb, 1, EvtVarTypeNull, 0);                       /* ProviderGuid (a classic source has none) */
        vb_value(&vb, 2, EvtVarTypeUInt16, r->EventID & 0xFFFF);
        vb_value(&vb, 3, EvtVarTypeUInt16, r->EventID >> 16);      /* Qualifiers */
        vb_value(&vb, 4, EvtVarTypeByte, nova_evt_level(r->EventType));
        vb_value(&vb, 5, EvtVarTypeUInt16, r->EventCategory);      /* Task */
        vb_value(&vb, 6, EvtVarTypeByte, 0);                       /* Opcode */
        vb_value(&vb, 7, EvtVarTypeHexInt64, ev_keywords(e));
        vb_value(&vb, 8, EvtVarTypeFileTime, ev_filetime(r->TimeGenerated));
        vb_value(&vb, 9, EvtVarTypeUInt64, r->RecordNumber);
        vb_value(&vb, 10, EvtVarTypeNull, 0);                      /* ActivityID */
        vb_value(&vb, 11, EvtVarTypeNull, 0);                      /* RelatedActivityID */
        vb_value(&vb, 12, EvtVarTypeUInt32, 0);                    /* ProcessID */
        vb_value(&vb, 13, EvtVarTypeUInt32, 0);                    /* ThreadID */
        vb_string(&vb, 14, e->channel);
        vb_string(&vb, 15, nova_evt_computer(r));
        if (r->UserSidLength) {                                    /* UserID */
            DWORD len = r->UserSidLength;
            if (vb.buf && vb.used + len <= size) {
                memcpy(vb.buf + vb.used, (const BYTE *)r + r->UserSidOffset, len);
                vb_value(&vb, 16, EvtVarTypeSid, (ULONGLONG)(ULONG_PTR)(vb.buf + vb.used));
            } else vb_value(&vb, 16, EvtVarTypeSid, 0);
            vb.used += len;
        } else vb_value(&vb, 16, EvtVarTypeNull, 0);
        vb_value(&vb, 17, EvtVarTypeByte, 0);                      /* Version */
    } else if (ctx->kind == 2) {                                   /* EvtRenderContextUser: the strings */
        for (DWORD i = 0; i < n; i++) vb_string(&vb, i, ev_string(e, (int)i));
    } else {
        for (DWORD i = 0; i < n; i++) path_value(&vb, i, e, ctx->paths[i]);
    }
    *used = vb.used;
    if (count) *count = n;
    if (!vb.buf) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    if (vb.used > size) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    return TRUE;
}

static BOOL give_text(const WCHAR *s, DWORD chars, DWORD size, void *buf, DWORD *used)
{
    *used = chars * sizeof(WCHAR);
    if (!buf || size < *used) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    memcpy(buf, s, *used);
    return TRUE;
}

WEVTAPI BOOL WINAPI EvtRender(HANDLE context, HANDLE fragment, DWORD flags, DWORD size, void *buf, DWORD *used, DWORD *count)
{
    if (!used) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *used = 0;
    if (count) *count = 0;
    if (flags == 2) {                                              /* EvtRenderBookmark */
        Bookmark *b = (Bookmark *)obj_of(fragment, H_BOOKMARK);
        if (!b) return FALSE;
        Sb s = { 0 };
        sb_put(&s, L"<BookmarkList>\r\n  <Bookmark Channel='", -1);
        sb_esc(&s, b->channel);
        sb_put(&s, L"' RecordId='", -1);
        sb_num(&s, b->record, 0, 0);
        sb_put(&s, L"' IsCurrent='true'/>\r\n</BookmarkList>", -1);
        BOOL ok = !s.oom && give_text(s.p, (DWORD)s.len + 1, size, buf, used);
        evt_free(s.p);
        return ok;
    }
    Event *e = event_of(fragment);
    if (!e) return FALSE;
    if (flags == 1) {                                              /* EvtRenderEventXml */
        Sb s = { 0 };
        evt_xml(e, &s, FALSE);
        if (s.oom) { evt_free(s.p); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
        BOOL ok = give_text(s.p, (DWORD)s.len + 1, size, buf, used);
        evt_free(s.p);
        return ok;
    }
    if (flags != 0) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    Render *ctx = (Render *)obj_of(context, H_RENDER);
    if (!ctx) return FALSE;
    return render_values(ctx, e, size, buf, used, count);
}

/* ---- messages ---- */

/* The source's key: HKLM\...\EventLog\<channel>\<source> */
static BOOL source_key(const WCHAR *channel, const WCHAR *source, HKEY *k)
{
    WCHAR path[400];
    wcpy(path, 400, NOVA_EVT_KEY L"\\");
    int n = lstrlenW(path);
    wcpy(path + n, (size_t)(400 - n), channel);
    n = lstrlenW(path);
    wcpy(path + n, (size_t)(400 - n), L"\\");
    n = lstrlenW(path);
    wcpy(path + n, (size_t)(400 - n), source);
    return !RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, k);
}

/* A message from one of the ;-separated files of the source's @value
 * (EventMessageFile, CategoryMessageFile), with the event's strings */
static WCHAR *file_message(const Event *e, const WCHAR *value, DWORD id, BOOL inserts)
{
    HKEY k;
    if (!source_key(e->channel, ev_source(e), &k)) return NULL;
    WCHAR raw[1024], files[2048];
    DWORD size = sizeof(raw) - 2, type;
    LSTATUS st = RegQueryValueExW(k, value, NULL, &type, (BYTE *)raw, &size);
    RegCloseKey(k);
    if (st || (type != REG_SZ && type != REG_EXPAND_SZ)) return NULL;
    raw[size / 2] = 0;
    if (!ExpandEnvironmentStringsW(raw, files, 2048)) wcpy(files, 2048, raw);
    DWORD_PTR args[100];
    for (int i = 0; i < 100; i++) {
        const WCHAR *s = ev_string(e, i);
        args[i] = (DWORD_PTR)(s ? s : L"");
    }
    for (WCHAR *f = files; f && *f;) {
        WCHAR *semi = wcschr(f, ';');
        if (semi) *semi = 0;
        HMODULE m = LoadLibraryExW(f, NULL, LOAD_LIBRARY_AS_DATAFILE);
        if (m) {
            WCHAR *out = NULL;
            DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                     (inserts ? FORMAT_MESSAGE_ARGUMENT_ARRAY : FORMAT_MESSAGE_IGNORE_INSERTS),
                                     m, id, 0, (LPWSTR)&out, 0, (va_list *)args);
            FreeLibrary(m);
            if (n && out) {
                WCHAR *r = evt_alloc(sizeof(WCHAR) * (n + 1));
                if (r) wcpy(r, n + 1, out);
                LocalFree(out);
                while (n && r && (r[n - 1] == '\r' || r[n - 1] == '\n')) r[--n] = 0;
                return r;
            }
        }
        f = semi ? semi + 1 : NULL;
    }
    return NULL;
}

/* @text with %1..%99 replaced by the event's strings, %n a line break */
static WCHAR *fill(const Event *e, const WCHAR *text)
{
    Sb b = { 0 };
    for (const WCHAR *c = text; *c; c++) {
        if (*c == '%' && c[1] == 'n') { sb_put(&b, L"\r\n", 2); c++; continue; }
        if (*c == '%' && c[1] >= '1' && c[1] <= '9') {
            int k = c[1] - '0';
            c++;
            if (c[1] >= '0' && c[1] <= '9') { k = k * 10 + (c[1] - '0'); c++; }
            const WCHAR *s = ev_string(e, k - 1);
            sb_put(&b, s ? s : L"", -1);
            continue;
        }
        sb_put(&b, c, 1);
    }
    if (!b.p) sb_put(&b, L"", 0);
    return b.p;
}

/* The event's message (heap), or NULL if its source has none */
WCHAR *evt_message(const Event *e)
{
    WCHAR *m = file_message(e, L"EventMessageFile", e->rec.EventID, TRUE);
    if (m) return m;
    const WCHAR *t = nova_evt_builtin(ev_source(e), e->rec.EventID);
    return t ? fill(e, t) : NULL;
}

/* What Event Viewer shows when a source has no message for the event */
WCHAR *evt_message_or_note(const Event *e)
{
    WCHAR *m = evt_message(e);
    if (m) return m;
    Sb b = { 0 };
    sb_put(&b, L"The description for Event ID ", -1);
    sb_num(&b, e->rec.EventID & 0xFFFF, 0, 0);
    sb_put(&b, L" from source ", -1);
    sb_put(&b, ev_source(e), -1);
    sb_put(&b, L" cannot be found. Either the component that raises this event is not installed on your local computer "
               L"or the installation is corrupted. You can install or repair the component on the local computer.\r\n\r\n"
               L"If the event originated on another computer, the display information had to be saved with the event.", -1);
    if (e->rec.NumStrings) {
        sb_put(&b, L"\r\n\r\nThe following information was included with the event: \r\n", -1);
        for (int i = 0; i < e->rec.NumStrings; i++) { sb_put(&b, L"\r\n", 2); sb_put(&b, ev_string(e, i), -1); }
    }
    return b.p;
}

void evt_level_name(const Event *e, WCHAR *out, int cap)
{
    switch (e->rec.EventType) {
    case EVENTLOG_ERROR_TYPE: wcpy(out, (size_t)cap, L"Error"); break;
    case EVENTLOG_WARNING_TYPE: wcpy(out, (size_t)cap, L"Warning"); break;
    case EVENTLOG_AUDIT_SUCCESS: wcpy(out, (size_t)cap, L"Audit Success"); break;
    case EVENTLOG_AUDIT_FAILURE: wcpy(out, (size_t)cap, L"Audit Failure"); break;
    default: wcpy(out, (size_t)cap, L"Information"); break;
    }
}

/* A task category: "None", the source's CategoryMessageFile text, or "(n)" */
void evt_task_name(const Event *e, WCHAR *out, int cap)
{
    if (!e->rec.EventCategory) { wcpy(out, (size_t)cap, L"None"); return; }
    WCHAR *m = file_message(e, L"CategoryMessageFile", e->rec.EventCategory, FALSE);
    if (m) { wcpy(out, (size_t)cap, m); evt_free(m); return; }
    Sb b = { 0 };
    sb_put(&b, L"(", 1);
    sb_num(&b, e->rec.EventCategory, 0, 0);
    sb_put(&b, L")", 1);
    wcpy(out, (size_t)cap, b.p);
    evt_free(b.p);
}

typedef struct { DWORD magic; WCHAR name[256]; } Publisher;

WEVTAPI BOOL WINAPI EvtFormatMessage(HANDLE pub, HANDLE event, DWORD id, DWORD n, const void *values, DWORD flags,
                                     DWORD size, LPWSTR buf, DWORD *used)
{
    (void)id; (void)n; (void)values;
    if (!used) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *used = 0;
    if (pub && !obj_of(pub, H_PUBLISHER)) return FALSE;
    Event *e = event_of(event);
    if (!e) return FALSE;
    WCHAR small[64];
    WCHAR *text = NULL;
    const WCHAR *s = NULL;
    switch (flags) {
    case 1: text = evt_message(e); break;                            /* EvtFormatMessageEvent */
    case 2: evt_level_name(e, small, 64); s = small; break;          /* Level */
    case 3: evt_task_name(e, small, 64); s = small; break;           /* Task */
    case 4: s = L"Info"; break;                                      /* Opcode */
    case 5:                                                          /* Keyword (a list: two 0s end it) */
        s = e->rec.EventType == EVENTLOG_AUDIT_SUCCESS ? L"Audit Success" :
            e->rec.EventType == EVENTLOG_AUDIT_FAILURE ? L"Audit Failure" : L"Classic";
        break;
    case 6: s = e->channel; break;                                   /* Channel */
    case 7: s = ev_source(e); break;                                 /* Provider */
    case 9: {                                                        /* Xml, with RenderingInfo */
        Sb b = { 0 };
        evt_xml(e, &b, TRUE);
        text = b.p;
        break;
    }
    default: SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    if (flags == 1 && !text) { SetLastError(ERROR_EVT_MESSAGE_NOT_FOUND); return FALSE; }
    if (text) s = text;
    DWORD chars = (DWORD)lstrlenW(s) + 1 + (flags == 5);
    *used = chars;
    BOOL ok = buf && size >= chars;
    if (ok) {
        memcpy(buf, s, sizeof(WCHAR) * (chars - (flags == 5)));
        if (flags == 5) buf[chars - 1] = 0;
    } else SetLastError(ERROR_INSUFFICIENT_BUFFER);
    evt_free(text);
    return ok;
}

/* ---- handles ---- */

static void free_sub(Sub *s);

WEVTAPI BOOL WINAPI EvtClose(HANDLE h)
{
    EvtObj *o = h;
    if (!o) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    switch (o->magic) {
    case H_QUERY: list_free(&((Query *)o)->l); break;
    case H_ENUM: evt_free(((Enum *)o)->names); break;
    case H_RENDER: {
        Render *r = (Render *)o;
        for (DWORD i = 0; r->paths && i < r->n; i++) evt_free(r->paths[i]);
        evt_free(r->paths);
        break;
    }
    case H_SUB: free_sub((Sub *)o); return TRUE;
    case H_EVENT: case H_BOOKMARK: case H_LOG: case H_CONFIG: case H_PUBLISHER: break;
    default: SetLastError(ERROR_INVALID_HANDLE); return FALSE;
    }
    o->magic = 0;
    evt_free(o);
    return TRUE;
}

WEVTAPI BOOL WINAPI EvtGetEventInfo(HANDLE event, int id, DWORD size, void *buf, DWORD *used)
{
    Event *e = event_of(event);
    if (!e) return FALSE;
    if (!used) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (id == 1) {                                                   /* EvtEventPath: its channel */
        DWORD len = (DWORD)(lstrlenW(e->channel) + 1) * sizeof(WCHAR);
        *used = sizeof(EvtVariant) + len;
        if (!buf || size < *used) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        EvtVariant *v = buf;
        memcpy(v + 1, e->channel, len);
        v->v = (ULONGLONG)(ULONG_PTR)(v + 1);
        v->count = 0;
        v->type = EvtVarTypeString;
        return TRUE;
    }
    *used = sizeof(EvtVariant);
    if (!buf || size < *used) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    EvtVariant *v = buf;
    v->v = 0; v->count = 0; v->type = EvtVarTypeNull;                /* EvtEventQueryIDs: none */
    return TRUE;
}

/* ---- bookmarks ---- */

WEVTAPI HANDLE WINAPI EvtCreateBookmark(LPCWSTR xml)
{
    Bookmark *b = make(H_BOOKMARK, sizeof(Bookmark));
    if (!b || !xml) return b;
    const WCHAR *c = wfind(xml, L"Channel=");
    if (c && (c[8] == '\'' || c[8] == '"')) {
        const WCHAR *end = wcschr(c + 9, c[8]);
        int n = end ? (int)(end - c - 9) : 0;
        if (n > 0 && n < NOVA_EVT_LOG_CHARS) { memcpy(b->channel, c + 9, sizeof(WCHAR) * (size_t)n); b->channel[n] = 0; }
    }
    const WCHAR *r = wfind(xml, L"RecordId=");
    if (r && (r[9] == '\'' || r[9] == '"'))
        for (const WCHAR *d = r + 10; *d >= '0' && *d <= '9'; d++) b->record = b->record * 10 + (DWORD)(*d - '0');
    return b;
}

WEVTAPI BOOL WINAPI EvtUpdateBookmark(HANDLE bookmark, HANDLE event)
{
    Bookmark *b = (Bookmark *)obj_of(bookmark, H_BOOKMARK);
    if (!b) return FALSE;
    Event *e = event_of(event);
    if (!e) return FALSE;
    wcpy(b->channel, NOVA_EVT_LOG_CHARS, e->channel);
    b->record = e->rec.RecordNumber;
    return TRUE;
}

/* ---- subscriptions ---- */

static void poll_sub(Sub *s)
{
    nova_evt_sync(1000);
    EvList l = { 0 };
    if (!run_query(s->path, s->query, EvtQueryChannelPath | EvtQueryTolerateQueryErrors, &l, s->after)) return;
    for (DWORD i = 0; i < l.n; i++) {
        Event *e = l.v[i];
        if (e->rec.RecordNumber > s->after) s->after = e->rec.RecordNumber;
        if (s->cb) {
            s->cb(1 /* EvtSubscribeActionDeliver */, s->ctx, e);
            evt_free(e);
        } else {
            EnterCriticalSection(&s->lock);
            list_add(&s->l, e);
            LeaveCriticalSection(&s->lock);
        }
    }
    if (l.n && s->signal) SetEvent(s->signal);
    evt_free(l.v);
}

static DWORD WINAPI sub_thread(void *arg)
{
    Sub *s = arg;
    while (!s->stop) {
        poll_sub(s);
        for (int i = 0; i < 5 && !s->stop; i++) Sleep(100);
    }
    return 0;
}

static void free_sub(Sub *s)
{
    s->magic = 0;
    s->stop = 1;
    if (s->thread) {
        if (GetCurrentThreadId() != GetThreadId(s->thread)) WaitForSingleObject(s->thread, 5000);
        CloseHandle(s->thread);
    }
    if (s->signal) CloseHandle(s->signal);
    list_free(&s->l);
    DeleteCriticalSection(&s->lock);
    evt_free(s->query);
    evt_free(s);
}

WEVTAPI HANDLE WINAPI EvtSubscribe(HANDLE session, HANDLE signal, LPCWSTR path, LPCWSTR query, HANDLE bookmark,
                                   void *context, EVT_SUBSCRIBE_CALLBACK callback, DWORD flags)
{
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return NULL; }
    if ((!path && !query) || (!signal && !callback) || (signal && callback)) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    Bookmark *b = NULL;
    if ((flags & 3) == 3 && !(b = (Bookmark *)obj_of(bookmark, H_BOOKMARK))) return NULL;   /* EvtSubscribeStartAfterBookmark */
    Sub *s = make(H_SUB, sizeof(Sub));
    if (!s) return NULL;
    WCHAR qpath[MAX_PATH], qexpr[1024];
    if (query && query_list(query, qpath, qexpr, 1024) && qpath[0]) path = qpath;
    wcpy(s->path, MAX_PATH, path ? path : L"Application");
    if (query) {
        s->query = evt_alloc(sizeof(WCHAR) * (size_t)(lstrlenW(query) + 1));
        if (s->query) wcpy(s->query, (size_t)lstrlenW(query) + 1, query);
    }
    BOOL bad = FALSE;
    if (query) {
        const WCHAR *expr = query_list(query, qpath, qexpr, 1024) ? qexpr : query;
        xp_match(expr, NULL, &bad);
    }
    if (bad && !(flags & EvtQueryTolerateQueryErrors)) { evt_free(s->query); evt_free(s); SetLastError(ERROR_EVT_INVALID_QUERY); return NULL; }
    InitializeCriticalSection(&s->lock);
    s->cb = callback;
    s->ctx = context;
    if (signal && !DuplicateHandle(GetCurrentProcess(), signal, GetCurrentProcess(), &s->signal, 0, FALSE, DUPLICATE_SAME_ACCESS))
        s->signal = NULL;
    /* where it starts: after the newest event (EvtSubscribeToFutureEvents,
     * and after an empty bookmark), at the oldest, or after the bookmark */
    if ((flags & 3) == 2) s->after = 0;
    else if ((flags & 3) == 3 && b->record) s->after = b->record;
    else {
        HANDLE h = channel_known(s->path) ? OpenEventLogW(NULL, s->path) : NULL;
        DWORD oldest = 0, count = 0;
        if (h) { GetOldestEventLogRecord(h, &oldest); GetNumberOfEventLogRecords(h, &count); CloseEventLog(h); }
        s->after = count ? oldest + count - 1 : 0;
    }
    s->thread = CreateThread(NULL, 0, sub_thread, s, 0, NULL);
    return s;
}

/* ---- logs ---- */

typedef struct { DWORD magic; WCHAR path[MAX_PATH]; BOOL file; } LogH;

WEVTAPI HANDLE WINAPI EvtOpenLog(HANDLE session, LPCWSTR path, DWORD flags)
{
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return NULL; }
    if (!path || (flags != EvtQueryChannelPath && flags != EvtQueryFilePath)) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    if (flags == EvtQueryFilePath && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return NULL;
    }
    LogH *l = make(H_LOG, sizeof(LogH));
    if (!l) return NULL;
    wcpy(l->path, MAX_PATH, path);
    l->file = flags == EvtQueryFilePath;
    return l;
}

static void log_file(const LogH *l, WCHAR *out)
{
    if (l->file) { wcpy(out, MAX_PATH, l->path); return; }
    wcpy(out, MAX_PATH, NOVA_EVT_LOG_DIR L"\\");
    int n = lstrlenW(out);
    wcpy(out + n, (size_t)(MAX_PATH - n - 5), l->path);
    n = lstrlenW(out);
    wcpy(out + n, (size_t)(MAX_PATH - n), L".evt");
}

/* EVT_LOG_PROPERTY_ID: the file's times and size, its attributes, the
 * number of events, the oldest's number, whether it is full */
WEVTAPI BOOL WINAPI EvtGetLogInfo(HANDLE log, int id, DWORD size, void *buf, DWORD *used)
{
    static const DWORD type[] = { EvtVarTypeFileTime, EvtVarTypeFileTime, EvtVarTypeFileTime, EvtVarTypeUInt64,
                                  EvtVarTypeUInt32, EvtVarTypeUInt64, EvtVarTypeUInt64, EvtVarTypeBoolean };
    LogH *l = (LogH *)obj_of(log, H_LOG);
    if (!l) return FALSE;
    if (id < 0 || id >= (int)(sizeof(type) / sizeof(type[0]))) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (used) *used = sizeof(EvtVariant);
    if (size < sizeof(EvtVariant) || !buf) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    ULONGLONG v = 0;
    WCHAR path[MAX_PATH];
    log_file(l, path);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    BOOL have = !(!l->file && !channel_known(l->path)) && GetFileAttributesExW(path, GetFileExInfoStandard, &fa);
    if (id <= 4 && have) {
        const FILETIME *t = id == 0 ? &fa.ftCreationTime : id == 1 ? &fa.ftLastAccessTime : &fa.ftLastWriteTime;
        v = id <= 2 ? ((ULONGLONG)t->dwHighDateTime << 32) | t->dwLowDateTime :
            id == 3 ? ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow : fa.dwFileAttributes;
    } else if ((id == 5 || id == 6) && (l->file || channel_known(l->path))) {
        HANDLE h = l->file ? OpenBackupEventLogW(NULL, l->path) : OpenEventLogW(NULL, l->path);
        DWORD n = 0;
        if (h) {
            if (id == 5) GetNumberOfEventLogRecords(h, &n); else GetOldestEventLogRecord(h, &n);
            CloseEventLog(h);
        }
        v = n;
    }
    EvtVariant *e = buf;
    e->v = v;
    e->count = 0;
    e->type = type[id];
    return TRUE;
}

WEVTAPI BOOL WINAPI EvtClearLog(HANDLE session, LPCWSTR channel, LPCWSTR target, DWORD flags)
{
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
    if (!channel || flags) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (!channel_known(channel)) { SetLastError(ERROR_EVT_CHANNEL_NOT_FOUND); return FALSE; }
    if (target && target[0]) {
        int n = lstrlenW(target);
        BOOL evtx = n > 5 && wsame(target + n - 5, L".evtx");
        if (evtx && !EvtExportLog(NULL, channel, L"*", target, EvtQueryChannelPath)) return FALSE;
        if (evtx) target = NULL;
    }
    HANDLE h = OpenEventLogW(NULL, channel);
    if (!h) return FALSE;
    BOOL ok = ClearEventLogW(h, target);
    DWORD err = GetLastError();
    CloseEventLog(h);
    SetLastError(err);
    return ok;
}

WEVTAPI BOOL WINAPI EvtExportLog(HANDLE session, LPCWSTR path, LPCWSTR query, LPCWSTR target, DWORD flags)
{
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
    if (!target || !target[0]) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (!(flags & 0x2000) && GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES) {    /* EvtExportLogOverwrite */
        SetLastError(ERROR_FILE_EXISTS);
        return FALSE;
    }
    EvList l = { 0 };
    if (!run_query(path, query, (flags & 3) | (flags & EvtQueryTolerateQueryErrors), &l, 0)) return FALSE;
    BOOL ok = evtx_write(target, (const Event *const *)l.v, l.n);
    list_free(&l);
    return ok;
}

WEVTAPI BOOL WINAPI EvtArchiveExportedLog(HANDLE session, LPCWSTR path, DWORD locale, DWORD flags)
{
    (void)locale; (void)flags;
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
    if (!path || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    return TRUE;                                     /* (exported logs carry their messages already: nothing to add) */
}

/* ---- channels and publishers ---- */

/* The logs (subkeys of the EventLog key) or the sources (their
 * subkeys), as a list of names */
static Enum *make_enum(BOOL sources)
{
    Enum *e = make(H_ENUM, sizeof(Enum));
    if (!e) return NULL;
    Sb b = { 0 };
    HKEY root;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, NOVA_EVT_KEY, 0, KEY_READ, &root)) {
        for (DWORD i = 0;; i++) {
            WCHAR log[NOVA_EVT_LOG_CHARS];
            DWORD n = NOVA_EVT_LOG_CHARS;
            if (RegEnumKeyExW(root, i, log, &n, NULL, NULL, NULL, NULL)) break;
            if (!sources) { sb_put(&b, log, -1); sb_put(&b, L"|", 1); continue; }
            HKEY lk;
            if (RegOpenKeyExW(root, log, 0, KEY_READ, &lk)) continue;
            for (DWORD j = 0;; j++) {
                WCHAR src[256];
                DWORD m = 256;
                if (RegEnumKeyExW(lk, j, src, &m, NULL, NULL, NULL, NULL)) break;
                Sb t = { 0 };
                sb_put(&t, L"|", 1); sb_put(&t, src, -1); sb_put(&t, L"|", 1);
                BOOL dup = b.p && (wfind(b.p, t.p) || (!wcsncmp(b.p, t.p + 1, (size_t)m) && b.p[m] == '|'));
                evt_free(t.p);
                if (!dup) { sb_put(&b, src, -1); sb_put(&b, L"|", 1); }
            }
            RegCloseKey(lk);
        }
        RegCloseKey(root);
    }
    e->names = b.p;
    return e;
}

static BOOL enum_next(HANDLE h, DWORD size, LPWSTR buf, DWORD *used)
{
    Enum *e = (Enum *)obj_of(h, H_ENUM);
    if (!e) return FALSE;
    if (!used) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *used = 0;
    const WCHAR *s = e->names ? e->names + e->pos : NULL;
    if (!s || !*s) { SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
    const WCHAR *bar = wcschr(s, '|');
    DWORD n = bar ? (DWORD)(bar - s) : (DWORD)lstrlenW(s);
    *used = n + 1;
    if (!buf || size < n + 1) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    memcpy(buf, s, sizeof(WCHAR) * n);
    buf[n] = 0;
    e->pos += n + (bar ? 1 : 0);
    return TRUE;
}

WEVTAPI HANDLE WINAPI EvtOpenChannelEnum(HANDLE session, DWORD flags)
{
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return NULL; }
    if (flags) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    return make_enum(FALSE);
}
WEVTAPI HANDLE WINAPI EvtOpenPublisherEnum(HANDLE session, DWORD flags)
{
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return NULL; }
    if (flags) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    return make_enum(TRUE);
}
WEVTAPI BOOL WINAPI EvtNextChannelPath(HANDLE e, DWORD size, LPWSTR buf, DWORD *used) { return enum_next(e, size, buf, used); }
WEVTAPI BOOL WINAPI EvtNextPublisherId(HANDLE e, DWORD size, LPWSTR buf, DWORD *used) { return enum_next(e, size, buf, used); }

typedef struct { DWORD magic; WCHAR channel[NOVA_EVT_LOG_CHARS]; } Config;

WEVTAPI HANDLE WINAPI EvtOpenChannelConfig(HANDLE session, LPCWSTR path, DWORD flags)
{
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return NULL; }
    if (!path || flags) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    Config *c = make(H_CONFIG, sizeof(Config));
    if (c) wcpy(c->channel, NOVA_EVT_LOG_CHARS, path);
    return c;
}

/* EVT_CHANNEL_CONFIG_PROPERTY_ID: a classic log's (unset for a channel
 * NovaOS has no log for) */
WEVTAPI BOOL WINAPI EvtGetChannelConfigProperty(HANDLE config, int id, DWORD flags, DWORD size, void *buf, DWORD *used)
{
    (void)flags;
    Config *c = (Config *)obj_of(config, H_CONFIG);
    if (!c) return FALSE;
    WCHAR file[MAX_PATH];
    LogH l = { H_LOG, L"", FALSE };
    wcpy(l.path, MAX_PATH, c->channel);
    log_file(&l, file);
    BOOL known = channel_known(c->channel);
    DWORD max = 1u << 20;
    if (known) {
        HKEY k;
        WCHAR p[200];
        wcpy(p, 200, NOVA_EVT_KEY L"\\");
        wcpy(p + lstrlenW(p), 200 - (size_t)lstrlenW(p), c->channel);
        DWORD n = sizeof(max);
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, p, 0, KEY_READ, &k)) { RegQueryValueExW(k, L"MaxSize", NULL, NULL, (BYTE *)&max, &n); RegCloseKey(k); }
    }
    EvtVariant tmp[1];
    Vb vb = { buf, size, sizeof(EvtVariant), 0, tmp, 1 };
    if (!buf || size < sizeof(EvtVariant)) vb.buf = NULL;
    if (!known) vb_value(&vb, 0, EvtVarTypeNull, 0);
    else if (id == 0 || id == 4) vb_value(&vb, 0, EvtVarTypeBoolean, 1);          /* Enabled, ClassicEventlog */
    else if (id == 1 || id == 2) vb_value(&vb, 0, EvtVarTypeUInt32, 0);           /* Isolation, Type: Application, Admin */
    else if (id == 6 || id == 7) vb_value(&vb, 0, EvtVarTypeBoolean, 0);          /* Retention, AutoBackup */
    else if (id == 8) vb_value(&vb, 0, EvtVarTypeUInt64, max);                    /* MaxSize */
    else if (id == 9) vb_string(&vb, 0, file);                                    /* LogFilePath */
    else vb_value(&vb, 0, EvtVarTypeNull, 0);
    if (used) *used = vb.used;
    if (!vb.buf || vb.used > size) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    memcpy(buf, tmp, sizeof(EvtVariant));
    return TRUE;
}

/* A publisher: a source with a key under a log, or one of NovaOS's own */
WEVTAPI HANDLE WINAPI EvtOpenPublisherMetadata(HANDLE session, LPCWSTR id, LPCWSTR file, DWORD locale, DWORD flags)
{
    (void)file; (void)locale; (void)flags;
    if (session) { SetLastError(ERROR_NOT_SUPPORTED); return NULL; }
    BOOL found = FALSE;
    for (unsigned i = 0; id && i < sizeof(nova_evt_messages) / sizeof(nova_evt_messages[0]) && !found; i++)
        found = wsame(nova_evt_messages[i].source, id);
    if (id && !found) {
        Enum *e = make_enum(TRUE);
        if (e && e->names) {
            for (const WCHAR *s = e->names; *s && !found;) {
                const WCHAR *bar = wcschr(s, '|');
                int n = bar ? (int)(bar - s) : lstrlenW(s);
                found = n == lstrlenW(id) && CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, s, n, id, n) == CSTR_EQUAL;
                s += n + (bar ? 1 : 0);
            }
        }
        if (e) EvtClose(e);
    }
    if (!found) { SetLastError(ERROR_EVT_PUBLISHER_METADATA_NOT_FOUND); return NULL; }
    Publisher *p = make(H_PUBLISHER, sizeof(Publisher));
    if (p) wcpy(p->name, 256, id);
    return p;
}

WEVTAPI BOOL WINAPI EvtGetPublisherMetadataProperty(HANDLE pub, int id, DWORD flags, DWORD size, void *buf, DWORD *used)
{
    (void)id; (void)flags;
    if (!obj_of(pub, H_PUBLISHER)) return FALSE;
    if (used) *used = sizeof(EvtVariant);
    if (!buf || size < sizeof(EvtVariant)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    EvtVariant *v = buf;
    v->v = 0; v->count = 0; v->type = EvtVarTypeNull;                /* (a classic source: no manifest) */
    return TRUE;
}

/* A classic source has no manifest: no event metadata to list */
WEVTAPI HANDLE WINAPI EvtOpenEventMetadataEnum(HANDLE pub, DWORD flags)
{
    (void)flags;
    if (!obj_of(pub, H_PUBLISHER)) return NULL;
    Enum *e = make(H_ENUM, sizeof(Enum));
    return e;
}
WEVTAPI HANDLE WINAPI EvtNextEventMetadata(HANDLE e, DWORD flags)
{
    (void)flags;
    if (!obj_of(e, H_ENUM)) return NULL;
    SetLastError(ERROR_NO_MORE_ITEMS);
    return NULL;
}
WEVTAPI BOOL WINAPI EvtGetEventMetadataProperty(HANDLE meta, int id, DWORD flags, DWORD size, void *buf, DWORD *used)
{
    (void)meta; (void)id; (void)flags; (void)size; (void)buf;
    if (used) *used = 0;
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
WEVTAPI BOOL WINAPI EvtGetObjectArraySize(HANDLE array, DWORD *n)
{
    (void)array;
    if (n) *n = 0;
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
WEVTAPI BOOL WINAPI EvtGetObjectArrayProperty(HANDLE array, DWORD id, DWORD index, DWORD flags, DWORD size, void *buf, DWORD *used)
{
    (void)array; (void)id; (void)index; (void)flags; (void)size; (void)buf;
    if (used) *used = 0;
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
