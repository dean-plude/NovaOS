/*
 * um_eventlog.c — events on their way to the event log
 *
 * Windows programs report events to the EventLog service, which keeps the
 * logs on disk (C:\Windows\System32\winevt\Logs).  Here the kernel is the
 * post box between them: advapi32's ReportEvent hands it a finished
 * EVENTLOGRECORD and the log's name (NtNovaGuiCtl's CTL_EVENTLOG, op 0),
 * the kernel's own events (the boot, a driver that does not load, a
 * program's crash) are made here by EvLogReport, and the service
 * (userland/programs/eventlog.c) fetches them (op 1), writes them to the
 * log's file and says how far it got (op 2).  An event stays here until
 * then, so the ones from before the service starts (the boot's) are kept
 * and none is lost if the service is restarted.  A reader (ReadEventLog,
 * EvtQuery) first waits for what was reported before it to be written
 * (op 3).  At most EV_MAX_BYTES wait: past that the oldest are dropped.
 */
#include "um_internal.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../ke/scheduler.h"
#include "../ke/probe.h"
#include "../hal/rtc.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

#define EV_MAX_RECORD  0x10000              /* one event (Windows' limit is a little under 64 KB) */
#define EV_MAX_BYTES   (2u << 20)
#define EV_LOG_CHARS   32
#define EV_ALIVE_TICKS 1000                 /* a service that fetched in the last 10 s is running */

typedef struct EvItem {
    struct EvItem *next;
    UINT32 seq, size;
    UINT16 log[EV_LOG_CHARS];
    UINT8  rec[];
} EvItem;

static KSpinLock g_lock = KSPINLOCK_INIT;
static EvItem *g_head, *g_tail;
static UINT32 g_seq, g_done, g_bytes, g_dropped;
static UINT64 g_fetched;                    /* tick of the service's last fetch (0: never) */

static void queue(EvItem *e)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    e->seq = ++g_seq;
    e->next = NULL;
    if (g_tail) g_tail->next = e; else g_head = e;
    g_tail = e;
    g_bytes += e->size;
    EvItem *drop = NULL;
    while (g_bytes > EV_MAX_BYTES && g_head && g_head != e) {      /* (nobody writes them down) */
        EvItem *o = g_head;
        g_head = o->next;
        g_bytes -= o->size;
        if (g_done < o->seq) g_done = o->seq;
        o->next = drop;
        drop = o;
        g_dropped++;
    }
    spin_unlock_irqrestore(&g_lock, s);
    while (drop) { EvItem *n = drop->next; kfree(drop); drop = n; }
}

/* ---- the kernel's own events ---- */

/* Seconds since 1970 for a UTC date (days_from_civil, H. Hinnant) */
static UINT32 unix_time(const RtcTime *t)
{
    int y = t->year - (t->month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (t->month + (t->month > 2 ? -3 : 9)) + 2) / 5 + t->day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    INT64 days = (INT64)era * 146097 + (INT64)doe - 719468;
    return (UINT32)(days * 86400 + t->hour * 3600 + t->minute * 60 + t->second);
}

static UINT32 put_ascii(UINT8 *at, const char *s)
{
    UINT32 n = 0;
    for (; s && s[n]; n++) { at[2 * n] = (UINT8)s[n]; at[2 * n + 1] = 0; }
    at[2 * n] = at[2 * n + 1] = 0;
    return 2 * (n + 1);
}

static void put32(UINT8 *p, UINT32 v) { memcpy(p, &v, 4); }
static void put16(UINT8 *p, UINT16 v) { memcpy(p, &v, 2); }

/* An event from the kernel: an EVENTLOGRECORD from @source with event ID
 * @id (its severity in the top bits, as a message file's IDs have them)
 * and @n strings, for the log @log ("System", "Application") */
