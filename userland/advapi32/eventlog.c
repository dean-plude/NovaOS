/*
 * eventlog.c — advapi32's event log API (RegisterEventSource, ReportEvent,
 * OpenEventLog, ReadEventLog, ClearEventLog, BackupEventLog, ...)
 *
 * The EventLog service (eventlog.exe) keeps each log in
 * C:\Windows\System32\winevt\Logs\<log>.evt (novaevt.h has the format).
 * ReportEvent builds the EVENTLOGRECORD and hands it to the kernel, which
 * keeps it until the service has written it down (CTL_EVENTLOG); the
 * service gives it its record number.  A source belongs to the log whose
 * key under HKLM\SYSTEM\CurrentControlSet\Services\EventLog has it as a
 * subkey, and to Application otherwise, as on Windows.  Reading waits for
 * what was reported before it to be written, then reads the log's file.
 * Clearing a log goes through the service as well.  A backup
 * (BackupEventLog, ClearEventLog's) is a copy of the .evt file, which
 * OpenBackupEventLog reads.
 */

#define NOVA_BUILD_ADVAPI32
#include <winternl.h>
#include "advapi32.h"
#include <novaevt.h>

void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);

#define EL_SOURCE 0x53455645    /* "EVES" */
#define EL_LOG    0x4C455645    /* "EVEL" */

typedef struct {
    DWORD  magic;
    WCHAR  log[NOVA_EVT_LOG_CHARS];
    WCHAR  source[256];
    WCHAR  path[MAX_PATH];
    BOOL   backup;                  /* a file OpenBackupEventLog opened: no service behind it */
    DWORD  next;                    /* the record a sequential read gives next (0: not started) */
    BYTE  *data;                    /* the file as last read */
    DWORD  size;
    volatile LONG notify;           /* NotifyChangeEventLog's thread runs */
    HANDLE notify_event;
} ElHandle;

static void *el_alloc(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void el_free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static void wcopy_(WCHAR *d, size_t cap, const WCHAR *s)
{
    size_t i = 0;
    for (; s && s[i] && i + 1 < cap; i++) d[i] = s[i];
    d[i] = 0;
}

static WCHAR *a2w_(const char *s)
{
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    WCHAR *w = el_alloc(sizeof(WCHAR) * (size_t)(n + 1));
    if (w) MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

static ElHandle *el_handle(HANDLE h, DWORD magic)
{
    ElHandle *e = h;
    if (!e || e->magic != magic) { SetLastError(ERROR_INVALID_HANDLE); return NULL; }
    return e;
}

/* The log a source writes to: the log whose key has it as a subkey */
static void log_of_source(const WCHAR *source, WCHAR *log)
{
    wcopy_(log, NOVA_EVT_LOG_CHARS, L"Application");
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NOVA_EVT_KEY, 0, KEY_READ, &root)) return;
    for (DWORD i = 0;; i++) {
        WCHAR name[NOVA_EVT_LOG_CHARS];
        DWORD nl = NOVA_EVT_LOG_CHARS;
        if (RegEnumKeyExW(root, i, name, &nl, NULL, NULL, NULL, NULL)) break;
        HKEY lk, sk;
        if (RegOpenKeyExW(root, name, 0, KEY_READ, &lk)) continue;
        LSTATUS r = RegOpenKeyExW(lk, source, 0, KEY_READ, &sk);
        RegCloseKey(lk);
        if (!r) {
            RegCloseKey(sk);
            wcopy_(log, NOVA_EVT_LOG_CHARS, name);
            break;
        }
    }
    RegCloseKey(root);
}

/* The log OpenEventLog(@name) opens: that log if it has a key, else
 * Application (as Windows does) */
static void log_named(const WCHAR *name, WCHAR *log)
{
    wcopy_(log, NOVA_EVT_LOG_CHARS, L"Application");
    if (!name || !name[0]) return;
    HKEY root, k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NOVA_EVT_KEY, 0, KEY_READ, &root)) return;
    if (!RegOpenKeyExW(root, name, 0, KEY_READ, &k)) {
        RegCloseKey(k);
        wcopy_(log, NOVA_EVT_LOG_CHARS, name);
    }
    RegCloseKey(root);
}

