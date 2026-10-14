/*
 * novaevt.h — the event log as NovaOS keeps it
 *
 * The EventLog service (userland/programs/eventlog.c) keeps each log in
 * C:\Windows\System32\winevt\Logs\<log>.evt, in the classic .evt format:
 * an ELF_LOGFILE_HEADER, then the events as EVENTLOGRECORDs (what
 * ReadEventLog returns), oldest first, then an ELF_EOF_RECORD.  Events
 * reach it through the kernel (kernel/um/um_eventlog.c, NtNovaGuiCtl's
 * CTL_EVENTLOG): advapi32's ReportEvent queues them, the service writes
 * them down.  advapi32 (the classic API), wevtapi (the Evt* API), the
 * service and Event Viewer share this header.
 */
#pragma once
#include <windows.h>
#include <winternl.h>
#include <string.h>

#define NOVA_CTL_EVENTLOG 60
#define NOVA_EVT_LOG_DIR  L"C:\\Windows\\System32\\winevt\\Logs"
#define NOVA_EVT_KEY      L"SYSTEM\\CurrentControlSet\\Services\\EventLog"
#define NOVA_EVT_LOG_CHARS 32
#define NOVA_EVT_MAX_RECORD 0x10000

#ifndef EVENTLOG_SUCCESS
#define EVENTLOG_SUCCESS          0x0000
#define EVENTLOG_ERROR_TYPE       0x0001
#define EVENTLOG_WARNING_TYPE     0x0002
#define EVENTLOG_INFORMATION_TYPE 0x0004
#define EVENTLOG_AUDIT_SUCCESS    0x0008
#define EVENTLOG_AUDIT_FAILURE    0x0010
#define EVENTLOG_SEQUENTIAL_READ  0x0001
#define EVENTLOG_SEEK_READ        0x0002
#define EVENTLOG_FORWARDS_READ    0x0004
#define EVENTLOG_BACKWARDS_READ   0x0008
#define EVENTLOG_FULL_INFO        0
typedef struct _EVENTLOGRECORD {
    DWORD Length, Reserved, RecordNumber, TimeGenerated, TimeWritten, EventID;
    WORD  EventType, NumStrings, EventCategory, ReservedFlags;
    DWORD ClosingRecordNumber, StringOffset, UserSidLength, UserSidOffset, DataLength, DataOffset;
} EVENTLOGRECORD, *PEVENTLOGRECORD;
typedef struct _EVENTLOG_FULL_INFORMATION { DWORD dwFull; } EVENTLOG_FULL_INFORMATION;
#endif

#ifndef ERROR_EVENTLOG_FILE_CORRUPT
#define ERROR_EVENTLOG_FILE_CORRUPT 1500
#endif
#ifndef RPC_S_SERVER_UNAVAILABLE
#define RPC_S_SERVER_UNAVAILABLE 1722
#endif
#ifndef ERROR_INVALID_LEVEL
#define ERROR_INVALID_LEVEL 124
#endif
#ifndef MAX_COMPUTERNAME_LENGTH
#define MAX_COMPUTERNAME_LENGTH 15
#endif

#define NOVA_ELF_SIG 0x654C664C                     /* "LfLe" */

/* The file's header (ELF_LOGFILE_HEADER) and its end (ELF_EOF_RECORD) */
typedef struct {
    DWORD HeaderSize, Signature, MajorVersion, MinorVersion, StartOffset, EndOffset, CurrentRecordNumber,
          OldestRecordNumber, MaxSize, Flags, Retention, EndHeaderSize;
} NovaEvtHeader;
typedef struct {
    DWORD RecordSizeBeginning, One, Two, Three, Four, BeginRecord, EndRecord, CurrentRecordNumber,
          OldestRecordNumber, RecordSizeEnd;
} NovaEvtEof;

/* ---- the kernel's post box (kernel/um/um_eventlog.c) ---- */

