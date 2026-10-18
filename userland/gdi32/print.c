/*
 * print.c — printer device contexts, and the Microsoft Print to PDF driver
 *
 * CreateDC with a printer's name (from PrintDlg's DEVNAMES, or any name
 * winspool.drv knows) makes a printer DC: a memory DC whose surface is
 * the page, at the printer's resolution (DEVMODE dmPrintQuality, 300 dpi)
 * and paper size (dmPaperSize, dmOrientation), white at each StartPage.
 * GetDeviceCaps answers for the page (HORZRES, LOGPIXELSX, PHYSICALWIDTH,
 * TECHNOLOGY DT_RASPRINTER...).  Programs draw on it with everything
 * gdi32 draws.
 *
 * StartDoc begins a job on the printer (winspool's StartDocPrinter, which
 * asks where to save when the printer is on a file port and the program
 * named no file), EndPage renders the page into the job and EndDoc
 * finishes it.  NovaOS's printer drivers (Microsoft Print To PDF, and the
 * IPP class driver, which sends the same) render a job as a PDF: each
 * page an RGB image the page's size, Flate-compressed with stb's zlib
 * writer (third_party/plutovg, public domain/MIT).  The PDF is written
 * as it goes (WritePrinter), with its cross-reference table at the end,
 * and carries no dates, so the same pages make the same file.
 *
 * winspool.drv is loaded when the first printer DC is made, as Windows'
 * gdi32 does.
 */
#define NOVA_BUILD_GDI32
#include "gdi_int.h"
#include <winspool.h>

extern GObj g_default_bitmap;

/* A small printf for the PDF's text (gdi32 has no C library, and user32's
 * wsprintf is above it): %d %u %lu %X %s, a width with 0 fill, %% */
static int fmt(char *b, const char *f, ...)
{
    va_list ap;
    char *o = b;
    va_start(ap, f);
    for (; *f; f++) {
        if (*f != '%') { *o++ = *f; continue; }
        f++;
        if (*f == '%') { *o++ = '%'; continue; }
        int zero = *f == '0', width = 0;
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == 'l') f++;
        if (*f == 's') {
            for (const char *s = va_arg(ap, const char *); *s; s++) *o++ = *s;
            continue;
        }
        unsigned long long v;
        int neg = 0, base = *f == 'X' ? 16 : 10;
        if (*f == 'd') { long long x = va_arg(ap, int); neg = x < 0; v = neg ? (unsigned long long)-x : (unsigned long long)x; }
        else v = va_arg(ap, unsigned int);
        char t[24];
        int n = 0;
        do { t[n++] = "0123456789ABCDEF"[v % base]; v /= base; } while (v);
        if (neg) *o++ = '-';
        for (int i = n; i < width; i++) *o++ = zero ? '0' : ' ';
        while (n) *o++ = t[--n];
    }
    *o = 0;
    va_end(ap);
    return (int)(o - b);
}

/* stb's zlib writer, on the process heap, with nothing else of the C library */
static void *stbw_realloc(void *p, size_t n)
{
    return p ? HeapReAlloc(GetProcessHeap(), 0, p, n) : HeapAlloc(GetProcessHeap(), 0, n);
}
static void stbw_memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    if (dd < ss) for (size_t i = 0; i < n; i++) dd[i] = ss[i];
    else for (size_t i = n; i--;) dd[i] = ss[i];
}
#define STBIW_MALLOC(n)     HeapAlloc(GetProcessHeap(), 0, n)
#define STBIW_REALLOC(p, n) stbw_realloc(p, n)
#define STBIW_FREE(p)       HeapFree(GetProcessHeap(), 0, p)
#define STBIW_MEMMOVE(d, s, n) stbw_memmove(d, s, n)
#define STBIW_ASSERT(x)     ((void)0)
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-variable"
#include "../../third_party/plutovg/source/plutovg-stb-image-write.h"
#pragma clang diagnostic pop