void EvLogReport(const char *log, UINT16 type, const char *source, UINT32 id, int n, const char *const *strings)
{
    char computer[64] = "NOVA-PC";
    um_registry_get_sz("Machine\\SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName", "ComputerName",
                       computer, sizeof(computer));
    UINT32 size = 56 + 2 * ((UINT32)strlen(source) + 1) + 2 * ((UINT32)strlen(computer) + 1);
    size = (size + 3) & ~3u;
    UINT32 strings_at = size;
    for (int i = 0; i < n; i++) size += 2 * ((UINT32)strlen(strings[i]) + 1);
    size = ((size + 3) & ~3u) + 4;
    if (size > EV_MAX_RECORD) return;
    EvItem *e = kzalloc(sizeof(EvItem) + size);
    if (!e) return;
    e->size = size;
    for (int i = 0; log[i] && i < EV_LOG_CHARS - 1; i++) e->log[i] = (UINT8)log[i];
    UINT8 *r = e->rec;
    RtcTime t;
    rtc_read(&t);
    UINT32 now = unix_time(&t);
    put32(r + 0, size);
    put32(r + 4, 0x654C664C);                       /* "LfLe" */
    put32(r + 12, now);                             /* TimeGenerated (the service sets TimeWritten) */
    put32(r + 16, now);
    put32(r + 20, id);
    put16(r + 24, type);
    put16(r + 26, (UINT16)n);
    put32(r + 36, strings_at);                      /* StringOffset */
    put32(r + 44, strings_at);                      /* UserSidOffset (no SID) */
    put32(r + 52, strings_at);                      /* DataOffset (no data) */
    UINT32 at = 56;
    at += put_ascii(r + at, source);
    put_ascii(r + at, computer);
    at = strings_at;
    for (int i = 0; i < n; i++) at += put_ascii(r + at, strings[i]);
    put32(r + 52, at);
    put32(r + size - 4, size);
    queue(e);
}

/* The boot, in the System log: Kernel-General's event 12 */
void EvLogBoot(void)
{
    RtcTime t;
    rtc_read(&t);
    char when[40];
    ksnprintf(when, sizeof(when), "%04u-%02u-%02uT%02u:%02u:%02u.000000000Z", t.year, t.month, t.day, t.hour, t.minute,
              t.second);
    const char *s[1] = { when };
    EvLogReport("System", 4, "Microsoft-Windows-Kernel-General", 12, 1, s);
}

/* A module's link time stamp (the PE header's TimeDateStamp) */
static UINT32 pe_stamp(UINT64 base)
{
    UINT32 lfanew = 0, stamp = 0;
    if (!base || !NT_SUCCESS(CopyFromUser(&lfanew, (const void *)(uintptr_t)(base + 0x3C), 4)) || lfanew > 0x1000 ||
        !NT_SUCCESS(CopyFromUser(&stamp, (const void *)(uintptr_t)(base + lfanew + 8), 4)))
        return 0;
    return stamp;
}

/* A program's crash, in the Application log: Application Error's event
 * 1000, with the strings Windows Error Reporting gives it */
void EvLogCrash(UmProcess *p, UINT32 status, UINT64 rip)
{
    const UmModule *exe = NULL, *at = um_module_at(p, rip);
    for (int i = 0; i < p->nmodules && !exe; i++)
        if (!p->modules[i].dll) exe = &p->modules[i];
    char app_ts[12], mod_ts[12], code[12], off[20], pid[12], start[20];
    ksnprintf(app_ts, sizeof(app_ts), "%08x", exe ? pe_stamp(exe->base) : 0);
    ksnprintf(mod_ts, sizeof(mod_ts), "%08x", at ? pe_stamp(at->base) : 0);
    ksnprintf(code, sizeof(code), "%08x", status);
    ksnprintf(off, sizeof(off), "%016llx", (unsigned long long)(at ? rip - at->base : rip));
    ksnprintf(pid, sizeof(pid), "%x", p->pid);
    ksnprintf(start, sizeof(start), "%016llx", (unsigned long long)p->create_time);
    const char *s[13] = { p->name, "0.0.0.0", app_ts, at ? at->name : "unknown", "0.0.0.0", mod_ts, code, off, pid, start,
                          exe ? exe->path : p->image_path, at ? at->path : "unknown", "" };
    EvLogReport("Application", 1, "Application Error", 1000, 13, s);
}

/* ---- CTL_EVENTLOG ---- */

/* op 0: ptr -> { size, log name (32 UTF-16 units) } then the record */
static UINT64 submit(UINT64 ptr)
{
    struct { UINT32 size; UINT16 log[EV_LOG_CHARS]; } h;
    if (!NT_SUCCESS(CopyFromUser(&h, (const void *)(uintptr_t)ptr, sizeof(h)))) return 0;
    if (h.size < 60 || h.size > EV_MAX_RECORD || (h.size & 3) || !h.log[0]) return 0;
    EvItem *e = kmalloc(sizeof(EvItem) + h.size);
    if (!e) return 0;
    e->size = h.size;
    memcpy(e->log, h.log, sizeof(e->log));
    e->log[EV_LOG_CHARS - 1] = 0;
    UINT32 len = 0, sig = 0;
    if (!NT_SUCCESS(CopyFromUser(e->rec, (const void *)(uintptr_t)(ptr + sizeof(h)), h.size)) ||
        (memcpy(&len, e->rec, 4), memcpy(&sig, e->rec + 4, 4), len != h.size || sig != 0x654C664C)) {
        kfree(e);
        return 0;
    }
    queue(e);
    return e->seq;
}