/* Queue @rec (its Length bytes) for the log @log; returns its seq, 0 refused */
static inline DWORD nova_evt_submit(const WCHAR *log, const EVENTLOGRECORD *rec)
{
    BYTE *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 4 + 2 * NOVA_EVT_LOG_CHARS + rec->Length);
    if (!b) return 0;
    memcpy(b, &rec->Length, 4);
    for (int i = 0; log[i] && i < NOVA_EVT_LOG_CHARS - 1; i++) ((WCHAR *)(b + 4))[i] = log[i];
    memcpy(b + 4 + 2 * NOVA_EVT_LOG_CHARS, rec, rec->Length);
    DWORD seq = (DWORD)NtNovaGuiCtl(0, NOVA_CTL_EVENTLOG, 0, b);
    HeapFree(GetProcessHeap(), 0, b);
    return seq;
}

/* Wait up to @ms for what was queued so far to be in the files: 1 it is,
 * 0 not yet, 2 the EventLog service is not running */
static inline int nova_evt_sync(DWORD ms)
{
    return (int)NtNovaGuiCtl(0, NOVA_CTL_EVENTLOG, 3 | (ULONG_PTR)ms << 8, NULL);
}

/* ---- events ---- */

/* The Windows Event Log level of a classic event type */
static inline BYTE nova_evt_level(WORD type)
{
    return type == EVENTLOG_ERROR_TYPE ? 2 : type == EVENTLOG_WARNING_TYPE ? 3 :
           type == EVENTLOG_AUDIT_SUCCESS || type == EVENTLOG_AUDIT_FAILURE ? 0 : 4;
}

static inline const WCHAR *nova_evt_source(const EVENTLOGRECORD *r) { return (const WCHAR *)(r + 1); }
static inline const WCHAR *nova_evt_computer(const EVENTLOGRECORD *r)
{
    const WCHAR *s = nova_evt_source(r);
    return s + lstrlenW(s) + 1;
}

/* The messages of NovaOS's own event sources (the ones a message file
 * would give on Windows), by source and event ID (its low 16 bits) */
static const struct { const WCHAR *source; WORD id; const WCHAR *text; } nova_evt_messages[] = {
    { L"Service Control Manager", 7000, L"The %1 service failed to start due to the following error: %n%2" },
    { L"Service Control Manager", 7036, L"The %1 service entered the %2 state." },
    { L"Service Control Manager", 7045, L"A service was installed in the system.%n%nService Name:  %1%nService File Name:  %2%n"
                                        L"Service Type:  %3%nService Start Type:  %4%nService Account:  %5" },
    { L"EventLog", 6005, L"The Event log service was started." },
    { L"EventLog", 6006, L"The Event log service was stopped." },
    { L"EventLog", 6009, L"NovaOS %1, Windows NT %2 build %3." },
    { L"Microsoft-Windows-Eventlog", 104, L"The %1 log file was cleared." },
    { L"Microsoft-Windows-Eventlog", 1102, L"The audit log was cleared." },
    { L"Microsoft-Windows-Kernel-General", 12, L"The operating system started at system time %1." },
    { L"Application Error", 1000, L"Faulting application name: %1, version: %2, time stamp: 0x%3%nFaulting module name: %4, "
                                  L"version: %5, time stamp: 0x%6%nException code: 0x%7%nFault offset: 0x%8%n"
                                  L"Faulting process id: 0x%9%nFaulting application start time: 0x%10%n"
                                  L"Faulting application path: %11%nFaulting module path: %12%nReport Id: %13" },
};

static inline const WCHAR *nova_evt_builtin(const WCHAR *source, DWORD id)
{
    for (unsigned i = 0; i < sizeof(nova_evt_messages) / sizeof(nova_evt_messages[0]); i++)
        if (nova_evt_messages[i].id == (WORD)id && !lstrcmpiW(nova_evt_messages[i].source, source))
            return nova_evt_messages[i].text;
    return NULL;
}