/* winspool.drv's calls */
static struct {
    BOOL (WINAPI *open)(LPWSTR, LPHANDLE, LPPRINTER_DEFAULTSW);
    BOOL (WINAPI *close)(HANDLE);
    LONG (WINAPI *docprops)(HWND, HANDLE, LPWSTR, DEVMODEW *, DEVMODEW *, DWORD);
    int (WINAPI *caps)(LPCWSTR, LPCWSTR, WORD, LPWSTR, const DEVMODEW *);
    DWORD (WINAPI *start_doc)(HANDLE, DWORD, LPBYTE);
    BOOL (WINAPI *start_page)(HANDLE);
    BOOL (WINAPI *end_page)(HANDLE);
    BOOL (WINAPI *write)(HANDLE, LPVOID, DWORD, LPDWORD);
    BOOL (WINAPI *end_doc)(HANDLE);
    BOOL (WINAPI *abort)(HANDLE);
    BOOL (WINAPI *get_printer)(HANDLE, DWORD, LPBYTE, DWORD, LPDWORD);
} spl;

static BOOL load_winspool(void)
{
    static int state;                        /* 0 not tried, 1 loaded, -1 missing */
    if (state) return state > 0;
    HMODULE m = LoadLibraryW(L"winspool.drv");
    state = -1;
    if (!m) return FALSE;
#define GET(f, n) if (!(*(FARPROC *)&spl.f = GetProcAddress(m, n))) return FALSE
    GET(open, "OpenPrinterW"); GET(close, "ClosePrinter"); GET(docprops, "DocumentPropertiesW");
    GET(caps, "DeviceCapabilitiesW"); GET(start_doc, "StartDocPrinterW"); GET(start_page, "StartPagePrinter");
    GET(end_page, "EndPagePrinter"); GET(write, "WritePrinter"); GET(end_doc, "EndDocPrinter");
    GET(abort, "AbortPrinter"); GET(get_printer, "GetPrinterW");
#undef GET
    state = 1;
    return TRUE;
}


typedef struct PrintDev {
    HANDLE printer;
    WCHAR name[128];
    DEVMODEW dm;
    int dpi_x, dpi_y;
    int pw, ph;                              /* the page, in device pixels */
    int mm_w, mm_h;
    HBITMAP page;                            /* the page's surface (from the first StartPage to EndDoc) */
    void *page_bits;
    int in_doc, in_page, aborted;
    DWORD job;
    ABORTPROC abort;
    /* the PDF being written */
    DWORD pos;                               /* bytes written so far */
    DWORD *offsets;                          /* each object's byte offset (object n at [n]) */
    int nobj, cap_obj, pages;
    WCHAR title[128];
} PrintDev;

/* -----------------------------------------------------------------------
 * The PDF writer
 * ----------------------------------------------------------------------- */
static BOOL out(PrintDev *p, const void *data, DWORD n)
{
    DWORD w = 0;
    if (p->aborted) return FALSE;
    if (n && (!spl.write(p->printer, (LPVOID)data, n, &w) || w != n)) { p->aborted = 1; return FALSE; }
    p->pos += n;
    return TRUE;
}
static BOOL outs(PrintDev *p, const char *s)
{
    DWORD n = 0;
    while (s[n]) n++;
    return out(p, s, n);
}

/* Object @n begins here */
static void obj_begin(PrintDev *p, int n)
{
    if (n >= p->cap_obj) {
        int cap = p->cap_obj ? p->cap_obj * 2 : 64;
        while (cap <= n) cap *= 2;
        DWORD *o = p->offsets ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p->offsets, cap * sizeof(DWORD))
                              : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap * sizeof(DWORD));
        if (!o) { p->aborted = 1; return; }
        p->offsets = o;
        p->cap_obj = cap;
    }
    p->offsets[n] = p->pos;
    if (n >= p->nobj) p->nobj = n + 1;
    char b[32];
    fmt(b, "%d 0 obj\n", n);
    outs(p, b);
}

