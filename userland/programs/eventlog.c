/*
 * eventlog.exe — the event log service ("EventLog", Windows Event Log)
 *
 * Keeps the logs: each event a program reports (advapi32's ReportEvent),
 * the service control manager writes (a service started or stopped) or
 * the kernel makes (the boot, a driver that did not load, a program's
 * crash) waits in the kernel (CTL_EVENTLOG) until this service fetches
 * it, numbers it and appends it to its log's file,
 * C:\Windows\System32\winevt\Logs\<log>.evt (the classic .evt layout,
 * novaevt.h), then tells the kernel it is written.  A log past its MaxSize
 * (HKLM\SYSTEM\CurrentControlSet\Services\EventLog\<log>, 1 MB unless
 * set) drops its oldest events, as Windows' "overwrite events as needed".
 * A request to clear a log (ClearEventLog) comes the same way; the
 * service empties the file and writes event 104 to the System log (1102
 * to Security for the Security log), as Windows does.
 *
 * At start it makes the Application, System and Security logs (their
 * registry keys, with NovaOS's own event sources under them, and their
 * files) and writes event 6005, "The Event log service was started.";
 * stopping writes 6006.  The kernel makes the service's key at boot
 * (automatic start).
 */
#include <windows.h>
#include <winsvc.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <novaevt.h>

#define DEFAULT_MAX (1u << 20)
#define FETCH_ROOM  (512u << 10)

static SERVICE_STATUS_HANDLE g_status;
static SERVICE_STATUS g_st;
static volatile LONG g_stop;

static void say(const char *fmt, ...)
{
    char line[300], out[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    snprintf(out, sizeof(out), "[EVENTLOG] %s\n", line);
    OutputDebugStringA(out);
}

static void report(DWORD state)
{
    g_st.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_st.dwCurrentState = state;
    g_st.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    SetServiceStatus(g_status, &g_st);
}

static DWORD WINAPI handler(DWORD control, DWORD type, LPVOID data, LPVOID ctx)
{
    (void)type; (void)data; (void)ctx;
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        report(SERVICE_STOP_PENDING);
        InterlockedExchange(&g_stop, 1);
        return 0;
    }
    return control == SERVICE_CONTROL_INTERROGATE ? 0 : ERROR_CALL_NOT_IMPLEMENTED;
}

/* ---- the registry ---- */

static const struct { const WCHAR *log, *source; } g_sources[] = {
    { L"System", L"EventLog" },
    { L"System", L"Service Control Manager" },
    { L"System", L"Microsoft-Windows-Kernel-General" },
    { L"System", L"Microsoft-Windows-Eventlog" },
    { L"Application", L"Application Error" },
    { L"Security", L"Microsoft-Windows-Eventlog" },
};

static void make_keys(void)
{
    static const WCHAR *const logs[] = { L"Application", L"System", L"Security" };
    for (int i = 0; i < 3; i++) {
        WCHAR path[200], file[200];
        swprintf(path, 200, L"%ls\\%ls", NOVA_EVT_KEY, logs[i]);
        HKEY k;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) continue;
        DWORD v, size = sizeof(v);
        if (RegQueryValueExW(k, L"MaxSize", NULL, NULL, (BYTE *)&v, &size)) {
            v = DEFAULT_MAX;
            RegSetValueExW(k, L"MaxSize", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
            v = 0;
            RegSetValueExW(k, L"Retention", 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
        }
        swprintf(file, 200, L"%%SystemRoot%%\\System32\\winevt\\Logs\\%ls.evt", logs[i]);
        RegSetValueExW(k, L"File", 0, REG_EXPAND_SZ, (const BYTE *)file, (DWORD)(wcslen(file) + 1) * 2);
        RegCloseKey(k);
    }
    for (unsigned i = 0; i < sizeof(g_sources) / sizeof(g_sources[0]); i++) {
        WCHAR path[260];
        swprintf(path, 260, L"%ls\\%ls\\%ls", NOVA_EVT_KEY, g_sources[i].log, g_sources[i].source);
        HKEY k;
        if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) RegCloseKey(k);
    }
}

