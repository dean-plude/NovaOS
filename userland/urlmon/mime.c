/*
 * mime.c — FindMimeFromData: the MIME type of some data, sniffed from its
 * first bytes (images, documents, archives, media), else named by the URL's
 * file extension, else the proposed type.  WPF asks it for the type of a
 * resource it loads by URI.  NovaOS's own: no MIT/BSD urlmon exists.
 */
#include <windows.h>
#include <objbase.h>
#include <wchar.h>
#include <string.h>

#define URLMONAPI __declspec(dllexport)
#define FMFD_RETURNUPDATEDIMGMIMES 0x20

/* Magic numbers at the start of the data */
static const struct { const char *magic; BYTE len, at; const WCHAR *mime, *updated; } g_sniff[] = {
    { "\x89PNG\r\n\x1a\n", 8, 0, L"image/x-png", L"image/png" },
    { "\xFF\xD8\xFF", 3, 0, L"image/pjpeg", L"image/jpeg" },
    { "GIF87a", 6, 0, L"image/gif" },
    { "GIF89a", 6, 0, L"image/gif" },
    { "BM", 2, 0, L"image/bmp" },
    { "II*\0", 4, 0, L"image/tiff" },
    { "MM\0*", 4, 0, L"image/tiff" },
    { "\0\0\1\0", 4, 0, L"image/x-icon" },
    { "%PDF-", 5, 0, L"application/pdf" },
    { "PK\3\4", 4, 0, L"application/x-zip-compressed" },
    { "\x1F\x8B", 2, 0, L"application/x-gzip-compressed" },
    { "MZ", 2, 0, L"application/x-msdownload" },
    { "RIFF", 4, 0, L"audio/wav" },                     /* (refined below: AVI) */
    { "OggS", 4, 0, L"audio/ogg" },
    { "ID3", 3, 0, L"audio/mpeg" },
    { "ftyp", 4, 4, L"video/mp4" },
    { "<?xml", 5, 0, L"text/xml" },
    { "{\\rtf", 5, 0, L"text/richtext" },
};

/* File extensions (as the registry's MIME database has them on Windows) */
static const struct { const WCHAR *ext, *mime; } g_ext[] = {
    { L".png", L"image/png" }, { L".jpg", L"image/jpeg" }, { L".jpeg", L"image/jpeg" }, { L".jpe", L"image/jpeg" },
    { L".gif", L"image/gif" }, { L".bmp", L"image/bmp" }, { L".dib", L"image/bmp" }, { L".ico", L"image/x-icon" },
    { L".tif", L"image/tiff" }, { L".tiff", L"image/tiff" }, { L".wdp", L"image/vnd.ms-photo" },
    { L".jxr", L"image/vnd.ms-photo" }, { L".svg", L"image/svg+xml" },
    { L".xaml", L"application/xaml+xml" }, { L".baml", L"application/baml+xml" },
    { L".xps", L"application/vnd.ms-xpsdocument" }, { L".xml", L"text/xml" },
    { L".htm", L"text/html" }, { L".html", L"text/html" }, { L".txt", L"text/plain" }, { L".css", L"text/css" },
    { L".js", L"application/javascript" }, { L".json", L"application/json" }, { L".pdf", L"application/pdf" },
    { L".zip", L"application/x-zip-compressed" }, { L".exe", L"application/x-msdownload" },
    { L".dll", L"application/x-msdownload" }, { L".wav", L"audio/wav" }, { L".mp3", L"audio/mpeg" },
    { L".wma", L"audio/x-ms-wma" }, { L".mp4", L"video/mp4" }, { L".avi", L"video/avi" },
    { L".wmv", L"video/x-ms-wmv" }, { L".mkv", L"video/x-matroska" }, { L".ttf", L"application/x-font-ttf" },
    { L".otf", L"application/x-font-otf" },
};

static const WCHAR *from_data(const BYTE *b, DWORD n, BOOL updated)
{
    for (unsigned i = 0; i < sizeof(g_sniff) / sizeof(g_sniff[0]); i++) {
        DWORD at = g_sniff[i].at, len = g_sniff[i].len;
        if (n < at + len || memcmp(b + at, g_sniff[i].magic, len)) continue;
        if (!memcmp(g_sniff[i].magic, "RIFF", 4) && n >= 12 && !memcmp(b + 8, "AVI ", 4)) return L"video/avi";
        return updated && g_sniff[i].updated ? g_sniff[i].updated : g_sniff[i].mime;
    }
    return NULL;
}

static const WCHAR *from_url(const WCHAR *url)
{
    const WCHAR *end = url + wcslen(url), *dot = NULL;
    for (const WCHAR *c = url; c < end; c++) {          /* the last path segment's extension, before ?query or #fragment */
        if (*c == L'?' || *c == L'#') { end = c; break; }
        if (*c == L'/' || *c == L'\\') dot = NULL;
        else if (*c == L'.') dot = c;
    }
    if (!dot) return NULL;
    for (unsigned i = 0; i < sizeof(g_ext) / sizeof(g_ext[0]); i++) {
        size_t k = wcslen(g_ext[i].ext);
        if ((size_t)(end - dot) == k && !_wcsnicmp(dot, g_ext[i].ext, k)) return g_ext[i].mime;
    }
    return NULL;
}

URLMONAPI HRESULT WINAPI FindMimeFromData(LPVOID bc, LPCWSTR url, LPVOID buf, DWORD size, LPCWSTR proposed,
                                         DWORD flags, LPWSTR *out, DWORD reserved)
{
    (void)bc; (void)reserved;
    if (!out || (!url && !buf)) return E_INVALIDARG;
    *out = NULL;
    const WCHAR *m = buf && size ? from_data(buf, size, (flags & FMFD_RETURNUPDATEDIMGMIMES) != 0) : NULL;
    if (!m && url) m = from_url(url);
    if (!m && buf && size) {                            /* text, or else bytes */
        BOOL text = TRUE;
        for (DWORD i = 0; i < size && i < 256 && text; i++) {
            BYTE c = ((const BYTE *)buf)[i];
            if (c < 0x20 && c != '\t' && c != '\r' && c != '\n' && c != 0x1B) text = FALSE;
        }
        m = proposed ? proposed : text ? L"text/plain" : L"application/octet-stream";
    }
    if (!m) m = proposed;
    if (!m) return E_FAIL;
    size_t n = (wcslen(m) + 1) * sizeof(WCHAR);
    *out = CoTaskMemAlloc(n);
    if (!*out) return E_OUTOFMEMORY;
    memcpy(*out, m, n);
    return S_OK;
}