/* Objects 1 (catalog) and 2 (pages) are written last; each page takes 3 */
static void pdf_begin(PrintDev *p)
{
    p->pos = 0;
    p->nobj = 3;                             /* 1, 2 and the info dictionary (written at the end) */
    p->pages = 0;
    outs(p, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
}

/* A PDF text string: UTF-16BE in hex, which takes any characters */
static void pdf_text(PrintDev *p, const WCHAR *s)
{
    char b[8];
    outs(p, "<FEFF");
    for (; *s; s++) { fmt(b, "%04X", (unsigned)*s); outs(p, b); }
    outs(p, ">");
}

static void pdf_page(PrintDev *p)
{
    int w = p->pw, h = p->ph;
    size_t n = (size_t)w * h * 3;
    BYTE *rgb = HeapAlloc(GetProcessHeap(), 0, n);
    if (!rgb) { p->aborted = 1; return; }
    const DWORD *src = p->page_bits;          /* 32-bit top-down BGRA */
    for (size_t i = 0, j = 0; i < (size_t)w * h; i++, j += 3) {
        DWORD px = src[i];
        rgb[j] = (BYTE)(px >> 16);
        rgb[j + 1] = (BYTE)(px >> 8);
        rgb[j + 2] = (BYTE)px;
    }
    int zlen = 0;
    unsigned char *z = stbi_zlib_compress(rgb, (int)n, &zlen, 6);
    HeapFree(GetProcessHeap(), 0, rgb);
    if (!z) { p->aborted = 1; return; }
    int base = 4 + p->pages * 3;              /* page, contents, image */
    char b[256];
    /* the page in points (1/72 inch) */
    int ptw = (int)((long long)w * 7200 / p->dpi_x), pth = (int)((long long)h * 7200 / p->dpi_y);
    char sw[24], sh[24];
    fmt(sw, "%d.%02d", ptw / 100, ptw % 100);
    fmt(sh, "%d.%02d", pth / 100, pth % 100);
    obj_begin(p, base);
    fmt(b, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %s %s] /Resources << /XObject << /Im%d %d 0 R >> >> "
                 "/Contents %d 0 R >>\nendobj\n", sw, sh, p->pages + 1, base + 2, base + 1);
    outs(p, b);
    char cs[96];
    fmt(cs, "q %s 0 0 %s 0 0 cm /Im%d Do Q\n", sw, sh, p->pages + 1);
    int cl = 0;
    while (cs[cl]) cl++;
    obj_begin(p, base + 1);
    fmt(b, "<< /Length %d >>\nstream\n", cl);
    outs(p, b);
    outs(p, cs);
    outs(p, "endstream\nendobj\n");
    obj_begin(p, base + 2);
    fmt(b, "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 "
                 "/Filter /FlateDecode /Length %d >>\nstream\n", w, h, zlen);
    outs(p, b);
    out(p, z, (DWORD)zlen);
    outs(p, "\nendstream\nendobj\n");
    STBIW_FREE(z);
    p->pages++;
}

