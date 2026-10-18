/*
 * ipp.c — the spooler's IPP port (ipp.h)
 *
 * IPP/2.0 requests (RFC 8010's encoding) over HTTP POST with WinHTTP:
 * ipp://host[:631]/path is http://host:631/path, ipps:// is the same over
 * TLS.  Printers on a home network carry self-signed certificates, so an
 * ipps:// printer's certificate is not checked against the trusted roots
 * (as CUPS and Windows' IPP class driver accept them).
 *
 * PWG Raster: gdi32's printer driver writes each page as one RGB image
 * (Flate) in a PDF (userland/gdi32/print.c); for a printer that takes no
 * PDF the pages are inflated (stb_image's zlib decoder, public domain/MIT,
 * third_party/stb) and sent as PWG Raster lines (PackBits-style runs, a
 * repeat count for equal lines), scaled to a resolution the printer has.
 */
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "ipp.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG                     /* (the zlib decoder is what this uses) */
#pragma clang diagnostic ignored "-Wunused-function"
#include "../../../third_party/stb/stb_image.h"

enum {
    OP_PRINT_JOB = 0x0002, OP_GET_PRINTER_ATTRIBUTES = 0x000B,
    TAG_OPERATION = 0x01, TAG_JOB = 0x02, TAG_END = 0x03, TAG_PRINTER = 0x04,
    TAG_INTEGER = 0x21, TAG_BOOLEAN = 0x22, TAG_ENUM = 0x23, TAG_RESOLUTION = 0x32,
    TAG_TEXT = 0x41, TAG_NAME = 0x42, TAG_KEYWORD = 0x44, TAG_URI = 0x45, TAG_CHARSET = 0x47,
    TAG_LANGUAGE = 0x48, TAG_MIME = 0x49,
};

static void say_why(char *why, int cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, (size_t)cap, fmt, ap);
    va_end(ap);
}

/* -----------------------------------------------------------------------
 * A growing buffer
 * ----------------------------------------------------------------------- */
typedef struct { BYTE *b; DWORD n, cap; BOOL bad; } Buf;

static void put(Buf *o, const void *d, DWORD n)
{
    if (o->bad) return;
    if (o->n + n > o->cap) {
        DWORD cap = o->cap ? o->cap : 4096;
        while (cap < o->n + n) cap *= 2;
        BYTE *nb = realloc(o->b, cap);
        if (!nb) { o->bad = TRUE; return; }
        o->b = nb;
        o->cap = cap;
    }
    memcpy(o->b + o->n, d, n);
    o->n += n;
}
static void put8(Buf *o, BYTE v) { put(o, &v, 1); }
static void put16(Buf *o, unsigned v) { BYTE b[2] = { (BYTE)(v >> 8), (BYTE)v }; put(o, b, 2); }
static void put32(Buf *o, DWORD v) { BYTE b[4] = { (BYTE)(v >> 24), (BYTE)(v >> 16), (BYTE)(v >> 8), (BYTE)v }; put(o, b, 4); }

/* One attribute (or, with @name NULL, another value of the one before) */
static void attr(Buf *o, BYTE tag, const char *name, const void *v, unsigned vn)
{
    put8(o, tag);
    unsigned nn = name ? (unsigned)strlen(name) : 0;
    put16(o, nn);
    if (nn) put(o, name, nn);
    put16(o, vn);
    put(o, v, vn);
}
static void attr_s(Buf *o, BYTE tag, const char *name, const char *v) { attr(o, tag, name, v, (unsigned)strlen(v)); }

/* -----------------------------------------------------------------------
 * URLs and HTTP
 * ----------------------------------------------------------------------- */
static BOOL wpre(const WCHAR *s, const WCHAR *p) { return !_wcsnicmp(s, p, wcslen(p)); }

BOOL ipp_is_port(const WCHAR *port)
{
    return wpre(port, L"http://") || wpre(port, L"https://") || wpre(port, L"ipp://") || wpre(port, L"ipps://");
}

/* The port's http(s) URL (ipp:// and ipps:// on port 631 unless one is named),
 * and its ipp(s):// form for printer-uri (UTF-8) */
