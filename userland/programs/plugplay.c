/*
 * plugplay.exe — the Plug and Play service ("PlugPlay")
 *
 * The registry's HKLM\SYSTEM\CurrentControlSet\Services\PlugPlay (made by
 * the kernel at boot, automatic start) runs it as a service, so the
 * service control manager reports PlugPlay running, as programs that
 * check for it expect.  It keeps HKLM\SYSTEM\CurrentControlSet\Enum the
 * way Windows' Plug and Play manager does: a key per device the kernel's
 * device tree (novapnp.h) has known, Enum\<instance ID>, with DeviceDesc,
 * HardwareID, CompatibleIDs, Class, ClassGUID, Service, Mfg,
 * LocationInformation, FriendlyName and ConfigFlags; written for every
 * node at start and again for each one that arrives.  Like Windows', the
 * keys stay when a device leaves.  The service takes no stop control.
 */
#include <windows.h>
#include <winsvc.h>
#include <stdio.h>
#include <string.h>
#include <novapnp.h>

#define ENUM_KEY "SYSTEM\\CurrentControlSet\\Enum\\"

static SERVICE_STATUS_HANDLE g_status;
static SERVICE_STATUS g_st;

static void say(const char *fmt, ...)
{
    char line[300], out[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    snprintf(out, sizeof(out), "[PNP] %s\n", line);
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

static DWORD multi_len(const char *m)
{
    DWORD n = 0;
    while (m[n]) n += (DWORD)strlen(m + n) + 1;
    return n + 1;
}

static void sz(HKEY k, const char *name, const char *v)
{
    if (v[0]) RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)strlen(v) + 1);
}

/* Enum\<instance> for node @n */
static BOOL write_node(const NovaPnpNode *n)
{
    char path[200];
    snprintf(path, sizeof(path), ENUM_KEY "%s", n->instance);
    HKEY k;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) return FALSE;
    sz(k, "DeviceDesc", n->desc);
    if (n->hwids[0]) RegSetValueExA(k, "HardwareID", 0, REG_MULTI_SZ, (const BYTE *)n->hwids, multi_len(n->hwids));
    if (n->compat[0]) RegSetValueExA(k, "CompatibleIDs", 0, REG_MULTI_SZ, (const BYTE *)n->compat, multi_len(n->compat));
    sz(k, "Class", n->cls);
    GUID g;
    if (nova_pnp_class_guid(n->cls, &g)) {
        char gs[40];
        snprintf(gs, sizeof(gs), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned long)g.Data1, g.Data2, g.Data3,
                 g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
        sz(k, "ClassGUID", gs);
    }
    if (!(n->flags & NOVA_PNP_RAW)) sz(k, "Service", n->service);    /* (a raw PDO has no service of its own) */
    sz(k, "Mfg", n->mfg);
    sz(k, "LocationInformation", n->location);
    sz(k, "FriendlyName", n->friendly);
    DWORD flags = 0;
    RegSetValueExA(k, "ConfigFlags", 0, REG_DWORD, (const BYTE *)&flags, sizeof(flags));
    RegCloseKey(k);
    return TRUE;
}

static void WINAPI service_main(DWORD argc, LPWSTR *argv)
{
    (void)argc;
    g_status = RegisterServiceCtrlHandlerExW(argv[0], handler, NULL);
    report(SERVICE_START_PENDING);
    NovaPnpRead r;
    memset(&r, 0, sizeof(r));
    nova_pnp_events(&r);                          /* (max 0: the newest event, followed from here) */
    DWORD after = r.newest, count = nova_pnp_count(), written = 0;
    for (DWORD id = 1; id <= count; id++) {
        NovaPnpNode n;
        if (nova_pnp_node(id, &n) && id != NOVA_PNP_ROOT && write_node(&n)) written++;
    }
    report(SERVICE_RUNNING);
    say("PlugPlay service running: %lu devices in HKLM\\" ENUM_KEY, (unsigned long)written);
    for (;;) {
        memset(&r, 0, sizeof(r));
        r.after = after;
        r.wait_ms = 1000;
        r.max = 32;
        int n = nova_pnp_events(&r);
        for (int i = 0; i < n; i++) {
            NovaPnpNode node;
            if (r.e[i].kind == NOVA_PNP_ARRIVAL && nova_pnp_node(r.e[i].id, &node)) write_node(&node);
        }
        if (n) after = r.e[n - 1].seq;
    }
}

int main(void)
{
    SERVICE_TABLE_ENTRYW table[] = { { (LPWSTR)L"PlugPlay", service_main }, { NULL, NULL } };
    if (StartServiceCtrlDispatcherW(table)) return 0;
    printf("plugplay: the Plug and Play service; the service control manager starts it (error %lu)\n",
           (unsigned long)GetLastError());
    return 1;
}