static void pdf_end(PrintDev *p)
{
    char b[96];
    obj_begin(p, 1);
    outs(p, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    obj_begin(p, 2);
    outs(p, "<< /Type /Pages /Kids [");
    for (int i = 0; i < p->pages; i++) { fmt(b, "%s%d 0 R", i ? " " : "", 4 + i * 3); outs(p, b); }
    fmt(b, "] /Count %d >>\nendobj\n", p->pages);
    outs(p, b);
    obj_begin(p, 3);
    outs(p, "<< /Producer (NovaOS Microsoft Print to PDF)");
    if (p->title[0]) { outs(p, " /Title "); pdf_text(p, p->title); }
    outs(p, " >>\nendobj\n");
    DWORD xref = p->pos;
    fmt(b, "xref\n0 %d\n0000000000 65535 f \n", p->nobj);
    outs(p, b);
    for (int i = 1; i < p->nobj; i++) {
        fmt(b, "%010lu 00000 n \n", (unsigned long)p->offsets[i]);
        outs(p, b);
    }
    fmt(b, "trailer\n<< /Size %d /Root 1 0 R /Info 3 0 R >>\nstartxref\n%lu\n%%%%EOF\n", p->nobj, (unsigned long)xref);
    outs(p, b);
}

/* -----------------------------------------------------------------------
 * Printer DCs
 * ----------------------------------------------------------------------- */
static PrintDev *dev_of(HDC h)
{
    NOVA_DC *d = dc_of(h);
    return d ? (PrintDev *)d->print : NULL;
}

/* A printer DC for printer @name (NULL when no printer has the name) */
HDC print_create(LPCWSTR name, const DEVMODEW *mode)
{
    if (!name || !*name || !load_winspool()) return 0;
    PrintDev *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *p);
    if (!p) return 0;
    int i = 0;
    for (; name[i] && i < 127; i++) p->name[i] = name[i];
    p->name[i] = 0;
    if (!spl.open(p->name, &p->printer, NULL)) { HeapFree(GetProcessHeap(), 0, p); return 0; }
    if (spl.docprops(NULL, p->printer, p->name, &p->dm, (DEVMODEW *)mode,
                     DM_OUT_BUFFER | (mode ? DM_IN_BUFFER : 0)) < 0) {
        spl.close(p->printer);
        HeapFree(GetProcessHeap(), 0, p);
        return 0;
    }
    p->dpi_x = p->dm.dmPrintQuality > 0 ? p->dm.dmPrintQuality : 300;
    p->dpi_y = p->dm.dmYResolution > 0 ? p->dm.dmYResolution : p->dpi_x;
    /* the paper (tenths of a millimetre) */
    int tw = 2159, th = 2794;
    WORD papers[64];
    POINT sizes[64];
    int n = spl.caps(p->name, NULL, DC_PAPERS, NULL, NULL);
    if (n > 0 && n <= 64 && spl.caps(p->name, NULL, DC_PAPERS, (LPWSTR)papers, NULL) == n &&
        spl.caps(p->name, NULL, DC_PAPERSIZE, (LPWSTR)sizes, NULL) == n)
        for (int k = 0; k < n; k++) if (papers[k] == (WORD)p->dm.dmPaperSize) { tw = sizes[k].x; th = sizes[k].y; }
    if (p->dm.dmOrientation == DMORIENT_LANDSCAPE) { int t = tw; tw = th; th = t; }
    p->mm_w = tw / 10;
    p->mm_h = th / 10;
    p->pw = (int)((long long)tw * p->dpi_x / 254);
    p->ph = (int)((long long)th * p->dpi_y / 254);
    HDC h = CreateCompatibleDC(0);
    if (!h) { spl.close(p->printer); HeapFree(GetProcessHeap(), 0, p); return 0; }
    dc_of(h)->print = p;
    return h;
}

void print_delete(HDC h)
{
    PrintDev *p = dev_of(h);
    if (!p) return;
    if (p->in_doc) AbortDoc(h);
    if (p->page) { SelectObject(h, &g_default_bitmap); DeleteObject(p->page); }
    spl.close(p->printer);
    if (p->offsets) HeapFree(GetProcessHeap(), 0, p->offsets);
    dc_of(h)->print = NULL;
    HeapFree(GetProcessHeap(), 0, p);
}