/* The log a queued event names, if it has a key (else Application) */
static void known_log(const WCHAR *name, WCHAR *log)
{
    wcscpy(log, L"Application");
    WCHAR path[200];
    HKEY k;
    if (!name[0] || wcschr(name, '\\') || wcschr(name, '/') || wcschr(name, '.')) return;
    swprintf(path, 200, L"%ls\\%ls", NOVA_EVT_KEY, name);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k)) return;
    RegCloseKey(k);
    wcsncpy(log, name, NOVA_EVT_LOG_CHARS - 1);
    log[NOVA_EVT_LOG_CHARS - 1] = 0;
}

static DWORD max_size(const WCHAR *log)
{
    WCHAR path[200];
    HKEY k;
    DWORD v = DEFAULT_MAX, size = sizeof(v);
    swprintf(path, 200, L"%ls\\%ls", NOVA_EVT_KEY, log);
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k)) {
        if (RegQueryValueExW(k, L"MaxSize", NULL, NULL, (BYTE *)&v, &size)) v = DEFAULT_MAX;
        RegCloseKey(k);
    }
    return v < 0x10000 ? 0x10000 : v;
}

/* ---- the files ---- */

static void file_of(const WCHAR *log, WCHAR *path)
{
    swprintf(path, MAX_PATH, L"%ls\\%ls.evt", NOVA_EVT_LOG_DIR, log);
}

static void fresh_header(NovaEvtHeader *h, DWORD next, DWORD max)
{
    memset(h, 0, sizeof(*h));
    h->HeaderSize = h->EndHeaderSize = sizeof(*h);
    h->Signature = NOVA_ELF_SIG;
    h->MajorVersion = h->MinorVersion = 1;
    h->StartOffset = h->EndOffset = sizeof(*h);
    h->CurrentRecordNumber = h->OldestRecordNumber = next;
    h->MaxSize = max;
}

static void eof_of(const NovaEvtHeader *h, NovaEvtEof *e)
{
    NovaEvtEof v = { sizeof(NovaEvtEof), 0x11111111, 0x22222222, 0x33333333, 0x44444444, h->StartOffset, h->EndOffset,
                     h->CurrentRecordNumber, h->OldestRecordNumber, sizeof(NovaEvtEof) };
    *e = v;
}

static BOOL write_at(HANDLE f, DWORD off, const void *p, DWORD n)
{
    DWORD w = 0;
    return SetFilePointer(f, (LONG)off, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && WriteFile(f, p, n, &w, NULL) && w == n;
}

/* An open log: its file and header */
typedef struct {
    WCHAR log[NOVA_EVT_LOG_CHARS];
    HANDLE f;
    NovaEvtHeader h;
    DWORD max;
} Log;

static BOOL open_log(Log *l, const WCHAR *log)
{
    WCHAR path[MAX_PATH];
    wcscpy(l->log, log);
    file_of(log, path);
    l->max = max_size(log);
    l->f = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_ALWAYS, 0, NULL);
    if (l->f == INVALID_HANDLE_VALUE) { say("cannot open %ls (error %lu)", path, GetLastError()); return FALSE; }
    DWORD got = 0;
    if (!ReadFile(l->f, &l->h, sizeof(l->h), &got, NULL) || got != sizeof(l->h) || l->h.Signature != NOVA_ELF_SIG ||
        l->h.HeaderSize != sizeof(l->h) || l->h.EndOffset < sizeof(l->h) || l->h.EndOffset > GetFileSize(l->f, NULL)) {
        if (got) say("%ls: not a log file; starting it again", path);
        fresh_header(&l->h, 1, l->max);
        NovaEvtEof e;
        eof_of(&l->h, &e);
        write_at(l->f, 0, &l->h, sizeof(l->h));
        write_at(l->f, sizeof(l->h), &e, sizeof(e));
        SetEndOfFile(l->f);
    }
    return TRUE;
}

static void close_log(Log *l)
{
    if (l->f && l->f != INVALID_HANDLE_VALUE) CloseHandle(l->f);
    l->f = NULL;
}

/* Drop the oldest events until @need more bytes fit in three quarters
 * of the log's size */
