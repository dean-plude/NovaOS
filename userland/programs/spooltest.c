/*
 * spooltest.exe — printing (core/171-spooltest.py)
 *
 * The print spooler (spoolsv.exe), winspool.drv, the printer DC with the
 * Microsoft Print to PDF driver (gdi32's print.c) and the print dialogs
 * (comdlg32's printdlg.c), as a program uses them:
 *   - the printers: EnumPrinters (W and A, levels 1, 2, 4, 5) lists
 *     Microsoft Print to PDF on PORTPROMPT:, GetDefaultPrinter names it,
 *     DocumentProperties and DeviceCapabilities describe it
 *   - a raw job (OpenPrinter, StartDocPrinter, WritePrinter, EndDocPrinter)
 *     reaches its output file byte for byte; on a paused printer it waits
 *     in the queue (EnumJobs, GetPrinter's cJobs) until SetJob deletes it
 *   - AddPrinter on a file port prints there; DeletePrinter removes it
 *   - with no output file, the PDF printer asks where to save (Save As)
 *   - PrintDlg (PD_RETURNDEFAULT | PD_RETURNDC) gives the default printer's
 *     DC: 300 dpi, Letter; two pages drawn on it come out as a PDF whose
 *     first page's image is exactly what GDI draws on a bitmap
 *   - the Print dialog: chosen copies come back (and Cancel cancels), and
 *     PageSetupDlg's defaults are Letter in thousandths of an inch
 * Prints "spooltest: N passed, 0 failed".
 */
#include <windows.h>
#include <winspool.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG                     /* (the zlib decoder is what this uses) */
#pragma clang diagnostic ignored "-Wunused-function"
#include "../../third_party/stb/stb_image.h"

#define PDF L"Microsoft Print to PDF"
#define OUT_RAW L"C:\\Temp\\spooltest-raw.prn"
#define OUT_PORT L"C:\\Temp\\spooltest-port.prn"
#define OUT_PDF L"C:\\Temp\\spooltest.pdf"
#define OUT_ASK L"C:\\Temp\\spooltest-asked.pdf"

static int g_pass, g_fail;
#define CHECK(what, cond) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (error %lu)\n", what, GetLastError()); } } while (0)

/* A file's bytes (NULL if it is not there); *n its size */
static BYTE *slurp(const WCHAR *path, DWORD *n)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL), got = 0;
    BYTE *b = malloc(size + 1);
    if (b && !ReadFile(f, b, size, &got, NULL)) got = 0;
    CloseHandle(f);
    if (b) b[got] = 0;
    *n = got;
    return b;
}

/* The jobs waiting on @printer */
static DWORD jobs(HANDLE h, JOB_INFO_1W *first)
{
    BYTE buf[8192];
    DWORD need = 0, n = 0;
    if (!EnumJobsW(h, 0, 32, 1, buf, sizeof buf, &need, &n)) return (DWORD)-1;
    if (n && first) *first = *(JOB_INFO_1W *)buf;
    return n;
}

/* Wait until the spooler has printed every job of @printer (up to @ms) */
static BOOL drained(const WCHAR *printer, DWORD ms)
{
    HANDLE h;
    if (!OpenPrinterW((LPWSTR)printer, &h, NULL)) return FALSE;
    DWORD t0 = GetTickCount();
    BOOL ok = FALSE;
    while (GetTickCount() - t0 < ms) {
        if (jobs(h, NULL) == 0) { ok = TRUE; break; }
        Sleep(100);
    }
    ClosePrinter(h);
    return ok;
}

static DWORD raw_job(HANDLE h, const WCHAR *doc, const WCHAR *out, const char *text)
{
    DOC_INFO_1W di = { (LPWSTR)doc, (LPWSTR)out, (LPWSTR)L"RAW" };
    DWORD job = StartDocPrinterW(h, 1, (LPBYTE)&di), w = 0;
    if (!job) return 0;
    StartPagePrinter(h);
    WritePrinter(h, (LPVOID)text, (DWORD)strlen(text), &w);
    EndPagePrinter(h);
    if (!EndDocPrinter(h) || w != strlen(text)) return 0;
    return job;
}

