/*
 * taskmgr.exe — Task Manager, laid out as Windows 11's: Processes (apps
 * and background processes with their CPU, memory, disk and status; End
 * task), Performance (CPU per logical processor, memory, disk and
 * Ethernet over the last 60 seconds), Details (every process with its PID,
 * user, CPU time, threads, handles and priority) and Services.
 *
 * It reads what Windows' own Task Manager reads, through the public
 * interfaces, so other Windows tools get the same answers:
 * NtQuerySystemInformation's SystemProcessInformation (processes, their
 * threads, CPU time and memory), SystemProcessorPerformanceInformation
 * (each logical processor's idle and busy time) and
 * SystemPerformanceInformation (disk transfers), GlobalMemoryStatusEx,
 * GetIfTable2 (the network adapter's octets), the process token's user
 * (OpenProcessToken, LookupAccountSid), EnumWindows (which processes are
 * apps), EnumServicesStatusEx, and OpenProcess + TerminateProcess to end a
 * task.  It refreshes once a second.
 *
 * Ctrl+Shift+Esc opens it (kernel/wm/desktop.c), as does the Start
 * button's right-click menu and "Task Manager" in Start's search.  Keys:
 * Ctrl+Tab and Ctrl+Shift+Tab change view, the arrows, Home, End and the
 * page keys move the selection, typing a name selects the first process
 * it starts, Delete ends the selected task.  What it does goes to the
 * debug log (OutputDebugString: "taskmgr: ..."), which the OS test reads
 * (tests/selftest/core/166-taskmgr.py).
 */
#include <windows.h>
#include <winsvc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <winternl.h>

#ifndef PROCESS_TERMINATE
#define PROCESS_TERMINATE 0x0001
#endif
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif
#ifndef TOKEN_QUERY
#define TOKEN_QUERY 0x0008
#endif
#ifndef FW_SEMIBOLD
#define FW_SEMIBOLD 600
#endif
#ifndef DT_LEFT
#define DT_LEFT 0
#endif
#ifndef DT_VCENTER
#define DT_VCENTER 0x4
#endif
#ifndef DT_SINGLELINE
#define DT_SINGLELINE 0x20
#endif
#ifndef DT_NOPREFIX
#define DT_NOPREFIX 0x800
#endif
#ifndef PS_DOT
#define PS_DOT 2
#endif
#ifndef VK_ESCAPE
#define VK_ESCAPE 0x1B
#endif
#ifndef VK_F5
#define VK_F5 0x74
#endif
#ifndef SERVICE_WIN32
#define SERVICE_WIN32 0x30
#endif
#ifndef SERVICE_STATE_ALL
#define SERVICE_STATE_ALL 3
#endif

/* iphlpapi and advapi32 calls the headers here leave out */
DWORD WINAPI GetIfTable2(PVOID *table);
VOID WINAPI FreeMibTable(PVOID table);
typedef struct {
    DWORD dwServiceType, dwCurrentState, dwControlsAccepted, dwWin32ExitCode, dwServiceSpecificExitCode,
          dwCheckPoint, dwWaitHint, dwProcessId, dwServiceFlags;
} SvcStatusProcess;
typedef struct { LPWSTR lpServiceName, lpDisplayName; SvcStatusProcess ServiceStatusProcess; } EnumSvcProcess;
BOOL WINAPI EnumServicesStatusExW(SC_HANDLE scm, int level, DWORD type, DWORD state, LPBYTE buf, DWORD n, LPDWORD need,
                                  LPDWORD count, LPDWORD resume, LPCWSTR group);

/* -----------------------------------------------------------------------
 * Colours and fonts (Windows 11's light theme)
 * ----------------------------------------------------------------------- */
#define C_NAV      RGB(0xF3, 0xF3, 0xF3)
#define C_BG       RGB(0xFF, 0xFF, 0xFF)
#define C_TEXT     RGB(0x1A, 0x1A, 0x1A)
#define C_DIM      RGB(0x5F, 0x5F, 0x5F)
#define C_LINE     RGB(0xE5, 0xE5, 0xE5)
#define C_ACCENT   RGB(0x00, 0x5F, 0xB8)
#define C_SEL      RGB(0xCC, 0xE4, 0xF7)
#define C_HOVER    RGB(0xEA, 0xEA, 0xEA)
#define C_CPU      RGB(0x11, 0x7D, 0xBB)
#define C_MEM      RGB(0x8B, 0x12, 0xAE)
#define C_DISK     RGB(0x4D, 0xA6, 0x0C)
#define C_NET      RGB(0xA7, 0x4F, 0x01)

static HFONT g_font, g_bold, g_big, g_small;

/* -----------------------------------------------------------------------
 * What it shows
 * ----------------------------------------------------------------------- */
#define MAX_PROCS 256
#define MAX_CPUS  64
#define HIST      60                          /* samples: one a second */

typedef struct {
    DWORD pid, ppid;
    char name[64];
    ULONGLONG cpu, create;                    /* CPU time (100 ns), creation */
    double pct;                               /* CPU % over the last second */
    ULONGLONG ws;                             /* working set (bytes) */
    ULONG threads, handles;
    LONG prio;
    BOOL suspended, app;
    WCHAR user[48];
} Proc;

typedef struct {
    DWORD pid;
    WCHAR name[64], disp[96];
    DWORD state;
} Svc;

static Proc g_procs[MAX_PROCS];
static int g_nprocs;
static Svc *g_svcs;
static int g_nsvcs;

static struct {
    int ncpu;
    ULONGLONG idle[MAX_CPUS], total[MAX_CPUS];  /* the last sample's */
    double core[MAX_CPUS][HIST];              /* % busy, newest last */
    double cpu[HIST], mem[HIST], disk_r[HIST], disk_w[HIST], net_in[HIST], net_out[HIST];
    ULONGLONG rd, wr, in, out;                /* counters at the last sample */
    ULONGLONG mem_total, mem_avail, commit, commit_limit;
    ULONGLONG link_speed;
    WCHAR adapter[128];
    BOOL have_net;
    int samples;
    ULONGLONG wall;                           /* QueryPerformanceCounter at the last sample */
    ULONG threads, handles;
} g_perf;

static char g_cpu_name[64];

/* -----------------------------------------------------------------------
 * The window's state
 * ----------------------------------------------------------------------- */
enum { V_PROCESSES, V_PERFORMANCE, V_DETAILS, V_SERVICES, V_COUNT };
static const WCHAR *g_view_names[V_COUNT] = { L"Processes", L"Performance", L"Details", L"Services" };
enum { P_CPU, P_MEM, P_DISK, P_NET, P_COUNT };

static HWND g_wnd;
static int g_view, g_perf_page;
static DWORD g_sel_pid = (DWORD)-1;
static int g_sel_svc = -1;
static int g_scroll[V_COUNT];
static int g_sort[V_COUNT] = { 0, 0, 0, 0 }, g_sort_desc[V_COUNT];
static WCHAR g_find[32];
static DWORD g_find_at;
static BOOL g_listed;                         /* the first listing has been logged */