static void log_path(const WCHAR *log, WCHAR *path)
{
    wcopy_(path, MAX_PATH, NOVA_EVT_LOG_DIR L"\\");
    int n = lstrlenW(path);
    wcopy_(path + n, (size_t)(MAX_PATH - n - 5), log);
    n = lstrlenW(path);
    wcopy_(path + n, (size_t)(MAX_PATH - n), L".evt");
}

/* -----------------------------------------------------------------------
 * Writing
 * ----------------------------------------------------------------------- */
WINADVAPI HANDLE WINAPI RegisterEventSourceW(LPCWSTR server, LPCWSTR source)
{
    if (server && server[0]) { SetLastError(RPC_S_SERVER_UNAVAILABLE); return NULL; }   /* (no remote logs) */
    if (!source || !source[0]) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    ElHandle *e = el_alloc(sizeof(*e));
    if (!e) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    e->magic = EL_SOURCE;
    wcopy_(e->source, 256, source);
    log_of_source(source, e->log);
    return e;
}

WINADVAPI HANDLE WINAPI RegisterEventSourceA(LPCSTR server, LPCSTR source)
{
    WCHAR *ws = a2w_(server), *wn = a2w_(source);
    HANDLE h = RegisterEventSourceW(ws, wn);
    el_free(ws);
    el_free(wn);
    return h;
}

WINADVAPI BOOL WINAPI DeregisterEventSource(HANDLE h)
{
    ElHandle *e = el_handle(h, EL_SOURCE);
    if (!e) return FALSE;
    e->magic = 0;
    el_free(e);
    return TRUE;
}

static DWORD unix_now(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (DWORD)((t - 116444736000000000ULL) / 10000000ULL);
}

/* An EVENTLOGRECORD (heap) for @source's event; the service fills in its
 * RecordNumber and TimeWritten */
static EVENTLOGRECORD *make_record(const WCHAR *source, WORD type, WORD cat, DWORD id, PSID sid, WORD n,
                                   DWORD size, LPCWSTR *strings, const void *data)
{
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 2] = L"NOVA-PC";
    DWORD cl = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer, &cl);
    DWORD sidlen = sid && IsValidSid(sid) ? GetLengthSid(sid) : 0;
    DWORD len = sizeof(EVENTLOGRECORD) + 2 * (DWORD)(lstrlenW(source) + 1) + 2 * (DWORD)(lstrlenW(computer) + 1);
    len = (len + 3) & ~3u;
    DWORD sid_at = len;
    len += sidlen;
    DWORD str_at = len;
    for (WORD i = 0; i < n; i++) len += 2 * (DWORD)((strings && strings[i] ? lstrlenW(strings[i]) : 0) + 1);
    DWORD data_at = len;
    len += size;
    len = ((len + 3) & ~3u) + 4;
    if (len > NOVA_EVT_MAX_RECORD) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    BYTE *b = el_alloc(len);
    if (!b) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    EVENTLOGRECORD *r = (EVENTLOGRECORD *)b;
    r->Length = len;
    r->Reserved = NOVA_ELF_SIG;
    r->TimeGenerated = r->TimeWritten = unix_now();
    r->EventID = id;
    r->EventType = type;
    r->NumStrings = n;
    r->EventCategory = cat;
    r->StringOffset = str_at;
    r->UserSidLength = sidlen;
    r->UserSidOffset = sid_at;
    r->DataLength = size;
    r->DataOffset = data_at;
    DWORD at = sizeof(EVENTLOGRECORD);
    memcpy(b + at, source, 2 * (size_t)(lstrlenW(source) + 1));
    at += 2 * (DWORD)(lstrlenW(source) + 1);
    memcpy(b + at, computer, 2 * (size_t)(lstrlenW(computer) + 1));
    if (sidlen) memcpy(b + sid_at, sid, sidlen);
    at = str_at;
    for (WORD i = 0; i < n; i++) {
        const WCHAR *s = strings && strings[i] ? strings[i] : L"";
        memcpy(b + at, s, 2 * (size_t)(lstrlenW(s) + 1));
        at += 2 * (DWORD)(lstrlenW(s) + 1);
    }
    if (size && data) memcpy(b + data_at, data, size);
    memcpy(b + len - 4, &len, 4);
    return r;
}

