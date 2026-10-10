/*
 * setupapi.dll — reading INF files: SetupOpenInfFile, SetupGetLineText and
 * the reader driver.c stages driver packages with.  An INF is a list of
 * [sections] of "key = value" lines; ';' starts a comment outside quotes,
 * a line ending in '\' goes on on the next, and %name% in a value is
 * replaced from the [Strings] section (%% is a percent sign).
 */
#include "inf.h"

#define SETUPAPI __declspec(dllexport)
#define ERROR_WRONG_INF_STYLE_     0xE0000100
#define ERROR_LINE_NOT_FOUND_      0xE0000102
#define ERROR_CLASS_MISMATCH_     0xE0000201
#define LINE_MAX_ 1024

static WCHAR lower(WCHAR c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static BOOL ieq(const WCHAR *a, const WCHAR *b)
{
    while (*a && lower(*a) == lower(*b)) { a++; b++; }
    return !*a && !*b;
}

Inf *inf_open(const WCHAR *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD n = GetFileSize(f, NULL), got = 0;
    BYTE *raw = n < 16u * 1024 * 1024 ? HeapAlloc(GetProcessHeap(), 0, n + 2) : NULL;
    BOOL ok = raw && ReadFile(f, raw, n, &got, NULL) && got == n;
    CloseHandle(f);
    if (!ok) {
        if (raw) HeapFree(GetProcessHeap(), 0, raw);
        SetLastError(ERROR_READ_FAULT);
        return NULL;
    }
    Inf *inf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Inf));
    WCHAR *text;
    if (n >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {             /* UTF-16 */
        DWORD k = (n - 2) / 2;
        text = HeapAlloc(GetProcessHeap(), 0, (k + 1) * sizeof(WCHAR));
        if (text) { memcpy(text, raw + 2, k * sizeof(WCHAR)); text[k] = 0; }
    } else {
        UINT cp = n >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF ? CP_UTF8 : CP_ACP;
        const char *s = (const char *)raw + (cp == CP_UTF8 ? 3 : 0);
        int len = (int)n - (cp == CP_UTF8 ? 3 : 0);
        int k = len ? MultiByteToWideChar(cp, 0, s, len, NULL, 0) : 0;
        text = HeapAlloc(GetProcessHeap(), 0, (k + 1) * sizeof(WCHAR));
        if (text) { if (k) MultiByteToWideChar(cp, 0, s, len, text, k); text[k] = 0; }
    }
    HeapFree(GetProcessHeap(), 0, raw);
    if (!inf || !text) {
        if (inf) HeapFree(GetProcessHeap(), 0, inf);
        if (text) HeapFree(GetProcessHeap(), 0, text);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    inf->magic = INF_MAGIC;
    inf->text = text;
    GetFullPathNameW(path, MAX_PATH, inf->path, NULL);
    return inf;
}

void inf_close(Inf *inf)
{
    if (!inf || inf->magic != INF_MAGIC) return;
    inf->magic = 0;
    HeapFree(GetProcessHeap(), 0, inf->text);
    HeapFree(GetProcessHeap(), 0, inf);
}

/* The next logical line from *@p into @buf: comments off, trimmed.
 * FALSE at the end of the text */
static BOOL next_line(const WCHAR **p, WCHAR *buf, int cap)
{
    const WCHAR *s = *p;
    if (!*s) return FALSE;
    int n = 0;
    BOOL quoted = FALSE, comment = FALSE;
    for (; *s; s++) {
        if (*s == '\r') continue;
        if (*s == '\n') {
            /* a '\' last on the line (outside a comment) joins the next one */
            int e = n;
            while (e && (buf[e - 1] == ' ' || buf[e - 1] == '\t')) e--;
            if (!comment && e && buf[e - 1] == '\\') { n = e - 1; continue; }
            s++;
            break;
        }
        if (comment) continue;
        if (*s == '"') quoted = !quoted;
        else if (*s == ';' && !quoted) { comment = TRUE; continue; }
        if (n < cap - 1) buf[n++] = *s;
    }
    *p = s;
    while (n && (buf[n - 1] == ' ' || buf[n - 1] == '\t')) n--;
    buf[n] = 0;
    int lead = 0;
    while (buf[lead] == ' ' || buf[lead] == '\t') lead++;
    if (lead) memmove(buf, buf + lead, (n - lead + 1) * sizeof(WCHAR));
    return TRUE;
}

/* Split a line into key and raw value (in place); key "" with no '=' */
static void split(WCHAR *line, WCHAR **key, WCHAR **value)
{
    BOOL quoted = FALSE;
    for (WCHAR *s = line; *s; s++) {
        if (*s == '"') quoted = !quoted;
        if (*s == '=' && !quoted) {
            WCHAR *e = s;
            while (e > line && (e[-1] == ' ' || e[-1] == '\t')) e--;
            *e = 0;
            s++;
            while (*s == ' ' || *s == '\t') s++;
            *key = line;
            *value = s;
            /* "key" in quotes */
            size_t k = lstrlenW(line);
            if (k >= 2 && line[0] == '"' && line[k - 1] == '"') { line[k - 1] = 0; *key = line + 1; }
            return;
        }
    }
    *key = line + lstrlenW(line);
    *value = line;
}

static BOOL find_raw(const Inf *inf, const WCHAR *section, const WCHAR *key, WCHAR *out, int cap);

/* @raw with its quotes taken off and %strings% replaced */
static void cook(const Inf *inf, const WCHAR *raw, WCHAR *out, int cap, int depth)
{
    int n = 0;
    BOOL quoted = FALSE;
    for (const WCHAR *s = raw; *s && n < cap - 1; s++) {
        if (*s == '"') {
            if (quoted && s[1] == '"') { out[n++] = '"'; s++; }   /* "" in quotes: a quote */
            else quoted = !quoted;
            continue;
        }
        if (*s == '%') {
            const WCHAR *e = s + 1;
            while (*e && *e != '%') e++;
            if (!*e) { out[n++] = '%'; continue; }
            if (e == s + 1) { out[n++] = '%'; s = e; continue; }       /* %% */
            WCHAR name[128], val[LINE_MAX_];
            int k = (int)(e - s - 1) < 127 ? (int)(e - s - 1) : 127;
            memcpy(name, s + 1, k * sizeof(WCHAR));
            name[k] = 0;
            if (depth < 4 && find_raw(inf, L"Strings", name, val, LINE_MAX_)) {
                WCHAR sub[LINE_MAX_];
                cook(inf, val, sub, LINE_MAX_, depth + 1);
                for (WCHAR *t = sub; *t && n < cap - 1; t++) out[n++] = *t;
            } else {                                                   /* left as it was */
                for (const WCHAR *t = s; t <= e && n < cap - 1; t++) out[n++] = *t;
            }
            s = e;
            continue;
        }
        out[n++] = *s;
    }
    out[n] = 0;
}

static BOOL section_is(const WCHAR *line, const WCHAR *section)
{
    if (line[0] != '[') return FALSE;
    const WCHAR *e = line + 1;
    while (*e && *e != ']') e++;
    if (*e != ']') return FALSE;
    WCHAR name[256];
    int k = (int)(e - line - 1) < 255 ? (int)(e - line - 1) : 255;
    memcpy(name, line + 1, k * sizeof(WCHAR));
    name[k] = 0;
    while (k && name[k - 1] == ' ') name[--k] = 0;
    return ieq(name, section);
}

static BOOL find_raw(const Inf *inf, const WCHAR *section, const WCHAR *key, WCHAR *out, int cap)
{
    const WCHAR *p = inf->text;
    WCHAR line[LINE_MAX_];
    BOOL in = FALSE;
    while (next_line(&p, line, LINE_MAX_)) {
        if (line[0] == '[') { in = section_is(line, section); continue; }
        if (!in || !line[0]) continue;
        WCHAR *k, *v;
        split(line, &k, &v);
        if (*k && ieq(k, key)) {
            lstrcpynW(out, v, cap);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL inf_get(const Inf *inf, const WCHAR *section, const WCHAR *key, WCHAR *out, int cap)
{
    WCHAR raw[LINE_MAX_];
    if (!find_raw(inf, section, key, raw, LINE_MAX_)) return FALSE;
    cook(inf, raw, out, cap, 0);
    return TRUE;
}

void inf_lines(const Inf *inf, const WCHAR *section, inf_line_fn fn, void *ctx)
{
    const WCHAR *p = inf->text;
    WCHAR line[LINE_MAX_], val[LINE_MAX_];
    BOOL in = FALSE;
    while (next_line(&p, line, LINE_MAX_)) {
        if (line[0] == '[') { in = section_is(line, section); continue; }
        if (!in || !line[0]) continue;
        WCHAR *k, *v;
        split(line, &k, &v);
        cook(inf, v, val, LINE_MAX_, 0);
        if (!fn(k, val, ctx)) return;
    }
}

void inf_field(const WCHAR *value, int n, WCHAR *out, int cap)
{
    const WCHAR *s = value;
    for (int i = 0; i < n && *s; i++) {
        while (*s && *s != ',') s++;
        if (*s) s++;
    }
    while (*s == ' ' || *s == '\t') s++;
    int k = 0;
    while (s[k] && s[k] != ',') k++;
    while (k && (s[k - 1] == ' ' || s[k - 1] == '\t')) k--;
    if (k > cap - 1) k = cap - 1;
    memcpy(out, s, k * sizeof(WCHAR));
    out[k] = 0;
}

/* -----------------------------------------------------------------------
 * The API
 * ----------------------------------------------------------------------- */
#define INF_STYLE_OLDNT 0x1
#define INF_STYLE_WIN4  0x2

SETUPAPI HANDLE WINAPI SetupOpenInfFileW(LPCWSTR name, LPCWSTR cls, DWORD style, PUINT err_line)
{
    if (err_line) *err_line = 0;
    if (!name) { SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }
    Inf *inf = inf_open(name);
    if (!inf) return INVALID_HANDLE_VALUE;
    WCHAR sig[64], c[64];
    if ((style & INF_STYLE_WIN4) && !inf_get(inf, L"Version", L"Signature", sig, 64)) {
        inf_close(inf);
        SetLastError(ERROR_WRONG_INF_STYLE_);
        return INVALID_HANDLE_VALUE;
    }
    if (cls && (!inf_get(inf, L"Version", L"Class", c, 64) || !ieq(c, cls))) {
        inf_close(inf);
        SetLastError(ERROR_CLASS_MISMATCH_);
        return INVALID_HANDLE_VALUE;
    }
    SetLastError(0);
    return inf;
}

SETUPAPI HANDLE WINAPI SetupOpenInfFileA(LPCSTR name, LPCSTR cls, DWORD style, PUINT err_line)
{
    WCHAR wn[MAX_PATH], wc[128];
    if (!name) { SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }
    MultiByteToWideChar(CP_ACP, 0, name, -1, wn, MAX_PATH);
    if (cls) MultiByteToWideChar(CP_ACP, 0, cls, -1, wc, 128);
    return SetupOpenInfFileW(wn, cls ? wc : NULL, style, err_line);
}

SETUPAPI void WINAPI SetupCloseInfFile(HANDLE h)
{
    if (h && h != INVALID_HANDLE_VALUE) inf_close((Inf *)h);
}

/* (with a context: the line it names; NovaOS has no INF contexts yet, so
 * only the section and key form) */
SETUPAPI BOOL WINAPI SetupGetLineTextW(PVOID ctx, HANDLE h, LPCWSTR section, LPCWSTR key,
                                       LPWSTR buf, DWORD size, PDWORD need)
{
    Inf *inf = (Inf *)h;
    if (ctx || !inf || inf == INVALID_HANDLE_VALUE || inf->magic != INF_MAGIC || !section || !key) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR v[LINE_MAX_];
    if (!inf_get(inf, section, key, v, LINE_MAX_)) { SetLastError(ERROR_LINE_NOT_FOUND_); return FALSE; }
    DWORD n = (DWORD)lstrlenW(v) + 1;
    if (need) *need = n;
    if (!buf) return TRUE;
    if (size < n) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    memcpy(buf, v, n * sizeof(WCHAR));
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetLineTextA(PVOID ctx, HANDLE h, LPCSTR section, LPCSTR key,
                                       LPSTR buf, DWORD size, PDWORD need)
{
    WCHAR ws[256], wk[256], v[LINE_MAX_];
    if (!section || !key) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    MultiByteToWideChar(CP_ACP, 0, section, -1, ws, 256);
    MultiByteToWideChar(CP_ACP, 0, key, -1, wk, 256);
    if (!SetupGetLineTextW(ctx, h, ws, wk, v, LINE_MAX_, NULL)) return FALSE;
    DWORD n = (DWORD)WideCharToMultiByte(CP_ACP, 0, v, -1, NULL, 0, NULL, NULL);
    if (need) *need = n;
    if (!buf) return TRUE;
    if (size < n) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    WideCharToMultiByte(CP_ACP, 0, v, -1, buf, (int)size, NULL, NULL);
    return TRUE;
}