static BOOL urls(const WCHAR *port, WCHAR *http, char *ipp, int cap)
{
    BOOL tls = wpre(port, L"https://") || wpre(port, L"ipps://");
    BOOL ippish = wpre(port, L"ipp://") || wpre(port, L"ipps://");
    const WCHAR *rest = wcsstr(port, L"://");
    if (!rest || cap < 64) return FALSE;
    rest += 3;
    WCHAR host[256];
    int hn = 0;
    while (rest[hn] && rest[hn] != L'/' && hn < 255) { host[hn] = rest[hn]; hn++; }
    host[hn] = 0;
    if (!hn) return FALSE;
    const WCHAR *path = rest[hn] == L'/' ? rest + hn : L"/ipp/print";
    const WCHAR *colon = wcsrchr(host, L':'), *bracket = wcsrchr(host, L']');
    BOOL has_port = colon && (!bracket || colon > bracket);
    WCHAR w[MAX_PATH + 64];
    swprintf(http, cap, L"%ls://%ls%ls%ls", tls ? L"https" : L"http", host, ippish && !has_port ? L":631" : L"", path);
    swprintf(w, MAX_PATH + 64, L"%ls://%ls%ls%ls", tls ? L"ipps" : L"ipp", host,
             !ippish && !has_port ? (tls ? L":443" : L":80") : L"", path);
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, ipp, cap, NULL, NULL) > 0;
}

/* POST @body as application/ipp to @port: the reply's body (malloc'd) or NULL with @why */
static BYTE *post(const WCHAR *port, const Buf *body, DWORD *rn, char *why, int whycap, DWORD *err)
{
    WCHAR url[MAX_PATH + 16], host[256], path[MAX_PATH];
    char ipp[MAX_PATH + 16];
    *err = ERROR_INVALID_PARAMETER;
    if (!urls(port, url, ipp, MAX_PATH + 16)) { say_why(why, whycap, "%ls is not a printer's URL", port); return NULL; }
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = MAX_PATH;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) { say_why(why, whycap, "%ls is not a printer's URL", port); return NULL; }
    BYTE *out = NULL;
    HINTERNET s = WinHttpOpen(L"NovaOS-IPP/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", path, NULL, NULL, NULL,
                                          uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (r && uc.nScheme == INTERNET_SCHEME_HTTPS) {
        DWORD f = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(r, WINHTTP_OPTION_SECURITY_FLAGS, &f, sizeof f);
    }
    if (r) WinHttpSetTimeouts(r, 10000, 10000, 60000, 60000);
    DWORD status = 0, sn = sizeof status;
    if (!r || !WinHttpSendRequest(r, L"Content-Type: application/ipp\r\n", (DWORD)-1, body->b, body->n, body->n, 0) ||
        !WinHttpReceiveResponse(r, NULL)) {
        *err = GetLastError();
        say_why(why, whycap, "%ls does not answer (error %lu)", url, (unsigned long)*err);
        goto done;
    }
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sn, NULL);
    if (status != 200) {
        *err = ERROR_BAD_NET_RESP;
        say_why(why, whycap, "%ls answers HTTP %lu", url, (unsigned long)status);
        goto done;
    }
    Buf b = { 0 };
    for (;;) {
        BYTE chunk[4096];
        DWORD got = 0;
        if (!WinHttpReadData(r, chunk, sizeof chunk, &got) || !got) break;
        put(&b, chunk, got);
    }
    if (b.bad || b.n < 8) {
        free(b.b);
        *err = ERROR_BAD_NET_RESP;
        say_why(why, whycap, "%ls sent no IPP reply", url);
        goto done;
    }
    out = b.b;
    *rn = b.n;
    *err = ERROR_SUCCESS;
done:
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return out;
}

/* A request's header and operation attributes, up to the printer's URI */
static void request(Buf *o, unsigned op, DWORD id, const WCHAR *port)
{
    WCHAR url[MAX_PATH + 16];
    char ipp[MAX_PATH + 16];
    urls(port, url, ipp, MAX_PATH + 16);
    put16(o, 0x0200);
    put16(o, op);
    put32(o, id);
    put8(o, TAG_OPERATION);
    attr_s(o, TAG_CHARSET, "attributes-charset", "utf-8");
    attr_s(o, TAG_LANGUAGE, "attributes-natural-language", "en");
    attr_s(o, TAG_URI, "printer-uri", ipp);
}