static void test_printers(void)
{
    BYTE buf[16384];
    DWORD need = 0, n = 0;
    CHECK("EnumPrinters asks for room", !EnumPrintersW(PRINTER_ENUM_LOCAL, NULL, 2, NULL, 0, &need, &n) &&
          GetLastError() == ERROR_INSUFFICIENT_BUFFER && need > sizeof(PRINTER_INFO_2W));
    BOOL found = FALSE;
    if (EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2, buf, sizeof buf, &need, &n))
        for (DWORD i = 0; i < n; i++) {
            PRINTER_INFO_2W *p = &((PRINTER_INFO_2W *)buf)[i];
            printf("spooltest: printer \"%ls\" on %ls, driver %ls%s\n", p->pPrinterName, p->pPortName, p->pDriverName,
                   (p->Attributes & PRINTER_ATTRIBUTE_DEFAULT) ? " (default)" : "");
            if (!wcscmp(p->pPrinterName, PDF)) {
                found = TRUE;
                CHECK("the PDF printer is on PORTPROMPT:", !wcscmp(p->pPortName, L"PORTPROMPT:"));
                CHECK("the PDF printer's driver", !wcscmp(p->pDriverName, L"Microsoft Print To PDF"));
                CHECK("the PDF printer's DEVMODE", p->pDevMode && p->pDevMode->dmPaperSize == DMPAPER_LETTER &&
                      p->pDevMode->dmPrintQuality == 300 && !wcscmp(p->pDevMode->dmDeviceName, PDF));
            }
        }
    CHECK("EnumPrinters lists Microsoft Print to PDF", found);
    CHECK("EnumPrinters level 1", EnumPrintersW(PRINTER_ENUM_LOCAL, NULL, 1, buf, sizeof buf, &need, &n) && n >= 1 &&
          wcsstr(((PRINTER_INFO_1W *)buf)[0].pDescription, L","));
    CHECK("EnumPrinters level 4", EnumPrintersW(PRINTER_ENUM_LOCAL, NULL, 4, buf, sizeof buf, &need, &n) && n >= 1);
    CHECK("EnumPrinters level 5", EnumPrintersW(PRINTER_ENUM_LOCAL, NULL, 5, buf, sizeof buf, &need, &n) && n >= 1 &&
          ((PRINTER_INFO_5W *)buf)[0].pPortName);
    found = FALSE;
    if (EnumPrintersA(PRINTER_ENUM_LOCAL, NULL, 2, buf, sizeof buf, &need, &n))
        for (DWORD i = 0; i < n; i++)
            if (!strcmp(((PRINTER_INFO_2A *)buf)[i].pPrinterName, "Microsoft Print to PDF") &&
                !strcmp(((PRINTER_INFO_2A *)buf)[i].pPortName, "PORTPROMPT:") &&
                !strcmp((const char *)((PRINTER_INFO_2A *)buf)[i].pDevMode->dmFormName, "Letter")) found = TRUE;
    CHECK("EnumPrintersA", found);
    CHECK("network printers: none", EnumPrintersW(PRINTER_ENUM_NETWORK, NULL, 1, buf, sizeof buf, &need, &n) && n == 0);

    WCHAR d[128];
    char da[128];
    DWORD cch = 1;
    CHECK("GetDefaultPrinter asks for room", !GetDefaultPrinterW(d, &cch) && GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
          cch == wcslen(PDF) + 1);
    cch = 128;
    CHECK("SetDefaultPrinter", SetDefaultPrinterW(PDF));
    CHECK("GetDefaultPrinter: the PDF printer", GetDefaultPrinterW(d, &cch) && !wcscmp(d, PDF));
    cch = 128;
    CHECK("GetDefaultPrinterA", GetDefaultPrinterA(da, &cch) && !strcmp(da, "Microsoft Print to PDF"));
    CHECK("SetDefaultPrinter refuses an unknown name", !SetDefaultPrinterW(L"No Such Printer") &&
          GetLastError() == ERROR_INVALID_PRINTER_NAME);
    HANDLE h = NULL;
    CHECK("OpenPrinter refuses an unknown name", !OpenPrinterW((LPWSTR)L"No Such Printer", &h, NULL) &&
          GetLastError() == ERROR_INVALID_PRINTER_NAME);

    CHECK("DocumentProperties' size", DocumentPropertiesW(NULL, NULL, (LPWSTR)PDF, NULL, NULL, 0) == sizeof(DEVMODEW));
    DEVMODEW in, out;
    memset(&in, 0, sizeof in);
    in.dmSize = sizeof in;
    in.dmFields = DM_PAPERSIZE | DM_ORIENTATION | DM_COPIES;
    in.dmPaperSize = DMPAPER_A4;
    in.dmOrientation = DMORIENT_LANDSCAPE;
    in.dmCopies = 2;
    CHECK("DocumentProperties merges a DEVMODE",
          DocumentPropertiesW(NULL, NULL, (LPWSTR)PDF, &out, &in, DM_IN_BUFFER | DM_OUT_BUFFER) == IDOK &&
          out.dmPaperSize == DMPAPER_A4 && out.dmOrientation == DMORIENT_LANDSCAPE && out.dmCopies == 2 &&
          !wcscmp(out.dmFormName, L"A4"));
    DEVMODEA outa;
    CHECK("DocumentPropertiesA", DocumentPropertiesA(NULL, NULL, (LPSTR)"Microsoft Print to PDF", &outa, NULL, DM_OUT_BUFFER) == IDOK &&
          !strcmp((const char *)outa.dmDeviceName, "Microsoft Print to PDF") && outa.dmSize == sizeof outa);
    int np = DeviceCapabilitiesW(PDF, L"PORTPROMPT:", DC_PAPERS, NULL, NULL);
    WCHAR names[16][64];
    BOOL a4 = FALSE;
    if (np > 0 && np <= 16 && DeviceCapabilitiesW(PDF, NULL, DC_PAPERNAMES, (LPWSTR)names, NULL) == np)
        for (int i = 0; i < np; i++) if (!wcscmp(names[i], L"A4")) a4 = TRUE;
    CHECK("DeviceCapabilities: the paper sizes", np >= 5 && a4);
    LONG res[2] = { 0 };
    CHECK("DeviceCapabilities: 300 dpi", DeviceCapabilitiesW(PDF, NULL, DC_ENUMRESOLUTIONS, (LPWSTR)res, NULL) == 1 &&
          res[0] == 300 && res[1] == 300);
    CHECK("DeviceCapabilities: colour", DeviceCapabilitiesW(PDF, NULL, DC_COLORDEVICE, NULL, NULL) == 1);
}

