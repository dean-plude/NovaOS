/*
 * spoolsv.exe — the print spooler service ("Spooler", Print Spooler)
 *
 * Keeps the print queue.  Programs reach it through winspool.drv over
 * \\.\pipe\spoolss (novaspool.h): a job starts (the spooler numbers it and
 * names its spool file, C:\Windows\System32\spool\PRINTERS\NNNNN.SPL,
 * which the program writes), gains pages, and ends; the spooler then
 * sends the finished job to its printer's port and deletes it.  Each job
 * also has a shadow file, NNNNN.SHD (its SplJob), so jobs that were
 * complete when the computer stopped print after the next start, as on
 * Windows; unfinished ones are dropped.
 *
 * Ports (the printer key's Port value):
 *   PORTPROMPT:, FILE:  the job's output file (DOC_INFO_1's pOutputFile;
 *                       winspool asked for one with Save As)
 *   a file path         that file (a local port named after a file)
 *   nul:                nowhere
 * Any other port (a network printer's) leaves the job in the queue with
 * an error: IPP printing is the next piece (docs/design/printing.md).
 * A paused printer or job keeps its jobs waiting.
 *
 * The kernel makes the service's key at boot (automatic start), and the
 * Microsoft Print to PDF printer's key.
 */
#include <windows.h>
#include <winsvc.h>
#include <winreg.h>
#include <winspool.h>
#include <novaspool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static SERVICE_STATUS_HANDLE g_status;
static SERVICE_STATUS g_st;
static volatile LONG g_stop;
static CRITICAL_SECTION g_lock;
static HANDLE g_work;                      /* set: a job may be ready to print */
static SplJob g_jobs[NOVA_SPL_MAX_JOBS];
static int g_njobs;
static DWORD g_next_id = 1;

static void say(const char *fmt, ...)
{
    char line[600], out[620];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    snprintf(out, sizeof(out), "[SPOOLER] %s\n", line);
    OutputDebugStringA(out);
}

static void report(DWORD state)
{
    g_st.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_st.dwCurrentState = state;
    g_st.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    SetServiceStatus(g_status, &g_st);
}

static void job_path(DWORD id, const WCHAR *ext, WCHAR *out)
{
    swprintf(out, MAX_PATH, L"%ls\\%05lu.%ls", NOVA_SPL_DIR, (unsigned long)id, ext);
}

static void save_shadow(const SplJob *j)
{
    WCHAR p[MAX_PATH];
    job_path(j->id, L"SHD", p);
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD n;
    WriteFile(f, j, sizeof *j, &n, NULL);
    CloseHandle(f);
}

static void delete_files(DWORD id)
{
    WCHAR p[MAX_PATH];
    job_path(id, L"SPL", p);
    DeleteFileW(p);
    job_path(id, L"SHD", p);
    DeleteFileW(p);
}

static SplJob *find_job(DWORD id)
{
    for (int i = 0; i < g_njobs; i++) if (g_jobs[i].id == id) return &g_jobs[i];
    return NULL;
}

static void remove_job(DWORD id)
{
    for (int i = 0; i < g_njobs; i++)
        if (g_jobs[i].id == id) {
            memmove(&g_jobs[i], &g_jobs[i + 1], (size_t)(g_njobs - i - 1) * sizeof g_jobs[0]);
            g_njobs--;
            break;
        }
    delete_files(id);
}