static const char *status_name(unsigned st)
{
    switch (st) {
    case 0x0400: return "bad request";
    case 0x0401: return "forbidden";
    case 0x0402: return "not authenticated";
    case 0x0406: return "not found";
    case 0x040A: return "document format not supported";
    case 0x0500: return "internal error";
    case 0x0501: return "operation not supported";
    case 0x0503: return "IPP version not supported";
    case 0x0506: return "not accepting jobs";
    case 0x0507: return "busy";
    default: return "an error";
    }
}

static volatile LONG g_request_id;

/* -----------------------------------------------------------------------
 * Get-Printer-Attributes
 * ----------------------------------------------------------------------- */
static void copy_text(char *dst, int cap, const BYTE *v, unsigned n)
{
    if (n >= (unsigned)cap) n = (unsigned)cap - 1;
    memcpy(dst, v, n);
    dst[n] = 0;
}

static BOOL is(const char *name, unsigned nn, const char *want) { return strlen(want) == nn && !memcmp(name, want, nn); }

DWORD ipp_attributes(const WCHAR *port, IppPrinter *out, char *why, int whycap)
{
    static const char *const want[] = {
        "printer-make-and-model", "printer-info", "printer-location", "printer-state", "printer-is-accepting-jobs",
        "document-format-supported", "pwg-raster-document-resolution-supported", "pwg-raster-document-type-supported",
    };
    memset(out, 0, sizeof *out);
    out->accepting = TRUE;
    Buf q = { 0 };
    request(&q, OP_GET_PRINTER_ATTRIBUTES, (DWORD)InterlockedIncrement(&g_request_id), port);
    attr_s(&q, TAG_NAME, "requesting-user-name", "spooler");
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++)
        attr_s(&q, TAG_KEYWORD, i ? NULL : "requested-attributes", want[i]);
    put8(&q, TAG_END);
    DWORD rn = 0, err;
    BYTE *r = q.bad ? NULL : post(port, &q, &rn, why, whycap, &err);
    free(q.b);
    if (q.bad) return ERROR_NOT_ENOUGH_MEMORY;
    if (!r) return err;
    unsigned st = (unsigned)(r[2] << 8 | r[3]);
    if (st >= 0x0100) {
        free(r);
        say_why(why, whycap, "the printer refused Get-Printer-Attributes: %s (0x%04X)", status_name(st), st);
        return ERROR_BAD_NET_RESP;
    }
    char name[64] = "";
    unsigned nn = 0;
    for (DWORD at = 8; at < rn;) {
        BYTE tag = r[at++];
        if (tag == TAG_END) break;
        if (tag < 0x10) continue;
        if (at + 2 > rn) break;
        unsigned n = (unsigned)(r[at] << 8 | r[at + 1]);
        at += 2;
        if (at + n + 2 > rn) break;
        if (n) { nn = n < sizeof name ? n : sizeof name - 1; memcpy(name, r + at, nn); }
        at += n;
        unsigned vn = (unsigned)(r[at] << 8 | r[at + 1]);
        at += 2;
        if (at + vn > rn) break;
        const BYTE *v = r + at;
        at += vn;
        DWORD num = vn == 4 ? (DWORD)(v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3]) : 0;
        if (is(name, nn, "printer-make-and-model") && tag == TAG_TEXT) copy_text(out->model, sizeof out->model, v, vn);
        else if (is(name, nn, "printer-info") && tag == TAG_TEXT) copy_text(out->info, sizeof out->info, v, vn);
        else if (is(name, nn, "printer-location") && tag == TAG_TEXT) copy_text(out->location, sizeof out->location, v, vn);
        else if (is(name, nn, "printer-state") && tag == TAG_ENUM) out->state = num;
        else if (is(name, nn, "printer-is-accepting-jobs") && tag == TAG_BOOLEAN && vn == 1) out->accepting = v[0] != 0;
        else if (is(name, nn, "document-format-supported") && tag == TAG_MIME) {
            if (vn == 15 && !memcmp(v, "application/pdf", 15)) out->pdf = TRUE;
            if (vn == 16 && !memcmp(v, "image/pwg-raster", 16)) out->pwg = TRUE;
            if (vn == 24 && !memcmp(v, "application/octet-stream", 24)) out->octet = TRUE;
        } else if (is(name, nn, "pwg-raster-document-resolution-supported") && tag == TAG_RESOLUTION && vn == 9) {
            DWORD y = (DWORD)(v[4] << 24 | v[5] << 16 | v[6] << 8 | v[7]);
            int dpi = v[8] == 4 ? (int)(num * 254 / 100) : (int)num;          /* (4: dots per cm) */
            if (num == y && dpi > 0 && out->nres < 8) out->res[out->nres++] = dpi;
        } else if (is(name, nn, "pwg-raster-document-type-supported") && tag == TAG_KEYWORD) {
            if (vn == 6 && !memcmp(v, "srgb_8", 6)) out->srgb = TRUE;
            if (vn == 7 && !memcmp(v, "sgray_8", 7)) out->sgray = TRUE;
        }
    }
    free(r);
    if (!out->info[0]) memcpy(out->info, out->model, sizeof out->info);
    return ERROR_SUCCESS;
}

