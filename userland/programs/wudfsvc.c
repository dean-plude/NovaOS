/*
 * wudfsvc.exe — the UMDF service ("wudfsvc"): Windows Driver Foundation's
 * user-mode driver framework, which runs UMDF drivers' hosts
 *
 * A device whose INF installs a UMDF driver gets the reflector,
 * WUDFRd.sys, as its kernel function driver (tools/umdf/wudfrd.c); the
 * UMDF driver itself runs in a driver host process, WUDFHost.exe
 * (tools/umdf/host), one per device.  The registry's
 * HKLM\SYSTEM\CurrentControlSet\Services\wudfsvc (made by the kernel at
 * boot, automatic start) runs this service, which watches the kernel's
 * device tree (novapnp.h) and starts "WUDFHost.exe <instance ID>" for
 * each started device the reflector runs that has no live host: at its
 * start, when a device arrives, and each second after (a device that
 * restarts gets a new host; the old one exits when the reflector removes
 * the device).  A host that dies while its device stays started is
 * restarted three times at most.  The service takes no stop control.
 */
#include <windows.h>
#include <winsvc.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <novapnp.h>

static SERVICE_STATUS_HANDLE g_status;
static SERVICE_STATUS g_st;

static void say(const char *fmt, ...)
{
    char line[300], out[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    snprintf(out, sizeof(out), "wudfsvc: %s\n", line);
    OutputDebugStringA(out);
}

static void report(DWORD state)
{
    g_st.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_st.dwCurrentState = state;
    g_st.dwControlsAccepted = 0;
    SetServiceStatus(g_status, &g_st);
}

static DWORD WINAPI handler(DWORD control, DWORD type, LPVOID data, LPVOID ctx)
{
    (void)type; (void)data; (void)ctx;
    return control == SERVICE_CONTROL_INTERROGATE ? 0 : ERROR_CALL_NOT_IMPLEMENTED;
}

/* A device the service has started a host for */
typedef struct {
    DWORD id;                       /* the node */
    HANDLE process;                 /* its host, while the device has been started */
    int restarts;
} Host;

#define MAX_HOSTS 64
static Host g_hosts[MAX_HOSTS];

static Host *host_of(DWORD id)
{
    for (int i = 0; i < MAX_HOSTS; i++)
        if (g_hosts[i].id == id) return &g_hosts[i];
    return NULL;
}

static BOOL host_alive(const Host *h)
{
    return h->process && WaitForSingleObject(h->process, 0) == WAIT_TIMEOUT;
}

static void start_host(const NovaPnpNode *n, Host *h)
{
    WCHAR cmd[400], dir[MAX_PATH];
    GetSystemDirectoryW(dir, MAX_PATH);
    int k = swprintf(cmd, 400, L"\"%ls\\WUDFHost.exe\" \"", dir);
    for (const char *c = n->instance; *c && k < 396; c++) cmd[k++] = (WCHAR)(unsigned char)*c;
    cmd[k++] = L'"';
    cmd[k] = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (h->process) CloseHandle(h->process);
    h->process = NULL;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        say("the driver host for %s did not start (error %lu)", n->instance, (unsigned long)GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    h->process = pi.hProcess;
    say("driver host %lu started for %s", (unsigned long)pi.dwProcessId, n->instance);
}

/* Start the hosts the tree's started UMDF devices lack */
static void scan(void)
{
    DWORD count = nova_pnp_count();
    for (DWORD id = 2; id <= count; id++) {
        NovaPnpNode n;
        if (!nova_pnp_node(id, &n)) continue;
        Host *h = host_of(id);
        if ((n.flags & (NOVA_PNP_PRESENT | NOVA_PNP_STARTED | NOVA_PNP_WINDOWS)) !=
                (NOVA_PNP_PRESENT | NOVA_PNP_STARTED | NOVA_PNP_WINDOWS) ||
            _stricmp(n.service, "WUDFRd")) {
            if (h && h->process) {              /* (stopped: its next start gets a new host) */
                CloseHandle(h->process);
                h->process = NULL;
                h->restarts = 0;
            }
            continue;
        }
        if (h && host_alive(h)) continue;
        if (h && h->process) {
            /* the host exited with the device still started */
            if (h->restarts >= 3) continue;
            h->restarts++;
            say("the driver host for %s exited; restarting it", n.instance);
        } else if (!h) {
            h = host_of(0);
            if (!h) continue;
            h->id = id;
            h->restarts = 0;
        }
        start_host(&n, h);
    }
}

static void WINAPI service_main(DWORD argc, LPWSTR *argv)
{
    (void)argc;
    g_status = RegisterServiceCtrlHandlerExW(argv[0], handler, NULL);
    report(SERVICE_START_PENDING);
    NovaPnpRead r;
    memset(&r, 0, sizeof(r));
    nova_pnp_events(&r);                          /* (max 0: the newest event, followed from here) */
    DWORD after = r.newest;
    report(SERVICE_RUNNING);
    say("UMDF service running");
    for (;;) {
        scan();
        memset(&r, 0, sizeof(r));
        r.after = after;
        r.wait_ms = 1000;
        r.max = 32;
        int n = nova_pnp_events(&r);
        if (n) after = r.e[n - 1].seq;
    }
}

int main(void)
{
    SERVICE_TABLE_ENTRYW table[] = { { (LPWSTR)L"wudfsvc", service_main }, { NULL, NULL } };
    if (StartServiceCtrlDispatcherW(table)) return 0;
    printf("wudfsvc: the UMDF service; the service control manager starts it (error %lu)\n",
           (unsigned long)GetLastError());
    return 1;
}