/* Jobs left from before the last stop: the complete ones wait to print */
static void reload(void)
{
    WCHAR pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    swprintf(pat, MAX_PATH, L"%ls\\*.SHD", NOVA_SPL_DIR);
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        WCHAR p[MAX_PATH];
        SplJob j;
        DWORD n = 0;
        swprintf(p, MAX_PATH, L"%ls\\%ls", NOVA_SPL_DIR, fd.cFileName);
        HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        BOOL ok = ReadFile(f, &j, sizeof j, &n, NULL) && n == sizeof j;
        CloseHandle(f);
        if (ok && j.id >= g_next_id) g_next_id = j.id + 1;
        if (ok && j.complete && g_njobs < NOVA_SPL_MAX_JOBS) {
            j.status &= ~(JOB_STATUS_PRINTING | JOB_STATUS_SPOOLING);
            g_jobs[g_njobs++] = j;
            say("Job %lu \"%ls\" from before the last stop waits to print", (unsigned long)j.id, j.document);
        } else if (ok) {
            delete_files(j.id);
        } else {
            DeleteFileW(p);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* -----------------------------------------------------------------------
 * Printing: a finished job to its printer's port
 * ----------------------------------------------------------------------- */
static BOOL printer_info(const WCHAR *name, WCHAR *port, DWORD *status, DWORD *attrs)
{
    WCHAR path[300];
    HKEY k;
    swprintf(path, 300, L"%ls\\%ls", NOVA_SPL_PRINTERS, name);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k)) return FALSE;
    DWORD type, cb = MAX_PATH * sizeof(WCHAR);
    port[0] = 0;
    if (RegQueryValueExW(k, L"Port", 0, &type, (BYTE *)port, &cb) || type != REG_SZ) port[0] = 0;
    port[MAX_PATH - 1] = 0;
    cb = sizeof *status;
    if (RegQueryValueExW(k, L"Status", 0, &type, (BYTE *)status, &cb) || type != REG_DWORD) *status = 0;
    cb = sizeof *attrs;
    if (RegQueryValueExW(k, L"Attributes", 0, &type, (BYTE *)attrs, &cb) || type != REG_DWORD) *attrs = 0;
    RegCloseKey(k);
    return TRUE;
}

static BOOL path_port(const WCHAR *p)
{
    return (p[0] && p[1] == ':' && (p[2] == '\\' || p[2] == '/')) || (p[0] == '\\' && p[1] == '\\');
}

/* Send job @j (a copy) to its port: 1 printed, 0 failed (stays, with an error), -1 not yet */
static int print_job(const SplJob *j)
{
    WCHAR port[MAX_PATH], spl[MAX_PATH];
    DWORD status = 0, attrs = 0;
    if (!printer_info(j->printer, port, &status, &attrs)) {
        say("Job %lu: printer \"%ls\" is gone", (unsigned long)j->id, j->printer);
        return 0;
    }
    if (status & PRINTER_STATUS_PAUSED) return -1;
    job_path(j->id, L"SPL", spl);
    const WCHAR *dest = NULL;
    if (!_wcsicmp(port, L"PORTPROMPT:") || !_wcsicmp(port, L"FILE:")) dest = j->output;
    else if (path_port(port)) dest = j->output[0] ? j->output : port;
    else if (!_wcsicmp(port, L"nul:") || !_wcsicmp(port, L"NUL")) dest = L"";
    else if (j->output[0]) dest = j->output;          /* (printing to a file instead of the port) */
    if (!dest) {
        say("Job %lu \"%ls\": port %ls is not supported yet", (unsigned long)j->id, j->document, port);
        return 0;
    }
    if (dest[0] && !CopyFileW(spl, dest, FALSE)) {
        say("Job %lu \"%ls\": could not write %ls (error %lu)", (unsigned long)j->id, j->document, dest,
            (unsigned long)GetLastError());
        return 0;
    }
    say("Printed job %lu \"%ls\" on %ls: %lu pages, %lu bytes to %ls", (unsigned long)j->id, j->document, j->printer,
        (unsigned long)j->pages, (unsigned long)j->size, dest[0] ? dest : L"nul:");
    return (attrs & PRINTER_ATTRIBUTE_KEEPPRINTEDJOBS) ? 2 : 1;
}