/* GetDeviceCaps for a printer DC */
int print_caps(HDC h, int index)
{
    PrintDev *p = dev_of(h);
    switch (index) {
    case 0:   return 0x0600;                 /* DRIVERVERSION */
    case 2:   return 2;                      /* TECHNOLOGY: DT_RASPRINTER */
    case 4:   return p->mm_w;                /* HORZSIZE */
    case 6:   return p->mm_h;                /* VERTSIZE */
    case 8:   return p->pw;                  /* HORZRES */
    case 10:  return p->ph;                  /* VERTRES */
    case 12:  return 24;                     /* BITSPIXEL */
    case 14:  return 1;                      /* PLANES */
    case 16:  return -1;                     /* NUMBRUSHES */
    case 18:  return -1;                     /* NUMPENS */
    case 20:  return 0;                      /* NUMMARKERS */
    case 22:  return -1;                     /* NUMFONTS */
    case 24:  return -1;                     /* NUMCOLORS */
    case 26:  return 0;                      /* PDEVICESIZE */
    case 28:  return 0;                      /* CURVECAPS */
    case 30:  return 0;                      /* LINECAPS */
    case 32:  return 0;                      /* POLYGONALCAPS */
    case 34:  return 0;                      /* TEXTCAPS */
    case 36:  return 0;                      /* CLIPCAPS */
    case 38:  return 0x7E99 | 0x0001;        /* RASTERCAPS (and RC_BITBLT) */
    case 40:  return 10;                     /* ASPECTX */
    case 42:  return 10;                     /* ASPECTY */
    case 44:  return 14;                     /* ASPECTXY */
    case 88:  return p->dpi_x;               /* LOGPIXELSX */
    case 90:  return p->dpi_y;               /* LOGPIXELSY */
    case 104: return 0;                      /* SIZEPALETTE */
    case 108: return 24;                     /* COLORRES */
    case 110: return p->pw;                  /* PHYSICALWIDTH */
    case 111: return p->ph;                  /* PHYSICALHEIGHT */
    case 112: return 0;                      /* PHYSICALOFFSETX (no margins: a PDF page is all printable) */
    case 113: return 0;                      /* PHYSICALOFFSETY */
    case 114: return 0;                      /* SCALINGFACTORX */
    case 115: return 0;                      /* SCALINGFACTORY */
    case 117: return p->ph;                  /* DESKTOPVERTRES */
    case 118: return p->pw;                  /* DESKTOPHORZRES */
    case 120: return 0;                      /* BLTALIGNMENT */
    case 121: return 0;                      /* SHADEBLENDCAPS */
    case 122: return 0;                      /* COLORMGMTCAPS */
    }
    return 0;
}

/* The page's surface: made at the first StartPage, kept until EndDoc */
static BOOL make_page(HDC h, PrintDev *p)
{
    if (p->page) return TRUE;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = p->pw;
    bi.bmiHeader.biHeight = -p->ph;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    p->page = CreateDIBSection(h, &bi, DIB_RGB_COLORS, &p->page_bits, NULL, 0);
    if (!p->page) return FALSE;
    SelectObject(h, p->page);
    return TRUE;
}

static void free_page(HDC h, PrintDev *p)
{
    if (!p->page) return;
    SelectObject(h, &g_default_bitmap);
    DeleteObject(p->page);
    p->page = NULL;
    p->page_bits = NULL;
}

static int start_doc(HDC h, LPCWSTR doc, LPCWSTR output, LPCWSTR datatype)
{
    PrintDev *p = dev_of(h);
    if (!p) { SetLastError(ERROR_INVALID_HANDLE); return -1; }
    if (p->in_doc) { SetLastError(ERROR_INVALID_PARAMETER); return -1; }
    DOC_INFO_1W di = { (LPWSTR)(doc && *doc ? doc : L"Untitled"), (LPWSTR)output, (LPWSTR)L"RAW" };
    (void)datatype;
    p->job = spl.start_doc(p->printer, 1, (LPBYTE)&di);
    if (!p->job) return -1;                  /* (winspool's error: cancelled, no spooler...) */
    int i = 0;
    for (; di.pDocName[i] && i < 127; i++) p->title[i] = di.pDocName[i];
    p->title[i] = 0;
    p->in_doc = 1;
    p->aborted = 0;
    pdf_begin(p);
    return (int)p->job;
}


GDIAPI int StartDocW(HDC h, const DOCINFOW *di)
{
    return start_doc(h, di ? di->lpszDocName : NULL, di && di->cbSize > (int)offsetof(DOCINFOW, lpszOutput) ? di->lpszOutput : NULL,
                     NULL);
}