static void test_raw(void)
{
    HANDLE h = NULL;
    CHECK("OpenPrinter", OpenPrinterW((LPWSTR)PDF, &h, NULL) && h);
    if (!h) return;
    BYTE buf[4096];
    DWORD need = 0;
    CHECK("GetPrinter level 2", GetPrinterW(h, 2, buf, sizeof buf, &need) &&
          !wcscmp(((PRINTER_INFO_2W *)buf)->pPrinterName, PDF) && ((PRINTER_INFO_2W *)buf)->pDevMode);
    CHECK("GetPrinterA level 2", GetPrinterA(h, 2, buf, sizeof buf, &need) &&
          !strcmp(((PRINTER_INFO_2A *)buf)->pPrinterName, "Microsoft Print to PDF"));
    DeleteFileW(OUT_RAW);
    const char *text = "NovaOS raw print job\r\nsecond line\r\n";
    DWORD job = raw_job(h, L"spooltest raw", OUT_RAW, text);
    CHECK("a raw job", job != 0);
    CHECK("the spooler prints it", drained(PDF, 20000));
    DWORD n = 0;
    BYTE *b = slurp(OUT_RAW, &n);
    CHECK("its output, byte for byte", b && n == strlen(text) && !memcmp(b, text, n));
    free(b);
    DOC_INFO_1W bad = { (LPWSTR)L"x", (LPWSTR)OUT_RAW, (LPWSTR)L"NT EMF 1.008" };
    CHECK("StartDocPrinter refuses EMF", !StartDocPrinterW(h, 1, (LPBYTE)&bad) && GetLastError() == ERROR_INVALID_DATATYPE);
    CHECK("WritePrinter needs a job", !WritePrinter(h, "x", 1, &n));

    /* a paused printer: the job waits in the queue */
    CHECK("pause the printer", SetPrinterW(h, 0, NULL, PRINTER_CONTROL_PAUSE));
    DeleteFileW(OUT_RAW);
    job = raw_job(h, L"spooltest paused", OUT_RAW, "paused\r\n");
    CHECK("a job on a paused printer", job != 0);
    Sleep(500);
    JOB_INFO_1W ji;
    memset(&ji, 0, sizeof ji);
    CHECK("EnumJobs shows it", jobs(h, &ji) == 1 && ji.JobId == job && !wcscmp(ji.pDocument, L"spooltest paused") &&
          ji.TotalPages == 1 && !wcscmp(ji.pDatatype, L"RAW"));
    CHECK("GetPrinter: paused, one job", GetPrinterW(h, 2, buf, sizeof buf, &need) &&
          (((PRINTER_INFO_2W *)buf)->Status & PRINTER_STATUS_PAUSED) && ((PRINTER_INFO_2W *)buf)->cJobs == 1);
    CHECK("SetJob deletes it", SetJobW(h, job, 0, NULL, JOB_CONTROL_DELETE) && jobs(h, NULL) == 0);
    CHECK("resume the printer", SetPrinterW(h, 0, NULL, PRINTER_CONTROL_RESUME));
    Sleep(500);
    CHECK("the deleted job never printed", GetFileAttributesW(OUT_RAW) == INVALID_FILE_ATTRIBUTES);
    CHECK("ClosePrinter", ClosePrinter(h));
}