#define NAV_W    176
#define TOP_H    48
#define ROW_H    26
#define HEAD_H   40

static void say(const char *fmt, ...)
{
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

/* -----------------------------------------------------------------------
 * Sampling
 * ----------------------------------------------------------------------- */
static void push(double *h, double v)
{
    memmove(h, h + 1, (HIST - 1) * sizeof(double));
    h[HIST - 1] = v;
}

static Proc *find_proc(Proc *list, int n, DWORD pid)
{
    for (int i = 0; i < n; i++) if (list[i].pid == pid) return &list[i];
    return NULL;
}

/* The account a process runs as ("NOVA-PC\\Ada Lovelace" shows as its name) */
static void proc_user(DWORD pid, WCHAR *out, int cap)
{
    out[0] = 0;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), t = NULL;
    if (!p) return;
    if (OpenProcessToken(p, TOKEN_QUERY, &t)) {
        BYTE buf[256];
        DWORD n = 0;
        if (GetTokenInformation(t, TokenUser, buf, sizeof(buf), &n)) {
            WCHAR name[64], dom[64];
            DWORD nn = 64, nd = 64;
            SID_NAME_USE use;
            if (LookupAccountSidW(NULL, *(PSID *)buf, name, &nn, dom, &nd, &use)) lstrcpynW(out, name, cap);
        }
        CloseHandle(t);
    }
    CloseHandle(p);
}

/* Processes with a visible top-level window are apps (Processes' first group) */
static BOOL CALLBACK mark_app(HWND h, LPARAM lp)
{
    (void)lp;
    if (!IsWindowVisible(h) || GetWindow(h, 4 /* GW_OWNER */)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    Proc *p = find_proc(g_procs, g_nprocs, pid);
    if (p && pid != GetCurrentProcessId()) p->app = TRUE;
    return TRUE;
}

static void sample_processes(double wall_100ns)
{
    static BYTE *buf;
    static ULONG cap = 256 * 1024;
    ULONG need = 0;
    NTSTATUS s;
    for (;;) {
        if (!buf) buf = malloc(cap);
        if (!buf) return;
        s = NtQuerySystemInformation(5, buf, cap, &need);
        if (s != (NTSTATUS)0xC0000004 || need <= cap) break;          /* STATUS_INFO_LENGTH_MISMATCH */
        free(buf);
        buf = NULL;
        cap = need + 16 * 1024;
    }
    if (s < 0) return;
    static Proc old[MAX_PROCS];
    int nold = g_nprocs;
    memcpy(old, g_procs, sizeof(Proc) * (size_t)nold);
    int n = 0;
    ULONG threads = 0, handles = 0;
    for (BYTE *p = buf; n < MAX_PROCS; ) {
        Proc *e = &g_procs[n];
        memset(e, 0, sizeof(*e));
        e->pid = (DWORD)*(ULONG_PTR *)(p + 0x50);
        e->ppid = (DWORD)*(ULONG_PTR *)(p + 0x58);
        ULONG nt = *(ULONG *)(p + 0x04);
        e->threads = nt;
        e->handles = *(ULONG *)(p + 0x60);
        e->create = *(ULONGLONG *)(p + 0x20);
        e->cpu = *(ULONGLONG *)(p + 0x28) + *(ULONGLONG *)(p + 0x30);
        e->prio = *(LONG *)(p + 0x48);
        e->ws = *(ULONG_PTR *)(p + 0x90);
        USHORT len = *(USHORT *)(p + 0x38);
        const WCHAR *w = *(WCHAR **)(p + 0x40);
        int k = 0;
        for (; w && k < len / 2 && k < 63; k++) e->name[k] = (char)w[k];
        e->name[k] = 0;
        if (!e->pid && !k) strcpy(e->name, "System Idle Process");
        /* suspended: every thread waits suspended (ThreadState 5, WaitReason 5) */
        e->suspended = nt > 0;
        for (ULONG t = 0; t < nt; t++) {
            BYTE *r = p + 0x100 + 0x50 * t;
            if (*(ULONG *)(r + 0x44) != 5 || *(ULONG *)(r + 0x48) != 5) e->suspended = FALSE;
        }
        Proc *was = find_proc(old, nold, e->pid);
        if (was && was->create == e->create) {
            e->pct = wall_100ns > 0 ? (double)(e->cpu - was->cpu) * 100.0 / (wall_100ns * g_perf.ncpu) : 0;
            if (e->pct < 0) e->pct = 0;
            if (e->pct > 100) e->pct = 100;
            lstrcpynW(e->user, was->user, 48);
        } else {
            proc_user(e->pid, e->user, 48);
            if (g_listed) say("taskmgr: listed %s (PID %lu)\n", e->name, (unsigned long)e->pid);
        }
        threads += nt;
        handles += e->handles;
        n++;
        ULONG next = *(ULONG *)p;
        if (!next) break;
        p += next;
    }
    g_nprocs = n;
    g_perf.threads = threads;
    g_perf.handles = handles;
    EnumWindows(mark_app, 0);
    for (int i = 0; i < nold; i++)
        if (!find_proc(g_procs, n, old[i].pid) && g_listed)
            say("taskmgr: %s (PID %lu) is gone\n", old[i].name, (unsigned long)old[i].pid);
    if (!g_listed) {
        g_listed = TRUE;
        say("taskmgr: %d processes, %lu threads\n", n, (unsigned long)threads);
        for (int i = 0; i < n; i++) say("taskmgr: listed %s (PID %lu)\n", g_procs[i].name, (unsigned long)g_procs[i].pid);
    }
}

static void sample(void)
{
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    double wall = g_perf.wall && f.QuadPart ? (double)(now.QuadPart - (LONGLONG)g_perf.wall) * 1e7 / (double)f.QuadPart : 0;
    double secs = wall / 1e7;
    g_perf.wall = (ULONGLONG)now.QuadPart;

    /* each logical processor: busy = 1 - idle / (kernel + user), kernel including idle */
    static ULONGLONG r[MAX_CPUS][6];
    ULONG got = 0;
    if (NtQuerySystemInformation(8, r, sizeof(r), &got) >= 0) {
        int n = (int)(got / 48);
        if (n > MAX_CPUS) n = MAX_CPUS;
        double sum = 0;
        for (int i = 0; i < n; i++) {
            ULONGLONG idle = r[i][0], total = r[i][1] + r[i][2];
            double busy = 0;
            if (g_perf.samples && total > g_perf.total[i])
                busy = 100.0 * (1.0 - (double)(idle - g_perf.idle[i]) / (double)(total - g_perf.total[i]));
            if (busy < 0) busy = 0;
            if (busy > 100) busy = 100;
            g_perf.idle[i] = idle;
            g_perf.total[i] = total;
            push(g_perf.core[i], busy);
            sum += busy;
        }
        g_perf.ncpu = n ? n : 1;
        push(g_perf.cpu, n ? sum / n : 0);
    }

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        g_perf.mem_total = ms.ullTotalPhys;
        g_perf.mem_avail = ms.ullAvailPhys;
        g_perf.commit = ms.ullTotalPageFile - ms.ullAvailPageFile;
        g_perf.commit_limit = ms.ullTotalPageFile;
        push(g_perf.mem, ms.ullTotalPhys ? 100.0 * (double)(ms.ullTotalPhys - ms.ullAvailPhys) / (double)ms.ullTotalPhys : 0);
    }

    /* disk: SystemPerformanceInformation's read and write transfer counts */
    BYTE spi[344];
    if (NtQuerySystemInformation(2, spi, sizeof(spi), &got) >= 0) {
        ULONGLONG rd = *(ULONGLONG *)(spi + 0x08), wr = *(ULONGLONG *)(spi + 0x10);
        push(g_perf.disk_r, g_perf.samples && secs > 0 ? (double)(rd - g_perf.rd) / secs : 0);
        push(g_perf.disk_w, g_perf.samples && secs > 0 ? (double)(wr - g_perf.wr) / secs : 0);
        g_perf.rd = rd;
        g_perf.wr = wr;
    }

    /* network: the adapter's octets (MIB_IF_ROW2 InOctets, OutOctets) */
    BYTE *t = NULL;
    g_perf.have_net = FALSE;
    if (GetIfTable2((PVOID *)&t) == 0 && t) {
        if (*(ULONG *)t) {
            BYTE *row = t + 8;
            ULONGLONG in = *(ULONGLONG *)(row + 1208), out = *(ULONGLONG *)(row + 1280);
            push(g_perf.net_in, g_perf.samples && secs > 0 && in >= g_perf.in ? (double)(in - g_perf.in) / secs : 0);
            push(g_perf.net_out, g_perf.samples && secs > 0 && out >= g_perf.out ? (double)(out - g_perf.out) / secs : 0);
            g_perf.in = in;
            g_perf.out = out;
            g_perf.link_speed = *(ULONGLONG *)(row + 1192);
            lstrcpynW(g_perf.adapter, (WCHAR *)(row + 542), 128);
            g_perf.have_net = TRUE;
        }
        FreeMibTable(t);
    }

    sample_processes(wall);
    g_perf.samples++;
}

