/*
 * notify.c — CM_Register_Notification: callbacks as devices arrive and leave
 *
 * A thread follows the kernel's Plug and Play events (novapnp.h) once a
 * registration exists and calls each registration's callback for what its
 * filter asks: a device interface class (or all of them) gets
 * CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL / REMOVAL with the interface's
 * symbolic link; a device instance ID (or all of them) gets
 * DEVICEINSTANCEENUMERATED and DEVICEINSTANCESTARTED on arrival and
 * DEVICEINSTANCEREMOVED on removal.  A file handle's device
 * (CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE) is registered but gets no events.
 * Callbacks run on that thread, one at a time; CM_Unregister_Notification
 * waits for a callback in progress to return (unless called from one).
 */
#include <windows.h>
#include <novapnp.h>
#include <string.h>

#define CFGMGR32 __declspec(dllexport)
typedef DWORD CONFIGRET;
#define CR_SUCCESS            0x00
#define CR_OUT_OF_MEMORY      0x02
#define CR_INVALID_POINTER    0x03
#define CR_INVALID_DATA       0x1F
#define CR_FAILURE            0x13

#define FILTER_DEVICEINTERFACE 0
#define FILTER_DEVICEHANDLE    1
#define FILTER_DEVICEINSTANCE  2
#define FLAG_ALL_INTERFACE_CLASSES 1
#define FLAG_ALL_DEVICE_INSTANCES  2
enum { ACT_IF_ARRIVAL = 0, ACT_IF_REMOVAL = 1, ACT_INST_ENUMERATED = 6, ACT_INST_STARTED = 7, ACT_INST_REMOVED = 8 };

typedef struct {
    DWORD cbSize, Flags, FilterType, Reserved;
    union {
        struct { GUID ClassGuid; } DeviceInterface;
        struct { HANDLE hTarget; } DeviceHandle;
        struct { WCHAR InstanceId[200]; } DeviceInstance;
    } u;
} Filter;

typedef DWORD (CALLBACK *Callback)(HANDLE notify, PVOID ctx, DWORD action, PVOID data, DWORD size);

#define MAX_REGS 64
#define REG_MAGIC 0x4E4D4352u
typedef struct {
    DWORD magic;
    Filter f;
    PVOID ctx;
    Callback cb;
} Reg;

static CRITICAL_SECTION g_lock, g_call;        /* the list; a callback running */
static volatile LONG g_ready, g_thread;
static DWORD g_calling_tid;
static Reg *g_reg[MAX_REGS];

static void init(void)
{
    if (InterlockedCompareExchange(&g_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
        InitializeCriticalSection(&g_call);
        InterlockedExchange(&g_ready, 2);
    }
    while (g_ready != 2) Sleep(0);
}

/* Call @r's callback if it is still registered */
static void call(Reg *r, DWORD action, void *data, DWORD size)
{
    EnterCriticalSection(&g_call);
    EnterCriticalSection(&g_lock);
    BOOL live = FALSE;
    for (int i = 0; i < MAX_REGS; i++) if (g_reg[i] == r) live = TRUE;
    LeaveCriticalSection(&g_lock);
    if (live) {
        g_calling_tid = GetCurrentThreadId();
        r->cb(r, r->ctx, action, data, size);
        g_calling_tid = 0;
    }
    LeaveCriticalSection(&g_call);
}

static int wide_ieq(const WCHAR *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        WCHAR x = *a; char y = *b;
        if (x >= 'a' && x <= 'z') x = (WCHAR)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != (WCHAR)(unsigned char)y) return 0;
    }
    return *a == 0 && *b == 0;
}