/* -----------------------------------------------------------------------
 * PWG Raster from NovaOS's PDF pages
 * ----------------------------------------------------------------------- */
static long find(const BYTE *b, DWORD n, const char *what, DWORD from)
{
    size_t k = strlen(what);
    for (DWORD i = from; i + k <= n; i++) if (b[i] == (BYTE)what[0] && !memcmp(b + i, what, k)) return (long)i;
    return -1;
}

/* "123.45" (a MediaBox number) in hundredths */
static long hundredths(const char *s)
{
    long v = 0, f = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    if (*s == '.') { s++; for (int i = 0; i < 2; i++) f = f * 10 + (*s >= '0' && *s <= '9' ? *s++ - '0' : 0); }
    return v * 100 + f;
}

/* The PWG media name of a page @w x @h points (PWG 5101.1) */
static const char *media_name(int w, int h)
{
    static const struct { int w, h; const char *name; } m[] = {
        { 612, 792, "na_letter_8.5x11in" }, { 612, 1008, "na_legal_8.5x14in" }, { 595, 842, "iso_a4_210x297mm" },
        { 842, 1191, "iso_a3_297x420mm" }, { 420, 595, "iso_a5_148x210mm" },
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++)
        if ((abs(w - m[i].w) <= 1 && abs(h - m[i].h) <= 1) || (abs(h - m[i].w) <= 1 && abs(w - m[i].h) <= 1)) return m[i].name;
    return "";
}

static void header_str(BYTE *h, int off, const char *s) { memcpy(h + off, s, strlen(s)); }
static void header_u32(BYTE *h, int off, DWORD v) { h[off] = (BYTE)(v >> 24); h[off + 1] = (BYTE)(v >> 16); h[off + 2] = (BYTE)(v >> 8); h[off + 3] = (BYTE)v; }

/* One line of @w pixels of @bpp bytes: runs of a repeated pixel (up to 128)
 * and literal pixels (up to 128) */
static void pack_line(Buf *o, const BYTE *line, int w, int bpp)
{
    int x = 0;
    while (x < w) {
        int run = 1;
        while (x + run < w && run < 128 && !memcmp(line + (x + run) * bpp, line + x * bpp, (size_t)bpp)) run++;
        if (run > 1) {
            put8(o, (BYTE)(run - 1));
            put(o, line + x * bpp, (DWORD)bpp);
            x += run;
            continue;
        }
        int lit = 1;                     /* until two equal pixels start a run */
        while (x + lit < w && lit < 128 &&
               !(x + lit + 1 < w && !memcmp(line + (x + lit) * bpp, line + (x + lit + 1) * bpp, (size_t)bpp))) lit++;
        put8(o, (BYTE)(257 - lit));
        put(o, line + x * bpp, (DWORD)(lit * bpp));
        x += lit;
    }
}