static void load_services(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return;
    DWORD need = 0, count = 0, resume = 0;
    EnumServicesStatusExW(scm, 0, SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0, &need, &count, &resume, NULL);
    BYTE *buf = need ? malloc(need) : NULL;
    resume = 0;
    if (buf && EnumServicesStatusExW(scm, 0, SERVICE_WIN32, SERVICE_STATE_ALL, buf, need, &need, &count, &resume, NULL)) {
        free(g_svcs);
        g_svcs = calloc(count ? count : 1, sizeof(Svc));
        g_nsvcs = 0;
        EnumSvcProcess *e = (EnumSvcProcess *)buf;
        for (DWORD i = 0; g_svcs && i < count; i++) {
            Svc *v = &g_svcs[g_nsvcs++];
            lstrcpynW(v->name, e[i].lpServiceName, 64);
            lstrcpynW(v->disp, e[i].lpDisplayName ? e[i].lpDisplayName : L"", 96);
            v->state = e[i].ServiceStatusProcess.dwCurrentState;
            v->pid = e[i].ServiceStatusProcess.dwProcessId;
        }
    }
    free(buf);
    CloseServiceHandle(scm);
}

static void cpu_brand(void)
{
    unsigned int r[12];
    unsigned int a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u), "c"(0));
    if (a < 0x80000004u) { strcpy(g_cpu_name, "Processor"); return; }
    for (unsigned int i = 0; i < 3; i++)
        __asm__ volatile("cpuid" : "=a"(r[4 * i]), "=b"(r[4 * i + 1]), "=c"(r[4 * i + 2]), "=d"(r[4 * i + 3])
                         : "a"(0x80000002u + i), "c"(0));
    char *s = (char *)r;
    s[47] = 0;
    while (*s == ' ') s++;
    lstrcpynA(g_cpu_name, s, sizeof(g_cpu_name));
}

/* -----------------------------------------------------------------------
 * Rows: the processes in the current view's order
 * ----------------------------------------------------------------------- */
static int g_order[MAX_PROCS], g_norder, g_napps;   /* Processes: apps first */

static int cmp_proc(const Proc *a, const Proc *b, int col, int view)
{
    if (view == V_PROCESSES) {
        switch (col) {
        case 1: return a->suspended - b->suspended;
        case 2: return a->pid < b->pid ? -1 : a->pid > b->pid;
        case 3: return a->pct < b->pct ? -1 : a->pct > b->pct;
        case 4: return a->ws < b->ws ? -1 : a->ws > b->ws;
        default: return lstrcmpiA(a->name, b->name);
        }
    }
    switch (col) {
    case 1: return a->pid < b->pid ? -1 : a->pid > b->pid;
    case 2: return a->suspended - b->suspended;
    case 3: return lstrcmpiW(a->user, b->user);
    case 4: return a->pct < b->pct ? -1 : a->pct > b->pct;
    case 5: return a->cpu < b->cpu ? -1 : a->cpu > b->cpu;
    case 6: return a->ws < b->ws ? -1 : a->ws > b->ws;
    case 7: return (int)a->threads - (int)b->threads;
    case 8: return (int)a->handles - (int)b->handles;
    case 9: return a->prio - b->prio;
    default: return lstrcmpiA(a->name, b->name);
    }
}

static void order_rows(void)
{
    int v = g_view == V_DETAILS ? V_DETAILS : V_PROCESSES;
    g_norder = 0;
    for (int i = 0; i < g_nprocs; i++) if (g_procs[i].pid || v == V_DETAILS) g_order[g_norder++] = i;
    for (int i = 1; i < g_norder; i++)              /* insertion sort: the list is short */
        for (int j = i; j > 0; j--) {
            const Proc *a = &g_procs[g_order[j - 1]], *b = &g_procs[g_order[j]];
            int c = (v == V_PROCESSES) ? (b->app - a->app) : 0;     /* (apps first) */
            if (!c) c = cmp_proc(a, b, g_sort[v], v) * (g_sort_desc[v] ? -1 : 1);
            if (!c) c = a->pid > b->pid;
            if (c <= 0) break;
            int t = g_order[j]; g_order[j] = g_order[j - 1]; g_order[j - 1] = t;
        }
    g_napps = 0;
    if (v == V_PROCESSES) for (int i = 0; i < g_norder; i++) if (g_procs[g_order[i]].app) g_napps++;
}

/* The list's lines: in Processes, a heading before each group */
typedef struct { int proc; const WCHAR *heading; } Line;
static Line g_lines[MAX_PROCS + 2];
static int g_nlines;

