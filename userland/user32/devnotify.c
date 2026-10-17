/*
 * devnotify.c — WM_DEVICECHANGE: devices arriving and leaving
 *
 * The kernel's Plug and Play device tree (novapnp.h) records each arrival
 * and removal as an event.  A thread of this process follows the events
 * once there is a reason to: a RegisterDeviceNotification, or the
 * desktop's WM_DEVICECHANGE to one of the process's windows (msg.c), which
 * carries the event the change started after.  For each event it sends
 * what Windows sends:
 *   - DBT_DEVICEQUERYREMOVE, DBT_DEVICEQUERYREMOVEFAILED,
 *     DBT_DEVICEREMOVEPENDING, DBT_DEVICEARRIVAL and
 *     DBT_DEVICEREMOVECOMPLETE with a DEV_BROADCAST_HANDLE to windows and
 *     services registered for an open device handle;
 *   - DBT_DEVICEARRIVAL / DBT_DEVICEREMOVECOMPLETE with a
 *     DEV_BROADCAST_DEVICEINTERFACE to the windows registered for the
 *     device's interface class (or for all of them), and to a service
 *     registered with its status handle as SERVICE_CONTROL_DEVICEEVENT
 *     (advapi32's NovaServiceDeviceEvent);
 *   - DBT_DEVICEARRIVAL / DBT_DEVICEREMOVECOMPLETE with a
 *     DEV_BROADCAST_VOLUME when drive letters came or went, and then
 *     DBT_DEVNODES_CHANGED, to every top-level window of the process,
 *     hidden ones included.
 * Messages go with SendMessageTimeout (a window that does not answer in
 * two seconds is passed over), as Windows' broadcasts do.
 * Query-remove notices are asynchronous status notifications; they do not
 * delay the kernel query or collect a window's veto response.
 */
#include "u32.h"
#include <novapnp.h>

#include <dbt.h>

#define SERVICE_CONTROL_DEVICEEVENT 0x0000000B
#define MAX_REGS  64
#define REG_MAGIC 0x444E5452u                    /* "RTND" */

typedef struct { DWORD size, type, reserved; } BroadcastHdr;
typedef struct { DWORD size, type, reserved; GUID guid; WCHAR name[256]; } BroadcastIfaceW;
typedef struct { DWORD size, type, reserved; GUID guid; char name[256]; } BroadcastIfaceA;
typedef struct { DWORD size, type, reserved, unitmask; WORD flags; } BroadcastVolume;
typedef struct {
    DWORD size, type, reserved;
    HANDLE handle, notification;
    GUID event;
    LONG nameoffset;
    BYTE data[256];
} BroadcastHandle;

typedef struct {
    DWORD magic;
    HANDLE target;                   /* a window, or a service's status handle */
    BOOL service, all, wide;
    GUID guid;
    DWORD node;
    HANDLE device;
    HANDLE registration;
    LONG nameoffset;
    DWORD data_size;
    BYTE data[256];
} Reg;

static CRITICAL_SECTION g_lock;
static volatile LONG g_lock_ready, g_thread;
static Reg *g_reg[MAX_REGS];
static DWORD g_after;                /* events up to this one are delivered */
static DWORD g_drives;               /* the drive letters last announced */

static void lock(void)
{
    if (InterlockedCompareExchange(&g_lock_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
        InterlockedExchange(&g_lock_ready, 2);
    }
    while (g_lock_ready != 2) Sleep(0);
    EnterCriticalSection(&g_lock);
}
static void unlock(void) { LeaveCriticalSection(&g_lock); }

static LRESULT send(HWND h, WPARAM wp, LPARAM lp)
{
    DWORD_PTR r = 0;
    SendMessageTimeoutW(h, WM_DEVICECHANGE, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &r);
    return (LRESULT)r;
}

typedef DWORD (WINAPI *service_event_fn)(HANDLE status, DWORD type, PVOID data);

/* An interface of @n arrived or left: tell who registered for it */
static void interface_event(const NovaPnpNode *n, const GUID *g, WPARAM what)
{
    BroadcastIfaceW w;
    memset(&w, 0, sizeof(w));
    w.type = DBT_DEVTYP_DEVICEINTERFACE;
    w.guid = *g;
    nova_pnp_iface_path(n, g, w.name, 256);
    w.size = (DWORD)(__builtin_offsetof(BroadcastIfaceW, name) + (wcslen(w.name) + 1) * sizeof(WCHAR));
    BroadcastIfaceA a;
    memset(&a, 0, sizeof(a));
    a.type = DBT_DEVTYP_DEVICEINTERFACE;
    a.guid = *g;
    for (int i = 0; i < 255 && w.name[i]; i++) a.name[i] = (char)w.name[i];
    a.size = (DWORD)(__builtin_offsetof(BroadcastIfaceA, name) + strlen(a.name) + 1);
    Reg sent[MAX_REGS];
    int k = 0;
    lock();
    for (int i = 0; i < MAX_REGS; i++)
        if (g_reg[i] && (g_reg[i]->all || !memcmp(&g_reg[i]->guid, g, sizeof(GUID)))) sent[k++] = *g_reg[i];
    unlock();
    for (int i = 0; i < k; i++) {
        if (sent[i].service) {
            service_event_fn fn = (service_event_fn)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "NovaServiceDeviceEvent");
            if (fn) fn(sent[i].target, (DWORD)what, &w);
        } else {
            send((HWND)sent[i].target, what, sent[i].wide ? (LPARAM)&w : (LPARAM)&a);
        }
    }
}