static void test_add(void)
{
    PRINTER_INFO_2W pi;
    memset(&pi, 0, sizeof pi);
    pi.pPrinterName = (LPWSTR)L"NovaOS Test File Printer";
    pi.pPortName = (LPWSTR)OUT_PORT;
    pi.pDriverName = (LPWSTR)L"Microsoft Print To PDF";
    pi.pComment = (LPWSTR)L"spooltest";
    pi.Attributes = PRINTER_ATTRIBUTE_LOCAL;
    pi.pDriverName = (LPWSTR)L"No Such Driver";
    CHECK("AddPrinter refuses an unknown driver", !AddPrinterW(NULL, 2, (LPBYTE)&pi) && GetLastError() == ERROR_UNKNOWN_PRINTER_DRIVER);
    pi.pDriverName = (LPWSTR)L"Microsoft Print To PDF";
    HANDLE h = AddPrinterW(NULL, 2, (LPBYTE)&pi);
    CHECK("AddPrinter", h != NULL);
    if (!h) return;
    CHECK("AddPrinter refuses a second one", !AddPrinterW(NULL, 2, (LPBYTE)&pi) && GetLastError() == ERROR_PRINTER_ALREADY_EXISTS);
    DeleteFileW(OUT_PORT);
    CHECK("a job on a file port", raw_job(h, L"spooltest port", NULL, "to the port\r\n") != 0);
    CHECK("the spooler prints it", drained(L"NovaOS Test File Printer", 20000));
    DWORD n = 0;
    BYTE *b = slurp(OUT_PORT, &n);
    CHECK("it reaches the port's file", b && n == 13 && !memcmp(b, "to the port\r\n", 13));
    free(b);
    CHECK("DeletePrinter", DeletePrinter(h));
    ClosePrinter(h);
    HANDLE again;
    CHECK("the printer is gone", !OpenPrinterW((LPWSTR)L"NovaOS Test File Printer", &again, NULL));
}

/* -----------------------------------------------------------------------
 * Dialogs, answered from another thread (as a person would)
 * ----------------------------------------------------------------------- */
typedef struct { const WCHAR *title; int action; } Answer;   /* action: 0 cancel, 1 print with 3 copies, 2 save as OUT_ASK */

static DWORD WINAPI answer(LPVOID arg)
{
    Answer *a = arg;
    HWND w = NULL;
    for (int i = 0; i < 200 && !w; i++) {
        Sleep(50);
        w = FindWindowW(NULL, a->title);
    }
    if (!w) { printf("spooltest: no \"%ls\" dialog\n", a->title); return 0; }
    Sleep(300);
    if (a->action == 1) {
        printf("spooltest: print dialog open\n");
        fflush(stdout);
        SetDlgItemTextW(w, 0x482, L"3");                      /* edt3: copies */
        Sleep(2500);                                          /* (the screenshot) */
        PostMessageW(w, WM_COMMAND, IDOK, 0);
    } else if (a->action == 2) {
        SetDlgItemTextW(w, 1096, OUT_ASK);                    /* the file dialog's name box (filedlg.c's ID_NAME) */
        PostMessageW(w, WM_COMMAND, IDOK, 0);
    } else {
        PostMessageW(w, WM_COMMAND, IDCANCEL, 0);
    }
    return 0;
}