BYTE *ipp_pdf_to_pwg(const BYTE *pdf, DWORD n, int dpi, BOOL gray, DWORD *out_n, char *why, int whycap)
{
    if (n < 9 || memcmp(pdf, "%PDF-", 5) || find(pdf, n, "/Producer (NovaOS Microsoft Print to PDF)", 0) < 0) {
        say_why(why, whycap, "only NovaOS's own PDF pages can be turned into PWG Raster");
        return NULL;
    }
    int pages = 0;
    for (long at = 0; (at = find(pdf, n, "/Subtype /Image ", (DWORD)at)) >= 0; at++) pages++;
    if (!pages) { say_why(why, whycap, "the PDF has no pages"); return NULL; }
    Buf o = { 0 };
    put(&o, "RaS2", 4);
    int bpp = gray ? 1 : 3;
    long box = 0, img = 0;
    for (int page = 0; page < pages && !o.bad; page++) {
        box = find(pdf, n, "/MediaBox [0 0 ", (DWORD)box);
        img = find(pdf, n, "/Subtype /Image /Width ", (DWORD)img);
        long len = img >= 0 ? find(pdf, n, "/Length ", (DWORD)img) : -1;
        long st = len >= 0 ? find(pdf, n, ">>\nstream\n", (DWORD)len) : -1;
        if (box < 0 || st < 0) { say_why(why, whycap, "page %d cannot be read", page + 1); break; }
        char nums[64];
        copy_text(nums, sizeof nums, pdf + box + 15, 40);
        char *sp = strchr(nums, ' ');
        long ptw = hundredths(nums), pth = sp ? hundredths(sp + 1) : 0;
        int sw = atoi((const char *)pdf + img + 23);
        const char *hp = strstr((const char *)pdf + img + 23, "/Height ");
        int sh = hp ? atoi(hp + 8) : 0;
        int zlen = atoi((const char *)pdf + len + 8);
        box++; img++;
        if (ptw <= 0 || pth <= 0 || sw <= 0 || sh <= 0 || zlen <= 0 || (DWORD)(st + 10 + zlen) > n) {
            say_why(why, whycap, "page %d cannot be read", page + 1);
            break;
        }
        int outlen = 0;
        BYTE *rgb = (BYTE *)stbi_zlib_decode_malloc_guesssize((const char *)pdf + st + 10, zlen, sw * sh * 3, &outlen);
        if (!rgb || outlen != sw * sh * 3) {
            free(rgb);
            say_why(why, whycap, "page %d's image does not inflate", page + 1);
            break;
        }
        /* the page at the printer's resolution */
        int w = (int)((long long)ptw * dpi / 7200), h = (int)((long long)pth * dpi / 7200);
        if (w <= 0 || h <= 0) { free(rgb); say_why(why, whycap, "page %d is empty", page + 1); break; }
        BYTE hd[1796];
        memset(hd, 0, sizeof hd);
        header_str(hd, 0, "PwgRaster");
        header_u32(hd, 276, (DWORD)dpi);                  /* HWResolution */
        header_u32(hd, 280, (DWORD)dpi);
        header_u32(hd, 352, (DWORD)((ptw + 50) / 100));   /* PageSize (points) */
        header_u32(hd, 356, (DWORD)((pth + 50) / 100));
        header_u32(hd, 372, (DWORD)w);                    /* Width, Height */
        header_u32(hd, 376, (DWORD)h);
        header_u32(hd, 384, 8);                           /* BitsPerColor */
        header_u32(hd, 388, (DWORD)(8 * bpp));            /* BitsPerPixel */
        header_u32(hd, 392, (DWORD)(w * bpp));            /* BytesPerLine */
        header_u32(hd, 396, 0);                           /* ColorOrder: chunky */
        header_u32(hd, 400, gray ? 18 : 19);              /* ColorSpace: sGray, sRGB */
        header_u32(hd, 420, (DWORD)bpp);                  /* NumColors */
        header_u32(hd, 452, (DWORD)pages);                /* TotalPageCount */
        header_u32(hd, 456, 1);                           /* CrossFeedTransform */
        header_u32(hd, 460, 1);                           /* FeedTransform */
        header_u32(hd, 472, (DWORD)w);                    /* ImageBoxRight, ImageBoxBottom */
        header_u32(hd, 476, (DWORD)h);
        header_str(hd, 1732, media_name((int)((ptw + 50) / 100), (int)((pth + 50) / 100)));
        put(&o, hd, sizeof hd);
        BYTE *line = malloc((size_t)w * bpp), *prev = malloc((size_t)w * bpp);
        if (!line || !prev) { o.bad = TRUE; free(line); free(prev); free(rgb); break; }
        int rep = 0;
        for (int y = 0; y < h; y++) {
            const BYTE *src = rgb + (size_t)((long long)y * sh / h) * sw * 3;
            for (int x = 0; x < w; x++) {
                const BYTE *p = src + (size_t)((long long)x * sw / w) * 3;
                if (gray) line[x] = (BYTE)((77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8);
                else memcpy(line + x * 3, p, 3);
            }
            /* equal lines share one encoding (up to 256) */
            if (y && rep < 255 && !memcmp(line, prev, (size_t)w * bpp)) { rep++; continue; }
            if (y) { put8(&o, (BYTE)rep); pack_line(&o, prev, w, bpp); }
            BYTE *t = prev; prev = line; line = t;
            rep = 0;
        }
        put8(&o, (BYTE)rep);
        pack_line(&o, prev, w, bpp);
        free(line);
        free(prev);
        free(rgb);
        if (page == pages - 1 && !o.bad) {
            *out_n = o.n;
            return o.b;
        }
    }
    if (o.bad) say_why(why, whycap, "not enough memory for the raster pages");
    free(o.b);
    return NULL;
}

/* -----------------------------------------------------------------------
 * Print-Job
 * ----------------------------------------------------------------------- */
DWORD ipp_print(const WCHAR *port, const IppPrinter *pr, const WCHAR *job_name, const WCHAR *user,
                const BYTE *data, DWORD n, const char **format, char *why, int whycap)
{
    BOOL is_pdf = n >= 5 && !memcmp(data, "%PDF-", 5);
    BYTE *raster = NULL;
    DWORD rn = 0;
    *format = "application/octet-stream";
    if (is_pdf && pr->pdf) *format = "application/pdf";
    else if (is_pdf && pr->pwg) {
        /* the page's own resolution if the printer has it, else the
         * nearest it has (the pixels are scaled) */
        int dpi = 0, page_dpi = 300;
        long im = find(data, n, "/Subtype /Image /Width ", 0), box = find(data, n, "/MediaBox [0 0 ", 0);
        if (im >= 0 && box >= 0) {
            long ptw = hundredths((const char *)data + box + 15);
            if (ptw > 0) page_dpi = (int)((long long)atoi((const char *)data + im + 23) * 7200 / ptw);
        }
        for (int i = 0; i < pr->nres; i++)
            if (!dpi || abs(pr->res[i] - page_dpi) < abs(dpi - page_dpi)) dpi = pr->res[i];
        if (!dpi) dpi = 300;
        raster = ipp_pdf_to_pwg(data, n, dpi, !pr->srgb && pr->sgray, &rn, why, whycap);
        if (!raster) return ERROR_INVALID_DATA;
        *format = "image/pwg-raster";
        data = raster;
        n = rn;
    } else if (is_pdf || !pr->octet) {
        if (!pr->octet && !(pr->pdf || pr->pwg)) {
            say_why(why, whycap, "the printer takes neither PDF nor PWG Raster");
            return ERROR_NOT_SUPPORTED;
        }
        if (!pr->octet) {
            say_why(why, whycap, "the printer does not take raw data");
            return ERROR_NOT_SUPPORTED;
        }
    }
    char jn[256], un[128];
    WideCharToMultiByte(CP_UTF8, 0, job_name && *job_name ? job_name : L"Untitled", -1, jn, sizeof jn, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, user && *user ? user : L"user", -1, un, sizeof un, NULL, NULL);
    Buf q = { 0 };
    request(&q, OP_PRINT_JOB, (DWORD)InterlockedIncrement(&g_request_id), port);
    attr_s(&q, TAG_NAME, "requesting-user-name", un);
    attr_s(&q, TAG_NAME, "job-name", jn);
    attr_s(&q, TAG_MIME, "document-format", *format);
    put8(&q, TAG_END);
    put(&q, data, n);
    free(raster);
    if (q.bad) { free(q.b); say_why(why, whycap, "not enough memory"); return ERROR_NOT_ENOUGH_MEMORY; }
    DWORD err, an = 0;
    BYTE *r = post(port, &q, &an, why, whycap, &err);
    free(q.b);
    if (!r) return err;
    unsigned st = (unsigned)(r[2] << 8 | r[3]);
    free(r);
    if (st >= 0x0100) {
        say_why(why, whycap, "the printer refused the job: %s (0x%04X)", status_name(st), st);
        return st == 0x040A ? ERROR_NOT_SUPPORTED : ERROR_BAD_NET_RESP;
    }
    return ERROR_SUCCESS;
}