/* A registered file handle belongs to this device node. */
static void handle_event(DWORD node, WPARAM what)
{
    Reg sent[MAX_REGS];
    int k = 0;
    lock();
    for (int i = 0; i < MAX_REGS; i++)
        if (g_reg[i] && g_reg[i]->node == node) {
            sent[k] = *g_reg[i];
            sent[k++].registration = (HANDLE)g_reg[i];
        }
    unlock();
    for (int i = 0; i < k; i++) {
        BroadcastHandle b;
        memset(&b, 0, sizeof(b));
        b.size = (DWORD)(__builtin_offsetof(BroadcastHandle, data) + sent[i].data_size);
        b.type = DBT_DEVTYP_HANDLE;
        b.handle = sent[i].device;
        b.notification = sent[i].registration;
        b.event = sent[i].guid;
        b.nameoffset = sent[i].nameoffset;
        if (sent[i].data_size) memcpy(b.data, sent[i].data, sent[i].data_size);
        if (sent[i].service) {
            service_event_fn fn = (service_event_fn)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "NovaServiceDeviceEvent");
            if (fn) fn(sent[i].target, (DWORD)what, &b);
        } else {
            send((HWND)sent[i].target, what, (LPARAM)&b);
        }
    }
}

static BOOL CALLBACK collect(HWND h, LPARAM lp)
{
    HWND *list = (HWND *)lp;
    Wnd *w = W_quiet(h);
    if (!w || (w->flags & WF_MESSAGE_ONLY)) return TRUE;   /* (message-only windows get no broadcasts) */
    for (int i = 0; i < 255; i++) if (!list[i]) { list[i] = h; break; }
    return TRUE;
}

/* Drive letters that came or went, then DBT_DEVNODES_CHANGED, to every
 * top-level window (not message-only ones) */
static void broadcast(void)
{
    static HWND list[256];
    memset(list, 0, sizeof(list));
    EnumWindows(collect, (LPARAM)list);
    DWORD now = GetLogicalDrives(), was = g_drives;
    g_drives = now;
    BroadcastVolume v;
    for (int pass = 0; pass < 2; pass++) {
        DWORD mask = pass ? now & ~was : was & ~now;
        if (!mask) continue;
        memset(&v, 0, sizeof(v));
        v.size = sizeof(v);
        v.type = DBT_DEVTYP_VOLUME;
        v.unitmask = mask;
        for (int i = 0; i < 256 && list[i]; i++)
            send(list[i], pass ? DBT_DEVICEARRIVAL : DBT_DEVICEREMOVECOMPLETE, (LPARAM)&v);
    }
    for (int i = 0; i < 256 && list[i]; i++) send(list[i], DBT_DEVNODES_CHANGED, 0);
}

static DWORD WINAPI follower(void *arg)
{
    (void)arg;
    static NovaPnpRead r;
    for (;;) {
        memset(&r, 0, sizeof(r));
        r.after = g_after;
        r.wait_ms = 1000;
        r.max = 32;
        int n = nova_pnp_events(&r);
        if (!n) {
            if (r.newest < g_after) g_after = r.newest;        /* (not to wait past the kernel's newest) */
            continue;
        }
        BOOL changed = FALSE;
        for (int i = 0; i < n; i++) {
            NovaPnpNode node;
            if (!nova_pnp_node(r.e[i].id, &node)) continue;
            GUID g[4];
            int k = nova_pnp_interfaces(&node, g);
            WPARAM what;
            switch (r.e[i].kind) {
            case NOVA_PNP_ARRIVAL: what = DBT_DEVICEARRIVAL; changed = TRUE; break;
            case NOVA_PNP_REMOVAL: what = DBT_DEVICEREMOVECOMPLETE; changed = TRUE; break;
            case NOVA_PNP_QUERY_REMOVE: what = DBT_DEVICEQUERYREMOVE; break;
            case NOVA_PNP_QUERY_REMOVE_FAILED: what = DBT_DEVICEQUERYREMOVEFAILED; break;
            case NOVA_PNP_REMOVE_PENDING: what = DBT_DEVICEREMOVEPENDING; break;
            default: continue;
            }
            if (r.e[i].kind == NOVA_PNP_ARRIVAL || r.e[i].kind == NOVA_PNP_REMOVAL)
                for (int j = 0; j < k; j++) interface_event(&node, &g[j], what);
            handle_event(node.id, what);
        }
        g_after = r.e[n - 1].seq;
        if (n < 32 && changed) broadcast();   /* (after the batch: one DBT_DEVNODES_CHANGED for it) */
    }
    return 0;
}

