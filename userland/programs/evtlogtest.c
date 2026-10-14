/*
 * evtlogtest.exe — the event log, from a program's side (core/167-eventlog.py)
 *
 *   - the EventLog service runs (the service control manager says so, and
 *     the System log has its own 6005 and the SCM's 7036 for it, and the
 *     kernel's boot event, Kernel-General 12);
 *   - ReportEventW / ReportEventA from a registered source (NovaEventTest,
 *     under the Application log) reach the Application log: ReadEventLog
 *     backwards and by record number, in UTF-16 and ANSI, gives them back
 *     with their type, category, ID, strings, data, computer and time;
 *     a buffer too small says how big it must be; BackupEventLog's copy
 *     opens with OpenBackupEventLog;
 *   - EvtQuery with an XPath filter finds the same event, EvtRender gives
 *     its XML and system values, a bad XPath is refused, EvtFormatMessage
 *     gives NovaOS's own sources' messages, EvtSubscribe delivers a new
 *     event, EvtExportLog writes .evtx, EvtOpenChannelEnum lists the logs;
 *   - a kernel driver that does not load leaves the SCM's event 7000, and
 *     a program that crashes (crash.exe) Application Error's event 1000.
 *
 * The test's event (ID 4321) stays in the Application log for Event
 * Viewer (eventvwr --check NovaEventTest 4321 ...).
 */
#include <windows.h>
#include <winsvc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <novaevt.h>

typedef struct { ULONGLONG v; DWORD count, type; } EVT_VARIANT;
__declspec(dllimport) HANDLE WINAPI EvtQuery(HANDLE session, LPCWSTR path, LPCWSTR query, DWORD flags);
__declspec(dllimport) BOOL WINAPI EvtNext(HANDLE rs, DWORD n, HANDLE *ev, DWORD timeout, DWORD flags, DWORD *got);
__declspec(dllimport) HANDLE WINAPI EvtCreateRenderContext(DWORD n, LPCWSTR *paths, DWORD flags);
__declspec(dllimport) BOOL WINAPI EvtRender(HANDLE ctx, HANDLE ev, DWORD flags, DWORD size, void *buf, DWORD *used, DWORD *count);
__declspec(dllimport) BOOL WINAPI EvtFormatMessage(HANDLE pub, HANDLE ev, DWORD id, DWORD n, const void *values, DWORD flags,
                                                   DWORD size, LPWSTR buf, DWORD *used);
__declspec(dllimport) BOOL WINAPI EvtClose(HANDLE h);
__declspec(dllimport) HANDLE WINAPI EvtSubscribe(HANDLE session, HANDLE signal, LPCWSTR path, LPCWSTR query, HANDLE bookmark,
                                                 void *ctx, void *callback, DWORD flags);
__declspec(dllimport) BOOL WINAPI EvtExportLog(HANDLE session, LPCWSTR path, LPCWSTR query, LPCWSTR target, DWORD flags);
__declspec(dllimport) HANDLE WINAPI EvtOpenChannelEnum(HANDLE session, DWORD flags);
__declspec(dllimport) BOOL WINAPI EvtNextChannelPath(HANDLE e, DWORD size, LPWSTR buf, DWORD *used);

#define SOURCE L"NovaEventTest"

static int g_pass, g_fail;

#define CHECK(what, cond) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (error %lu)\n", what, GetLastError()); } } while (0)

/* The newest event @query selects in @log: its XML (and the handle, if @keep) */
static BOOL newest(const WCHAR *log, const WCHAR *query, WCHAR *xml, DWORD cap, HANDLE *keep)
{
    HANDLE q = EvtQuery(NULL, log, query, 0x1 | 0x200);       /* EvtQueryChannelPath | EvtQueryReverseDirection */
    HANDLE ev = NULL;
    DWORD n = 0, used = 0, count;
    BOOL ok = q && EvtNext(q, 1, &ev, INFINITE, 0, &n) && n == 1 &&
              EvtRender(NULL, ev, 1 /* EvtRenderEventXml */, cap * sizeof(WCHAR), xml, &used, &count);
    if (ev && keep && ok) *keep = ev;
    else if (ev) EvtClose(ev);
    if (q) EvtClose(q);
    return ok;
}

static BOOL message(HANDLE ev, WCHAR *out, DWORD cap)
{
    DWORD used;
    return EvtFormatMessage(NULL, ev, 0, 0, NULL, 1 /* EvtFormatMessageEvent */, cap, out, &used);
}

