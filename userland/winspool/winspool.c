/*
 * winspool.drv — the print spooler's client calls
 *
 * Printers are the keys under HKLM\SYSTEM\CurrentControlSet\Control\
 * Print\Printers, as on Windows (Name, Port, Printer Driver, Attributes,
 * Description, Location...); the default printer is HKCU's
 * Software\Microsoft\Windows NT\CurrentVersion\Windows "Device"
 * ("name,winspool,port"), or the first printer when that is unset.  This
 * DLL reads and writes those keys itself (EnumPrinters, GetPrinter,
 * AddPrinter, SetDefaultPrinter...).
 *
 * A print job goes through the Print Spooler service (spoolsv.exe) over
 * \\.\pipe\spoolss (novaspool.h): StartDocPrinter asks it for a job number
 * and a spool file, WritePrinter writes that file, EndDocPrinter hands
 * the job back and the spooler sends it to the printer's port.  With the
 * spooler stopped, StartDocPrinter fails with RPC_S_SERVER_UNAVAILABLE, as
 * on Windows.  A printer on a file port (PORTPROMPT:, FILE:; Microsoft
 * Print to PDF's) asks where to save when the program names no output
 * file, with the Save As dialog.
 *
 * What a job's bytes are is the printer driver's business: gdi32's
 * printer DC (gdi32/print.c) renders pages and writes a PDF through
 * these calls; a program may also write raw bytes itself.
 */
#define NOVA_BUILD_WINSPOOL
#include <windows.h>
#include <winreg.h>
#include <winspool.h>
#include <novaspool.h>
#include <string.h>
#include <wchar.h>

#define SPLAPI __declspec(dllexport)

#ifndef ERROR_INVALID_HANDLE
#define ERROR_INVALID_HANDLE 6
#endif
#ifndef ERROR_UNKNOWN_PRINTER_DRIVER
#define ERROR_UNKNOWN_PRINTER_DRIVER 1797
#endif
#ifndef ERROR_INVALID_LEVEL
#define ERROR_INVALID_LEVEL 124
#endif
#ifndef ERROR_CANCELLED
#define ERROR_CANCELLED 1223
#endif
#ifndef ERROR_NOT_SUPPORTED
#define ERROR_NOT_SUPPORTED 50
#endif
#ifndef ERROR_SPL_NO_STARTDOC
#define ERROR_SPL_NO_STARTDOC 3003
#endif

#ifndef MAX_COMPUTERNAME_LENGTH
#define MAX_COMPUTERNAME_LENGTH 15
#endif
#define DEVICE_KEY L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows"
#define MAX_PRINTERS 64
#define SPL_MAGIC 0x4C505357u            /* "WSPL" */

/* The printer drivers NovaOS has (EnumPrinterDrivers, AddPrinter) */
static const WCHAR *const g_drivers[] = { NOVA_SPL_PDF_DRIVER, L"Microsoft IPP Class Driver" };

typedef struct {
    WCHAR name[NOVA_SPL_NAME], port[MAX_PATH], driver[NOVA_SPL_NAME];
    WCHAR comment[NOVA_SPL_NAME], location[NOVA_SPL_NAME], share[NOVA_SPL_NAME];
    WCHAR processor[32], datatype[32];
    DWORD attributes, priority, status;
} Printer;

typedef struct {
    DWORD magic;
    BOOL server;                        /* OpenPrinter(NULL): the print server */
    Printer p;
    DWORD job;                          /* the job StartDocPrinter began (0: none) */
    HANDLE file;                        /* its spool file */
    DWORD written, pages;
    BOOL in_page;
} Spl;

static int wlen(const WCHAR *s) { int n = 0; if (s) while (s[n]) n++; return n; }
static void wcopy(WCHAR *d, const WCHAR *s, int cap)
{
    int i = 0;
    for (; s && s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}
static WCHAR lower(WCHAR c) { return c >= 'A' && c <= 'Z' ? (WCHAR)(c + 32) : c; }
static BOOL wsame(const WCHAR *a, const WCHAR *b)
{
    if (!a || !b) return FALSE;
    for (; *a && lower(*a) == lower(*b); a++, b++) {}
    return lower(*a) == lower(*b);
}
static BOOL wprefix(const WCHAR *s, const WCHAR *p)
{
    for (; *p; s++, p++) if (lower(*s) != lower(*p)) return FALSE;
    return TRUE;
}

static Spl *spl_of(HANDLE h)
{
    Spl *s = (Spl *)h;
    if (!s || s->magic != SPL_MAGIC) { SetLastError(ERROR_INVALID_HANDLE); return NULL; }
    return s;
}

/* -----------------------------------------------------------------------
 * The printers' registry keys
 * ----------------------------------------------------------------------- */
static void reg_sz(HKEY k, const WCHAR *name, WCHAR *out, DWORD cap)
{
    DWORD type = 0, cb = cap * sizeof(WCHAR);
    out[0] = 0;
    if (RegQueryValueExW(k, name, 0, &type, (BYTE *)out, &cb) || (type != REG_SZ && type != REG_EXPAND_SZ)) out[0] = 0;
    out[cap - 1] = 0;
}
static DWORD reg_dw(HKEY k, const WCHAR *name, DWORD def)
{
    DWORD v = 0, type = 0, cb = sizeof v;
    return RegQueryValueExW(k, name, 0, &type, (BYTE *)&v, &cb) || type != REG_DWORD ? def : v;
}
static void set_sz(HKEY k, const WCHAR *name, const WCHAR *v)
{
    if (!v) v = L"";
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)(wlen(v) + 1) * sizeof(WCHAR));
}
static void set_dw(HKEY k, const WCHAR *name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof v); }

static BOOL load_printer(const WCHAR *name, Printer *p)
{
    WCHAR path[300];
    HKEY k;
    if (!name || !*name || wlen(name) >= NOVA_SPL_NAME) return FALSE;
    wcopy(path, NOVA_SPL_PRINTERS L"\\", 300);
    wcopy(path + wlen(path), name, 300 - wlen(path));
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k)) return FALSE;
    memset(p, 0, sizeof *p);
    reg_sz(k, L"Name", p->name, NOVA_SPL_NAME);
    if (!p->name[0]) wcopy(p->name, name, NOVA_SPL_NAME);
    reg_sz(k, L"Port", p->port, MAX_PATH);
    reg_sz(k, L"Printer Driver", p->driver, NOVA_SPL_NAME);
    reg_sz(k, L"Description", p->comment, NOVA_SPL_NAME);
    reg_sz(k, L"Location", p->location, NOVA_SPL_NAME);
    reg_sz(k, L"Share Name", p->share, NOVA_SPL_NAME);
    reg_sz(k, L"Print Processor", p->processor, 32);
    reg_sz(k, L"Datatype", p->datatype, 32);
    if (!p->processor[0]) wcopy(p->processor, L"winprint", 32);
    if (!p->datatype[0]) wcopy(p->datatype, L"RAW", 32);
    p->attributes = reg_dw(k, L"Attributes", PRINTER_ATTRIBUTE_LOCAL);
    p->priority = reg_dw(k, L"Priority", 1);
    p->status = reg_dw(k, L"Status", 0);
    RegCloseKey(k);
    return TRUE;
}

/* The printers' names, in the registry's order; their count */
static int list_printers(WCHAR (*names)[NOVA_SPL_NAME], int cap)
{
    HKEY k;
    int n = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NOVA_SPL_PRINTERS, 0, KEY_READ, &k)) return 0;
    for (DWORD i = 0; n < cap; i++) {
        DWORD cch = NOVA_SPL_NAME;
        if (RegEnumKeyExW(k, i, names[n], &cch, 0, 0, 0, 0)) break;
        n++;
    }
    RegCloseKey(k);
    return n;
}

/* The default printer's name (the first printer when none is set); FALSE with no printers */
static BOOL default_printer(WCHAR *out)
{
    WCHAR dev[400];
    HKEY k;
    Printer p;
    dev[0] = 0;
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, DEVICE_KEY, 0, KEY_READ, &k)) {
        reg_sz(k, L"Device", dev, 400);
        RegCloseKey(k);
    }
    for (WCHAR *c = dev; *c; c++) if (*c == ',') { *c = 0; break; }
    if (dev[0] && load_printer(dev, &p)) { wcopy(out, p.name, NOVA_SPL_NAME); return TRUE; }
    static WCHAR names[MAX_PRINTERS][NOVA_SPL_NAME];
    int n = list_printers(names, MAX_PRINTERS);
    for (int i = 0; i < n; i++)
        if (load_printer(names[i], &p)) { wcopy(out, p.name, NOVA_SPL_NAME); return TRUE; }
    return FALSE;
}