static HANDLE answer_later(const WCHAR *title, int action)
{
    static Answer a[4];
    static int n;
    Answer *x = &a[n++ & 3];
    x->title = title;
    x->action = action;
    return CreateThread(NULL, 0, answer, x, 0, NULL);
}

static void test_ask(void)
{
    HANDLE h;
    if (!OpenPrinterW((LPWSTR)PDF, &h, NULL)) { CHECK("OpenPrinter", FALSE); return; }
    DeleteFileW(OUT_ASK);
    HANDLE t = answer_later(L"Save Print Output As", 2);
    DOC_INFO_1W di = { (LPWSTR)L"spooltest asked", NULL, NULL };
    DWORD job = StartDocPrinterW(h, 1, (LPBYTE)&di), w;
    CHECK("a file port asks where to save", job != 0);
    if (job) {
        WritePrinter(h, "%PDF-1.4\n", 9, &w);
        EndDocPrinter(h);
    }
    WaitForSingleObject(t, 15000);
    CloseHandle(t);
    CHECK("the spooler prints it", drained(PDF, 20000));
    CHECK("to the chosen file", GetFileAttributesW(OUT_ASK) != INVALID_FILE_ATTRIBUTES);
    t = answer_later(L"Save Print Output As", 0);
    SetLastError(0);
    CHECK("Cancel there cancels the job", StartDocPrinterW(h, 1, (LPBYTE)&di) == 0 && GetLastError() == ERROR_CANCELLED);
    WaitForSingleObject(t, 15000);
    CloseHandle(t);
    ClosePrinter(h);
}

/* The test page: what both the printer and a bitmap get drawn */
static void draw_page(HDC dc, int page)
{
    HBRUSH red = CreateSolidBrush(RGB(220, 30, 30)), blue = CreateSolidBrush(RGB(30, 60, 220));
    HPEN pen = CreatePen(PS_SOLID, 6, RGB(0, 120, 0));
    HFONT font = CreateFontW(-100, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
    HGDIOBJ old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    if (page == 1) {
        RECT r = { 300, 300, 1200, 900 };
        FillRect(dc, &r, red);
        HGDIOBJ ob = SelectObject(dc, blue);
        Ellipse(dc, 1400, 300, 2200, 900);
        SelectObject(dc, ob);
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, 300, 1100, NULL);
        LineTo(dc, 2250, 1100);
        SelectObject(dc, op);
        TextOutW(dc, 300, 1300, L"NovaOS print test", 17);
    } else {
        TextOutW(dc, 300, 300, L"Page 2", 6);
    }
    SelectObject(dc, old_font);
    DeleteObject(font);
    DeleteObject(pen);
    DeleteObject(red);
    DeleteObject(blue);
}

/* Find @what in @b (n bytes) from @from; its offset or -1 */
static long find(const BYTE *b, DWORD n, const char *what, DWORD from)
{
    size_t k = strlen(what);
    for (DWORD i = from; i + k <= n; i++) if (!memcmp(b + i, what, k)) return (long)i;
    return -1;
}