static void make_room(Log *l, DWORD need)
{
    DWORD keep = l->max / 4 * 3, used = l->h.EndOffset - l->h.StartOffset;
    if (l->h.EndOffset + need + sizeof(NovaEvtEof) <= l->max || used == 0) return;
    BYTE *b = HeapAlloc(GetProcessHeap(), 0, used);
    DWORD got = 0;
    if (!b) return;
    if (SetFilePointer(l->f, (LONG)l->h.StartOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !ReadFile(l->f, b, used, &got, NULL) || got != used) {
        HeapFree(GetProcessHeap(), 0, b);
        return;
    }
    DWORD off = 0, dropped = 0;
    while (off < used && used - off + need > keep) {
        const EVENTLOGRECORD *r = (const EVENTLOGRECORD *)(b + off);
        if (r->Length < sizeof(*r) || off + r->Length > used) { off = used; break; }
        off += r->Length;
        dropped++;
    }
    l->h.OldestRecordNumber += dropped;
    l->h.StartOffset = sizeof(l->h);
    l->h.EndOffset = sizeof(l->h) + (used - off);
    if (used > off) write_at(l->f, sizeof(l->h), b + off, used - off);
    NovaEvtEof e;
    eof_of(&l->h, &e);
    write_at(l->f, l->h.EndOffset, &e, sizeof(e));
    SetEndOfFile(l->f);
    write_at(l->f, 0, &l->h, sizeof(l->h));
    HeapFree(GetProcessHeap(), 0, b);
    say("%ls: full, dropped its %lu oldest events", l->log, (unsigned long)dropped);
}

/* Number @rec and append it: data, then the end, then the header (so a
 * reader never sees the header ahead of the data) */
static BOOL append(Log *l, EVENTLOGRECORD *rec)
{
    make_room(l, rec->Length);
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    rec->RecordNumber = l->h.CurrentRecordNumber;
    rec->TimeWritten = (DWORD)((t - 116444736000000000ULL) / 10000000ULL);
    NovaEvtHeader h = l->h;
    h.EndOffset += rec->Length;
    h.CurrentRecordNumber++;
    BYTE *b = HeapAlloc(GetProcessHeap(), 0, rec->Length + sizeof(NovaEvtEof));  /* (the record and the end: one write) */
    if (!b) return FALSE;
    memcpy(b, rec, rec->Length);
    eof_of(&h, (NovaEvtEof *)(b + rec->Length));
    BOOL ok = write_at(l->f, l->h.EndOffset, b, rec->Length + sizeof(NovaEvtEof));
    HeapFree(GetProcessHeap(), 0, b);
    if (!ok) { say("%ls: cannot write (error %lu)", l->log, GetLastError()); return FALSE; }
    l->h = h;
    return write_at(l->f, 0, &l->h, sizeof(l->h));
}

/* An event of the service's own (6005, 104...) */
static EVENTLOGRECORD *own_event(const WCHAR *source, DWORD id, const WCHAR *str, BYTE *buf)
{
    WCHAR computer[64] = L"NOVA-PC";
    DWORD cl = 64;
    GetComputerNameW(computer, &cl);
    EVENTLOGRECORD *r = (EVENTLOGRECORD *)buf;
    memset(r, 0, sizeof(*r));
    DWORD at = sizeof(*r);
    wcscpy((WCHAR *)(buf + at), source);
    at += (DWORD)(wcslen(source) + 1) * 2;
    wcscpy((WCHAR *)(buf + at), computer);
    at += (DWORD)(wcslen(computer) + 1) * 2;
    at = (at + 3) & ~3u;
    r->StringOffset = r->UserSidOffset = at;
    if (str) {
        wcscpy((WCHAR *)(buf + at), str);
        at += (DWORD)(wcslen(str) + 1) * 2;
        r->NumStrings = 1;
    }
    r->DataOffset = at;
    at = ((at + 3) & ~3u) + 4;
    r->Length = at;
    memcpy(buf + at - 4, &at, 4);
    r->Reserved = NOVA_ELF_SIG;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    r->TimeGenerated = (DWORD)((t - 116444736000000000ULL) / 10000000ULL);
    r->EventID = id;
    r->EventType = EVENTLOG_INFORMATION_TYPE;
    return r;
}

static void clear_log(Log *cur, const WCHAR *name)
{
    WCHAR log[NOVA_EVT_LOG_CHARS];
    known_log(name, log);
    close_log(cur);                                 /* (its header would be stale: System gets event 104) */
    Log l;
    memset(&l, 0, sizeof(l));
    if (!open_log(&l, log)) return;
    DWORD next = l.h.CurrentRecordNumber;           /* (numbers go on from where they were, as on Windows) */
    fresh_header(&l.h, next, l.max);
    NovaEvtEof e;
    eof_of(&l.h, &e);
    write_at(l.f, sizeof(l.h), &e, sizeof(e));
    SetEndOfFile(l.f);
    write_at(l.f, 0, &l.h, sizeof(l.h));
    say("cleared the %ls log", log);
    static BYTE buf[1024];
    BOOL security = !wcscmp(log, L"Security");
    if (!security) {
        close_log(&l);
        if (!open_log(&l, L"System")) return;
    }
    append(&l, own_event(L"Microsoft-Windows-Eventlog", security ? 1102 : 104, security ? NULL : log, buf));
    close_log(&l);
}

/* ---- the service ---- */

static void own(DWORD id)
{
    static BYTE buf[1024];
    EVENTLOGRECORD *r = own_event(L"EventLog", id, NULL, buf);
    nova_evt_submit(L"System", r);
}

static void WINAPI service_main(DWORD argc, LPWSTR *argv)
{
    (void)argc;
    g_status = RegisterServiceCtrlHandlerExW(argv[0], handler, NULL);
    report(SERVICE_START_PENDING);
    CreateDirectoryW(L"C:\\Windows\\System32\\winevt", NULL);
    CreateDirectoryW(NOVA_EVT_LOG_DIR, NULL);
    make_keys();
    static const WCHAR *const logs[] = { L"Application", L"System", L"Security" };
    for (int i = 0; i < 3; i++) {                         /* (each log has its file, if empty) */
        Log l;
        memset(&l, 0, sizeof(l));
        if (open_log(&l, logs[i])) close_log(&l);
    }
    own(0x80001775);                                      /* 6005: started */
    report(SERVICE_RUNNING);
    say("Event log service running: logs in %ls", NOVA_EVT_LOG_DIR);
    BYTE *buf = HeapAlloc(GetProcessHeap(), 0, 24 + FETCH_ROOM);
    DWORD after = 0, written = 0, dropped_seen = 0;
    BOOL stopping = FALSE;
    while (buf) {
        if (g_stop && !stopping) { own(0x80001776); stopping = TRUE; }   /* 6006: stopped (written below) */
        DWORD *q = (DWORD *)buf;
        memset(q, 0, 24);
        q[0] = after;
        q[1] = stopping ? 0 : 1000;
        q[2] = FETCH_ROOM;
        DWORD n = (DWORD)NtNovaGuiCtl(0, NOVA_CTL_EVENTLOG, 1, buf);
        DWORD began = GetTickCount();
        if (q[4] != dropped_seen) {
            say("%lu events were lost before they could be written", (unsigned long)(q[4] - dropped_seen));
            dropped_seen = q[4];
        }
        Log cur;
        memset(&cur, 0, sizeof(cur));
        BYTE *p = buf + 24;
        for (DWORD i = 0; i < n; i++) {
            DWORD seq, size;
            memcpy(&seq, p, 4);
            memcpy(&size, p + 4, 4);
            WCHAR name[NOVA_EVT_LOG_CHARS], log[NOVA_EVT_LOG_CHARS];
            memcpy(name, p + 8, sizeof(name));
            name[NOVA_EVT_LOG_CHARS - 1] = 0;
            EVENTLOGRECORD *rec = (EVENTLOGRECORD *)(p + 8 + sizeof(name));
            if (!wcscmp(name, L"*clear*")) {
                clear_log(&cur, nova_evt_source(rec));
            } else {
                known_log(name, log);
                if (cur.f && wcscmp(cur.log, log)) close_log(&cur);
                if (!cur.f && !open_log(&cur, log)) cur.f = NULL;
                if (cur.f && append(&cur, rec)) written++;
            }
            after = seq;
            p += 8 + sizeof(name) + size;
        }
        close_log(&cur);
        if (n) NtNovaGuiCtl(0, NOVA_CTL_EVENTLOG, 2 | (ULONG_PTR)after << 8, NULL);
        if (n && GetTickCount() - began > 1000)
            say("%lu events took %lu ms to write", (unsigned long)n, (unsigned long)(GetTickCount() - began));
        if (stopping && !n) break;
    }
    say("Event log service stopped after writing %lu events", (unsigned long)written);
    report(SERVICE_STOPPED);
}

int main(void)
{
    SERVICE_TABLE_ENTRYW table[] = { { (LPWSTR)L"EventLog", service_main }, { NULL, NULL } };
    if (StartServiceCtrlDispatcherW(table)) return 0;
    printf("eventlog: the event log service; the service control manager starts it (error %lu)\n",
           (unsigned long)GetLastError());
    return 1;
}