static void build_lines(void)
{
    order_rows();
    g_nlines = 0;
    if (g_view == V_SERVICES) {
        for (int i = 0; i < g_nsvcs && i < MAX_PROCS; i++) g_lines[g_nlines++] = (Line){ i, NULL };
        return;
    }
    static WCHAR h1[40], h2[40];
    for (int i = 0; i < g_norder; i++) {
        if (g_view == V_PROCESSES && i == 0 && g_napps) {
            swprintf(h1, 40, L"Apps (%d)", g_napps);
            g_lines[g_nlines++] = (Line){ -1, h1 };
        }
        if (g_view == V_PROCESSES && i == g_napps) {
            swprintf(h2, 40, L"Background processes (%d)", g_norder - g_napps);
            g_lines[g_nlines++] = (Line){ -1, h2 };
        }
        g_lines[g_nlines++] = (Line){ g_order[i], NULL };
    }
}

static Proc *selected(void)
{
    return g_sel_pid == (DWORD)-1 ? NULL : find_proc(g_procs, g_nprocs, g_sel_pid);
}

/* -----------------------------------------------------------------------
 * Drawing
 * ----------------------------------------------------------------------- */
static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void text(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, UINT fmt, const WCHAR *s)
{
    RECT r = { x, y, x + w, y + h };
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

static void textf(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, UINT fmt, const WCHAR *spec, ...)
{
    WCHAR buf[160];
    va_list ap;
    va_start(ap, spec);
    vswprintf(buf, 160, spec, ap);
    va_end(ap);
    text(dc, f, c, x, y, w, h, fmt, buf);
}

static void frame(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    fill(dc, x, y, w, 1, c);
    fill(dc, x, y + h - 1, w, 1, c);
    fill(dc, x, y, 1, h, c);
    fill(dc, x + w - 1, y, 1, h, c);
}

static COLORREF mix(COLORREF a, COLORREF b, int t)   /* t of 255 towards b */
{
    return RGB((GetRValue(a) * (255 - t) + GetRValue(b) * t) / 255, (GetGValue(a) * (255 - t) + GetGValue(b) * t) / 255,
               (GetBValue(a) * (255 - t) + GetBValue(b) * t) / 255);
}

/* A 60-second graph: grid, a filled area under @h (of @max), and an
 * optional second series @h2 drawn as a dashed-looking line */
static void graph(HDC dc, int x, int y, int w, int h, const double *v, const double *v2, double max, COLORREF c)
{
    fill(dc, x, y, w, h, C_BG);
    COLORREF grid = mix(C_BG, c, 40);
    for (int i = 1; i < 10; i++) fill(dc, x + w * i / 10, y, 1, h, grid);
    for (int i = 1; i < 10; i++) fill(dc, x, y + h * i / 10, w, 1, grid);
    if (max <= 0) max = 1;
    POINT pt[HIST + 2];
    for (int i = 0; i < HIST; i++) {
        double f = v[i] / max;
        if (f > 1) f = 1;
        pt[i].x = x + (w - 1) * i / (HIST - 1);
        pt[i].y = y + h - 1 - (int)(f * (h - 2));
    }
    pt[HIST] = (POINT){ x + w - 1, y + h - 1 };
    pt[HIST + 1] = (POINT){ x, y + h - 1 };
    HBRUSH b = CreateSolidBrush(mix(C_BG, c, 50));
    HPEN pen = CreatePen(PS_SOLID, 1, mix(C_BG, c, 50));
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
    Polygon(dc, pt, HIST + 2);
    SelectObject(dc, op);
    DeleteObject(pen);
    pen = CreatePen(PS_SOLID, 1, c);
    SelectObject(dc, pen);
    Polyline(dc, pt, HIST);
    if (v2) {
        for (int i = 0; i < HIST; i++) {
            double f = v2[i] / max;
            if (f > 1) f = 1;
            pt[i].y = y + h - 1 - (int)(f * (h - 2));
        }
        HPEN p2 = CreatePen(PS_DOT, 1, c);
        SelectObject(dc, p2);
        Polyline(dc, pt, HIST);
        SelectObject(dc, pen);
        DeleteObject(p2);
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(pen);
    frame(dc, x, y, w, h, c);
}

static void bytes_text(WCHAR *out, int cap, double b, BOOL rate)
{
    const WCHAR *u[] = { L"B", L"KB", L"MB", L"GB" };
    int i = 0;
    while (b >= 1024 && i < 3) { b /= 1024; i++; }
    swprintf(out, cap, i ? L"%.1f %ls%ls" : L"%.0f %ls%ls", b, u[i], rate ? L"/s" : L"");
}

static double hist_max(const double *a, const double *b)
{
    double m = 0;
    for (int i = 0; i < HIST; i++) {
        if (a[i] > m) m = a[i];
        if (b && b[i] > m) m = b[i];
    }
    double s = 1024;                          /* round up to a tidy scale */
    while (s < m) s *= 2;
    return s;
}

/* -- columns -- */
typedef struct { const WCHAR *name; int w; UINT align; } Col;
static const Col g_cols_proc[] = {
    { L"Name", 0, DT_LEFT }, { L"Status", 110, DT_LEFT }, { L"PID", 70, DT_RIGHT },
    { L"CPU", 80, DT_RIGHT }, { L"Memory", 110, DT_RIGHT },
};
static const Col g_cols_det[] = {
    { L"Name", 0, DT_LEFT }, { L"PID", 60, DT_RIGHT }, { L"Status", 90, DT_LEFT }, { L"User name", 110, DT_LEFT },
    { L"CPU", 50, DT_RIGHT }, { L"CPU time", 80, DT_RIGHT }, { L"Memory", 90, DT_RIGHT },
    { L"Threads", 64, DT_RIGHT }, { L"Handles", 64, DT_RIGHT }, { L"Base priority", 96, DT_LEFT },
};
static const Col g_cols_svc[] = {
    { L"Name", 170, DT_LEFT }, { L"PID", 60, DT_RIGHT }, { L"Description", 0, DT_LEFT }, { L"Status", 100, DT_LEFT },
};

static const Col *columns(int *n)
{
    if (g_view == V_DETAILS) { *n = 10; return g_cols_det; }
    if (g_view == V_SERVICES) { *n = 4; return g_cols_svc; }
    *n = 5;
    return g_cols_proc;
}

/* The x of each column (and its width) across @w, the flexible one (w 0) taking the rest */
static void layout_cols(const Col *c, int n, int x, int w, int *cx, int *cw)
{
    int fixed = 0;
    for (int i = 0; i < n; i++) fixed += c[i].w;
    for (int i = 0; i < n; i++) {
        cw[i] = c[i].w ? c[i].w : (w - fixed > 120 ? w - fixed : 120);
        cx[i] = x;
        x += cw[i];
    }
}

static const WCHAR *prio_name(LONG p)
{
    return p >= 24 ? L"Realtime" : p >= 13 ? L"High" : p >= 10 ? L"Above normal" : p >= 8 ? L"Normal" :
           p >= 6 ? L"Below normal" : L"Low";
}

static void cell(HDC dc, const Proc *p, int col, WCHAR *s, int cap)
{
    s[0] = 0;
    if (g_view == V_PROCESSES) {
        switch (col) {
        case 0: swprintf(s, cap, L"%hs", p->name); break;
        case 1: if (p->suspended) lstrcpynW(s, L"Suspended", cap); break;
        case 2: swprintf(s, cap, L"%lu", (unsigned long)p->pid); break;
        case 3: swprintf(s, cap, L"%.1f%%", p->pct); break;
        case 4: swprintf(s, cap, L"%.1f MB", (double)p->ws / (1024.0 * 1024.0)); break;
        }
        return;
    }
    switch (col) {
    case 0: swprintf(s, cap, L"%hs", p->name); break;
    case 1: swprintf(s, cap, L"%lu", (unsigned long)p->pid); break;
    case 2: lstrcpynW(s, p->suspended ? L"Suspended" : L"Running", cap); break;
    case 3: lstrcpynW(s, p->user[0] ? p->user : L"SYSTEM", cap); break;
    case 4: swprintf(s, cap, L"%02d", (int)(p->pct + 0.5)); break;
    case 5: {
        ULONGLONG sec = p->cpu / 10000000ull;
        swprintf(s, cap, L"%llu:%02llu:%02llu", sec / 3600, sec / 60 % 60, sec % 60);
        break; }
    case 6: swprintf(s, cap, L"%llu K", (unsigned long long)(p->ws / 1024)); break;
    case 7: swprintf(s, cap, L"%lu", (unsigned long)p->threads); break;
    case 8: swprintf(s, cap, L"%lu", (unsigned long)p->handles); break;
    case 9: lstrcpynW(s, prio_name(p->prio), cap); break;
    }
    (void)dc;
}

static const WCHAR *svc_state(DWORD s)
{
    switch (s) {
    case SERVICE_RUNNING: return L"Running";
    case SERVICE_START_PENDING: return L"Starting";
    case SERVICE_STOP_PENDING: return L"Stopping";
    case SERVICE_PAUSED: return L"Paused";
    default: return L"Stopped";
    }
}

static RECT g_list_rc;                        /* the list's rows (client coordinates) */
static int g_col_x[12], g_col_w[12], g_ncols;

static int visible_rows(void) { return (g_list_rc.bottom - g_list_rc.top) / ROW_H; }

static void draw_list(HDC dc, int x, int y, int w, int h)
{
    int n;
    const Col *c = columns(&n);
    g_ncols = n;
    layout_cols(c, n, x + 12, w - 24, g_col_x, g_col_w);
    /* headings: the CPU and memory totals over their columns, as Windows shows them */
    fill(dc, x, y, w, HEAD_H, C_BG);
    int v = g_view;
    for (int i = 0; i < n; i++) {
        int cx = g_col_x[i] + 6, cw = g_col_w[i] - 12;
        BOOL sorted = g_sort[v] == i;
        if (v == V_PROCESSES && (i == 3 || i == 4)) {
            double pct = i == 3 ? g_perf.cpu[HIST - 1] : g_perf.mem[HIST - 1];
            textf(dc, g_big, C_TEXT, cx, y + 2, cw, 22, c[i].align, L"%.0f%%", pct);
            text(dc, g_small, sorted ? C_ACCENT : C_DIM, cx, y + 22, cw, 16, c[i].align, c[i].name);
        } else {
            text(dc, sorted ? g_bold : g_small, sorted ? C_ACCENT : C_DIM, cx, y + 14, cw, 24, c[i].align, c[i].name);
        }
        if (i) fill(dc, g_col_x[i], y + 6, 1, HEAD_H - 10, C_LINE);
    }
    fill(dc, x, y + HEAD_H - 1, w, 1, C_LINE);

    g_list_rc = (RECT){ x, y + HEAD_H, x + w, y + h };
    int rows = visible_rows();
    if (g_scroll[v] > g_nlines - rows) g_scroll[v] = g_nlines - rows;
    if (g_scroll[v] < 0) g_scroll[v] = 0;
    for (int r = 0; r < rows && g_scroll[v] + r < g_nlines; r++) {
        Line *L = &g_lines[g_scroll[v] + r];
        int ry = y + HEAD_H + r * ROW_H;
        if (L->heading) {
            text(dc, g_bold, C_TEXT, x + 18, ry + 4, w - 36, ROW_H - 4, DT_LEFT, L->heading);
            continue;
        }
        WCHAR s[96];
        if (v == V_SERVICES) {
            Svc *sv = &g_svcs[L->proc];
            if (L->proc == g_sel_svc) fill(dc, x + 4, ry, w - 8, ROW_H, C_SEL);
            for (int i = 0; i < n; i++) {
                s[0] = 0;
                if (i == 0) lstrcpynW(s, sv->name, 96);
                if (i == 1 && sv->pid) swprintf(s, 96, L"%lu", (unsigned long)sv->pid);
                if (i == 2) lstrcpynW(s, sv->disp, 96);
                if (i == 3) lstrcpynW(s, svc_state(sv->state), 96);
                text(dc, g_font, C_TEXT, g_col_x[i] + 6, ry, g_col_w[i] - 12, ROW_H, c[i].align, s);
            }
            continue;
        }
        Proc *p = &g_procs[L->proc];
        BOOL sel = p->pid == g_sel_pid;
        if (sel) fill(dc, x + 4, ry, w - 8, ROW_H, C_SEL);
        for (int i = 0; i < n; i++) {
            /* Processes' CPU and memory cells shade from white to amber with the load */
            if (!sel && v == V_PROCESSES && (i == 3 || i == 4)) {
                double f = i == 3 ? p->pct / 50.0 : (double)p->ws / (256.0 * 1024 * 1024);
                if (f > 1) f = 1;
                fill(dc, g_col_x[i], ry, g_col_w[i], ROW_H, mix(RGB(0xFF, 0xF4, 0xC4), RGB(0xFF, 0xC8, 0x5C), (int)(f * 255)));
            }
            cell(dc, p, i, s, 96);
            int ix = g_col_x[i] + 6;
            if (i == 0) {                         /* a small tile for the program */
                fill(dc, ix, ry + 6, 14, 14, p->app ? C_ACCENT : RGB(0x9A, 0x9A, 0x9A));
                ix += 22;
            }
            text(dc, g_font, C_TEXT, ix, ry, g_col_w[i] - 12 - (ix - g_col_x[i] - 6), ROW_H, c[i].align, s);
        }
    }
    if (g_nlines > rows) {                        /* a thin scroll bar */
        int th = h - HEAD_H, bh = th * rows / g_nlines, by = th * g_scroll[v] / g_nlines;
        fill(dc, x + w - 6, y + HEAD_H + by, 3, bh > 12 ? bh : 12, RGB(0x8A, 0x8A, 0x8A));
    }
}

/* Performance's tiles (left) and the chosen page (right) */
static RECT g_tile_rc[P_COUNT];

static void perf_tile_info(int i, const WCHAR **name, WCHAR *val, int cap, const double **h, const double **h2,
                           double *max, COLORREF *c)
{
    WCHAR a[32], b[32];
    *h2 = NULL;
    switch (i) {
    case P_CPU:
        *name = L"CPU"; *h = g_perf.cpu; *max = 100; *c = C_CPU;
        swprintf(val, cap, L"%.0f%%", g_perf.cpu[HIST - 1]);
        break;
    case P_MEM:
        *name = L"Memory"; *h = g_perf.mem; *max = 100; *c = C_MEM;
        swprintf(val, cap, L"%.1f/%.1f GB (%.0f%%)", (double)(g_perf.mem_total - g_perf.mem_avail) / 1073741824.0,
                 (double)g_perf.mem_total / 1073741824.0, g_perf.mem[HIST - 1]);
        break;
    case P_DISK:
        *name = L"Disk 0"; *h = g_perf.disk_r; *h2 = g_perf.disk_w; *max = hist_max(g_perf.disk_r, g_perf.disk_w); *c = C_DISK;
        bytes_text(a, 32, g_perf.disk_r[HIST - 1], TRUE);
        bytes_text(b, 32, g_perf.disk_w[HIST - 1], TRUE);
        swprintf(val, cap, L"R: %ls  W: %ls", a, b);
        break;
    default:
        *name = L"Ethernet"; *h = g_perf.net_in; *h2 = g_perf.net_out; *max = hist_max(g_perf.net_in, g_perf.net_out); *c = C_NET;
        if (!g_perf.have_net) { swprintf(val, cap, L"Not connected"); break; }
        bytes_text(a, 32, g_perf.net_out[HIST - 1], TRUE);
        bytes_text(b, 32, g_perf.net_in[HIST - 1], TRUE);
        swprintf(val, cap, L"S: %ls  R: %ls", a, b);
        break;
    }
}

static void stat(HDC dc, int x, int y, const WCHAR *label, const WCHAR *value)
{
    text(dc, g_small, C_DIM, x, y, 150, 16, DT_LEFT, label);
    text(dc, g_big, C_TEXT, x, y + 16, 190, 26, DT_LEFT, value);
}

static void draw_performance(HDC dc, int x, int y, int w, int h)
{
    int tw = 230;
    for (int i = 0; i < P_COUNT; i++) {
        const WCHAR *name;
        WCHAR val[64];
        const double *hv, *hv2;
        double max;
        COLORREF c;
        perf_tile_info(i, &name, val, 64, &hv, &hv2, &max, &c);
        int ty = y + 8 + i * 70;
        g_tile_rc[i] = (RECT){ x + 8, ty, x + tw, ty + 64 };
        if (i == g_perf_page) fill(dc, x + 8, ty, tw - 8, 64, C_SEL);
        graph(dc, x + 16, ty + 10, 64, 44, hv, hv2, max, c);
        text(dc, g_bold, C_TEXT, x + 90, ty + 10, tw - 96, 20, DT_LEFT, name);
        text(dc, g_small, C_DIM, x + 90, ty + 32, tw - 96, 18, DT_LEFT, val);
    }
    fill(dc, x + tw + 8, y, 1, h, C_LINE);

    int px = x + tw + 28, pw = w - tw - 48, py = y + 12;
    const WCHAR *name;
    WCHAR val[64], a[32], b[32], s[64];
    const double *hv, *hv2;
    double max;
    COLORREF c;
    perf_tile_info(g_perf_page, &name, val, 64, &hv, &hv2, &max, &c);
    text(dc, g_big, C_TEXT, px, py, 200, 30, DT_LEFT, name);
    int sy = y + h - 120;                         /* the figures beneath the graphs */
    if (g_perf_page == P_CPU) {
        textf(dc, g_font, C_DIM, px + 200, py, pw - 200, 30, DT_RIGHT, L"%hs", g_cpu_name);
        text(dc, g_small, C_DIM, px, py + 34, pw, 16, DT_LEFT, L"% Utilization over 60 seconds, each logical processor");
        int n = g_perf.ncpu, cols = 1;
        while (cols * cols < n) cols++;
        int rows = (n + cols - 1) / cols, gy = py + 54, gh = sy - gy - 12;
        int cw = (pw - (cols - 1) * 6) / cols, ch = (gh - (rows - 1) * 6) / rows;
        for (int i = 0; i < n; i++)
            graph(dc, px + (i % cols) * (cw + 6), gy + (i / cols) * (ch + 6), cw, ch, g_perf.core[i], NULL, 100, C_CPU);
        swprintf(s, 64, L"%.0f%%", g_perf.cpu[HIST - 1]);
        stat(dc, px, sy, L"Utilization", s);
        swprintf(s, 64, L"%d", g_nprocs);
        stat(dc, px + 150, sy, L"Processes", s);
        swprintf(s, 64, L"%lu", (unsigned long)g_perf.threads);
        stat(dc, px + 260, sy, L"Threads", s);
        swprintf(s, 64, L"%lu", (unsigned long)g_perf.handles);
        stat(dc, px + 370, sy, L"Handles", s);
        ULONGLONG up = GetTickCount64() / 1000;
        swprintf(s, 64, L"%llu:%02llu:%02llu:%02llu", up / 86400, up / 3600 % 24, up / 60 % 60, up % 60);
        stat(dc, px, sy + 50, L"Up time", s);
        swprintf(s, 64, L"%d", n);
        stat(dc, px + 150, sy + 50, L"Logical processors", s);
        return;
    }
    const WCHAR *what = g_perf_page == P_MEM ? L"Memory usage" : g_perf_page == P_DISK ?
                        L"Transfer rate (read, and write dotted)" : L"Throughput (receive, and send dotted)";
    if (g_perf_page == P_NET && g_perf.have_net) text(dc, g_font, C_DIM, px + 200, py, pw - 200, 30, DT_RIGHT, g_perf.adapter);
    if (g_perf_page == P_MEM) {
        WCHAR t[32];
        bytes_text(t, 32, (double)g_perf.mem_total, FALSE);
        text(dc, g_font, C_DIM, px + 200, py, pw - 200, 30, DT_RIGHT, t);
    }
    text(dc, g_small, C_DIM, px, py + 34, pw, 16, DT_LEFT, what);
    if (g_perf_page != P_MEM) {
        bytes_text(s, 64, max, TRUE);
        text(dc, g_small, C_DIM, px, py + 34, pw, 16, DT_RIGHT, s);
    }
    graph(dc, px, py + 54, pw, sy - py - 66, hv, hv2, max, c);
    switch (g_perf_page) {
    case P_MEM:
        bytes_text(a, 32, (double)(g_perf.mem_total - g_perf.mem_avail), FALSE);
        stat(dc, px, sy, L"In use", a);
        bytes_text(a, 32, (double)g_perf.mem_avail, FALSE);
        stat(dc, px + 170, sy, L"Available", a);
        bytes_text(a, 32, (double)g_perf.commit, FALSE);
        bytes_text(b, 32, (double)g_perf.commit_limit, FALSE);
        swprintf(s, 64, L"%ls / %ls", a, b);
        stat(dc, px + 340, sy, L"Committed", s);
        break;
    case P_DISK:
        bytes_text(a, 32, g_perf.disk_r[HIST - 1], TRUE);
        stat(dc, px, sy, L"Read speed", a);
        bytes_text(a, 32, g_perf.disk_w[HIST - 1], TRUE);
        stat(dc, px + 170, sy, L"Write speed", a);
        bytes_text(a, 32, (double)g_perf.rd, FALSE);
        stat(dc, px, sy + 50, L"Read since start", a);
        bytes_text(a, 32, (double)g_perf.wr, FALSE);
        stat(dc, px + 170, sy + 50, L"Written since start", a);
        break;
    default:
        if (!g_perf.have_net) { stat(dc, px, sy, L"Status", L"Not connected"); break; }
        bytes_text(a, 32, g_perf.net_out[HIST - 1], TRUE);
        stat(dc, px, sy, L"Send", a);
        bytes_text(a, 32, g_perf.net_in[HIST - 1], TRUE);
        stat(dc, px + 170, sy, L"Receive", a);
        swprintf(s, 64, L"%.0f Mbps", (double)g_perf.link_speed / 1e6);
        stat(dc, px + 340, sy, L"Link speed", s);
        break;
    }
}

static RECT g_nav_rc[V_COUNT], g_end_rc;

static void draw(HDC dc, int cw, int ch)
{
    fill(dc, 0, 0, cw, ch, C_NAV);
    SetBkMode(dc, TRANSPARENT);
    /* the top bar: the app's name, the view's and End task */
    text(dc, g_bold, C_TEXT, 16, 0, 200, TOP_H, DT_LEFT, L"Task Manager");
    for (int i = 0; i < V_COUNT; i++) {
        int y = TOP_H + 8 + i * 40;
        g_nav_rc[i] = (RECT){ 6, y, NAV_W - 6, y + 36 };
        if (i == g_view) {
            fill(dc, 6, y, NAV_W - 12, 36, C_HOVER);
            fill(dc, 6, y + 9, 3, 18, C_ACCENT);
        }
        /* a glyph per view: bars, a graph line, a list, gears' cogs (squares) */
        int gx = 20, gy = y + 11;
        HPEN pen = CreatePen(PS_SOLID, 2, C_TEXT);
        HGDIOBJ op = SelectObject(dc, pen);
        if (i == V_PROCESSES) for (int k = 0; k < 3; k++) { MoveToEx(dc, gx + 3 + k * 5, gy + 14, NULL); LineTo(dc, gx + 3 + k * 5, gy + 4 + k * 3); }
        if (i == V_PERFORMANCE) { POINT p[] = { {gx, gy + 12}, {gx + 5, gy + 6}, {gx + 9, gy + 10}, {gx + 15, gy + 2} }; Polyline(dc, p, 4); }
        if (i == V_DETAILS) for (int k = 0; k < 3; k++) { MoveToEx(dc, gx, gy + 3 + k * 5, NULL); LineTo(dc, gx + 15, gy + 3 + k * 5); }
        if (i == V_SERVICES) { HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH)); Ellipse(dc, gx + 2, gy + 1, gx + 15, gy + 14); SelectObject(dc, ob); }
        SelectObject(dc, op);
        DeleteObject(pen);
        text(dc, i == g_view ? g_bold : g_font, C_TEXT, 48, y, NAV_W - 56, 36, DT_LEFT, g_view_names[i]);
    }
    int x = NAV_W, y = TOP_H, w = cw - NAV_W, h = ch - TOP_H;
    /* the page: white, with its title and, for processes, End task */
    fill(dc, x, y, w, h, C_BG);
    fill(dc, x, y, w, 1, C_LINE);
    fill(dc, x, y, 1, h, C_LINE);
    text(dc, g_big, C_TEXT, x + 20, y + 8, 300, 36, DT_LEFT, g_view_names[g_view]);
    g_end_rc = (RECT){ 0, 0, 0, 0 };
    if (g_view == V_PROCESSES || g_view == V_DETAILS) {
        BOOL can = selected() != NULL;
        g_end_rc = (RECT){ cw - 132, y + 10, cw - 20, y + 42 };
        fill(dc, g_end_rc.left, g_end_rc.top, 112, 32, can ? C_ACCENT : RGB(0xD0, 0xD0, 0xD0));
        text(dc, g_bold, can ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x80, 0x80, 0x80), g_end_rc.left, g_end_rc.top, 112, 32,
             DT_CENTER, L"End task");
    }
    if (g_view == V_PERFORMANCE) draw_performance(dc, x, y + 52, w, h - 52);
    else draw_list(dc, x, y + 52, w, h - 52);
}