/* Follow the events after @after (if not already) */
static void follow(DWORD after)
{
    if (InterlockedCompareExchange(&g_thread, 1, 0)) return;
    g_after = after;
    HANDLE t = CreateThread(NULL, 0, follower, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

/* At start-up: the drive letters there are (a stick's letter comes before
 * its device's event, so the letters must be known from before) */
void devnotify_init(void) { g_drives = GetLogicalDrives(); }

/* msg.c: the desktop's WM_DEVICECHANGE (devices changed after event @after) */
void devnotify_kernel(DWORD after) { follow(after); }

static HANDLE reg(HANDLE target, LPVOID filter, DWORD flags, BOOL wide)
{
    const BroadcastHdr *h = filter;
    if (!target || !h || h->size < sizeof(BroadcastHdr)) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    if (!(flags & DEVICE_NOTIFY_SERVICE_HANDLE) && !IsWindow((HWND)target)) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return NULL; }
    if (h->type != DBT_DEVTYP_DEVICEINTERFACE && h->type != DBT_DEVTYP_HANDLE) { SetLastError(13 /* ERROR_INVALID_DATA */); return NULL; }
    if (h->type == DBT_DEVTYP_HANDLE &&
        (h->size < __builtin_offsetof(BroadcastHandle, data) + 1 ||
         h->size > __builtin_offsetof(BroadcastHandle, data) + sizeof(((BroadcastHandle *)0)->data))) {
        SetLastError(13 /* ERROR_INVALID_DATA */);
        return NULL;
    }
    Reg *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Reg));
    if (!r) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    r->magic = REG_MAGIC;
    r->target = target;
    r->service = (flags & DEVICE_NOTIFY_SERVICE_HANDLE) != 0;
    r->wide = wide;
    if (h->type == DBT_DEVTYP_DEVICEINTERFACE) {
        r->all = (flags & DEVICE_NOTIFY_ALL_INTERFACE_CLASSES) != 0;
        if (h->size >= __builtin_offsetof(BroadcastIfaceW, name)) r->guid = ((const BroadcastIfaceW *)h)->guid;
    } else {
        const BroadcastHandle *b = (const BroadcastHandle *)h;
        r->device = b->handle;
        r->node = nova_pnp_handle_node(r->device);
        if (!r->node) {
            HeapFree(GetProcessHeap(), 0, r);
            SetLastError(ERROR_INVALID_HANDLE);
            return NULL;
        }
        r->guid = b->event;
        r->nameoffset = b->nameoffset;
        r->data_size = h->size - (DWORD)__builtin_offsetof(BroadcastHandle, data);
        memcpy(r->data, b->data, r->data_size);
    }
    lock();
    int i = 0;
    while (i < MAX_REGS && g_reg[i]) i++;
    if (i < MAX_REGS) g_reg[i] = r;
    unlock();
    if (i == MAX_REGS) { HeapFree(GetProcessHeap(), 0, r); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    NovaPnpRead q;
    memset(&q, 0, sizeof(q));
    nova_pnp_events(&q);                           /* (max 0: just the newest) */
    follow(q.newest);
    return r;
}

USERAPI HANDLE RegisterDeviceNotificationW(HANDLE target, LPVOID filter, DWORD flags) { return reg(target, filter, flags, TRUE); }
USERAPI HANDLE RegisterDeviceNotificationA(HANDLE target, LPVOID filter, DWORD flags) { return reg(target, filter, flags, FALSE); }

USERAPI BOOL UnregisterDeviceNotification(HANDLE h)
{
    Reg *r = h;
    BOOL found = FALSE;
    lock();
    for (int i = 0; i < MAX_REGS; i++) if (g_reg[i] && g_reg[i] == r) { g_reg[i] = NULL; found = TRUE; }
    unlock();
    if (!found) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    r->magic = 0;
    HeapFree(GetProcessHeap(), 0, r);
    return TRUE;
}