static BOOL is_default(const WCHAR *name)
{
    WCHAR d[NOVA_SPL_NAME];
    return default_printer(d) && wsame(d, name);
}

/* -----------------------------------------------------------------------
 * The spooler
 * ----------------------------------------------------------------------- */
static BOOL spooler(SplRequest *rq, SplReply *rp)
{
    DWORD got = 0;
    BOOL ok = FALSE;
    for (int i = 0; i < 3 && !ok; i++) {          /* (a busy spooler: a moment later) */
        memset(rp, 0, sizeof *rp);
        ok = CallNamedPipeW(NOVA_SPL_PIPE, rq, sizeof *rq, rp, sizeof *rp, &got, 5000) && got >= offsetof(SplReply, jobs);
        if (!ok) Sleep(50);
    }
    if (!ok) {
        char m[96];
        wsprintfA(m, "[WINSPOOL] the spooler did not answer (error %lu, %lu bytes)\n", GetLastError(), got);
        OutputDebugStringA(m);
        SetLastError(RPC_S_SERVER_UNAVAILABLE);
        return FALSE;
    }
    if (rp->error) { SetLastError(rp->error); return FALSE; }
    return TRUE;
}

static SplReply g_reply;                 /* (EnumJobs' answer is too big for the stack) */
static CRITICAL_SECTION g_lock;
static LONG g_lock_ready;
static void lock(void)
{
    if (InterlockedCompareExchange(&g_lock_ready, 1, 0) == 0) { InitializeCriticalSection(&g_lock); g_lock_ready = 2; }
    while (g_lock_ready != 2) Sleep(0);
    EnterCriticalSection(&g_lock);
}
static void unlock(void) { LeaveCriticalSection(&g_lock); }

static DWORD job_count(const WCHAR *printer)
{
    SplRequest rq;
    DWORD n = 0;
    memset(&rq, 0, sizeof rq);
    rq.op = SPL_ENUM;
    wcopy(rq.printer, printer, NOVA_SPL_NAME);
    lock();
    if (spooler(&rq, &g_reply)) n = g_reply.count;
    unlock();
    return n;
}

/* -----------------------------------------------------------------------
 * DEVMODE: what the printers do (paper, resolution, copies)
 * ----------------------------------------------------------------------- */
typedef struct { short id; const WCHAR *name; int w, h; } Paper;      /* tenths of a millimetre */
static const Paper g_papers[] = {
    { DMPAPER_LETTER, L"Letter", 2159, 2794 },
    { DMPAPER_LEGAL,  L"Legal",  2159, 3556 },
    { DMPAPER_A3,     L"A3",     2970, 4200 },
    { DMPAPER_A4,     L"A4",     2100, 2970 },
    { DMPAPER_A5,     L"A5",     1480, 2100 },
};
#define NPAPERS (int)(sizeof g_papers / sizeof g_papers[0])
#define PRINT_DPI 300

static const Paper *paper_by_id(short id)
{
    for (int i = 0; i < NPAPERS; i++) if (g_papers[i].id == id) return &g_papers[i];
    return NULL;
}
static const Paper *paper_by_name(const WCHAR *n)
{
    for (int i = 0; i < NPAPERS; i++) if (wsame(g_papers[i].name, n)) return &g_papers[i];
    return NULL;
}

#define DM_PRINTER_FIELDS (DM_ORIENTATION | DM_PAPERSIZE | DM_SCALE | DM_COPIES | DM_DEFAULTSOURCE | DM_PRINTQUALITY | \
                           DM_COLOR | DM_DUPLEX | DM_YRESOLUTION | DM_COLLATE | DM_FORMNAME)

static void default_devmode(const Printer *p, DEVMODEW *dm)
{
    memset(dm, 0, sizeof *dm);
    wcopy(dm->dmDeviceName, p->name, 32);
    dm->dmSpecVersion = DM_SPECVERSION;
    dm->dmDriverVersion = 0x0600;
    dm->dmSize = sizeof *dm;
    dm->dmFields = DM_PRINTER_FIELDS;
    dm->dmOrientation = DMORIENT_PORTRAIT;
    dm->dmPaperSize = DMPAPER_LETTER;
    dm->dmScale = 100;
    dm->dmCopies = 1;
    dm->dmDefaultSource = 15;                   /* DMBIN_FORMSOURCE */
    dm->dmPrintQuality = PRINT_DPI;
    dm->dmYResolution = PRINT_DPI;
    dm->dmColor = DMCOLOR_COLOR;
    dm->dmDuplex = DMDUP_SIMPLEX;
    dm->dmCollate = 0;
    wcopy(dm->dmFormName, L"Letter", 32);
}

/* The fields of @in that it says it has, over @dm */
static void merge_devmode(DEVMODEW *dm, const DEVMODEW *in)
{
    DWORD f = in->dmFields;
    if ((f & DM_ORIENTATION) && (in->dmOrientation == DMORIENT_PORTRAIT || in->dmOrientation == DMORIENT_LANDSCAPE))
        dm->dmOrientation = in->dmOrientation;
    if ((f & DM_PAPERSIZE) && paper_by_id(in->dmPaperSize)) {
        dm->dmPaperSize = in->dmPaperSize;
        wcopy(dm->dmFormName, paper_by_id(in->dmPaperSize)->name, 32);
    } else if ((f & DM_FORMNAME) && paper_by_name(in->dmFormName)) {
        dm->dmPaperSize = paper_by_name(in->dmFormName)->id;
        wcopy(dm->dmFormName, paper_by_name(in->dmFormName)->name, 32);
    }
    if ((f & DM_SCALE) && in->dmScale > 0) dm->dmScale = in->dmScale;
    if ((f & DM_COPIES) && in->dmCopies > 0) dm->dmCopies = in->dmCopies;
    if ((f & DM_COLOR) && (in->dmColor == DMCOLOR_COLOR || in->dmColor == DMCOLOR_MONOCHROME)) dm->dmColor = in->dmColor;
    if ((f & DM_COLLATE)) dm->dmCollate = in->dmCollate ? 1 : 0;
    if ((f & DM_PRINTQUALITY) && in->dmPrintQuality >= 72 && in->dmPrintQuality <= 600) {
        dm->dmPrintQuality = in->dmPrintQuality;
        dm->dmYResolution = (f & DM_YRESOLUTION) && in->dmYResolution >= 72 && in->dmYResolution <= 600 ?
                            in->dmYResolution : in->dmPrintQuality;
    }
}

static void devmode_w2a(const DEVMODEW *w, DEVMODEA *a)
{
    memset(a, 0, sizeof *a);
    WideCharToMultiByte(CP_ACP, 0, w->dmDeviceName, -1, (char *)a->dmDeviceName, 32, 0, 0);
    a->dmDeviceName[31] = 0;
    memcpy(&a->dmSpecVersion, &w->dmSpecVersion, offsetof(DEVMODEW, dmFormName) - offsetof(DEVMODEW, dmSpecVersion));
    a->dmSize = sizeof *a;
    WideCharToMultiByte(CP_ACP, 0, w->dmFormName, -1, (char *)a->dmFormName, 32, 0, 0);
    a->dmFormName[31] = 0;
    memcpy(&a->dmLogPixels, &w->dmLogPixels, sizeof *w - offsetof(DEVMODEW, dmLogPixels));
}

