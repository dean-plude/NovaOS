/*
 * sc.exe — the service control tool
 *
 *   sc create NAME binPath= PATH [type= own|share|kernel|filesys]
 *                 [start= boot|system|auto|demand|disabled] [DisplayName= TEXT]
 *   sc start NAME [ARGS...]
 *   sc stop NAME
 *   sc query NAME
 *   sc delete NAME
 *
 * As Windows' sc: each option is "name=" followed by its value as the next
 * argument ("name=value" in one is taken too), and the exit code is the
 * Win32 error of the call that failed (0 on success).  A kernel driver's
 * service (type= kernel) starts by loading its .sys file (NtLoadDriver,
 * through advapi32's StartService).
 *
 * Starting or stopping a driver needs SeLoadDriverPrivilege, which only an
 * administrator's elevated token holds.  Windows' sc runs as its caller
 * and works from an elevated prompt; NovaOS's Terminal is not elevated, so
 * this one's manifest asks for highestAvailable (sc.manifest).
 */
#include <windows.h>
#include <winsvc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

static WCHAR *wide(const char *s)
{
    static WCHAR buf[8][520];
    static int next;
    WCHAR *w = buf[next++ & 7];
    if (!MultiByteToWideChar(CP_ACP, 0, s, -1, w, 520)) w[0] = 0;
    return w;
}

static int failed(const char *what)
{
    DWORD e = GetLastError();
    char msg[256] = "";
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, msg, sizeof(msg), NULL);
    printf("[SC] %s FAILED %lu:\n\n%s\n", what, e, msg);
    return (int)e;
}

static const char *type_name(DWORD t)
{
    return t & SERVICE_KERNEL_DRIVER ? "KERNEL_DRIVER" : t & SERVICE_FILE_SYSTEM_DRIVER ? "FILE_SYSTEM_DRIVER" :
           t & SERVICE_WIN32_SHARE_PROCESS ? "WIN32_SHARE_PROCESS" : "WIN32_OWN_PROCESS";
}

static void show(const char *name, const SERVICE_STATUS *st)
{
    static const char *states[] = { "?", "STOPPED", "START_PENDING", "STOP_PENDING", "RUNNING", "CONTINUE_PENDING",
                                    "PAUSE_PENDING", "PAUSED" };
    printf("\nSERVICE_NAME: %s\n", name);
    printf("        TYPE               : %lx  %s\n", st->dwServiceType, type_name(st->dwServiceType));
    printf("        STATE              : %lu  %s\n", st->dwCurrentState,
           st->dwCurrentState < 8 ? states[st->dwCurrentState] : "?");
    printf("        WIN32_EXIT_CODE    : %lu  (0x%lx)\n", st->dwWin32ExitCode, st->dwWin32ExitCode);
    printf("        SERVICE_EXIT_CODE  : %lu  (0x%lx)\n", st->dwServiceSpecificExitCode, st->dwServiceSpecificExitCode);
    printf("        CHECKPOINT         : 0x%lx\n", st->dwCheckPoint);
    printf("        WAIT_HINT          : 0x%lx\n", st->dwWaitHint);
}

/* The value of option @key ("binPath=") in argv[from..], or NULL */
static const char *option(int argc, char **argv, int from, const char *key)
{
    size_t n = strlen(key);
    for (int i = from; i < argc; i++) {
        if (_strnicmp(argv[i], key, n)) continue;
        if (argv[i][n]) return argv[i] + n;
        return i + 1 < argc ? argv[i + 1] : "";
    }
    return NULL;
}

static int create(SC_HANDLE scm, const char *name, int argc, char **argv)
{
    const char *bin = option(argc, argv, 3, "binPath="), *type = option(argc, argv, 3, "type="),
               *start = option(argc, argv, 3, "start="), *display = option(argc, argv, 3, "DisplayName=");
    if (!bin || !*bin) { printf("[SC] CreateService: binPath= is required\n"); return ERROR_INVALID_PARAMETER; }
    DWORD t = SERVICE_WIN32_OWN_PROCESS, s = SERVICE_DEMAND_START;
    if (type) {
        if (!_stricmp(type, "kernel")) t = SERVICE_KERNEL_DRIVER;
        else if (!_stricmp(type, "filesys")) t = SERVICE_FILE_SYSTEM_DRIVER;
        else if (!_stricmp(type, "share")) t = SERVICE_WIN32_SHARE_PROCESS;
        else if (_stricmp(type, "own")) { printf("[SC] CreateService: unknown type= %s\n", type); return ERROR_INVALID_PARAMETER; }
    }
    if (start) {
        static const char *const names[] = { "boot", "system", "auto", "demand", "disabled" };
        s = 5;
        for (DWORD i = 0; i < 5; i++) if (!_stricmp(start, names[i])) s = i;
        if (s == 5) { printf("[SC] CreateService: unknown start= %s\n", start); return ERROR_INVALID_PARAMETER; }
    }
    WCHAR wname[260], wbin[520], wdisp[260];
    wcscpy(wname, wide(name));
    wcscpy(wbin, wide(bin));
    wcscpy(wdisp, wide(display ? display : name));
    SC_HANDLE h = CreateServiceW(scm, wname, wdisp, SERVICE_ALL_ACCESS, t, s, SERVICE_ERROR_NORMAL, wbin,
                                 NULL, NULL, NULL, NULL, NULL);
    if (!h) return failed("CreateService");
    CloseServiceHandle(h);
    printf("[SC] CreateService SUCCESS\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: sc create|start|stop|query|delete NAME [options]\n");
        return argc < 2 ? 0 : ERROR_INVALID_PARAMETER;
    }
    const char *cmd = argv[1], *name = argv[2];
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) return failed("OpenSCManager");
    int r = 0;
    if (!_stricmp(cmd, "create")) {
        r = create(scm, name, argc, argv);
    } else {
        SC_HANDLE h = OpenServiceW(scm, wide(name), SERVICE_ALL_ACCESS);
        SERVICE_STATUS st;
        memset(&st, 0, sizeof(st));
        if (!h) r = failed("OpenService");
        else if (!_stricmp(cmd, "start")) {
            LPCWSTR args[16];
            int n = 0;
            for (int i = 3; i < argc && n < 16; i++) {
                WCHAR *w = malloc(520 * sizeof(WCHAR));
                if (w) { wcscpy(w, wide(argv[i])); args[n++] = w; }
            }
            if (!StartServiceW(h, (DWORD)n, n ? args : NULL)) r = failed("StartService");
            else if (QueryServiceStatus(h, &st)) show(name, &st);
        } else if (!_stricmp(cmd, "stop")) {
            if (!ControlService(h, SERVICE_CONTROL_STOP, &st)) r = failed("ControlService");
            else show(name, &st);
        } else if (!_stricmp(cmd, "query")) {
            if (!QueryServiceStatus(h, &st)) r = failed("QueryService");
            else show(name, &st);
        } else if (!_stricmp(cmd, "delete")) {
            if (!DeleteService(h)) r = failed("DeleteService");
            else printf("[SC] DeleteService SUCCESS\n");
        } else {
            printf("sc: unknown command %s\n", cmd);
            r = ERROR_INVALID_PARAMETER;
        }
        if (h) CloseServiceHandle(h);
    }
    CloseServiceHandle(scm);
    return r;
}