static WCHAR *a2w(LPCSTR s)
{
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    WCHAR *w = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
    if (w) MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

GDIAPI int StartDocA(HDC h, const DOCINFOA *di)
{
    WCHAR *doc = di ? a2w(di->lpszDocName) : NULL;
    WCHAR *output = di && di->cbSize > (int)offsetof(DOCINFOA, lpszOutput) ? a2w(di->lpszOutput) : NULL;
    int r = start_doc(h, doc, output, NULL);
    DWORD e = GetLastError();
    if (doc) HeapFree(GetProcessHeap(), 0, doc);
    if (output) HeapFree(GetProcessHeap(), 0, output);
    SetLastError(e);
    return r;
}

GDIAPI int StartPage(HDC h)
{
    PrintDev *p = dev_of(h);
    if (!p || !p->in_doc) { SetLastError(ERROR_INVALID_HANDLE); return -1; }
    if (p->aborted) return -1;
    if (!make_page(h, p)) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return -1; }
    memset(p->page_bits, 0xFF, (size_t)p->pw * p->ph * 4);
    spl.start_page(p->printer);
    p->in_page = 1;
    return 1;
}

GDIAPI int EndPage(HDC h)
{
    PrintDev *p = dev_of(h);
    if (!p || !p->in_doc || !p->in_page) { SetLastError(ERROR_INVALID_HANDLE); return -1; }
    dc_sync(dc_of(h));
    p->in_page = 0;
    if (p->abort && !p->abort(h, 0)) { AbortDoc(h); return -1; }
    pdf_page(p);
    spl.end_page(p->printer);
    if (p->aborted) { AbortDoc(h); return -1; }
    return 1;
}

GDIAPI int EndDoc(HDC h)
{
    PrintDev *p = dev_of(h);
    if (!p || !p->in_doc) { SetLastError(ERROR_INVALID_HANDLE); return -1; }
    if (p->in_page) EndPage(h);
    if (!p->in_doc) return -1;               /* (EndPage aborted it) */
    pdf_end(p);
    if (p->aborted) { AbortDoc(h); return -1; }
    BOOL ok = spl.end_doc(p->printer);
    p->in_doc = 0;
    free_page(h, p);
    return ok ? 1 : -1;
}

GDIAPI int AbortDoc(HDC h)
{
    PrintDev *p = dev_of(h);
    if (!p || !p->in_doc) { SetLastError(ERROR_INVALID_HANDLE); return -1; }
    spl.abort(p->printer);
    p->in_doc = p->in_page = 0;
    free_page(h, p);
    return 1;
}

GDIAPI int SetAbortProc(HDC h, ABORTPROC proc)
{
    PrintDev *p = dev_of(h);
    if (!p) { SetLastError(ERROR_INVALID_HANDLE); return -1; }
    p->abort = proc;
    return 1;
}

/* The old printer escapes programs still send (QUERYESCSUPPORT says which) */
GDIAPI int Escape(HDC h, int code, int n, LPCSTR in, LPVOID outp)
{
    PrintDev *p = dev_of(h);
    if (!p) return 0;
    switch (code) {
    case 8:                                  /* QUERYESCSUPPORT */
        if (n < (int)sizeof(int) || !in) return 0;
        switch (*(const int *)in) {
        case 1: case 2: case 8: case 9: case 10: case 11: case 12: case 13: case 14: return 1;
        }
        return 0;
    case 1:                                  /* NEWFRAME: the page ends, the next begins */
        if (EndPage(h) <= 0) return -1;
        return StartPage(h);
    case 2: return AbortDoc(h);              /* ABORTDOC */
    case 9: return SetAbortProc(h, (ABORTPROC)(void *)in);   /* SETABORTPROC */
    case 10: {                               /* STARTDOC: in is the document's name */
        WCHAR *doc = a2w(in);
        int r = start_doc(h, doc, NULL, NULL);
        if (doc) HeapFree(GetProcessHeap(), 0, doc);
        return r > 0 ? StartPage(h) : r;
    }
    case 11: return EndDoc(h);               /* ENDDOC */
    case 12:                                 /* GETPHYSPAGESIZE */
        if (outp) { ((POINT *)outp)->x = p->pw; ((POINT *)outp)->y = p->ph; }
        return 1;
    case 13:                                 /* GETPRINTINGOFFSET */
        if (outp) { ((POINT *)outp)->x = 0; ((POINT *)outp)->y = 0; }
        return 1;
    case 14:                                 /* GETSCALINGFACTOR */
        if (outp) { ((POINT *)outp)->x = 0; ((POINT *)outp)->y = 0; }
        return 1;
    }
    return 0;
}