static void devmode_a2w(const DEVMODEA *a, DEVMODEW *w)
{
    memset(w, 0, sizeof *w);
    MultiByteToWideChar(CP_ACP, 0, (const char *)a->dmDeviceName, 32, w->dmDeviceName, 32);
    w->dmDeviceName[31] = 0;
    memcpy(&w->dmSpecVersion, &a->dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
    w->dmSize = sizeof *w;
    MultiByteToWideChar(CP_ACP, 0, (const char *)a->dmFormName, 32, w->dmFormName, 32);
    w->dmFormName[31] = 0;
    if (a->dmSize > offsetof(DEVMODEA, dmLogPixels))
        memcpy(&w->dmLogPixels, &a->dmLogPixels, (a->dmSize < sizeof *a ? a->dmSize : sizeof *a) - offsetof(DEVMODEA, dmLogPixels));
}

/* -----------------------------------------------------------------------
 * Filling a caller's buffer: the fixed structures at the front, their
 * strings packed from the back (as Windows does).  A first pass with no
 * buffer counts the bytes.
 * ----------------------------------------------------------------------- */
typedef struct { BYTE *buf; DWORD cb, front, back, need; } Pack;

static void *pack_fixed(Pack *k, DWORD size)
{
    void *at = k->buf && k->front + size <= k->cb ? k->buf + k->front : NULL;
    k->front += size;
    k->need += size;
    return at;
}
static void *pack_tail(Pack *k, const void *data, DWORD size)
{
    size = (size + 7) & ~7u;
    k->need += size;
    if (!k->buf || k->back + size > k->cb) return NULL;
    k->back += size;
    BYTE *at = k->buf + k->cb - k->back;
    memcpy(at, data, size);
    return at;
}
static WCHAR *pack_str(Pack *k, const WCHAR *s)
{
    if (!s) return NULL;
    DWORD n = (DWORD)(wlen(s) + 1) * sizeof(WCHAR);
    WCHAR tmp[MAX_PATH + 8];
    if (n > sizeof tmp) n = sizeof tmp;
    memcpy(tmp, s, n);
    tmp[n / 2 - 1] = 0;
    return pack_tail(k, tmp, n);
}

static BOOL level_ok(DWORD level) { return level == 1 || level == 2 || level == 4 || level == 5; }

static void pack_printer(Pack *k, DWORD level, const Printer *p)
{
    static const DWORD size[6] = { 0, sizeof(PRINTER_INFO_1W), sizeof(PRINTER_INFO_2W), 0,
                                   sizeof(PRINTER_INFO_4W), sizeof(PRINTER_INFO_5W) };
    void *at = pack_fixed(k, size[level]);
    if (level == 1) {
        WCHAR desc[NOVA_SPL_NAME * 3 + 4];
        wcopy(desc, p->name, NOVA_SPL_NAME);
        wcopy(desc + wlen(desc), L",", 2);
        wcopy(desc + wlen(desc), p->driver, NOVA_SPL_NAME);
        wcopy(desc + wlen(desc), L",", 2);
        wcopy(desc + wlen(desc), p->location, NOVA_SPL_NAME);
        PRINTER_INFO_1W v = { pack_str(k, desc), pack_str(k, p->name), pack_str(k, p->comment), PRINTER_ENUM_ICON8 };
        if (at) memcpy(at, &v, sizeof v);
    } else if (level == 2) {
        DEVMODEW dm;
        default_devmode(p, &dm);
        PRINTER_INFO_2W v;
        memset(&v, 0, sizeof v);
        v.pPrinterName = pack_str(k, p->name);
        v.pShareName = pack_str(k, p->share);
        v.pPortName = pack_str(k, p->port);
        v.pDriverName = pack_str(k, p->driver);
        v.pComment = pack_str(k, p->comment);
        v.pLocation = pack_str(k, p->location);
        v.pDevMode = pack_tail(k, &dm, sizeof dm);
        v.pSepFile = pack_str(k, L"");
        v.pPrintProcessor = pack_str(k, p->processor);
        v.pDatatype = pack_str(k, p->datatype);
        v.pParameters = pack_str(k, L"");
        v.Attributes = p->attributes | (is_default(p->name) ? PRINTER_ATTRIBUTE_DEFAULT : 0);
        v.Priority = p->priority;
        v.DefaultPriority = 1;
        v.Status = p->status;
        v.cJobs = at ? job_count(p->name) : 0;
        if (at) memcpy(at, &v, sizeof v);
    } else if (level == 4) {
        PRINTER_INFO_4W v = { pack_str(k, p->name), NULL, p->attributes };
        if (at) memcpy(at, &v, sizeof v);
    } else {
        PRINTER_INFO_5W v = { pack_str(k, p->name), pack_str(k, p->port), p->attributes, 15000, 45000 };
        if (at) memcpy(at, &v, sizeof v);
    }
}

/* Fill @buf with @n printers' level-@level structures (two passes) */
static BOOL fill_printers(DWORD level, const Printer *ps, int n, LPBYTE buf, DWORD cb, LPDWORD needed)
{
    Pack k = { 0 };
    for (int i = 0; i < n; i++) pack_printer(&k, level, &ps[i]);
    if (needed) *needed = k.need;
    if (!buf || cb < k.need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    Pack f = { buf, cb, 0, 0, 0 };
    for (int i = 0; i < n; i++) pack_printer(&f, level, &ps[i]);
    return TRUE;
}

/* -----------------------------------------------------------------------
 * ANSI: the wide call into a buffer of its own, then each string (and a
 * DEVMODE) converted into the caller's.  The A and W structures have the
 * same layout apart from what their pointers point at.
 * ----------------------------------------------------------------------- */
static char *pack_astr(Pack *k, const WCHAR *s)
{
    if (!s) return NULL;
    char tmp[MAX_PATH * 3 + 8];
    int n = WideCharToMultiByte(CP_ACP, 0, s, -1, tmp, sizeof tmp, 0, 0);
    if (n <= 0) { tmp[0] = 0; n = 1; }
    return pack_tail(k, tmp, (DWORD)n);
}

static void pack_entries_a(Pack *k, DWORD level, const BYTE *w, DWORD n)
{
    static const DWORD size[6] = { 0, sizeof(PRINTER_INFO_1W), sizeof(PRINTER_INFO_2W), 0,
                                   sizeof(PRINTER_INFO_4W), sizeof(PRINTER_INFO_5W) };
    DWORD sz = size[level];
    for (DWORD i = 0; i < n; i++) {
        BYTE ent[sizeof(PRINTER_INFO_2W)];
        const BYTE *src = w + i * sz;
        void *at = pack_fixed(k, sz);
        memcpy(ent, src, sz);
        int nptr = level == 1 ? 3 : level == 2 ? 12 : 2;
        for (int j = 0; j < nptr; j++) {
            WCHAR *const *pw = (WCHAR *const *)(src + j * sizeof(void *));
            void **pa = (void **)(ent + j * sizeof(void *));
            if (level == 2 && j == 7) {                       /* pDevMode */
                if (*pw) {
                    DEVMODEA dma;
                    devmode_w2a((const DEVMODEW *)*pw, &dma);
                    *pa = pack_tail(k, &dma, sizeof dma);
                }
                continue;
            }
            if (level == 2 && j == 0) continue;               /* pServerName: NULL */
            *pa = pack_astr(k, *pw);
        }
        if (at) memcpy(at, ent, sz);
    }
}

static BOOL to_ansi(DWORD level, const BYTE *w, DWORD n, LPBYTE buf, DWORD cb, LPDWORD needed)
{
    Pack k = { 0 };
    pack_entries_a(&k, level, w, n);
    if (needed) *needed = k.need;
    if (!buf || cb < k.need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    Pack f = { buf, cb, 0, 0, 0 };
    pack_entries_a(&f, level, w, n);
    return TRUE;
}

static WCHAR *a2w(const char *s)
{
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    WCHAR *w = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
    if (w) MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}
static void wfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

/* -----------------------------------------------------------------------
 * EnumPrinters
 * ----------------------------------------------------------------------- */
static BOOL local_name(const WCHAR *name)
{
    if (!name || !*name) return TRUE;
    WCHAR me[MAX_COMPUTERNAME_LENGTH + 4];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (name[0] == '\\' && name[1] == '\\' && GetComputerNameW(me, &n) && wsame(name + 2, me)) return TRUE;
    return FALSE;
}

SPLAPI BOOL WINAPI EnumPrintersW(DWORD flags, LPWSTR name, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned)
{
    static Printer ps[MAX_PRINTERS];
    static WCHAR names[MAX_PRINTERS][NOVA_SPL_NAME];
    if (returned) *returned = 0;
    if (needed) *needed = 0;
    if (!level_ok(level)) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    /* only this computer's printers: the network has none to offer */
    BOOL local = (flags & (PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS | PRINTER_ENUM_DEFAULT)) ||
                 ((flags & PRINTER_ENUM_NAME) && local_name(name));
    if (!local) return TRUE;
    lock();
    int n = 0;
    if (flags & PRINTER_ENUM_DEFAULT) {
        WCHAR d[NOVA_SPL_NAME];
        if (default_printer(d) && load_printer(d, &ps[0])) n = 1;
    } else {
        int m = list_printers(names, MAX_PRINTERS);
        for (int i = 0; i < m; i++) if (load_printer(names[i], &ps[n])) n++;
    }
    unlock();
    if (!n) return TRUE;
    if (!fill_printers(level, ps, n, buf, cb, needed)) return FALSE;
    if (returned) *returned = (DWORD)n;
    return TRUE;
}

SPLAPI BOOL WINAPI EnumPrintersA(DWORD flags, LPSTR name, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned)
{
    WCHAR *wn = a2w(name);
    DWORD need = 0, n = 0;
    if (returned) *returned = 0;
    if (needed) *needed = 0;
    EnumPrintersW(flags, wn, level, NULL, 0, &need, &n);
    BYTE *w = need ? HeapAlloc(GetProcessHeap(), 0, need) : NULL;
    BOOL ok = EnumPrintersW(flags, wn, level, w, need, &need, &n);
    wfree(wn);
    if (ok && n) ok = to_ansi(level, w, n, buf, cb, needed);
    if (ok && returned) *returned = n;
    wfree(w);
    return ok;
}

/* -----------------------------------------------------------------------
 * The default printer
 * ----------------------------------------------------------------------- */
SPLAPI BOOL WINAPI GetDefaultPrinterW(LPWSTR buf, LPDWORD cch)
{
    WCHAR d[NOVA_SPL_NAME];
    if (!cch) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    lock();
    BOOL have = default_printer(d);
    unlock();
    if (!have) { *cch = 0; SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    DWORD n = (DWORD)wlen(d) + 1;
    if (!buf || *cch < n) { *cch = n; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    memcpy(buf, d, n * sizeof(WCHAR));
    *cch = n;
    return TRUE;
}

SPLAPI BOOL WINAPI GetDefaultPrinterA(LPSTR buf, LPDWORD cch)
{
    WCHAR d[NOVA_SPL_NAME];
    DWORD wn = NOVA_SPL_NAME;
    if (!cch) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (!GetDefaultPrinterW(d, &wn)) { *cch = wn; return FALSE; }
    DWORD n = (DWORD)WideCharToMultiByte(CP_ACP, 0, d, -1, NULL, 0, 0, 0);
    if (!buf || *cch < n) { *cch = n; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    WideCharToMultiByte(CP_ACP, 0, d, -1, buf, (int)*cch, 0, 0);
    *cch = n;
    return TRUE;
}

SPLAPI BOOL WINAPI SetDefaultPrinterW(LPCWSTR name)
{
    Printer p;
    WCHAR d[NOVA_SPL_NAME];
    lock();
    BOOL ok = name && *name ? load_printer(name, &p) : (default_printer(d) && load_printer(d, &p));
    if (ok) {
        WCHAR dev[NOVA_SPL_NAME + MAX_PATH + 16];
        HKEY k;
        wcopy(dev, p.name, NOVA_SPL_NAME);
        wcopy(dev + wlen(dev), L",winspool,", 12);
        wcopy(dev + wlen(dev), p.port, MAX_PATH);
        if (!RegCreateKeyExW(HKEY_CURRENT_USER, DEVICE_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) {
            set_sz(k, L"Device", dev);
            RegCloseKey(k);
        }
    }
    unlock();
    if (!ok) { SetLastError(ERROR_INVALID_PRINTER_NAME); return FALSE; }
    return TRUE;
}

SPLAPI BOOL WINAPI SetDefaultPrinterA(LPCSTR name)
{
    WCHAR *w = a2w(name);
    BOOL ok = SetDefaultPrinterW(w);
    wfree(w);
    return ok;
}

/* -----------------------------------------------------------------------
 * Printer handles
 * ----------------------------------------------------------------------- */
SPLAPI BOOL WINAPI OpenPrinterW(LPWSTR name, LPHANDLE h, LPPRINTER_DEFAULTSW defaults)
{
    (void)defaults;
    if (!h) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *h = NULL;
    Spl *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s);
    if (!s) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    s->magic = SPL_MAGIC;
    s->file = INVALID_HANDLE_VALUE;
    if (!name || !*name || local_name(name)) {
        s->server = TRUE;
    } else {
        lock();
        BOOL ok = load_printer(name, &s->p);
        unlock();
        if (!ok) { HeapFree(GetProcessHeap(), 0, s); SetLastError(ERROR_INVALID_PRINTER_NAME); return FALSE; }
    }
    *h = s;
    return TRUE;
}

SPLAPI BOOL WINAPI OpenPrinterA(LPSTR name, LPHANDLE h, LPPRINTER_DEFAULTSA defaults)
{
    (void)defaults;
    WCHAR *w = a2w(name);
    BOOL ok = OpenPrinterW(w, h, NULL);
    wfree(w);
    return ok;
}

SPLAPI BOOL WINAPI OpenPrinter2W(LPCWSTR name, LPHANDLE h, LPPRINTER_DEFAULTSW defaults, LPVOID options)
{
    (void)options;
    return OpenPrinterW((LPWSTR)name, h, defaults);
}

SPLAPI BOOL WINAPI ClosePrinter(HANDLE h)
{
    Spl *s = spl_of(h);
    if (!s) return FALSE;
    if (s->job) AbortPrinter(h);
    s->magic = 0;
    HeapFree(GetProcessHeap(), 0, s);
    return TRUE;
}

SPLAPI BOOL WINAPI GetPrinterW(HANDLE h, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed)
{
    Spl *s = spl_of(h);
    if (needed) *needed = 0;
    if (!s) return FALSE;
    if (s->server) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (!level_ok(level)) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    lock();
    load_printer(s->p.name, &s->p);              /* (as it is now: the port or comment may have changed) */
    unlock();
    return fill_printers(level, &s->p, 1, buf, cb, needed);
}

SPLAPI BOOL WINAPI GetPrinterA(HANDLE h, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed)
{
    DWORD need = 0;
    if (needed) *needed = 0;
    GetPrinterW(h, level, NULL, 0, &need);
    if (!need) return FALSE;
    BYTE *w = HeapAlloc(GetProcessHeap(), 0, need);
    BOOL ok = GetPrinterW(h, level, w, need, &need) && to_ansi(level, w, 1, buf, cb, needed);
    wfree(w);
    return ok;
}

static BOOL known_driver(const WCHAR *d)
{
    for (unsigned i = 0; i < sizeof g_drivers / sizeof g_drivers[0]; i++) if (wsame(d, g_drivers[i])) return TRUE;
    return FALSE;
}

static void store_printer(HKEY k, const PRINTER_INFO_2W *pi)
{
    if (pi->pPrinterName) set_sz(k, L"Name", pi->pPrinterName);
    if (pi->pPortName) set_sz(k, L"Port", pi->pPortName);
    if (pi->pDriverName) set_sz(k, L"Printer Driver", pi->pDriverName);
    if (pi->pComment) set_sz(k, L"Description", pi->pComment);
    if (pi->pLocation) set_sz(k, L"Location", pi->pLocation);
    if (pi->pShareName) set_sz(k, L"Share Name", pi->pShareName);
    set_sz(k, L"Print Processor", pi->pPrintProcessor && *pi->pPrintProcessor ? pi->pPrintProcessor : L"winprint");
    set_sz(k, L"Datatype", pi->pDatatype && *pi->pDatatype ? pi->pDatatype : L"RAW");
    set_dw(k, L"Attributes", pi->Attributes ? pi->Attributes & ~PRINTER_ATTRIBUTE_DEFAULT : PRINTER_ATTRIBUTE_LOCAL);
    set_dw(k, L"Priority", pi->Priority ? pi->Priority : 1);
}

SPLAPI BOOL WINAPI SetPrinterW(HANDLE h, DWORD level, LPBYTE buf, DWORD cmd)
{
    Spl *s = spl_of(h);
    if (!s) return FALSE;
    if (s->server) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    WCHAR path[300];
    HKEY k;
    wcopy(path, NOVA_SPL_PRINTERS L"\\", 300);
    wcopy(path + wlen(path), s->p.name, 300 - wlen(path));
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WRITE, &k)) { SetLastError(ERROR_INVALID_PRINTER_NAME); return FALSE; }
    if (level == 2 && buf) {
        PRINTER_INFO_2W pi = *(PRINTER_INFO_2W *)buf;
        pi.pPrinterName = NULL;                  /* (renaming is not supported: the key keeps its name) */
        if (pi.pDriverName && !known_driver(pi.pDriverName)) { RegCloseKey(k); SetLastError(ERROR_UNKNOWN_PRINTER_DRIVER); return FALSE; }
        store_printer(k, &pi);
    } else if (level == 5 && buf) {
        PRINTER_INFO_5W *pi = (PRINTER_INFO_5W *)buf;
        if (pi->pPortName) set_sz(k, L"Port", pi->pPortName);
        set_dw(k, L"Attributes", pi->Attributes & ~PRINTER_ATTRIBUTE_DEFAULT);
    } else if (level != 0) {
        RegCloseKey(k);
        SetLastError(ERROR_INVALID_LEVEL);
        return FALSE;
    }
    if (cmd == PRINTER_CONTROL_PAUSE) set_dw(k, L"Status", PRINTER_STATUS_PAUSED);
    else if (cmd == PRINTER_CONTROL_RESUME) set_dw(k, L"Status", 0);
    RegCloseKey(k);
    if (cmd == PRINTER_CONTROL_PURGE) {
        SplRequest rq;
        memset(&rq, 0, sizeof rq);
        rq.op = SPL_ENUM;
        wcopy(rq.printer, s->p.name, NOVA_SPL_NAME);
        lock();
        if (spooler(&rq, &g_reply)) {
            DWORD ids[NOVA_SPL_MAX_JOBS], n = g_reply.count;
            for (DWORD i = 0; i < n; i++) ids[i] = g_reply.jobs[i].id;
            for (DWORD i = 0; i < n; i++) {
                rq.op = SPL_SETJOB; rq.job = ids[i]; rq.arg = JOB_CONTROL_DELETE;
                spooler(&rq, &g_reply);
            }
        }
        unlock();
    }
    lock();
    load_printer(s->p.name, &s->p);
    unlock();
    return TRUE;
}

SPLAPI BOOL WINAPI SetPrinterA(HANDLE h, DWORD level, LPBYTE buf, DWORD cmd)
{
    if (level == 0 || !buf) return SetPrinterW(h, level, buf, cmd);
    if (level != 2) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    PRINTER_INFO_2A *a = (PRINTER_INFO_2A *)buf;
    PRINTER_INFO_2W w;
    memcpy(&w, a, sizeof w);
    w.pPrinterName = NULL;
    w.pShareName = a2w(a->pShareName); w.pPortName = a2w(a->pPortName); w.pDriverName = a2w(a->pDriverName);
    w.pComment = a2w(a->pComment); w.pLocation = a2w(a->pLocation); w.pPrintProcessor = a2w(a->pPrintProcessor);
    w.pDatatype = a2w(a->pDatatype); w.pDevMode = NULL; w.pSepFile = NULL; w.pParameters = NULL;
    BOOL ok = SetPrinterW(h, 2, (LPBYTE)&w, cmd);
    wfree(w.pShareName); wfree(w.pPortName); wfree(w.pDriverName); wfree(w.pComment); wfree(w.pLocation);
    wfree(w.pPrintProcessor); wfree(w.pDatatype);
    return ok;
}

/* AddPrinter (level 2): a new printer's key; its handle */
SPLAPI HANDLE WINAPI AddPrinterW(LPWSTR server, DWORD level, LPBYTE buf)
{
    if (!local_name(server)) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    if (level != 2 || !buf) { SetLastError(ERROR_INVALID_LEVEL); return NULL; }
    PRINTER_INFO_2W *pi = (PRINTER_INFO_2W *)buf;
    if (!pi->pPrinterName || !*pi->pPrinterName || wlen(pi->pPrinterName) >= NOVA_SPL_NAME ||
        !pi->pPortName || !*pi->pPortName || !pi->pDriverName) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    for (const WCHAR *c = pi->pPrinterName; *c; c++)
        if (*c == '\\' || *c == ',') { SetLastError(ERROR_INVALID_PRINTER_NAME); return NULL; }
    if (!known_driver(pi->pDriverName)) { SetLastError(ERROR_UNKNOWN_PRINTER_DRIVER); return NULL; }
    WCHAR path[300];
    HKEY k;
    DWORD disp = 0;
    wcopy(path, NOVA_SPL_PRINTERS L"\\", 300);
    wcopy(path + wlen(path), pi->pPrinterName, 300 - wlen(path));
    lock();
    LONG r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &k, &disp);
    if (!r && disp == REG_OPENED_EXISTING_KEY) { RegCloseKey(k); unlock(); SetLastError(ERROR_PRINTER_ALREADY_EXISTS); return NULL; }
    if (!r) { store_printer(k, pi); RegCloseKey(k); }
    unlock();
    if (r) { SetLastError((DWORD)r); return NULL; }
    HANDLE h = NULL;
    OpenPrinterW(pi->pPrinterName, &h, NULL);
    return h;
}

SPLAPI HANDLE WINAPI AddPrinterA(LPSTR server, DWORD level, LPBYTE buf)
{
    if (level != 2 || !buf) { SetLastError(ERROR_INVALID_LEVEL); return NULL; }
    PRINTER_INFO_2A *a = (PRINTER_INFO_2A *)buf;
    PRINTER_INFO_2W w;
    memcpy(&w, a, sizeof w);
    WCHAR *srv = a2w(server);
    w.pServerName = NULL;
    w.pPrinterName = a2w(a->pPrinterName); w.pShareName = a2w(a->pShareName); w.pPortName = a2w(a->pPortName);
    w.pDriverName = a2w(a->pDriverName); w.pComment = a2w(a->pComment); w.pLocation = a2w(a->pLocation);
    w.pPrintProcessor = a2w(a->pPrintProcessor); w.pDatatype = a2w(a->pDatatype);
    w.pDevMode = NULL; w.pSepFile = NULL; w.pParameters = NULL;
    HANDLE h = AddPrinterW(srv, 2, (LPBYTE)&w);
    wfree(srv); wfree(w.pPrinterName); wfree(w.pShareName); wfree(w.pPortName); wfree(w.pDriverName);
    wfree(w.pComment); wfree(w.pLocation); wfree(w.pPrintProcessor); wfree(w.pDatatype);
    return h;
}

SPLAPI BOOL WINAPI DeletePrinter(HANDLE h)
{
    Spl *s = spl_of(h);
    if (!s) return FALSE;
    if (s->server) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    WCHAR path[300];
    wcopy(path, NOVA_SPL_PRINTERS L"\\", 300);
    wcopy(path + wlen(path), s->p.name, 300 - wlen(path));
    lock();
    BOOL was_default = is_default(s->p.name);
    LONG r = RegDeleteKeyW(HKEY_LOCAL_MACHINE, path);
    if (!r && was_default) {                     /* the next printer becomes the default */
        HKEY k;
        if (!RegOpenKeyExW(HKEY_CURRENT_USER, DEVICE_KEY, 0, KEY_WRITE, &k)) {
            RegDeleteValueW(k, L"Device");
            RegCloseKey(k);
        }
    }
    unlock();
    if (r) { SetLastError((DWORD)r); return FALSE; }
    return TRUE;
}

/* -----------------------------------------------------------------------
 * Jobs
 * ----------------------------------------------------------------------- */
static BOOL file_port(const WCHAR *port)
{
    return wsame(port, L"PORTPROMPT:") || wsame(port, L"FILE:");
}

/* Ask where a file port's output goes (Save As); FALSE when cancelled */
typedef struct {
    DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance; LPCWSTR lpstrFilter; LPWSTR lpstrCustomFilter;
    DWORD nMaxCustFilter, nFilterIndex; LPWSTR lpstrFile; DWORD nMaxFile; LPWSTR lpstrFileTitle; DWORD nMaxFileTitle;
    LPCWSTR lpstrInitialDir, lpstrTitle; DWORD Flags; WORD nFileOffset, nFileExtension; LPCWSTR lpstrDefExt;
    LPARAM lCustData; void *lpfnHook; LPCWSTR lpTemplateName; void *pvReserved; DWORD dwReserved, FlagsEx;
} Ofn;
static BOOL ask_output(const Printer *p, const WCHAR *doc, WCHAR *out)
{
    HMODULE cd = LoadLibraryW(L"comdlg32.dll");
    BOOL (WINAPI *save)(Ofn *) = cd ? (BOOL (WINAPI *)(Ofn *))GetProcAddress(cd, "GetSaveFileNameW") : NULL;
    if (!save) return FALSE;
    BOOL pdf = wsame(p->driver, NOVA_SPL_PDF_DRIVER);
    Ofn o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = GetActiveWindow();
    o.lpstrFilter = pdf ? L"PDF Document (*.pdf)\0*.pdf\0" : L"Printer Files (*.prn)\0*.prn\0All Files (*.*)\0*.*\0";
    o.lpstrFile = out;
    o.nMaxFile = MAX_PATH;
    o.lpstrTitle = L"Save Print Output As";
    o.lpstrDefExt = pdf ? L"pdf" : L"prn";
    o.Flags = 0x00000002 | 0x00000800 | 0x00080000;   /* OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER */
    out[0] = 0;
    if (doc && *doc) {                               /* the document's name, without what Windows won't take in a file name */
        int j = 0;
        for (int i = 0; doc[i] && j < MAX_PATH - 5; i++)
            if (!wcschr(L"\\/:*?\"<>|", doc[i])) out[j++] = doc[i];
        out[j] = 0;
    }
    return save(&o) && out[0];
}

static BOOL raw_datatype(const WCHAR *d)
{
    return !d || !*d || wprefix(d, L"RAW") || wsame(d, L"TEXT");
}

SPLAPI DWORD WINAPI StartDocPrinterW(HANDLE h, DWORD level, LPBYTE info)
{
    Spl *s = spl_of(h);
    if (!s) return 0;
    if (s->server) { SetLastError(ERROR_INVALID_HANDLE); return 0; }
    if (level < 1 || level > 3 || !info) { SetLastError(ERROR_INVALID_LEVEL); return 0; }
    if (s->job) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    DOC_INFO_1W *di = (DOC_INFO_1W *)info;
    if (!raw_datatype(di->pDatatype)) { SetLastError(ERROR_INVALID_DATATYPE); return 0; }
    SplRequest rq;
    memset(&rq, 0, sizeof rq);
    rq.op = SPL_START;
    wcopy(rq.printer, s->p.name, NOVA_SPL_NAME);
    wcopy(rq.document, di->pDocName && *di->pDocName ? di->pDocName : L"Untitled", NOVA_SPL_NAME);
    wcopy(rq.datatype, di->pDatatype && *di->pDatatype ? di->pDatatype : L"RAW", 32);
    DWORD un = 64;
    if (!GetUserNameW(rq.user, &un)) wcopy(rq.user, L"User", 64);
    if (di->pOutputFile && *di->pOutputFile) {
        if (wsame(di->pOutputFile, L"FILE:") || wsame(di->pOutputFile, L"PORTPROMPT:")) {
            if (!ask_output(&s->p, rq.document, rq.output)) { SetLastError(ERROR_CANCELLED); return 0; }
        } else {
            GetFullPathNameW(di->pOutputFile, MAX_PATH, rq.output, NULL);
        }
    } else if (file_port(s->p.port)) {
        if (!ask_output(&s->p, rq.document, rq.output)) { SetLastError(ERROR_CANCELLED); return 0; }
        WCHAR full[MAX_PATH];
        if (GetFullPathNameW(rq.output, MAX_PATH, full, NULL)) wcopy(rq.output, full, MAX_PATH);
    }
    lock();
    BOOL ok = spooler(&rq, &g_reply);
    DWORD job = g_reply.job;
    WCHAR path[MAX_PATH];
    wcopy(path, g_reply.path, MAX_PATH);
    unlock();
    if (!ok) return 0;
    s->file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s->file == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        rq.op = SPL_ABORT; rq.job = job;
        lock(); spooler(&rq, &g_reply); unlock();
        SetLastError(e ? e : ERROR_SPOOL_FILE_NOT_FOUND);
        return 0;
    }
    s->job = job;
    s->written = s->pages = 0;
    s->in_page = FALSE;
    return job;
}

SPLAPI DWORD WINAPI StartDocPrinterA(HANDLE h, DWORD level, LPBYTE info)
{
    if (level < 1 || level > 3 || !info) { SetLastError(ERROR_INVALID_LEVEL); return 0; }
    DOC_INFO_1A *a = (DOC_INFO_1A *)info;
    DOC_INFO_1W w = { a2w(a->pDocName), a2w(a->pOutputFile), a2w(a->pDatatype) };
    DWORD job = StartDocPrinterW(h, 1, (LPBYTE)&w);
    DWORD e = GetLastError();
    wfree(w.pDocName); wfree(w.pOutputFile); wfree(w.pDatatype);
    SetLastError(e);
    return job;
}

static Spl *job_of(HANDLE h)
{
    Spl *s = spl_of(h);
    if (s && !s->job) { SetLastError(ERROR_SPL_NO_STARTDOC); return NULL; }
    return s;
}

SPLAPI BOOL WINAPI StartPagePrinter(HANDLE h)
{
    Spl *s = job_of(h);
    if (!s) return FALSE;
    s->in_page = TRUE;
    return TRUE;
}

SPLAPI BOOL WINAPI EndPagePrinter(HANDLE h)
{
    Spl *s = job_of(h);
    if (!s) return FALSE;
    if (s->in_page) {
        SplRequest rq;
        memset(&rq, 0, sizeof rq);
        rq.op = SPL_PAGE;
        rq.job = s->job;
        lock(); spooler(&rq, &g_reply); unlock();
        s->pages++;
    }
    s->in_page = FALSE;
    return TRUE;
}

SPLAPI BOOL WINAPI WritePrinter(HANDLE h, LPVOID buf, DWORD cb, LPDWORD written)
{
    Spl *s = job_of(h);
    if (written) *written = 0;
    if (!s) return FALSE;
    DWORD n = 0;
    if (cb && !WriteFile(s->file, buf, cb, &n, NULL)) return FALSE;
    s->written += n;
    if (written) *written = n;
    return TRUE;
}

SPLAPI BOOL WINAPI EndDocPrinter(HANDLE h)
{
    Spl *s = job_of(h);
    if (!s) return FALSE;
    if (s->in_page) EndPagePrinter(h);
    CloseHandle(s->file);
    s->file = INVALID_HANDLE_VALUE;
    SplRequest rq;
    memset(&rq, 0, sizeof rq);
    rq.op = SPL_END;
    rq.job = s->job;
    rq.arg = s->written;
    lock();
    BOOL ok = spooler(&rq, &g_reply);
    unlock();
    s->job = 0;
    return ok;
}

SPLAPI BOOL WINAPI AbortPrinter(HANDLE h)
{
    Spl *s = job_of(h);
    if (!s) return FALSE;
    if (s->file != INVALID_HANDLE_VALUE) CloseHandle(s->file);
    s->file = INVALID_HANDLE_VALUE;
    SplRequest rq;
    memset(&rq, 0, sizeof rq);
    rq.op = SPL_ABORT;
    rq.job = s->job;
    lock(); spooler(&rq, &g_reply); unlock();
    s->job = 0;
    return TRUE;
}

static const WCHAR *job_status_text(DWORD st)
{
    if (st & JOB_STATUS_OFFLINE) return L"Printer offline";
    if (st & JOB_STATUS_ERROR) return L"Error";
    if (st & JOB_STATUS_PAUSED) return L"Paused";
    if (st & JOB_STATUS_PRINTING) return L"Printing";
    if (st & JOB_STATUS_SPOOLING) return L"Spooling";
    return NULL;
}

static void pack_job(Pack *k, DWORD level, const SplJob *j, DWORD pos)
{
    void *at = pack_fixed(k, level == 1 ? sizeof(JOB_INFO_1W) : sizeof(JOB_INFO_1W));
    JOB_INFO_1W v;
    memset(&v, 0, sizeof v);
    v.JobId = j->id;
    v.pPrinterName = pack_str(k, j->printer);
    WCHAR me[MAX_COMPUTERNAME_LENGTH + 4] = { '\\', '\\' };
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(me + 2, &n)) me[2] = 0;
    v.pMachineName = pack_str(k, me);
    v.pUserName = pack_str(k, j->user);
    v.pDocument = pack_str(k, j->document);
    v.pDatatype = pack_str(k, j->datatype);
    v.pStatus = pack_str(k, job_status_text(j->status));
    v.Status = j->status;
    v.Priority = 1;
    v.Position = pos;
    v.TotalPages = j->pages;
    v.PagesPrinted = 0;
    v.Submitted = j->submitted;
    if (at) memcpy(at, &v, sizeof v);
}

SPLAPI BOOL WINAPI EnumJobsW(HANDLE h, DWORD first, DWORD n, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned)
{
    Spl *s = spl_of(h);
    if (needed) *needed = 0;
    if (returned) *returned = 0;
    if (!s) return FALSE;
    if (level != 1) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    SplRequest rq;
    memset(&rq, 0, sizeof rq);
    rq.op = SPL_ENUM;
    if (!s->server) wcopy(rq.printer, s->p.name, NOVA_SPL_NAME);
    lock();
    if (!spooler(&rq, &g_reply)) { unlock(); return FALSE; }
    static SplJob jobs[NOVA_SPL_MAX_JOBS];
    DWORD count = 0;
    for (DWORD i = first; i < g_reply.count && count < n; i++) jobs[count++] = g_reply.jobs[i];
    unlock();
    Pack k = { 0 };
    for (DWORD i = 0; i < count; i++) pack_job(&k, level, &jobs[i], first + i + 1);
    if (needed) *needed = k.need;
    if (!count) return TRUE;
    if (!buf || cb < k.need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    Pack f = { buf, cb, 0, 0, 0 };
    for (DWORD i = 0; i < count; i++) pack_job(&f, level, &jobs[i], first + i + 1);
    if (returned) *returned = count;
    return TRUE;
}

SPLAPI BOOL WINAPI GetJobW(HANDLE h, DWORD id, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed)
{
    Spl *s = spl_of(h);
    if (needed) *needed = 0;
    if (!s) return FALSE;
    if (level != 1) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    SplRequest rq;
    memset(&rq, 0, sizeof rq);
    rq.op = SPL_ENUM;
    lock();
    if (!spooler(&rq, &g_reply)) { unlock(); return FALSE; }
    SplJob j;
    DWORD pos = 0;
    BOOL found = FALSE;
    for (DWORD i = 0; i < g_reply.count; i++) if (g_reply.jobs[i].id == id) { j = g_reply.jobs[i]; pos = i + 1; found = TRUE; }
    unlock();
    if (!found) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    Pack k = { 0 };
    pack_job(&k, level, &j, pos);
    if (needed) *needed = k.need;
    if (!buf || cb < k.need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    Pack f = { buf, cb, 0, 0, 0 };
    pack_job(&f, level, &j, pos);
    return TRUE;
}

SPLAPI BOOL WINAPI SetJobW(HANDLE h, DWORD id, DWORD level, LPBYTE buf, DWORD cmd)
{
    (void)buf;
    Spl *s = spl_of(h);
    if (!s) return FALSE;
    if (level != 0) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    SplRequest rq;
    memset(&rq, 0, sizeof rq);
    rq.op = SPL_SETJOB;
    rq.job = id;
    rq.arg = cmd;
    lock();
    BOOL ok = spooler(&rq, &g_reply);
    unlock();
    return ok;
}
SPLAPI BOOL WINAPI SetJobA(HANDLE h, DWORD id, DWORD level, LPBYTE buf, DWORD cmd) { return SetJobW(h, id, level, buf, cmd); }

/* -----------------------------------------------------------------------
 * DocumentProperties and DeviceCapabilities
 * ----------------------------------------------------------------------- */
static BOOL printer_for(HANDLE h, const WCHAR *name, Printer *p)
{
    Spl *s = h ? (Spl *)h : NULL;
    if (s && s->magic == SPL_MAGIC && !s->server) { *p = s->p; return TRUE; }
    lock();
    BOOL ok = name && load_printer(name, p);
    unlock();
    return ok;
}

SPLAPI LONG WINAPI DocumentPropertiesW(HWND w, HANDLE h, LPWSTR name, DEVMODEW *out, DEVMODEW *in, DWORD mode)
{
    (void)w;
    Printer p;
    if (!printer_for(h, name, &p)) { SetLastError(ERROR_INVALID_PRINTER_NAME); return -1; }
    if (!mode) return sizeof(DEVMODEW);
    DEVMODEW dm;
    default_devmode(&p, &dm);
    if ((mode & DM_IN_BUFFER) && in) merge_devmode(&dm, in);
    if ((mode & DM_OUT_BUFFER) && out) memcpy(out, &dm, sizeof dm);
    return IDOK;                                  /* (DM_IN_PROMPT: there is no properties sheet; as if OK was pressed) */
}

SPLAPI LONG WINAPI DocumentPropertiesA(HWND w, HANDLE h, LPSTR name, DEVMODEA *out, DEVMODEA *in, DWORD mode)
{
    WCHAR *wn = a2w(name);
    LONG r;
    if (!mode) {
        r = DocumentPropertiesW(w, h, wn, NULL, NULL, 0);
        if (r > 0) r = sizeof(DEVMODEA);
    } else {
        DEVMODEW win, wout;
        if (in) devmode_a2w(in, &win);
        r = DocumentPropertiesW(w, h, wn, &wout, in ? &win : NULL, mode | DM_OUT_BUFFER);
        if (r > 0 && (mode & DM_OUT_BUFFER) && out) devmode_w2a(&wout, out);
    }
    wfree(wn);
    return r;
}

SPLAPI LONG WINAPI AdvancedDocumentPropertiesW(HWND w, HANDLE h, LPWSTR name, DEVMODEW *out, DEVMODEW *in)
{
    return DocumentPropertiesW(w, h, name, out, in, DM_IN_BUFFER | DM_OUT_BUFFER) == IDOK ? 1 : 0;
}

SPLAPI BOOL WINAPI PrinterProperties(HWND w, HANDLE h) { (void)w; return spl_of(h) != NULL; }

/* @wide: the names are WCHARs (else ANSI chars) */
static int caps(LPCWSTR dev, WORD cap, void *out, const DEVMODEW *mode, BOOL wide)
{
    Printer p;
    if (!printer_for(NULL, dev, &p)) { SetLastError(ERROR_INVALID_PRINTER_NAME); return -1; }
    (void)mode;
    switch (cap) {
    case DC_FIELDS: return DM_PRINTER_FIELDS;
    case DC_PAPERS:
        if (out) for (int i = 0; i < NPAPERS; i++) ((WORD *)out)[i] = (WORD)g_papers[i].id;
        return NPAPERS;
    case DC_PAPERNAMES:
        if (out) for (int i = 0; i < NPAPERS; i++) {
            if (wide) { memset((WCHAR *)out + i * 64, 0, 128); wcopy((WCHAR *)out + i * 64, g_papers[i].name, 64); }
            else { memset((char *)out + i * 64, 0, 64); WideCharToMultiByte(CP_ACP, 0, g_papers[i].name, -1, (char *)out + i * 64, 64, 0, 0); }
        }
        return NPAPERS;
    case DC_PAPERSIZE:
        if (out) for (int i = 0; i < NPAPERS; i++) { ((POINT *)out)[i].x = g_papers[i].w; ((POINT *)out)[i].y = g_papers[i].h; }
        return NPAPERS;
    case DC_MINEXTENT: return MAKELONG(1000, 1000);
    case DC_MAXEXTENT: return MAKELONG(4200, 4200);
    case DC_BINS:
        if (out) ((WORD *)out)[0] = 15;
        return 1;
    case DC_BINNAMES:
        if (out) {
            if (wide) { memset(out, 0, 48); wcopy(out, L"Automatically Select", 24); }
            else { memset(out, 0, 24); memcpy(out, "Automatically Select", 21); }
        }
        return 1;
    case DC_ENUMRESOLUTIONS:
        if (out) { ((LONG *)out)[0] = PRINT_DPI; ((LONG *)out)[1] = PRINT_DPI; }
        return 1;
    case DC_DUPLEX: return 0;
    case DC_SIZE: return wide ? sizeof(DEVMODEW) : sizeof(DEVMODEA);
    case DC_EXTRA: return 0;
    case DC_VERSION: return DM_SPECVERSION;
    case DC_DRIVER: return 0x0600;
    case DC_TRUETYPE: return 2;                   /* DCTT_DOWNLOAD */
    case DC_ORIENTATION: return 90;
    case DC_COPIES: return 999;
    case DC_COLLATE: return 1;
    case DC_COLORDEVICE: return 1;
    case DC_NUP:
        if (out) ((DWORD *)out)[0] = 1;
        return 1;
    case DC_MEDIATYPES:
        if (out) ((DWORD *)out)[0] = 1;            /* DMMEDIA_STANDARD */
        return 1;
    case DC_FILEDEPENDENCIES: return 0;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return -1;
}

SPLAPI int WINAPI DeviceCapabilitiesW(LPCWSTR dev, LPCWSTR port, WORD cap, LPWSTR out, const DEVMODEW *mode)
{
    (void)port;
    return caps(dev, cap, out, mode, TRUE);
}

SPLAPI int WINAPI DeviceCapabilitiesA(LPCSTR dev, LPCSTR port, WORD cap, LPSTR out, const DEVMODEA *mode)
{
    (void)port; (void)mode;
    WCHAR *w = a2w(dev);
    int r = caps(w, cap, out, NULL, FALSE);
    wfree(w);
    return r;
}

/* -----------------------------------------------------------------------
 * Drivers, ports, forms, printer data
 * ----------------------------------------------------------------------- */
SPLAPI BOOL WINAPI EnumPrinterDriversW(LPWSTR srv, LPWSTR env, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned)
{
    (void)srv; (void)env;
    if (needed) *needed = 0;
    if (returned) *returned = 0;
    if (level != 1 && level != 2) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    DWORD n = sizeof g_drivers / sizeof g_drivers[0];
    for (int pass = 0; pass < 2; pass++) {
        Pack k = { pass ? buf : NULL, pass ? cb : 0, 0, 0, 0 };
        for (DWORD i = 0; i < n; i++) {
            if (level == 1) {
                void *at = pack_fixed(&k, sizeof(DRIVER_INFO_1W));
                DRIVER_INFO_1W v = { pack_str(&k, g_drivers[i]) };
                if (at) memcpy(at, &v, sizeof v);
            } else {
                void *at = pack_fixed(&k, sizeof(DRIVER_INFO_2W));
                DRIVER_INFO_2W v = { 3, pack_str(&k, g_drivers[i]), pack_str(&k, L"Windows x64"),
                                     pack_str(&k, L"C:\\Windows\\System32\\gdi32.dll"), pack_str(&k, L""), pack_str(&k, L"") };
                if (at) memcpy(at, &v, sizeof v);
            }
        }
        if (!pass) {
            if (needed) *needed = k.need;
            if (!buf || cb < k.need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        }
    }
    if (returned) *returned = n;
    return TRUE;
}

SPLAPI BOOL WINAPI EnumPrinterDriversA(LPSTR srv, LPSTR env, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned)
{
    (void)srv; (void)env;
    if (needed) *needed = 0;
    if (returned) *returned = 0;
    if (level != 1) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    DWORD n = sizeof g_drivers / sizeof g_drivers[0];
    for (int pass = 0; pass < 2; pass++) {
        Pack k = { pass ? buf : NULL, pass ? cb : 0, 0, 0, 0 };
        for (DWORD i = 0; i < n; i++) {
            void *at = pack_fixed(&k, sizeof(void *));
            char *s = pack_astr(&k, g_drivers[i]);
            if (at) memcpy(at, &s, sizeof s);
        }
        if (!pass) {
            if (needed) *needed = k.need;
            if (!buf || cb < k.need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        }
    }
    if (returned) *returned = n;
    return TRUE;
}

SPLAPI BOOL WINAPI GetPrinterDriverW(HANDLE h, LPWSTR env, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed)
{
    (void)env;
    Spl *s = spl_of(h);
    if (needed) *needed = 0;
    if (!s) return FALSE;
    if (s->server) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (level != 1 && level != 2) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    for (int pass = 0; pass < 2; pass++) {
        Pack k = { pass ? buf : NULL, pass ? cb : 0, 0, 0, 0 };
        if (level == 1) {
            void *at = pack_fixed(&k, sizeof(DRIVER_INFO_1W));
            DRIVER_INFO_1W v = { pack_str(&k, s->p.driver) };
            if (at) memcpy(at, &v, sizeof v);
        } else {
            void *at = pack_fixed(&k, sizeof(DRIVER_INFO_2W));
            DRIVER_INFO_2W v = { 3, pack_str(&k, s->p.driver), pack_str(&k, L"Windows x64"),
                                 pack_str(&k, L"C:\\Windows\\System32\\gdi32.dll"), pack_str(&k, L""), pack_str(&k, L"") };
            if (at) memcpy(at, &v, sizeof v);
        }
        if (!pass) {
            if (needed) *needed = k.need;
            if (!buf || cb < k.need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        }
    }
    return TRUE;
}

/* The ports: the file ports, and each port a printer names */
SPLAPI BOOL WINAPI EnumPortsW(LPWSTR srv, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned)
{
    (void)srv;
    static WCHAR names[MAX_PRINTERS][NOVA_SPL_NAME];
    static WCHAR ports[MAX_PRINTERS + 3][MAX_PATH];
    if (needed) *needed = 0;
    if (returned) *returned = 0;
    if (level != 1 && level != 2) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    int n = 0;
    wcopy(ports[n++], L"PORTPROMPT:", MAX_PATH);
    wcopy(ports[n++], L"FILE:", MAX_PATH);
    wcopy(ports[n++], L"nul:", MAX_PATH);
    lock();
    int m = list_printers(names, MAX_PRINTERS);
    for (int i = 0; i < m; i++) {
        Printer p;
        if (!load_printer(names[i], &p) || !p.port[0]) continue;
        BOOL dup = FALSE;
        for (int j = 0; j < n; j++) if (wsame(ports[j], p.port)) dup = TRUE;
        if (!dup) wcopy(ports[n++], p.port, MAX_PATH);
    }
    unlock();
    for (int pass = 0; pass < 2; pass++) {
        Pack k = { pass ? buf : NULL, pass ? cb : 0, 0, 0, 0 };
        for (int i = 0; i < n; i++) {
            if (level == 1) {
                void *at = pack_fixed(&k, sizeof(PORT_INFO_1W));
                PORT_INFO_1W v = { pack_str(&k, ports[i]) };
                if (at) memcpy(at, &v, sizeof v);
            } else {
                void *at = pack_fixed(&k, sizeof(PORT_INFO_2W));
                BOOL ipp = wprefix(ports[i], L"http://") || wprefix(ports[i], L"ipp://");
                PORT_INFO_2W v = { pack_str(&k, ports[i]), pack_str(&k, ipp ? L"WSD Port" : L"Local Port"),
                                   pack_str(&k, ipp ? L"Internet Printing Protocol port" : L"Local Port"), 1 | 2, 0 };
                if (at) memcpy(at, &v, sizeof v);
            }
        }
        if (!pass) {
            if (needed) *needed = k.need;
            if (!buf || cb < k.need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        }
    }
    if (returned) *returned = (DWORD)n;
    return TRUE;
}

/* Forms: the paper sizes (thousandths of a millimetre) */
SPLAPI BOOL WINAPI EnumFormsW(HANDLE h, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned)
{
    if (needed) *needed = 0;
    if (returned) *returned = 0;
    if (!spl_of(h)) return FALSE;
    if (level != 1) { SetLastError(ERROR_INVALID_LEVEL); return FALSE; }
    for (int pass = 0; pass < 2; pass++) {
        Pack k = { pass ? buf : NULL, pass ? cb : 0, 0, 0, 0 };
        for (int i = 0; i < NPAPERS; i++) {
            void *at = pack_fixed(&k, sizeof(FORM_INFO_1W));
            FORM_INFO_1W v;
            memset(&v, 0, sizeof v);
            v.Flags = 1;                          /* FORM_BUILTIN */
            v.pName = pack_str(&k, g_papers[i].name);
            v.Size.cx = g_papers[i].w * 100;
            v.Size.cy = g_papers[i].h * 100;
            v.ImageableArea.right = v.Size.cx;
            v.ImageableArea.bottom = v.Size.cy;
            if (at) memcpy(at, &v, sizeof v);
        }
        if (!pass) {
            if (needed) *needed = k.need;
            if (!buf || cb < k.need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        }
    }
    if (returned) *returned = NPAPERS;
    return TRUE;
}

/* Printer data: values under the printer's PrinterDriverData key */
static DWORD printer_data(HANDLE h, LPCWSTR key, LPCWSTR name, LPDWORD type, LPBYTE buf, DWORD cb, LPDWORD needed)
{
    Spl *s = spl_of(h);
    if (needed) *needed = 0;
    if (!s) return ERROR_INVALID_HANDLE;
    if (s->server) return ERROR_FILE_NOT_FOUND;
    WCHAR path[400];
    HKEY k;
    wcopy(path, NOVA_SPL_PRINTERS L"\\", 400);
    wcopy(path + wlen(path), s->p.name, 400 - wlen(path));
    wcopy(path + wlen(path), L"\\", 400 - wlen(path));
    wcopy(path + wlen(path), key && *key ? key : L"PrinterDriverData", 400 - wlen(path));
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k)) return ERROR_FILE_NOT_FOUND;
    DWORD n = cb;
    LONG r = RegQueryValueExW(k, name, 0, type, buf, &n);
    RegCloseKey(k);
    if (needed) *needed = n;
    return (DWORD)r;
}
SPLAPI DWORD WINAPI GetPrinterDataW(HANDLE h, LPWSTR name, LPDWORD type, LPBYTE buf, DWORD cb, LPDWORD needed)
{ return printer_data(h, NULL, name, type, buf, cb, needed); }
SPLAPI DWORD WINAPI GetPrinterDataExW(HANDLE h, LPCWSTR key, LPCWSTR name, LPDWORD type, LPBYTE buf, DWORD cb, LPDWORD needed)
{ return printer_data(h, key, name, type, buf, cb, needed); }

/* The spooler's own folder, where drivers would live */
SPLAPI BOOL WINAPI GetPrinterDriverDirectoryW(LPWSTR srv, LPWSTR env, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed)
{
    static const WCHAR dir[] = L"C:\\Windows\\System32\\spool\\drivers\\x64";
    (void)srv; (void)env; (void)level;
    if (needed) *needed = sizeof dir;
    if (!buf || cb < sizeof dir) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    memcpy(buf, dir, sizeof dir);
    return TRUE;
}

SPLAPI BOOL WINAPI GetPrintProcessorDirectoryW(LPWSTR srv, LPWSTR env, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed)
{
    static const WCHAR dir[] = L"C:\\Windows\\System32\\spool\\prtprocs\\x64";
    (void)srv; (void)env; (void)level;
    if (needed) *needed = sizeof dir;
    if (!buf || cb < sizeof dir) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    memcpy(buf, dir, sizeof dir);
    return TRUE;
}