static void test_gdi(void)
{
    PRINTDLGW pd;
    memset(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.Flags = PD_RETURNDEFAULT | PD_RETURNDC;
    CHECK("PrintDlg PD_RETURNDEFAULT", PrintDlgW(&pd) && pd.hDC && pd.hDevMode && pd.hDevNames);
    if (!pd.hDC) return;
    DEVNAMES *dn = GlobalLock(pd.hDevNames);
    CHECK("its DEVNAMES", dn && !wcscmp((WCHAR *)dn + dn->wDeviceOffset, PDF) && !wcscmp((WCHAR *)dn + dn->wDriverOffset, L"winspool") &&
          !wcscmp((WCHAR *)dn + dn->wOutputOffset, L"PORTPROMPT:") && (dn->wDefault & DN_DEFAULTPRN));
    GlobalUnlock(pd.hDevNames);
    HDC dc = pd.hDC;
    int w = GetDeviceCaps(dc, HORZRES), h = GetDeviceCaps(dc, VERTRES);
    CHECK("a printer DC", GetDeviceCaps(dc, TECHNOLOGY) == DT_RASPRINTER);
    CHECK("300 dpi, Letter", GetDeviceCaps(dc, LOGPIXELSX) == 300 && GetDeviceCaps(dc, LOGPIXELSY) == 300 && w == 2550 &&
          h == 3300 && GetDeviceCaps(dc, PHYSICALWIDTH) == 2550 && GetDeviceCaps(dc, PHYSICALOFFSETX) == 0 &&
          GetDeviceCaps(dc, HORZSIZE) == 215);
    DeleteFileW(OUT_PDF);
    DOCINFOW di = { sizeof di, L"NovaOS spooltest", OUT_PDF, NULL, 0 };
    int job = StartDocW(dc, &di);
    CHECK("StartDoc", job > 0);
    CHECK("StartPage", StartPage(dc) > 0);
    draw_page(dc, 1);
    CHECK("EndPage", EndPage(dc) > 0);
    StartPage(dc);
    draw_page(dc, 2);
    EndPage(dc);
    CHECK("EndDoc", EndDoc(dc) > 0);
    CHECK("DeleteDC", DeleteDC(dc));
    GlobalFree(pd.hDevMode);
    GlobalFree(pd.hDevNames);
    CHECK("the spooler prints it", drained(PDF, 60000));

    DWORD n = 0;
    BYTE *b = slurp(OUT_PDF, &n);
    CHECK("the PDF is written", b && n > 1000);
    if (!b) return;
    printf("spooltest: %ls: %lu bytes\n", OUT_PDF, (unsigned long)n);
    CHECK("a PDF", !memcmp(b, "%PDF-1.4\n", 9) && n > 6 && !memcmp(b + n - 6, "%%EOF\n", 6));
    CHECK("two pages", find(b, n, "/Type /Pages /Kids [4 0 R 7 0 R] /Count 2", 0) >= 0);
    CHECK("Letter in points", find(b, n, "/MediaBox [0 0 612.00 792.00]", 0) >= 0);
    CHECK("its title", find(b, n, "/Title <FEFF004E006F00760061004F0053", 0) >= 0);
    /* the cross-reference table: each object where it says */
    long sx = find(b, n, "startxref\n", 0);
    long xref = sx >= 0 ? atol((const char *)b + sx + 10) : -1;
    BOOL xok = xref > 0 && xref < (long)n && !memcmp(b + xref, "xref\n0 ", 7);
    int objs = xok ? atoi((const char *)b + xref + 7) : 0;
    const char *e = xok ? strchr((const char *)b + xref + 7, '\n') + 1 + 20 : NULL;
    for (int i = 1; xok && i < objs; i++, e += 20) {
        char want[16];
        long off = atol(e);
        snprintf(want, sizeof want, "%d 0 obj\n", i);
        if (off <= 0 || off >= (long)n || memcmp(b + off, want, strlen(want))) xok = FALSE;
    }
    CHECK("the cross-reference table", xok && objs == 10);
    /* page 1's image is what GDI draws on a bitmap of the page's size */
    long im = find(b, n, "/Subtype /Image /Width 2550 /Height 3300", 0);
    long len_at = im >= 0 ? find(b, n, "/Length ", (DWORD)im) : -1;
    long st = im >= 0 ? find(b, n, "stream\n", (DWORD)im) : -1;
    int zlen = len_at >= 0 ? atoi((const char *)b + len_at + 8) : 0;
    int outlen = 0;
    char *pix = st >= 0 && zlen > 0 ? stbi_zlib_decode_malloc_guesssize((const char *)b + st + 7, zlen, w * h * 3, &outlen) : NULL;
    CHECK("page 1's image inflates", pix && outlen == w * h * 3);
    if (pix && outlen == w * h * 3) {
        BITMAPINFO bi;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void *bits = NULL;
        HDC mem = CreateCompatibleDC(NULL);
        HBITMAP bm = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        HGDIOBJ old = SelectObject(mem, bm);
        memset(bits, 0xFF, (size_t)w * h * 4);
        draw_page(mem, 1);
        GdiFlush();
        long diff = 0;
        const DWORD *px = bits;
        const BYTE *rgb = (const BYTE *)pix;
        for (long i = 0; i < (long)w * h; i++) {
            DWORD p = px[i];
            if (rgb[i * 3] != (BYTE)(p >> 16) || rgb[i * 3 + 1] != (BYTE)(p >> 8) || rgb[i * 3 + 2] != (BYTE)p) diff++;
        }
        CHECK("page 1 is exactly what GDI draws", diff == 0);
        if (diff) printf("spooltest: %ld pixels differ\n", diff);
        const BYTE *c = rgb + ((size_t)600 * w + 750) * 3;
        CHECK("the red rectangle", c[0] == 220 && c[1] == 30 && c[2] == 30);
        c = rgb + ((size_t)600 * w + 1800) * 3;
        CHECK("the blue ellipse", c[0] == 30 && c[1] == 60 && c[2] == 220);
        long dark = 0;
        for (int y = 1300; y < 1420; y++) for (int x = 300; x < 1500; x++) if (rgb[((size_t)y * w + x) * 3] < 100) dark++;
        CHECK("the text", dark > 2000);
        SelectObject(mem, old);
        DeleteObject(bm);
        DeleteDC(mem);
    }
    free(pix);
    free(b);
}

static void test_dialogs(void)
{
    PRINTDLGW pd;
    memset(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION;
    pd.nMinPage = 1; pd.nMaxPage = 9; pd.nFromPage = 1; pd.nToPage = 9;
    pd.nCopies = 1;
    HANDLE t = answer_later(L"Print", 1);
    BOOL ok = PrintDlgW(&pd);
    WaitForSingleObject(t, 15000);
    CloseHandle(t);
    CHECK("the Print dialog: Print", ok && pd.hDC && pd.nCopies == 3 && !(pd.Flags & PD_PAGENUMS));
    DEVNAMES *dn = pd.hDevNames ? GlobalLock(pd.hDevNames) : NULL;
    CHECK("the Print dialog's printer", dn && !wcscmp((WCHAR *)dn + dn->wDeviceOffset, PDF));
    if (dn) GlobalUnlock(pd.hDevNames);
    CHECK("its DC prints", pd.hDC && GetDeviceCaps(pd.hDC, TECHNOLOGY) == DT_RASPRINTER);
    if (pd.hDC) DeleteDC(pd.hDC);
    pd.hDC = NULL;
    /* again with what came back: Cancel */
    t = answer_later(L"Print", 0);
    ok = PrintDlgW(&pd);
    WaitForSingleObject(t, 15000);
    CloseHandle(t);
    CHECK("the Print dialog: Cancel", !ok && CommDlgExtendedError() == 0);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);

    PRINTDLGA pa;
    memset(&pa, 0, sizeof pa);
    pa.lStructSize = sizeof pa;
    pa.Flags = PD_RETURNDEFAULT;
    ok = PrintDlgA(&pa);
    DEVNAMES *dna = ok ? GlobalLock(pa.hDevNames) : NULL;
    CHECK("PrintDlgA PD_RETURNDEFAULT", dna && !strcmp((char *)dna + dna->wDeviceOffset, "Microsoft Print to PDF"));
    if (dna) GlobalUnlock(pa.hDevNames);
    CHECK("PrintDlg's size is Windows'", sizeof(PRINTDLGW) == (sizeof(void *) == 8 ? 120 : 66));

    PRINTDLGW bad;
    memset(&bad, 0, sizeof bad);
    bad.lStructSize = 12;
    CHECK("PrintDlg checks lStructSize", !PrintDlgW(&bad) && CommDlgExtendedError() == CDERR_STRUCTSIZE);

    PAGESETUPDLGW ps;
    memset(&ps, 0, sizeof ps);
    ps.lStructSize = sizeof ps;
    ps.Flags = PSD_RETURNDEFAULT | PSD_INTHOUSANDTHSOFINCHES;
    CHECK("PageSetupDlg PSD_RETURNDEFAULT", PageSetupDlgW(&ps) && ps.ptPaperSize.x == 8500 && ps.ptPaperSize.y == 11000 &&
          ps.rtMargin.left == 1000);
    if (ps.hDevMode) GlobalFree(ps.hDevMode);
    if (ps.hDevNames) GlobalFree(ps.hDevNames);
}

int main(void)
{
    CreateDirectoryW(L"C:\\Temp", NULL);
    /* the spooler starts with the desktop's services */
    BOOL up = FALSE;
    for (int i = 0; i < 240 && !up; i++) {
        up = WaitNamedPipeW(L"\\\\.\\pipe\\spoolss", 250);
        if (!up) Sleep(250);
    }
    if (!up) printf("spooltest: the spooler's pipe is not there\n");
    test_printers();
    test_raw();
    test_add();
    test_ask();
    test_gdi();
    test_dialogs();
    printf("spooltest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