/* op 1: ptr <-> { after, wait ms (1 s at most), room; out: count, dropped;
 * 0 } then room bytes: { seq, size, log name } and the record, each after the
 * last; returns how many */
static UINT64 fetch(UINT64 ptr)
{
    UINT32 q[6];
    if (!NT_SUCCESS(CopyFromUser(q, (const void *)(uintptr_t)ptr, 3 * sizeof(UINT32)))) return 0;
    UINT32 after = q[0], room = q[2], wait = q[1] > 1000 ? 1000 : q[1];
    UINT64 end = sched_ticks() + (wait + 9) / 10;
    UINT8 *buf = room ? kmalloc(room) : NULL;
    if (!buf && room) return 0;
    UINT32 count = 0, used = 0;
    for (;;) {
        IrqState s = spin_lock_irqsave(&g_lock);
        g_fetched = sched_ticks() | 1;
        for (EvItem *e = g_head; e; e = e->next) {
            if (e->seq <= after) continue;
            UINT32 need = 8 + sizeof(e->log) + e->size;
            if (used + need > room) break;
            memcpy(buf + used, &e->seq, 4);
            memcpy(buf + used + 4, &e->size, 4);
            memcpy(buf + used + 8, e->log, sizeof(e->log));
            memcpy(buf + used + 8 + sizeof(e->log), e->rec, e->size);
            used += need;
            count++;
        }
        q[4] = g_dropped;
        spin_unlock_irqrestore(&g_lock, s);
        if (count || sched_ticks() >= end) break;
        sched_sleep_until(NULL, sched_ticks() + 1);
    }
    q[3] = count;
    bool ok = NT_SUCCESS(CopyToUser((void *)(uintptr_t)(ptr + 3 * sizeof(UINT32)), &q[3], 2 * sizeof(UINT32))) &&
              (!used || NT_SUCCESS(CopyToUser((void *)(uintptr_t)(ptr + sizeof(q)), buf, used)));
    kfree(buf);
    return ok ? count : 0;
}

/* op 2: the service wrote everything up to @seq */
static UINT64 commit(UINT32 seq)
{
    EvItem *drop = NULL;
    IrqState s = spin_lock_irqsave(&g_lock);
    if (seq > g_seq) seq = g_seq;
    while (g_head && g_head->seq <= seq) {
        EvItem *o = g_head;
        g_head = o->next;
        if (!g_head) g_tail = NULL;
        g_bytes -= o->size;
        o->next = drop;
        drop = o;
    }
    if (g_done < seq) g_done = seq;
    spin_unlock_irqrestore(&g_lock, s);
    while (drop) { EvItem *n = drop->next; kfree(drop); drop = n; }
    return 1;
}

/* op 3: wait up to @ms (3 s at most) for the events reported so far to
 * be written: 1 they are, 0 not yet, 2 no service is running */
static UINT64 sync(UINT32 ms)
{
    IrqState s = spin_lock_irqsave(&g_lock);
    UINT32 want = g_seq;
    spin_unlock_irqrestore(&g_lock, s);
    UINT64 end = sched_ticks() + ((ms > 3000 ? 3000 : ms) + 9) / 10;
    for (;;) {
        s = spin_lock_irqsave(&g_lock);
        bool done = g_done >= want, alive = g_fetched && sched_ticks() + 1 - g_fetched < EV_ALIVE_TICKS;   /* (g_fetched is odd: up to now + 1) */
        spin_unlock_irqrestore(&g_lock, s);
        if (done) return 1;
        if (!alive) return 2;
        if (sched_ticks() >= end) { kprintf("[EVENTLOG] the service has not written event %u yet\n", want); return 0; }
        sched_sleep_until(NULL, sched_ticks() + 1);
    }
}

UINT64 UmEventLogCtl(UINT64 arg, UINT64 ptr)
{
    switch (arg & 0xFF) {
    case 0: return submit(ptr);
    case 1: return fetch(ptr);
    case 2: return commit((UINT32)(arg >> 8));
    case 3: return sync((UINT32)(arg >> 8));
    }
    return 0;
}