static void event(const NovaPnpNode *n, BOOL arrival)
{
    /* CM_NOTIFY_EVENT_DATA: FilterType, Reserved, then the union */
    static BYTE buf[8 + 16 + 512];
    Reg *list[MAX_REGS];
    int k = 0;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_REGS; i++) if (g_reg[i]) list[k++] = g_reg[i];
    LeaveCriticalSection(&g_lock);
    GUID g[4];
    int ng = nova_pnp_interfaces(n, g);
    for (int i = 0; i < k; i++) {
        Reg *r = list[i];
        DWORD *hdr = (DWORD *)buf;
        if (r->f.FilterType == FILTER_DEVICEINTERFACE) {
            for (int j = 0; j < ng; j++) {
                if (!(r->f.Flags & FLAG_ALL_INTERFACE_CLASSES) && memcmp(&r->f.u.DeviceInterface.ClassGuid, &g[j], sizeof(GUID)))
                    continue;
                memset(buf, 0, sizeof(buf));
                hdr[0] = FILTER_DEVICEINTERFACE;
                memcpy(buf + 8, &g[j], sizeof(GUID));
                WCHAR *link = (WCHAR *)(buf + 24);
                nova_pnp_iface_path(n, &g[j], link, 256);
                DWORD len = (DWORD)lstrlenW(link);
                call(r, arrival ? ACT_IF_ARRIVAL : ACT_IF_REMOVAL, buf, 24 + (len + 1) * sizeof(WCHAR));
            }
        } else if (r->f.FilterType == FILTER_DEVICEINSTANCE) {
            if (!(r->f.Flags & FLAG_ALL_DEVICE_INSTANCES) && !wide_ieq(r->f.u.DeviceInstance.InstanceId, n->instance)) continue;
            memset(buf, 0, sizeof(buf));
            hdr[0] = FILTER_DEVICEINSTANCE;
            WCHAR *id = (WCHAR *)(buf + 8);
            int len = 0;
            for (; n->instance[len] && len < 199; len++) id[len] = (WCHAR)(unsigned char)n->instance[len];
            DWORD size = 8 + (DWORD)(len + 1) * sizeof(WCHAR);
            if (arrival) {
                call(r, ACT_INST_ENUMERATED, buf, size);
                if (n->flags & NOVA_PNP_STARTED) call(r, ACT_INST_STARTED, buf, size);
            } else {
                call(r, ACT_INST_REMOVED, buf, size);
            }
        }
    }
}

static DWORD WINAPI follower(void *arg)
{
    DWORD after = (DWORD)(ULONG_PTR)arg;
    static NovaPnpRead r;
    for (;;) {
        memset(&r, 0, sizeof(r));
        r.after = after;
        r.wait_ms = 1000;
        r.max = 32;
        int n = nova_pnp_events(&r);
        for (int i = 0; i < n; i++) {
            NovaPnpNode node;
            if ((r.e[i].kind == NOVA_PNP_ARRIVAL || r.e[i].kind == NOVA_PNP_REMOVAL) &&
                nova_pnp_node(r.e[i].id, &node))
                event(&node, r.e[i].kind == NOVA_PNP_ARRIVAL);
        }
        if (n) after = r.e[n - 1].seq;
    }
    return 0;
}

CFGMGR32 CONFIGRET WINAPI CM_Register_Notification(PVOID filter, PVOID ctx, PVOID callback, HANDLE *out)
{
    const Filter *f = filter;
    if (!f || !callback || !out) return CR_INVALID_POINTER;
    if (f->cbSize < __builtin_offsetof(Filter, u) || f->FilterType > FILTER_DEVICEINSTANCE) return CR_INVALID_DATA;
    init();
    Reg *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Reg));
    if (!r) return CR_OUT_OF_MEMORY;
    r->magic = REG_MAGIC;
    memcpy(&r->f, f, f->cbSize < sizeof(Filter) ? f->cbSize : sizeof(Filter));
    r->ctx = ctx;
    r->cb = (Callback)callback;
    EnterCriticalSection(&g_lock);
    int i = 0;
    while (i < MAX_REGS && g_reg[i]) i++;
    if (i < MAX_REGS) g_reg[i] = r;
    LeaveCriticalSection(&g_lock);
    if (i == MAX_REGS) { HeapFree(GetProcessHeap(), 0, r); return CR_OUT_OF_MEMORY; }
    if (!InterlockedCompareExchange(&g_thread, 1, 0)) {
        NovaPnpRead q;
        memset(&q, 0, sizeof(q));
        nova_pnp_events(&q);                          /* (max 0: the newest event) */
        HANDLE t = CreateThread(NULL, 0, follower, (PVOID)(ULONG_PTR)q.newest, 0, NULL);
        if (t) CloseHandle(t);
    }
    *out = r;
    return CR_SUCCESS;
}

CFGMGR32 CONFIGRET WINAPI CM_Unregister_Notification(HANDLE h)
{
    Reg *r = h;
    if (!r || !g_ready) return CR_INVALID_POINTER;
    init();
    BOOL found = FALSE;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_REGS; i++) if (g_reg[i] == r) { g_reg[i] = NULL; found = TRUE; }
    LeaveCriticalSection(&g_lock);
    if (!found) return CR_INVALID_DATA;
    if (g_calling_tid != GetCurrentThreadId()) {              /* (a callback running finishes first) */
        EnterCriticalSection(&g_call);
        LeaveCriticalSection(&g_call);
        r->magic = 0;
        HeapFree(GetProcessHeap(), 0, r);
    }
    return CR_SUCCESS;
}