static DWORD WINAPI worker(LPVOID arg)
{
    (void)arg;
    while (!g_stop) {
        WaitForSingleObject(g_work, 2000);
        for (;;) {
            SplJob j;
            BOOL have = FALSE;
            EnterCriticalSection(&g_lock);
            for (int i = 0; i < g_njobs && !have; i++) {
                SplJob *c = &g_jobs[i];
                if (c->complete && !(c->status & (JOB_STATUS_PAUSED | JOB_STATUS_ERROR | JOB_STATUS_PRINTING | JOB_STATUS_PRINTED))) {
                    c->status |= JOB_STATUS_PRINTING;
                    j = *c;
                    have = TRUE;
                }
            }
            LeaveCriticalSection(&g_lock);
            if (!have) break;
            int r = print_job(&j);
            EnterCriticalSection(&g_lock);
            SplJob *c = find_job(j.id);
            if (c) {
                c->status &= ~JOB_STATUS_PRINTING;
                if (r == 1 || (c->status & JOB_STATUS_DELETING)) remove_job(j.id);
                else if (r == 2) { c->status |= JOB_STATUS_PRINTED; save_shadow(c); }
                else if (r == 0) { c->status |= JOB_STATUS_ERROR; save_shadow(c); }
            }
            LeaveCriticalSection(&g_lock);
            if (r < 0) break;                      /* (a paused printer: look again later) */
        }
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Requests from winspool
 * ----------------------------------------------------------------------- */
static void serve(const SplRequest *rq, SplReply *rp)
{
    memset(rp, 0, sizeof *rp);
    EnterCriticalSection(&g_lock);
    SplJob *j = rq->op != SPL_START && rq->op != SPL_ENUM ? find_job(rq->job) : NULL;
    switch (rq->op) {
    case SPL_START: {
        if (g_njobs >= NOVA_SPL_MAX_JOBS) { rp->error = ERROR_NOT_ENOUGH_MEMORY; break; }
        WCHAR port[MAX_PATH];
        DWORD status, attrs;
        if (!printer_info(rq->printer, port, &status, &attrs)) { rp->error = ERROR_INVALID_PRINTER_NAME; break; }
        SplJob *n = &g_jobs[g_njobs++];
        memset(n, 0, sizeof *n);
        n->id = g_next_id++;
        n->status = JOB_STATUS_SPOOLING;
        GetLocalTime(&n->submitted);
        memcpy(n->printer, rq->printer, sizeof n->printer);
        memcpy(n->document, rq->document, sizeof n->document);
        memcpy(n->output, rq->output, sizeof n->output);
        memcpy(n->datatype, rq->datatype, sizeof n->datatype);
        memcpy(n->user, rq->user, sizeof n->user);
        n->printer[NOVA_SPL_NAME - 1] = n->document[NOVA_SPL_NAME - 1] = n->output[MAX_PATH - 1] = 0;
        n->datatype[31] = n->user[63] = 0;
        save_shadow(n);
        rp->job = n->id;
        job_path(n->id, L"SPL", rp->path);
        break;
    }
    case SPL_PAGE:
        if (!j) { rp->error = ERROR_INVALID_PARAMETER; break; }
        j->pages++;
        break;
    case SPL_END:
        if (!j) { rp->error = ERROR_INVALID_PARAMETER; break; }
        j->size = rq->arg;
        j->complete = 1;
        j->status &= ~JOB_STATUS_SPOOLING;
        save_shadow(j);
        say("Job %lu \"%ls\" queued on %ls (%lu bytes)", (unsigned long)j->id, j->document, j->printer,
            (unsigned long)j->size);
        SetEvent(g_work);
        break;
    case SPL_ABORT:
        if (!j) { rp->error = ERROR_INVALID_PARAMETER; break; }
        remove_job(j->id);
        break;
    case SPL_ENUM:
        for (int i = 0; i < g_njobs && rp->count < NOVA_SPL_MAX_JOBS; i++)
            if (!rq->printer[0] || !_wcsicmp(rq->printer, g_jobs[i].printer)) rp->jobs[rp->count++] = g_jobs[i];
        break;
    case SPL_SETJOB:
        if (!j) { rp->error = ERROR_INVALID_PARAMETER; break; }
        switch (rq->arg) {
        case JOB_CONTROL_PAUSE: j->status |= JOB_STATUS_PAUSED; break;
        case JOB_CONTROL_RESUME: j->status &= ~JOB_STATUS_PAUSED; SetEvent(g_work); break;
        case JOB_CONTROL_RESTART: j->status &= ~(JOB_STATUS_ERROR | JOB_STATUS_PRINTED); SetEvent(g_work); break;
        case JOB_CONTROL_CANCEL:
        case JOB_CONTROL_DELETE:
            if (j->status & JOB_STATUS_PRINTING) j->status |= JOB_STATUS_DELETING;
            else if (j->complete || (j->status & JOB_STATUS_ERROR)) remove_job(j->id);
            else j->status |= JOB_STATUS_DELETING;     /* (still being written: its EndDoc ends it) */
            break;
        default: rp->error = ERROR_INVALID_PARAMETER; break;
        }
        if (j && (j = find_job(rq->job)) != NULL) save_shadow(j);
        break;
    default:
        rp->error = ERROR_INVALID_FUNCTION;
    }
    LeaveCriticalSection(&g_lock);
}

static HANDLE listen_pipe(void)
{
    return CreateNamedPipeW(NOVA_SPL_PIPE, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                            PIPE_UNLIMITED_INSTANCES, sizeof(SplReply), sizeof(SplRequest), 0, NULL);
}

static DWORD WINAPI handler(DWORD control, DWORD type, LPVOID data, LPVOID ctx)
{
    (void)type; (void)data; (void)ctx;
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        report(SERVICE_STOP_PENDING);
        InterlockedExchange(&g_stop, 1);
        SetEvent(g_work);
        HANDLE h = CreateFileW(NOVA_SPL_PIPE, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);        /* (wakes the listening pipe) */
        return ERROR_SUCCESS;
    }
    if (control == SERVICE_CONTROL_INTERROGATE) return ERROR_SUCCESS;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI service_main(DWORD argc, LPWSTR *argv)
{
    (void)argc;
    g_status = RegisterServiceCtrlHandlerExW(argv[0], handler, NULL);
    report(SERVICE_START_PENDING);
    InitializeCriticalSection(&g_lock);
    g_work = CreateEventW(NULL, FALSE, FALSE, NULL);
    CreateDirectoryW(L"C:\\Windows\\System32\\spool", NULL);
    CreateDirectoryW(NOVA_SPL_DIR, NULL);
    reload();
    HANDLE th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (g_njobs) SetEvent(g_work);
    static SplRequest rq;
    static SplReply rp;
    report(SERVICE_RUNNING);
    say("Print spooler running: queue in %ls", NOVA_SPL_DIR);
    /* the next instance listens before a request is served, so a client
     * never finds the pipe missing between two requests */
    HANDLE next = listen_pipe();
    while (!g_stop) {
        HANDLE p = next;
        if (p == INVALID_HANDLE_VALUE) {
            say("cannot make %ls (error %lu)", NOVA_SPL_PIPE, (unsigned long)GetLastError());
            Sleep(1000);
            next = listen_pipe();
            continue;
        }
        BOOL ok = ConnectNamedPipe(p, NULL) || GetLastError() == ERROR_PIPE_CONNECTED;
        next = listen_pipe();
        DWORD n = 0;
        BOOL got = ok && !g_stop && ReadFile(p, &rq, sizeof rq, &n, NULL) && n == sizeof rq;
        if (ok && !g_stop && !got) say("a request could not be read (error %lu, %lu bytes)", (unsigned long)GetLastError(), (unsigned long)n);
        if (got) {
            serve(&rq, &rp);
            DWORD size = (DWORD)(offsetof(SplReply, jobs) + rp.count * sizeof rp.jobs[0]);
            WriteFile(p, &rp, size, &n, NULL);
            FlushFileBuffers(p);
            /* the client reads the reply, then closes its end: wait for that
             * (a disconnect would drop a reply not yet read), up to 2 s */
            for (int i = 0; i < 200; i++) {
                DWORD avail = 0;
                if (!PeekNamedPipe(p, NULL, 0, NULL, &avail, NULL)) break;
                Sleep(10);
            }
        }
        DisconnectNamedPipe(p);
        CloseHandle(p);
    }
    if (next != INVALID_HANDLE_VALUE) CloseHandle(next);
    WaitForSingleObject(th, 5000);
    say("Print spooler stopped");
    report(SERVICE_STOPPED);
}

int main(void)
{
    SERVICE_TABLE_ENTRYW table[] = { { (LPWSTR)L"Spooler", service_main }, { NULL, NULL } };
    if (StartServiceCtrlDispatcherW(table)) return 0;
    printf("spoolsv: the print spooler service; the service control manager starts it (error %lu)\n",
           (unsigned long)GetLastError());
    return 1;
}