static const WCHAR *string_of(const EVENTLOGRECORD *r, int i)
{
    const WCHAR *s = (const WCHAR *)((const BYTE *)r + r->StringOffset);
    while (i-- > 0) s += wcslen(s) + 1;
    return s;
}

int main(void)
{
    static BYTE buf[0x10000];
    static WCHAR xml[16384], msg[4096];
    WCHAR text[96], tmp[MAX_PATH], path[MAX_PATH];
    swprintf(text, 96, L"NovaOS event log test %lu", GetTickCount());
    GetTempPathW(MAX_PATH, tmp);

    /* the service */
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    SC_HANDLE svc = scm ? OpenServiceW(scm, L"EventLog", SERVICE_QUERY_STATUS) : NULL;
    SERVICE_STATUS st;
    memset(&st, 0, sizeof(st));
    for (int i = 0; svc && i < 300 && st.dwCurrentState != SERVICE_RUNNING; i++) {
        QueryServiceStatus(svc, &st);
        if (st.dwCurrentState != SERVICE_RUNNING) Sleep(100);
    }
    CHECK("the EventLog service is running", st.dwCurrentState == SERVICE_RUNNING);
    if (svc) CloseServiceHandle(svc);
    printf("evtlogtest: EventLog service state %lu\n", st.dwCurrentState);

    /* a registered source writes */
    HKEY k;
    CHECK("the source's key is made", !RegCreateKeyExW(HKEY_LOCAL_MACHINE, NOVA_EVT_KEY L"\\Application\\" SOURCE, 0, NULL, 0,
                                                       KEY_ALL_ACCESS, NULL, &k, NULL));
    RegCloseKey(k);
    HANDLE src = RegisterEventSourceW(NULL, SOURCE);
    CHECK("RegisterEventSourceW", src != NULL);
    LPCWSTR strings[2] = { text, L"second string" };
    BYTE data[4] = { 1, 2, 3, 4 };
    CHECK("ReportEventW", ReportEventW(src, EVENTLOG_INFORMATION_TYPE, 3, 4321, NULL, 2, 4, strings, data));
    LPCSTR astrings[1] = { "ANSI report" };
    CHECK("ReportEventA", ReportEventA(src, EVENTLOG_WARNING_TYPE, 0, 4322, NULL, 1, 0, astrings, NULL));
    CHECK("DeregisterEventSource", DeregisterEventSource(src));

    /* ReadEventLog */
    CHECK("the events reported so far are written", nova_evt_sync(3000) == 1);
    HANDLE el = OpenEventLogW(NULL, L"Application");
    CHECK("OpenEventLogW(Application)", el != NULL);
    DWORD count = 0, oldest = 0, read, need, found = 0;
    CHECK("GetNumberOfEventLogRecords", GetNumberOfEventLogRecords(el, &count) && count >= 2);
    CHECK("GetOldestEventLogRecord", GetOldestEventLogRecord(el, &oldest) && oldest >= 1);
    printf("evtlogtest: Application has %lu events from record %lu\n", count, oldest);
    BOOL saw_ansi = FALSE;
    WCHAR computer[64];
    DWORD cl = 64;
    GetComputerNameW(computer, &cl);
    DWORD now = (DWORD)time(NULL);
    while (!found && ReadEventLogW(el, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, buf, sizeof(buf), &read, &need)) {
        for (DWORD off = 0; off < read && !found;) {
            EVENTLOGRECORD *r = (EVENTLOGRECORD *)(buf + off);
            const WCHAR *s = (const WCHAR *)(r + 1);
            if (!wcscmp(s, SOURCE) && (r->EventID & 0xFFFF) == 4322 && r->NumStrings == 1 && !wcscmp(string_of(r, 0), L"ANSI report"))
                saw_ansi = r->EventType == EVENTLOG_WARNING_TYPE;
            if (!wcscmp(s, SOURCE) && r->EventID == 4321 && r->NumStrings == 2 && !wcscmp(string_of(r, 0), text)) {
                found = r->RecordNumber;
                CHECK("its type and category", r->EventType == EVENTLOG_INFORMATION_TYPE && r->EventCategory == 3);
                CHECK("its second string", !wcscmp(string_of(r, 1), L"second string"));
                CHECK("its data", r->DataLength == 4 && !memcmp((BYTE *)r + r->DataOffset, data, 4));
                CHECK("its computer", !_wcsicmp(s + wcslen(s) + 1, computer));
                CHECK("its time", r->TimeGenerated + 300 > now && r->TimeGenerated <= now + 300 && r->TimeWritten >= r->TimeGenerated);
                CHECK("its length at both ends", *(DWORD *)((BYTE *)r + r->Length - 4) == r->Length);
            }
            off += r->Length;
        }
    }
    CHECK("ReadEventLogW finds the ANSI report first (it is newer)", saw_ansi);
    CHECK("ReadEventLogW finds the event", found != 0);
    printf("evtlogtest: the event is record %lu\n", found);
    CHECK("EVENTLOG_SEEK_READ gives the same record",
          ReadEventLogW(el, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, found, buf, sizeof(buf), &read, &need) &&
          ((EVENTLOGRECORD *)buf)->RecordNumber == found);
    CHECK("too small a buffer says how big",
          !ReadEventLogW(el, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, found, buf, 8, &read, &need) &&
          GetLastError() == ERROR_INSUFFICIENT_BUFFER && need == ((EVENTLOGRECORD *)buf)->Length);
    CHECK("ReadEventLogA gives ANSI strings",
          ReadEventLogA(el, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, found, buf, sizeof(buf), &read, &need) &&
          !strcmp((char *)(buf + sizeof(EVENTLOGRECORD)), "NovaEventTest") &&
          !strncmp((char *)buf + ((EVENTLOGRECORD *)buf)->StringOffset, "NovaOS event log test", 21));
    swprintf(path, MAX_PATH, L"%lsevtlogtest-backup.evt", tmp);
    DeleteFileW(path);
    CHECK("BackupEventLogW", BackupEventLogW(el, path));
    HANDLE bk = OpenBackupEventLogW(NULL, path);
    DWORD bcount = 0;
    CHECK("OpenBackupEventLogW reads the copy", bk && GetNumberOfEventLogRecords(bk, &bcount) && bcount >= count);
    if (bk) CloseEventLog(bk);
    CloseEventLog(el);

    /* the Windows Event Log API */
    HANDLE ev = NULL;
    CHECK("EvtQuery finds the event",
          newest(L"Application", L"*[System[Provider[@Name='NovaEventTest'] and (EventID=4321)]]", xml, 16384, &ev));
    CHECK("its XML has its text", wcsstr(xml, text) != NULL);
    CHECK("its XML has its ID, level, task and data", wcsstr(xml, L"<EventID Qualifiers='0'>4321</EventID>") &&
          wcsstr(xml, L"<Level>4</Level>") && wcsstr(xml, L"<Task>3</Task>") && wcsstr(xml, L"<Binary>01020304</Binary>"));
    wprintf(L"evtlogtest: %ls\n", xml);
    HANDLE sys = EvtCreateRenderContext(0, NULL, 1);
    DWORD used, n;
    EVT_VARIANT *v = (EVT_VARIANT *)buf;
    CHECK("EvtRender's system values", ev && sys && EvtRender(sys, ev, 0, sizeof(buf), buf, &used, &n) && n == 18 &&
          v[0].type == 1 && !wcscmp((WCHAR *)(ULONG_PTR)v[0].v, SOURCE) && v[2].v == 4321 && v[4].v == 4 && v[9].v == found);
    CHECK("EvtFormatMessage: no message file for this source",
          ev && !message(ev, msg, 4096) && GetLastError() == 15027 /* ERROR_EVT_MESSAGE_NOT_FOUND */);
    if (ev) EvtClose(ev);
    if (sys) EvtClose(sys);
    SetLastError(0);
    CHECK("a bad XPath is refused", !EvtQuery(NULL, L"Application", L"*[System[Nonsense=1]]", 1) && GetLastError() == 15001);

    /* the System log: the boot, the service's start */
    CHECK("System has the boot (Kernel-General 12)",
          newest(L"System", L"*[System[Provider[@Name='Microsoft-Windows-Kernel-General'] and EventID=12]]", xml, 16384, &ev));
    if (ev) {
        CHECK("its message", message(ev, msg, 4096) && wcsstr(msg, L"The operating system started at system time"));
        wprintf(L"evtlogtest: System: %ls\n", msg);
        EvtClose(ev);
        ev = NULL;
    }
    CHECK("System has the service's 6005",
          newest(L"System", L"*[System[Provider[@Name='EventLog'] and EventID=6005]]", xml, 16384, NULL));
    CHECK("System has the SCM's 7036 for it",
          newest(L"System", L"*[System[Provider[@Name='Service Control Manager'] and EventID=7036]]", xml, 16384, NULL));

    /* a subscription sees a new event */
    HANDLE signal = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE sub = EvtSubscribe(NULL, signal, L"Application", L"*[System[Provider[@Name='NovaEventTest']]]", NULL, NULL, NULL, 1);
    CHECK("EvtSubscribe", sub != NULL);
    src = RegisterEventSourceW(NULL, SOURCE);
    LPCWSTR later[1] = { L"for the subscription" };
    ReportEventW(src, EVENTLOG_INFORMATION_TYPE, 0, 4323, NULL, 1, 0, later, NULL);
    DeregisterEventSource(src);
    BOOL delivered = FALSE;
    if (sub && WaitForSingleObject(signal, 10000) == WAIT_OBJECT_0) {
        HANDLE got[4];
        while (EvtNext(sub, 4, got, 0, 0, &n))
            for (DWORD i = 0; i < n; i++) {
                if (EvtRender(NULL, got[i], 1, sizeof(xml), xml, &used, &count) && wcsstr(xml, L"for the subscription")) delivered = TRUE;
                EvtClose(got[i]);
            }
    }
    CHECK("the subscription delivers the new event", delivered);
    if (sub) EvtClose(sub);
    CloseHandle(signal);

    /* a driver that does not load */
    SC_HANDLE drv = scm ? CreateServiceW(scm, L"NovaEvtMissing", L"NovaEvtMissing", SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                                         SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                                         L"\\SystemRoot\\System32\\drivers\\novaevt-missing.sys", NULL, NULL, NULL, NULL, NULL) : NULL;
    CHECK("CreateService (a driver whose file is missing)", drv != NULL);
    CHECK("System has the SCM's 7045 for it",
          newest(L"System", L"*[System[Provider[@Name='Service Control Manager'] and EventID=7045]]", xml, 16384, NULL) &&
          wcsstr(xml, L"NovaEvtMissing"));
    CHECK("StartService fails", drv && !StartServiceW(drv, 0, NULL));
    CHECK("System has the SCM's 7000 for it",
          newest(L"System", L"*[System[Provider[@Name='Service Control Manager'] and EventID=7000]]", xml, 16384, &ev) &&
          wcsstr(xml, L"NovaEvtMissing"));
    if (ev) {
        CHECK("its message", message(ev, msg, 4096) && wcsstr(msg, L"The NovaEvtMissing service failed to start"));
        wprintf(L"evtlogtest: System: %ls\n", msg);
        EvtClose(ev);
        ev = NULL;
    }
    if (drv) { DeleteService(drv); CloseServiceHandle(drv); }
    if (scm) CloseServiceHandle(scm);

    /* a program's crash */
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    WCHAR cmd[] = L"C:\\Programs\\crash.exe";
    BOOL started = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    CHECK("crash.exe starts", started);
    if (started) {
        WaitForSingleObject(pi.hProcess, 20000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CHECK("Application has Application Error 1000 for it",
          newest(L"Application", L"*[System[Provider[@Name='Application Error'] and EventID=1000]]", xml, 16384, &ev) &&
          wcsstr(xml, L"<Data>crash.exe</Data>") && wcsstr(xml, L"<Data>c0000005</Data>"));
    if (ev) {
        CHECK("its message", message(ev, msg, 4096) && wcsstr(msg, L"Faulting application name: crash.exe"));
        wprintf(L"evtlogtest: Application: %ls\n", msg);
        EvtClose(ev);
    }

    /* the logs, and an export */
    HANDLE ch = EvtOpenChannelEnum(NULL, 0);
    int logs = 0;
    WCHAR name[64];
    while (ch && EvtNextChannelPath(ch, 64, name, &used))
        logs += !_wcsicmp(name, L"Application") || !_wcsicmp(name, L"System") || !_wcsicmp(name, L"Security");
    if (ch) EvtClose(ch);
    CHECK("EvtOpenChannelEnum lists Application, Security and System", logs == 3);
    swprintf(path, MAX_PATH, L"%lsevtlogtest.evtx", tmp);
    CHECK("EvtExportLog", EvtExportLog(NULL, L"Application", L"*", path, 0x1 | 0x2000));
    char head[8] = { 0 };
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD got = 0;
    if (f != INVALID_HANDLE_VALUE) { ReadFile(f, head, 8, &got, NULL); CloseHandle(f); }
    CHECK("the .evtx starts with its signature", got == 8 && !memcmp(head, "ElfFile", 8));

    printf("evtlogtest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