/* -----------------------------------------------------------------------
 * Acting
 * ----------------------------------------------------------------------- */
static void set_view(int v)
{
    g_view = (v + V_COUNT) % V_COUNT;
    if (g_view == V_SERVICES) load_services();
    build_lines();
    say("taskmgr: view %ls\n", g_view_names[g_view]);
    if (g_view == V_PERFORMANCE)
        say("taskmgr: performance: CPU %.0f%% on %d logical processors, memory %llu MB of %llu MB in use\n",
            g_perf.cpu[HIST - 1], g_perf.ncpu, (unsigned long long)((g_perf.mem_total - g_perf.mem_avail) >> 20),
            (unsigned long long)(g_perf.mem_total >> 20));
    if (g_view == V_SERVICES) say("taskmgr: %d services\n", g_nsvcs);
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void select_pid(DWORD pid, BOOL tell)
{
    g_sel_pid = pid;
    Proc *p = selected();
    if (p && tell) say("taskmgr: selected %s (PID %lu)\n", p->name, (unsigned long)p->pid);
    /* keep it in view */
    for (int i = 0; i < g_nlines; i++) {
        if (g_lines[i].heading || g_procs[g_lines[i].proc].pid != pid) continue;
        int rows = visible_rows();
        if (i < g_scroll[g_view]) g_scroll[g_view] = i;
        if (rows > 0 && i >= g_scroll[g_view] + rows) g_scroll[g_view] = i - rows + 1;
        break;
    }
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void end_task(void)
{
    Proc *p = selected();
    if (!p) return;
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p->pid);
    if (h && TerminateProcess(h, 1)) say("taskmgr: ended %s (PID %lu)\n", p->name, (unsigned long)p->pid);
    else say("taskmgr: could not end %s (PID %lu): error %lu\n", p->name, (unsigned long)p->pid, GetLastError());
    if (h) CloseHandle(h);
}

static void move_sel(int by, BOOL absolute)
{
    if (g_view == V_SERVICES) {
        int n = g_nsvcs;
        if (!n) return;
        int i = absolute ? (by < 0 ? 0 : n - 1) : g_sel_svc + by;
        g_sel_svc = i < 0 ? 0 : i >= n ? n - 1 : i;
        InvalidateRect(g_wnd, NULL, FALSE);
        return;
    }
    int cur = -1, first = -1, last = -1;
    for (int i = 0; i < g_nlines; i++) {
        if (g_lines[i].heading) continue;
        if (first < 0) first = i;
        last = i;
        if (g_procs[g_lines[i].proc].pid == g_sel_pid) cur = i;
    }
    if (first < 0) return;
    int i = absolute ? (by < 0 ? first : last) : (cur < 0 ? first : cur + by);
    if (i < first) i = first;
    if (i > last) i = last;
    while (g_lines[i].heading && i < last) i++;
    select_pid(g_procs[g_lines[i].proc].pid, TRUE);
}

/* Typing selects the first process whose name starts with what was typed
 * (what was typed in the last second, as in Windows' lists) */
static void find_typed(WCHAR ch)
{
    DWORD now = GetTickCount();
    size_t n = wcslen(g_find);
    if (now - g_find_at > 1000) n = 0;
    g_find_at = now;
    if (n < 31) { g_find[n++] = ch; g_find[n] = 0; }
    for (int i = 0; i < g_nlines; i++) {
        if (g_lines[i].heading) continue;
        const char *name = g_procs[g_lines[i].proc].name;
        size_t k = 0;
        while (k < n && name[k] && towlower((WCHAR)(BYTE)name[k]) == towlower(g_find[k])) k++;
        if (k == n) { select_pid(g_procs[g_lines[i].proc].pid, TRUE); return; }
    }
}

static BOOL in(const RECT *r, int x, int y) { return x >= r->left && x < r->right && y >= r->top && y < r->bottom; }

static void click(int x, int y)
{
    for (int i = 0; i < V_COUNT; i++) if (in(&g_nav_rc[i], x, y)) { set_view(i); return; }
    if (in(&g_end_rc, x, y)) { end_task(); return; }
    if (g_view == V_PERFORMANCE) {
        for (int i = 0; i < P_COUNT; i++) if (in(&g_tile_rc[i], x, y)) { g_perf_page = i; InvalidateRect(g_wnd, NULL, FALSE); }
        return;
    }
    if (y >= g_list_rc.top - HEAD_H && y < g_list_rc.top) {      /* a column heading: sort by it */
        for (int i = 0; i < g_ncols; i++)
            if (x >= g_col_x[i] && x < g_col_x[i] + g_col_w[i]) {
                if (g_sort[g_view] == i) g_sort_desc[g_view] = !g_sort_desc[g_view];
                else { g_sort[g_view] = i; g_sort_desc[g_view] = (i >= 3 && g_view != V_SERVICES); }
                build_lines();
                InvalidateRect(g_wnd, NULL, FALSE);
            }
        return;
    }
    if (!in(&g_list_rc, x, y)) return;
    int i = g_scroll[g_view] + (y - g_list_rc.top) / ROW_H;
    if (i < 0 || i >= g_nlines || g_lines[i].heading) return;
    if (g_view == V_SERVICES) { g_sel_svc = g_lines[i].proc; InvalidateRect(g_wnd, NULL, FALSE); }
    else select_pid(g_procs[g_lines[i].proc].pid, TRUE);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        SetTimer(h, 1, 1000, NULL);
        return 0;
    case WM_TIMER:
        sample();
        if (g_view == V_SERVICES && g_perf.samples % 5 == 0) load_services();
        build_lines();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bm);
        draw(mem, rc.right, rc.bottom);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bm);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetFocus(h);
        click((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_MOUSEWHEEL:
        g_scroll[g_view] -= (short)HIWORD(wp) / 40;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_KEYDOWN: {
        BOOL ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        int page = visible_rows() > 1 ? visible_rows() - 1 : 1;
        switch (wp) {
        case VK_TAB:    if (ctrl) set_view(g_view + (shift ? -1 : 1)); break;
        case VK_UP:     if (g_view == V_PERFORMANCE) { g_perf_page = (g_perf_page + P_COUNT - 1) % P_COUNT; InvalidateRect(h, NULL, FALSE); } else move_sel(-1, FALSE); break;
        case VK_DOWN:   if (g_view == V_PERFORMANCE) { g_perf_page = (g_perf_page + 1) % P_COUNT; InvalidateRect(h, NULL, FALSE); } else move_sel(1, FALSE); break;
        case VK_PRIOR:  move_sel(-page, FALSE); break;
        case VK_NEXT:   move_sel(page, FALSE); break;
        case VK_HOME:   move_sel(-1, TRUE); break;
        case VK_END:    move_sel(1, TRUE); break;
        case VK_DELETE: if (g_view == V_PROCESSES || g_view == V_DETAILS) end_task(); break;
        case VK_F5:     SendMessageW(h, WM_TIMER, 1, 0); break;
        }
        return 0;
    }
    case WM_CHAR:
        if (wp > ' ' && wp < 0x7F && g_view != V_PERFORMANCE && g_view != V_SERVICES) find_typed((WCHAR)wp);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = 760;
        mm->ptMinTrackSize.y = 480;
        return 0;
    }
    case WM_DESTROY:
        say("taskmgr: closed\n");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int main(int argc, char **argv)
{
    /* One Task Manager: a second start brings the first forward */
    HWND old = FindWindowW(L"TaskManagerWindow", NULL);
    if (old) {
        ShowWindow(old, SW_RESTORE);
        SetForegroundWindow(old);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        if (!lstrcmpiA(argv[i], "/performance") || !lstrcmpiA(argv[i], "-performance")) g_view = V_PERFORMANCE;
        if (!lstrcmpiA(argv[i], "/details")) g_view = V_DETAILS;
        if (!lstrcmpiA(argv[i], "/services")) g_view = V_SERVICES;
    }
    g_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_bold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_big = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_small = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    cpu_brand();
    sample();                                     /* (a baseline: rates need two samples) */

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"TaskManagerWindow";
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(0, L"TaskManagerWindow", L"Task Manager", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1000, 660, NULL, NULL, wc.hInstance, NULL);
    if (!g_wnd) { say("taskmgr: cannot open its window\n"); return 1; }
    build_lines();
    say("taskmgr: open (%d logical processors, %s)\n", g_perf.ncpu, g_cpu_name);
    set_view(g_view);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