/* The event in the kernel log too, one line (what NovaOS's tests and
 * crash reports read) */
static void debug_line(const WCHAR *source, WORD type, DWORD id, WORD n, LPCWSTR *strings)
{
    char line[300];
    int o = 0;
    const char *kind = type == EVENTLOG_ERROR_TYPE ? "error" : type == EVENTLOG_WARNING_TYPE ? "warning" : "event";
    const char pre[] = "[EVT] ";
    for (int i = 0; pre[i]; i++) line[o++] = pre[i];
    for (const WCHAR *s = source; *s && o < 80; s++) line[o++] = *s < 0x80 ? (char)*s : '?';
    line[o++] = ' ';
    while (*kind) line[o++] = *kind++;
    line[o++] = ' ';
    char num[12];
    int k = 0;
    DWORD v = id & 0xFFFF;
    do { num[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) line[o++] = num[--k];
    line[o++] = ':';
    for (WORD i = 0; i < n && strings && o < 290; i++) {
        line[o++] = ' ';
        for (const WCHAR *s = strings[i]; s && *s && o < 290; s++) line[o++] = *s < 0x20 ? ' ' : *s < 0x80 ? (char)*s : '?';
    }
    line[o++] = '\n';
    NtNovaDebugPrint(line, (ULONG)o);
}

/* advapi32's own events (the service control manager's): @source's
 * event @id in @log */
BOOL nova_report_event(const WCHAR *log, const WCHAR *source, WORD type, DWORD id, WORD n, LPCWSTR *strings)
{
    EVENTLOGRECORD *r = make_record(source, type, 0, id, NULL, n, 0, strings, NULL);
    if (!r) return FALSE;
    debug_line(source, type, id, n, strings);
    DWORD seq = nova_evt_submit(log, r);
    el_free(r);
    return seq != 0;
}

WINADVAPI BOOL WINAPI ReportEventW(HANDLE h, WORD type, WORD cat, DWORD id, PSID sid, WORD n, DWORD size, LPCWSTR *strings, LPVOID data)
{
    ElHandle *e = el_handle(h, EL_SOURCE);
    if (!e) return FALSE;
    if ((n && !strings) || (size && !data)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    EVENTLOGRECORD *r = make_record(e->source, type, cat, id, sid, n, size, strings, data);
    if (!r) return FALSE;
    debug_line(e->source, type, id, n, strings);
    DWORD seq = nova_evt_submit(e->log, r);
    el_free(r);
    if (!seq) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return TRUE;
}

WINADVAPI BOOL WINAPI ReportEventA(HANDLE h, WORD type, WORD cat, DWORD id, PSID sid, WORD n, DWORD size, LPCSTR *strings, LPVOID data)
{
    LPCWSTR *ws = n ? el_alloc(sizeof(LPCWSTR) * n) : NULL;
    if (n && !ws) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    for (WORD i = 0; i < n; i++) ws[i] = strings ? a2w_(strings[i]) : NULL;
    BOOL ok = ReportEventW(h, type, cat, id, sid, n, size, strings ? ws : NULL, data);
    for (WORD i = 0; i < n; i++) el_free((void *)ws[i]);
    el_free(ws);
    return ok;
}

/* -----------------------------------------------------------------------
 * Reading
 * ----------------------------------------------------------------------- */
static ElHandle *open_log(const WCHAR *log, const WCHAR *path, BOOL backup)
{
    ElHandle *e = el_alloc(sizeof(*e));
    if (!e) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    e->magic = EL_LOG;
    wcopy_(e->log, NOVA_EVT_LOG_CHARS, log);
    wcopy_(e->path, MAX_PATH, path);
    e->backup = backup;
    return e;
}

/* Read the log's file again (after the events reported so far reach it) */
static BOOL load(ElHandle *e)
{
    if (!e->backup) nova_evt_sync(3000);
    HANDLE f = CreateFileW(e->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        if (e->backup) return FALSE;
        el_free(e->data);                                   /* (not written yet: an empty log) */
        e->data = NULL;
        e->size = 0;
        return TRUE;
    }
    DWORD size = GetFileSize(f, NULL), got = 0;
    BYTE *b = size != INVALID_FILE_SIZE ? el_alloc(size + 4) : NULL;
    BOOL ok = b && ReadFile(f, b, size, &got, NULL) && got == size;
    CloseHandle(f);
    if (!ok) { el_free(b); SetLastError(ERROR_EVENTLOG_FILE_CORRUPT); return FALSE; }
    const NovaEvtHeader *hd = (const NovaEvtHeader *)b;
    if (size < sizeof(*hd) || hd->Signature != NOVA_ELF_SIG || hd->HeaderSize != sizeof(*hd) || hd->EndOffset > size) {
        el_free(b);
        SetLastError(ERROR_EVENTLOG_FILE_CORRUPT);
        return FALSE;
    }
    el_free(e->data);
    e->data = b;
    e->size = size;
    return TRUE;
}

/* The records in the loaded file, in order: calls @fn for each until it
 * returns FALSE */
static const EVENTLOGRECORD *record_at(const ElHandle *e, DWORD off)
{
    const NovaEvtHeader *hd = (const NovaEvtHeader *)e->data;
    if (!e->data || off < hd->StartOffset || off + sizeof(EVENTLOGRECORD) + 4 > hd->EndOffset) return NULL;
    const EVENTLOGRECORD *r = (const EVENTLOGRECORD *)(e->data + off);
    if (r->Reserved != NOVA_ELF_SIG || r->Length < sizeof(*r) + 4 || (r->Length & 3) || off + r->Length > hd->EndOffset) return NULL;
    return r;
}

/* The record numbered @num, or NULL */
static const EVENTLOGRECORD *find_record(const ElHandle *e, DWORD num)
{
    if (!e->data) return NULL;
    const NovaEvtHeader *hd = (const NovaEvtHeader *)e->data;
    for (DWORD off = hd->StartOffset;;) {
        const EVENTLOGRECORD *r = record_at(e, off);
        if (!r) return NULL;
        if (r->RecordNumber == num) return r;
        if (r->RecordNumber > num) return NULL;
        off += r->Length;
    }
}

static void range(const ElHandle *e, DWORD *oldest, DWORD *count)
{
    const NovaEvtHeader *hd = (const NovaEvtHeader *)e->data;
    *oldest = e->data ? hd->OldestRecordNumber : 0;
    *count = e->data && hd->CurrentRecordNumber > hd->OldestRecordNumber ? hd->CurrentRecordNumber - hd->OldestRecordNumber : 0;
}

WINADVAPI HANDLE WINAPI OpenEventLogW(LPCWSTR server, LPCWSTR source)
{
    if (server && server[0]) { SetLastError(RPC_S_SERVER_UNAVAILABLE); return NULL; }
    WCHAR log[NOVA_EVT_LOG_CHARS], path[MAX_PATH];
    log_named(source, log);
    log_path(log, path);
    ElHandle *e = open_log(log, path, FALSE);
    if (e && !load(e)) { el_free(e); return NULL; }
    return e;
}

WINADVAPI HANDLE WINAPI OpenEventLogA(LPCSTR server, LPCSTR source)
{
    WCHAR *ws = a2w_(server), *wn = a2w_(source);
    HANDLE h = OpenEventLogW(ws, wn);
    el_free(ws);
    el_free(wn);
    return h;
}

WINADVAPI HANDLE WINAPI OpenBackupEventLogW(LPCWSTR server, LPCWSTR file)
{
    if (server && server[0]) { SetLastError(RPC_S_SERVER_UNAVAILABLE); return NULL; }
    if (!file) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    ElHandle *e = open_log(L"", file, TRUE);
    if (e && !load(e)) {
        DWORD err = GetLastError() == ERROR_EVENTLOG_FILE_CORRUPT ? ERROR_EVENTLOG_FILE_CORRUPT : ERROR_FILE_NOT_FOUND;
        el_free(e);
        SetLastError(err);
        return NULL;
    }
    return e;
}

WINADVAPI HANDLE WINAPI OpenBackupEventLogA(LPCSTR server, LPCSTR file)
{
    WCHAR *ws = a2w_(server), *wf = a2w_(file);
    HANDLE h = OpenBackupEventLogW(ws, wf);
    el_free(ws);
    el_free(wf);
    return h;
}

WINADVAPI BOOL WINAPI CloseEventLog(HANDLE h)
{
    ElHandle *e = el_handle(h, EL_LOG);
    if (!e) return FALSE;
    e->magic = 0;
    if (InterlockedExchange(&e->notify, 0)) return TRUE;    /* (the notify thread frees it) */
    el_free(e->data);
    el_free(e);
    return TRUE;
}

WINADVAPI BOOL WINAPI GetNumberOfEventLogRecords(HANDLE h, PDWORD n)
{
    ElHandle *e = el_handle(h, EL_LOG);
    if (!e) return FALSE;
    if (!n) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (!load(e)) return FALSE;
    DWORD oldest;
    range(e, &oldest, n);
    return TRUE;
}

WINADVAPI BOOL WINAPI GetOldestEventLogRecord(HANDLE h, PDWORD n)
{
    ElHandle *e = el_handle(h, EL_LOG);
    if (!e) return FALSE;
    if (!n) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (!load(e)) return FALSE;
    DWORD count;
    range(e, n, &count);
    return TRUE;
}

WINADVAPI BOOL WINAPI GetEventLogInformation(HANDLE h, DWORD level, LPVOID buf, DWORD n, LPDWORD need)
{
    if (!el_handle(h, EL_LOG)) return FALSE;
    if (level != EVENTLOG_FULL_INFO) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    if (need) *need = sizeof(EVENTLOG_FULL_INFORMATION);
    if (!buf || n < sizeof(EVENTLOG_FULL_INFORMATION)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    ((EVENTLOG_FULL_INFORMATION *)buf)->dwFull = 0;         /* (the service drops the oldest instead) */
    return TRUE;
}

/* ReadEventLogW: whole records into @buf, from the record @offset
 * (EVENTLOG_SEEK_READ) or where the last read stopped, forwards or
 * backwards */
static BOOL read_log(ElHandle *e, DWORD flags, DWORD offset, BYTE *buf, DWORD n, DWORD *read, DWORD *need, BOOL ansi);

WINADVAPI BOOL WINAPI ReadEventLogW(HANDLE h, DWORD flags, DWORD offset, LPVOID buf, DWORD n, DWORD *read, DWORD *need)
{
    ElHandle *e = el_handle(h, EL_LOG);
    if (!e) return FALSE;
    return read_log(e, flags, offset, buf, n, read, need, FALSE);
}

WINADVAPI BOOL WINAPI ReadEventLogA(HANDLE h, DWORD flags, DWORD offset, LPVOID buf, DWORD n, DWORD *read, DWORD *need)
{
    ElHandle *e = el_handle(h, EL_LOG);
    if (!e) return FALSE;
    return read_log(e, flags, offset, buf, n, read, need, TRUE);
}

/* @r with its source, computer name and strings in the ANSI code page
 * (ReadEventLogA): its size, the record written to @out if it fits @cap */
static DWORD ansi_record(const EVENTLOGRECORD *r, BYTE *out, DWORD cap)
{
    BYTE tmp[NOVA_EVT_MAX_RECORD];
    EVENTLOGRECORD *a = (EVENTLOGRECORD *)tmp;
    memcpy(a, r, sizeof(*a));
    DWORD at = sizeof(*a);
    const WCHAR *src = nova_evt_source(r), *comp = nova_evt_computer(r);
    at += (DWORD)WideCharToMultiByte(CP_ACP, 0, src, -1, (char *)tmp + at, (int)(sizeof(tmp) - at - 8), NULL, NULL);
    at += (DWORD)WideCharToMultiByte(CP_ACP, 0, comp, -1, (char *)tmp + at, (int)(sizeof(tmp) - at - 8), NULL, NULL);
    at = (at + 3) & ~3u;
    a->UserSidOffset = at;
    if (r->UserSidLength && at + r->UserSidLength < sizeof(tmp) - 8) {
        memcpy(tmp + at, (const BYTE *)r + r->UserSidOffset, r->UserSidLength);
        at += r->UserSidLength;
    }
    a->StringOffset = at;
    const WCHAR *s = (const WCHAR *)((const BYTE *)r + r->StringOffset);
    for (WORD i = 0; i < r->NumStrings; i++) {
        at += (DWORD)WideCharToMultiByte(CP_ACP, 0, s, -1, (char *)tmp + at, (int)(sizeof(tmp) - at - 8), NULL, NULL);
        s += lstrlenW(s) + 1;
    }
    a->DataOffset = at;
    if (r->DataLength && at + r->DataLength < sizeof(tmp) - 8) {
        memcpy(tmp + at, (const BYTE *)r + r->DataOffset, r->DataLength);
        at += r->DataLength;
    }
    at = ((at + 3) & ~3u) + 4;
    a->Length = at;
    memcpy(tmp + at - 4, &at, 4);
    if (out && at <= cap) memcpy(out, tmp, at);
    return at;
}

static BOOL read_log(ElHandle *e, DWORD flags, DWORD offset, BYTE *buf, DWORD n, DWORD *read, DWORD *need, BOOL ansi)
{
    BOOL seek = (flags & EVENTLOG_SEEK_READ) != 0, back = (flags & EVENTLOG_BACKWARDS_READ) != 0;
    if (!read || !need || seek == ((flags & EVENTLOG_SEQUENTIAL_READ) != 0) ||
        back == ((flags & EVENTLOG_FORWARDS_READ) != 0) || (n && !buf)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *read = *need = 0;
    if (!load(e)) return FALSE;
    DWORD oldest, count;
    range(e, &oldest, &count);
    DWORD num = seek ? offset : e->next ? e->next : back ? oldest + count - 1 : oldest;
    if (!count || num < oldest || num >= oldest + count) {
        if (seek && count) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        SetLastError(ERROR_HANDLE_EOF);
        return FALSE;
    }
    DWORD used = 0;
    for (;;) {
        const EVENTLOGRECORD *r = find_record(e, num);
        if (!r) break;
        DWORD len = ansi ? ansi_record(r, NULL, 0) : r->Length;
        if (used + len > n) {
            if (!used) { *need = len; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
            break;
        }
        if (ansi) ansi_record(r, buf + used, n - used);
        else memcpy(buf + used, r, len);
        used += len;
        if (back) { if (num == oldest) { num = 0; break; } num--; }
        else num++;
    }
    e->next = num ? num : (DWORD)-1;                  /* (-1: past the start; the next read finds EOF) */
    *read = used;
    return TRUE;
}

/* -----------------------------------------------------------------------
 * Backups, clearing and notifications
 * ----------------------------------------------------------------------- */
WINADVAPI BOOL WINAPI BackupEventLogW(HANDLE h, LPCWSTR file)
{
    ElHandle *e = el_handle(h, EL_LOG);
    if (!e) return FALSE;
    if (!file || !file[0]) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES) { SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
    if (!load(e)) return FALSE;
    if (!e->data) {                                         /* (nothing written yet: an empty log's file) */
        NovaEvtHeader hd = { sizeof(hd), NOVA_ELF_SIG, 1, 1, sizeof(hd), sizeof(hd), 1, 1, 0x100000, 0, 0, sizeof(hd) };
        NovaEvtEof eof = { sizeof(eof), 0x11111111, 0x22222222, 0x33333333, 0x44444444, sizeof(hd), sizeof(hd), 1, 1, sizeof(eof) };
        HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) return FALSE;
        DWORD w;
        BOOL ok = WriteFile(f, &hd, sizeof(hd), &w, NULL) && WriteFile(f, &eof, sizeof(eof), &w, NULL);
        CloseHandle(f);
        return ok;
    }
    HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w = 0;
    BOOL ok = WriteFile(f, e->data, e->size, &w, NULL) && w == e->size;
    CloseHandle(f);
    return ok;
}

WINADVAPI BOOL WINAPI BackupEventLogA(HANDLE h, LPCSTR file)
{
    WCHAR *w = a2w_(file);
    BOOL r = BackupEventLogW(h, w);
    el_free(w);
    return r;
}

/* The service clears the log (and says so in the System log, event 104):
 * a request for the log "*clear*" whose source is the log's name */
WINADVAPI BOOL WINAPI ClearEventLogW(HANDLE h, LPCWSTR backup)
{
    ElHandle *e = el_handle(h, EL_LOG);
    if (!e) return FALSE;
    if (e->backup) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (backup && backup[0] && !BackupEventLogW(h, backup)) return FALSE;
    EVENTLOGRECORD *r = make_record(e->log, EVENTLOG_INFORMATION_TYPE, 0, 0, NULL, 0, 0, NULL, NULL);
    if (!r) return FALSE;
    DWORD seq = nova_evt_submit(L"*clear*", r);
    el_free(r);
    if (!seq) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (nova_evt_sync(3000) == 2) { SetLastError(RPC_S_SERVER_UNAVAILABLE); return FALSE; }   /* (no service: left queued) */
    e->next = 0;
    return load(e);
}

WINADVAPI BOOL WINAPI ClearEventLogA(HANDLE h, LPCSTR backup)
{
    WCHAR *w = a2w_(backup);
    BOOL r = ClearEventLogW(h, w);
    el_free(w);
    return r;
}

/* NotifyChangeEventLog: @event is set whenever an event is written to the
 * log (checked twice a second) until the handle is closed */
static DWORD WINAPI notify_thread(void *arg)
{
    ElHandle *e = arg;
    WIN32_FILE_ATTRIBUTE_DATA was, now;
    memset(&was, 0, sizeof(was));
    GetFileAttributesExW(e->path, GetFileExInfoStandard, &was);
    while (e->notify) {
        Sleep(500);
        memset(&now, 0, sizeof(now));
        GetFileAttributesExW(e->path, GetFileExInfoStandard, &now);
        if (now.nFileSizeLow != was.nFileSizeLow || now.ftLastWriteTime.dwLowDateTime != was.ftLastWriteTime.dwLowDateTime)
            SetEvent(e->notify_event);
        was = now;
    }
    CloseHandle(e->notify_event);
    el_free(e->data);
    el_free(e);
    return 0;
}

WINADVAPI BOOL WINAPI NotifyChangeEventLog(HANDLE h, HANDLE event)
{
    ElHandle *e = el_handle(h, EL_LOG);
    if (!e) return FALSE;
    if (e->backup) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (e->notify) return TRUE;
    if (!DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &e->notify_event, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return FALSE;
    e->notify = 1;
    HANDLE t = CreateThread(NULL, 0, notify_thread, e, 0, NULL);
    if (!t) { e->notify = 0; CloseHandle(e->notify_event); return FALSE; }
    CloseHandle(t);
    return TRUE;
}
