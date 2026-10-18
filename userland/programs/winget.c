/*
 * winget.exe — the Windows Package Manager's command line, NovaOS's own
 *
 *   winget search [query] [--id ID] [--name NAME] [-s SOURCE] [-e] [-n COUNT]
 *   winget show <query> [--id ID] [-s SOURCE] [-v VERSION]
 *   winget install <query> [--id ID] [-s SOURCE] [-v VERSION] [-e] [-i|-h]
 *   winget install -m <manifest file or folder>
 *   winget list [query] [--upgrade-available]
 *   winget upgrade [query] [--all] [--include-unknown] [-i|-h]
 *   winget uninstall <query> [--id ID] [-s SOURCE] [-i|-h]
 *   winget source list | add <name> <url> [-t Microsoft.Rest] | remove <name> | update | reset --force
 *   winget hash <file>   winget --version   winget --info
 *
 * Microsoft's winget-cli is an MSIX package (App Installer) started through
 * an App Execution Alias, written in C++/WinRT on Windows.Management.
 * Deployment, and its community source is a SQLite index inside an MSIX;
 * this is a C program with the same commands and options, which reads the
 * same manifests:
 *
 *   novaos   the App Store's catalog (C:\Windows\AppStore\catalog.json, read
 *            with the App Store's own parser, kernel/apps/store_catalog.h),
 *            installed the App Store's way: downloads in C:\Downloads,
 *            archives unpacked by 7-Zip into C:\Programs\<dest>
 *   winget   the community repository, github.com/microsoft/winget-pkgs
 *            (YAML manifests, MIT): a package by its exact identifier, its
 *            versions listed by api.github.com, its manifests from
 *            raw.githubusercontent.com
 *   others   REST sources ("winget source add NAME URL"), the open
 *            WinGet REST source API (information, manifestSearch,
 *            packageManifests)
 *
 * Installers: portable (C:\Programs\<command>\<command>.exe, so the
 * Terminal finds it by name), zip (7-Zip, then the nested portable or
 * installer), exe / inno / nullsoft / burn (run with their silent switches)
 * and msi / wix (msiexec /qn); the download's SHA-256 must match the
 * manifest's.  What winget installed is kept in
 * HKLM\SOFTWARE\NovaOS\WinGet\Packages; "list" adds the App Store's
 * programs and the installed programs Windows lists (the Uninstall keys).
 */
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#include "../common/hash.c"

/* -----------------------------------------------------------------------
 * The App Store's catalog parser, built for user mode
 * ----------------------------------------------------------------------- */
typedef unsigned GdiColor;
#define GDI_C(r, g, b) ((GdiColor)((r) << 16 | (g) << 8 | (b)))
#define RAMFS_PATH_MAX 256
static void *kzalloc(size_t n) { return calloc(1, n); }
static void kfree(void *p) { free(p); }
/* The kernel's URL check (net/http.c): a scheme, a host, a port */
static bool NetParseUrl(const char *url, char *host, int host_cap, UINT16 *port, char *path, int path_cap, bool *https)
{
    const char *p = url;
    *https = !strncmp(p, "https://", 8);
    *port = *https ? 443 : 80;
    if (*https) p += 8; else if (!strncmp(p, "http://", 7)) p += 7; else return false;
    int n = 0;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#') { if (n >= host_cap - 1) return false; host[n++] = *p++; }
    host[n] = 0;
    if (!n) return false;
    if (*p == ':') {
        unsigned v = 0;
        for (p++; *p >= '0' && *p <= '9'; p++) v = v * 10 + (unsigned)(*p - '0');
        if (!v || v > 65535) return false;
        *port = (UINT16)v;
    }
    snprintf(path, (size_t)path_cap, "%s%s", *p == '/' ? "" : "/", p);
    return true;
}
#include "../../kernel/apps/store_catalog.h"

/* -----------------------------------------------------------------------
 * Exit codes (winget's own: APPINSTALLER_CLI_ERROR_*)
 * ----------------------------------------------------------------------- */
#define E_INVALID_ARGS     0x8A150002u
#define E_COMMAND_FAILED   0x8A150003u
#define E_MANIFEST_FAILED  0x8A150004u
#define E_DOWNLOAD_FAILED  0x8A150008u
#define E_NO_INSTALLER     0x8A150010u
#define E_HASH_MISMATCH    0x8A150011u
#define E_NOT_FOUND        0x8A150014u
#define E_MULTIPLE         0x8A150015u
#define E_SOURCE_FAILED    0x8A15000Fu
#define E_REQUIRES_ADMIN   0x8A150019u
#define E_INSTALL_FAILED   0x8A150101u
#define E_NO_UPGRADE       0x8A15002Bu
#define E_UNINSTALL_FAILED 0x8A150030u

#define WINGET_VERSION "1.0.0-novaos"
#define KEY_ROOT     "SOFTWARE\\NovaOS\\WinGet"
#define KEY_PACKAGES KEY_ROOT "\\Packages"
#define KEY_SOURCES  KEY_ROOT "\\Sources"
#define COMMUNITY_API "https://api.github.com/repos/microsoft/winget-pkgs/contents/"
#define COMMUNITY_RAW "https://raw.githubusercontent.com/microsoft/winget-pkgs/master/"
#define SEVEN_ZIP "C:\\Programs\\7-Zip\\7z.exe"

static int ieq(const char *a, const char *b) { return !_stricmp(a, b); }

static const char *istr(const char *hay, const char *needle)    /* case-insensitive strstr */
{
    size_t n = strlen(needle);
    for (; *hay; hay++) if (!_strnicmp(hay, needle, n)) return hay;
    return n ? NULL : hay;
}

static void scopy(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src ? src : "");
}

/* -----------------------------------------------------------------------
 * Documents: JSON (REST sources, GitHub's listings) and the YAML winget's
 * manifests are written in, both read into the same tree
 * ----------------------------------------------------------------------- */
enum { N_NULL, N_STR, N_MAP, N_LIST };
typedef struct Node {
    int type;
    char *key;                     /* in a map */
    char *str;                     /* N_STR */
    struct Node *kids, *last, *next;
} Node;

static Node *node(int type, const char *key)
{
    Node *n = calloc(1, sizeof(Node));
    n->type = type;
    if (key) n->key = _strdup(key);
    return n;
}

static void add(Node *parent, Node *kid)
{
    if (parent->last) parent->last->next = kid; else parent->kids = kid;
    parent->last = kid;
}

static void node_free(Node *n)
{
    while (n) {
        Node *next = n->next;
        node_free(n->kids);
        free(n->key); free(n->str); free(n);
        n = next;
    }
}

static Node *get(const Node *map, const char *key)
{
    if (!map || map->type != N_MAP) return NULL;
    for (Node *k = map->kids; k; k = k->next) if (k->key && !strcmp(k->key, key)) return k;
    return NULL;
}

static const char *str(const Node *map, const char *key)
{
    Node *n = get(map, key);
    return n && n->type == N_STR ? n->str : NULL;
}

/* JSON */
typedef struct { const char *p, *end; } Json;
static void js_ws(Json *j) { while (j->p < j->end && strchr(" \t\r\n", *j->p)) j->p++; }

static char *js_string(Json *j)
{
    if (j->p >= j->end || *j->p != '"') return NULL;
    j->p++;
    size_t cap = 64, n = 0;
    char *out = malloc(cap);
    while (j->p < j->end && *j->p != '"') {
        if (n + 8 > cap) out = realloc(out, cap *= 2);
        char c = *j->p++;
        if (c != '\\') { out[n++] = c; continue; }
        if (j->p >= j->end) break;
        c = *j->p++;
        switch (c) {
        case 'n': out[n++] = '\n'; break;
        case 't': out[n++] = '\t'; break;
        case 'r': out[n++] = '\r'; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case 'u': {
            unsigned u = 0;
            for (int i = 0; i < 4 && j->p < j->end; i++) {
                char h = *j->p++;
                u = u << 4 | (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
            }
            if (u >= 0xD800 && u <= 0xDBFF && j->end - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
                unsigned lo = 0;
                for (int i = 2; i < 6; i++) {
                    char h = j->p[i];
                    lo = lo << 4 | (unsigned)(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
                }
                j->p += 6;
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
            }
            if (u < 0x80) out[n++] = (char)u;
            else if (u < 0x800) { out[n++] = (char)(0xC0 | u >> 6); out[n++] = (char)(0x80 | (u & 63)); }
            else if (u < 0x10000) { out[n++] = (char)(0xE0 | u >> 12); out[n++] = (char)(0x80 | (u >> 6 & 63)); out[n++] = (char)(0x80 | (u & 63)); }
            else { out[n++] = (char)(0xF0 | u >> 18); out[n++] = (char)(0x80 | (u >> 12 & 63));
                   out[n++] = (char)(0x80 | (u >> 6 & 63)); out[n++] = (char)(0x80 | (u & 63)); }
            break; }
        default: out[n++] = c;
        }
    }
    out[n] = 0;
    if (j->p >= j->end) { free(out); return NULL; }
    j->p++;
    return out;
}

static Node *js_value(Json *j, const char *key, int depth)
{
    js_ws(j);
    if (j->p >= j->end || depth > 64) return NULL;
    if (*j->p == '{' || *j->p == '[') {
        bool map = *j->p == '{';
        Node *n = node(map ? N_MAP : N_LIST, key);
        j->p++;
        js_ws(j);
        if (j->p < j->end && *j->p == (map ? '}' : ']')) { j->p++; return n; }
        for (;;) {
            char *k = NULL;
            if (map) {
                js_ws(j);
                if (!(k = js_string(j))) goto bad;
                js_ws(j);
                if (j->p >= j->end || *j->p++ != ':') { free(k); goto bad; }
            }
            Node *v = js_value(j, k, depth + 1);
            free(k);
            if (!v) goto bad;
            add(n, v);
            js_ws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == (map ? '}' : ']')) { j->p++; return n; }
            goto bad;
        }
    bad:
        node_free(n);
        return NULL;
    }
    if (*j->p == '"') {
        char *s = js_string(j);
        if (!s) return NULL;
        Node *n = node(N_STR, key);
        n->str = s;
        return n;
    }
    const char *start = j->p;                       /* a number, true, false or null */
    while (j->p < j->end && !strchr(",}] \t\r\n", *j->p)) j->p++;
    if (j->p == start) return NULL;
    if (j->p - start == 4 && !strncmp(start, "null", 4)) return node(N_NULL, key);
    Node *n = node(N_STR, key);
    n->str = calloc(1, (size_t)(j->p - start) + 1);
    memcpy(n->str, start, (size_t)(j->p - start));
    return n;
}

static Node *json_parse(const char *text, size_t len)
{
    Json j = { text, text + len };
    Node *n = js_value(&j, NULL, 0);
    js_ws(&j);
    if (n && j.p != j.end) { node_free(n); return NULL; }
    return n;
}

/* YAML: the block mappings, sequences and scalars winget's manifests use
 * (plain, quoted, literal "|" and folded ">" scalars, "[]" and simple flow
 * lists), into the JSON tree's shape */
typedef struct { char *text; int indent; } YLine;
typedef struct { YLine *l; int n, i; } Yaml;

static char *y_scalar(const char *s)
{
    while (*s == ' ') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r')) n--;
    char *out = calloc(1, n + 1);
    if (n >= 2 && s[0] == '\'' && s[n - 1] == '\'') {     /* '' is a quote */
        size_t k = 0;
        for (size_t i = 1; i < n - 1; i++) { out[k++] = s[i]; if (s[i] == '\'' && s[i + 1] == '\'') i++; }
    } else if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        size_t k = 0;
        for (size_t i = 1; i < n - 1; i++) {
            if (s[i] == '\\' && i + 1 < n - 1) {
                i++;
                out[k++] = s[i] == 'n' ? '\n' : s[i] == 't' ? '\t' : s[i];
            } else out[k++] = s[i];
        }
    } else memcpy(out, s, n);
    return out;
}

/* The comment of a line, if any: '#' at its start or after a space, outside quotes */
static void y_uncomment(char *s)
{
    char q = 0;
    for (char *p = s; *p; p++) {
        if (q) { if (*p == q) q = 0; continue; }
        if ((*p == '"' || *p == '\'') && (p == s || p[-1] == ' ' || p[-1] == ':' || p[-1] == '-')) q = *p;
        else if (*p == '#' && (p == s || p[-1] == ' ')) { *p = 0; break; }
    }
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\t')) s[--n] = 0;
}

/* "key: value" -> the colon, or NULL */
static char *y_colon(char *s)
{
    char q = 0;
    for (char *p = s; *p; p++) {
        if (q) { if (*p == q) q = 0; continue; }
        if (p == s && (*p == '"' || *p == '\'')) { q = *p; continue; }
        if (*p == ':' && (!p[1] || p[1] == ' ')) return p;
    }
    return NULL;
}

static Node *y_block(Yaml *y, int indent, const char *key);

static void y_skip_blank(Yaml *y)
{
    while (y->i < y->n && !y->l[y->i].text[0]) y->i++;
}

/* A value after "key:" or "- ": inline, a block scalar or a nested block */
static Node *y_value(Yaml *y, char *rest, int indent, const char *key)
{
    while (*rest == ' ') rest++;
    if (*rest == '|' || *rest == '>') {
        bool fold = *rest == '>';
        size_t cap = 256, n = 0;
        char *out = calloc(1, cap);
        int body = -1;
        while (y->i < y->n && (y->l[y->i].indent > indent || !y->l[y->i].text[0])) {
            YLine *l = &y->l[y->i++];
            if (body < 0 && l->text[0]) body = l->indent;
            size_t len = strlen(l->text) + (size_t)(l->indent > body && body >= 0 ? l->indent - body : 0);
            if (n + len + 2 >= cap) out = realloc(out, cap = (n + len) * 2 + 2);
            if (n) out[n++] = fold && l->text[0] ? ' ' : '\n';
            for (int s = body; s >= 0 && s < l->indent; s++) out[n++] = ' ';
            strcpy(out + n, l->text);
            n += strlen(l->text);
        }
        while (n && (out[n - 1] == '\n' || out[n - 1] == ' ')) out[--n] = 0;
        Node *v = node(N_STR, key);
        v->str = out;
        return v;
    }
    if (*rest == '[' && rest[strlen(rest) - 1] == ']') {      /* [a, b] */
        Node *v = node(N_LIST, key);
        char *items = _strdup(rest + 1);
        items[strlen(items) - 1] = 0;
        for (char *ctx = NULL, *t = strtok_s(items, ",", &ctx); t; t = strtok_s(NULL, ",", &ctx)) {
            char *s = y_scalar(t);
            if (*s) { Node *e = node(N_STR, NULL); e->str = s; add(v, e); } else free(s);
        }
        free(items);
        return v;
    }
    if (!strcmp(rest, "{}")) return node(N_MAP, key);
    if (*rest) {
        Node *v = node(N_STR, key);
        v->str = y_scalar(rest);
        if (!strcmp(v->str, "null") || !strcmp(v->str, "~")) { free(v->str); v->str = NULL; v->type = N_NULL; }
        return v;
    }
    /* a nested block: deeper, or a sequence at the same indent ("key:\n- a") */
    y_skip_blank(y);
    if (y->i < y->n && (y->l[y->i].indent > indent ||
                        (y->l[y->i].indent == indent && key && !strncmp(y->l[y->i].text, "- ", 2)) ||
                        (y->l[y->i].indent == indent && key && !strcmp(y->l[y->i].text, "-"))))
        return y_block(y, y->l[y->i].indent, key);
    return node(N_NULL, key);
}

static Node *y_block(Yaml *y, int indent, const char *key)
{
    y_skip_blank(y);
    if (y->i >= y->n) return node(N_NULL, key);
    bool seq = !strncmp(y->l[y->i].text, "- ", 2) || !strcmp(y->l[y->i].text, "-");
    Node *n = node(seq ? N_LIST : N_MAP, key);
    for (y_skip_blank(y); y->i < y->n && y->l[y->i].indent == indent; y_skip_blank(y)) {
        YLine *l = &y->l[y->i];
        if (seq) {
            if (strncmp(l->text, "- ", 2) && strcmp(l->text, "-")) break;
            /* "- key: v": the item is a map whose first line sits 2 further in */
            char *rest = l->text[1] ? l->text + 2 : l->text + 1;
            while (*rest == ' ') rest++;
            if (*rest && y_colon(rest) && rest[0] != '"' && rest[0] != '\'') {
                l->indent = indent + (int)(rest - l->text);
                l->text = rest;
                add(n, y_block(y, l->indent, NULL));
            } else {
                y->i++;
                add(n, y_value(y, rest, indent, NULL));
            }
        } else {
            char *colon = y_colon(l->text);
            if (!colon) { y->i++; continue; }        /* (not a mapping line) */
            *colon = 0;
            char *k = y_scalar(l->text);
            y->i++;
            add(n, y_value(y, colon + 1, indent, k));
            free(k);
        }
    }
    return n;
}

static Node *yaml_parse(const char *text, size_t len)
{
    char *buf = calloc(1, len + 1);
    memcpy(buf, text, len);
    Yaml y = { calloc(len / 2 + 2, sizeof(YLine)), 0, 0 };
    for (char *line = buf, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (!strncmp(line, "\xEF\xBB\xBF", 3)) line += 3;           /* the BOM */
        if (!strcmp(line, "---") || !strncmp(line, "%YAML", 5)) continue;
        int ind = 0;
        while (line[ind] == ' ') ind++;
        y_uncomment(line + ind);
        y.l[y.n].text = line + ind;              /* (blank lines stay: block scalars keep them) */
        y.l[y.n].indent = ind;
        y.n++;
    }
    y_skip_blank(&y);
    Node *root = y.i < y.n ? y_block(&y, y.l[y.i].indent, NULL) : node(N_MAP, NULL);
    free(y.l);
    free(buf);
    return root;
}

/* -----------------------------------------------------------------------
 * HTTP (WinHTTP)
 * ----------------------------------------------------------------------- */
typedef struct { char *data; size_t len, cap; } Buf;

static void buf_add(Buf *b, const void *d, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, d, n);
    b->len += n;
    b->data[b->len] = 0;
}

static WCHAR *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = calloc((size_t)n + 1, sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static HINTERNET g_session;
static char g_http_error[160];

/* GET (or POST @body) @url; the body goes into @out, or into @file with
 * its SHA-256 in @sha.  Returns the HTTP status, 0 if the request failed */
static DWORD http(const char *url, const char *body, Buf *out, HANDLE file, uint8_t sha[32], bool progress)
{
    g_http_error[0] = 0;
    if (!g_session) g_session = WinHttpOpen(L"winget-cli WindowsPackageManager/" WINGET_VERSION " NovaOS",
                                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 0, 0, 0);
    WCHAR *w = wide(url), host[256], path[2048];
    URL_COMPONENTS uc = { sizeof(uc) };
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    WCHAR extra[1024];
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
    DWORD status = 0;
    HINTERNET c = NULL, r = NULL;
    if (!g_session || !WinHttpCrackUrl(w, 0, 0, &uc)) { snprintf(g_http_error, sizeof(g_http_error), "bad address"); goto out; }
    wcsncat(path, extra, 2047 - wcslen(path));
    c = WinHttpConnect(g_session, host, uc.nPort, 0);
    r = c ? WinHttpOpenRequest(c, body ? L"POST" : L"GET", path, 0, 0, 0,
                               uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    const WCHAR *hdr = body ? L"Content-Type: application/json\r\nAccept: application/json\r\nVersion: 1.4.0\r\n"
                            : L"Accept: */*\r\nVersion: 1.4.0\r\n";
    if (!r || !WinHttpSendRequest(r, hdr, (DWORD)-1, (void *)body, body ? (DWORD)strlen(body) : 0,
                                  body ? (DWORD)strlen(body) : 0, 0) || !WinHttpReceiveResponse(r, 0)) {
        snprintf(g_http_error, sizeof(g_http_error), "error %lu", GetLastError());
        goto out;
    }
    DWORD n = sizeof(status);
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 0, &status, &n, 0);
    DWORD total = 0;
    n = sizeof(total);
    WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, 0, &total, &n, 0);
    NovaHash h;
    nova_hash_init(&h, NOVA_SHA256);
    static char chunk[64 * 1024];
    UINT64 got = 0, shown = 0;
    for (;;) {
        DWORD avail = 0, read = 0;
        if (!WinHttpQueryDataAvailable(r, &avail)) { status = 0; snprintf(g_http_error, sizeof(g_http_error), "connection lost"); break; }
        if (!avail) break;
        if (avail > sizeof(chunk)) avail = sizeof(chunk);
        if (!WinHttpReadData(r, chunk, avail, &read)) { status = 0; snprintf(g_http_error, sizeof(g_http_error), "connection lost"); break; }
        if (!read) break;
        got += read;
        if (status == 200 && file) {
            DWORD wrote = 0;
            if (!WriteFile(file, chunk, read, &wrote, NULL) || wrote != read) {
                status = 0; snprintf(g_http_error, sizeof(g_http_error), "could not write the file"); break;
            }
            nova_hash_update(&h, chunk, read);
            if (progress && got - shown >= 8u * 1024 * 1024) {
                shown = got;
                if (total) printf("  %.1f MB / %.1f MB\n", got / 1048576.0, total / 1048576.0);
                else printf("  %.1f MB\n", got / 1048576.0);
            }
        } else if (out && got <= 16u * 1024 * 1024) buf_add(out, chunk, read);
    }
    if (status == 200 && file && sha) nova_hash_final(&h, sha);
    if (progress && status == 200 && file) printf("  %.1f MB downloaded\n", got / 1048576.0);
out:
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    free(w);
    return status;
}

static Node *fetch_doc(const char *url, const char *body, bool yaml, DWORD *status)
{
    Buf b = {0};
    *status = http(url, body, &b, NULL, NULL, false);
    Node *n = NULL;
    if (*status == 200 && b.data) n = yaml ? yaml_parse(b.data, b.len) : json_parse(b.data, b.len);
    free(b.data);
    return n;
}

/* -----------------------------------------------------------------------
 * Registry helpers
 * ----------------------------------------------------------------------- */
static bool reg_get(HKEY root, const char *key, const char *name, char *out, DWORD cap)
{
    out[0] = 0;
    DWORD type = 0, n = cap;
    if (RegGetValueA(root, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, out, &n) != ERROR_SUCCESS) {
        out[0] = 0;
        return false;
    }
    return true;
}

static void reg_set(HKEY k, const char *name, const char *value)
{
    RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)value, (DWORD)strlen(value) + 1);
}

/* -----------------------------------------------------------------------
 * Sources
 * ----------------------------------------------------------------------- */
enum { SRC_STORE, SRC_COMMUNITY, SRC_REST };
typedef struct { char name[64], arg[512], type[40]; int kind; } Source;
static Source g_src[16];
static int g_nsrc;

static bool builtin_removed(const char *name)
{
    char key[128], v[8];
    snprintf(key, sizeof(key), KEY_SOURCES "\\%s", name);
    return reg_get(HKEY_LOCAL_MACHINE, key, "Removed", v, sizeof(v)) && v[0] == '1';
}

static void load_sources(void)
{
    g_nsrc = 0;
    if (!builtin_removed("novaos")) {
        Source *s = &g_src[g_nsrc++];
        strcpy(s->name, "novaos"); strcpy(s->type, "NovaOS.AppStore"); s->kind = SRC_STORE;
        scopy(s->arg, sizeof(s->arg), "C:" STORE_CATALOG_PATH);
    }
    if (!builtin_removed("winget")) {
        Source *s = &g_src[g_nsrc++];
        strcpy(s->name, "winget"); strcpy(s->type, "Microsoft.Community.GitHub"); s->kind = SRC_COMMUNITY;
        scopy(s->arg, sizeof(s->arg), "https://github.com/microsoft/winget-pkgs");
    }
    HKEY k;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, KEY_SOURCES, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    char name[64];
    for (DWORD i = 0; g_nsrc < 16; i++) {
        DWORD n = sizeof(name);
        if (RegEnumKeyExA(k, i, name, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        char key[160];
        snprintf(key, sizeof(key), KEY_SOURCES "\\%s", name);
        Source *s = &g_src[g_nsrc];
        if (!reg_get(HKEY_LOCAL_MACHINE, key, "Arg", s->arg, sizeof(s->arg))) continue;
        reg_get(HKEY_LOCAL_MACHINE, key, "Type", s->type, sizeof(s->type));
        scopy(s->name, sizeof(s->name), name);
        s->kind = SRC_REST;
        g_nsrc++;
    }
    RegCloseKey(k);
}

static Source *find_source(const char *name)
{
    for (int i = 0; i < g_nsrc; i++) if (ieq(g_src[i].name, name)) return &g_src[i];
    return NULL;
}

/* -----------------------------------------------------------------------
 * Packages
 * ----------------------------------------------------------------------- */
typedef struct {
    char arch[16], type[16], nested[16], url[1024], sha[72], scope[16];
    char silent[256], progress[256], custom[256], product[160], file[260];
    char nested_files[4][260], alias[64];
    int nnested;
} Installer;

typedef struct {
    char id[130], name[130], publisher[130], version[64], moniker[64], source[64];
    char desc[600], homepage[300], license[130];
    char match[40];                /* search: what matched */
    int store;                     /* the catalog's index (novaos), or -1 */
    Installer inst[12];
    int ninst;
} Package;

static StoreCatalog *g_cat;

static void load_catalog(void)
{
    if (g_cat) return;
    const char *paths[] = { "C:" STORE_CATALOG_PATH, "C:" STORE_DEFAULT_PATH };
    for (int i = 0; i < 2 && !g_cat; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (!f) continue;
        static char text[STORE_JSON_MAX + 1];
        size_t n = fread(text, 1, STORE_JSON_MAX + 1, f);
        fclose(f);
        if (n <= STORE_JSON_MAX) g_cat = store_catalog_parse(text, (UINT32)n);
    }
    if (!g_cat) g_cat = calloc(1, sizeof(StoreCatalog));
}

/* A catalog entry's identifier: its own, or Store.<name> */
static void store_id(const StoreApp *a, char *out, size_t cap)
{
    if (a->id) { scopy(out, cap, a->id); return; }
    size_t n = (size_t)snprintf(out, cap, "Store.");
    for (const char *p = a->name; *p && n + 1 < cap; p++)
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("+-_", *p))
            out[n++] = *p;
    out[n] = 0;
}

static void from_store(Package *p, int i)
{
    const StoreApp *a = &g_cat->apps[i];
    memset(p, 0, sizeof(*p));
    p->store = i;
    store_id(a, p->id, sizeof(p->id));
    scopy(p->name, sizeof(p->name), a->name);
    scopy(p->publisher, sizeof(p->publisher), a->publisher);
    scopy(p->version, sizeof(p->version), a->version ? a->version : "Unknown");
    scopy(p->desc, sizeof(p->desc), a->summary);
    scopy(p->source, sizeof(p->source), "novaos");
    snprintf(p->license, sizeof(p->license), "%s", a->note);
    Installer *in = &p->inst[p->ninst++];
    scopy(in->url, sizeof(in->url), a->url);
    scopy(in->file, sizeof(in->file), a->file);
    strcpy(in->arch, "x64");
    strcpy(in->type, a->kind == KIND_ARCHIVE ? "archive" : a->kind == KIND_PORTABLE ? "portable" : "setup");
}

/* A manifest's installers: @root's InstallerType, InstallerSwitches...
 * are the defaults each entry of its Installers list may override */
static void read_switches(Installer *in, const Node *sw)
{
    if (!sw) return;
    if (str(sw, "Silent")) scopy(in->silent, sizeof(in->silent), str(sw, "Silent"));
    if (str(sw, "SilentWithProgress")) scopy(in->progress, sizeof(in->progress), str(sw, "SilentWithProgress"));
    if (str(sw, "Custom")) scopy(in->custom, sizeof(in->custom), str(sw, "Custom"));
}

static void read_installer(Installer *in, const Node *m)
{
    if (str(m, "Architecture")) scopy(in->arch, sizeof(in->arch), str(m, "Architecture"));
    if (str(m, "InstallerType")) scopy(in->type, sizeof(in->type), str(m, "InstallerType"));
    if (str(m, "NestedInstallerType")) scopy(in->nested, sizeof(in->nested), str(m, "NestedInstallerType"));
    if (str(m, "InstallerUrl")) scopy(in->url, sizeof(in->url), str(m, "InstallerUrl"));
    if (str(m, "InstallerSha256")) scopy(in->sha, sizeof(in->sha), str(m, "InstallerSha256"));
    if (str(m, "Scope")) scopy(in->scope, sizeof(in->scope), str(m, "Scope"));
    if (str(m, "ProductCode")) scopy(in->product, sizeof(in->product), str(m, "ProductCode"));
    read_switches(in, get(m, "InstallerSwitches"));
    Node *nf = get(m, "NestedInstallerFiles");
    if (nf && nf->type == N_LIST) {
        in->nnested = 0;
        for (Node *f = nf->kids; f && in->nnested < 4; f = f->next) {
            scopy(in->nested_files[in->nnested], 260, str(f, "RelativeFilePath"));
            if (!in->nnested && str(f, "PortableCommandAlias"))
                scopy(in->alias, sizeof(in->alias), str(f, "PortableCommandAlias"));
            in->nnested++;
        }
    }
    Node *cmds = get(m, "Commands");
    if (cmds && cmds->type == N_LIST && cmds->kids && cmds->kids->str && !in->alias[0])
        scopy(in->alias, sizeof(in->alias), cmds->kids->str);
}

/* @locale: PackageName, Publisher...; @inst: the installer document (or
 * a REST version, or a singleton manifest: all three are the same keys) */
static void from_manifest(Package *p, const Node *version, const Node *locale, const Node *inst, const char *source)
{
    memset(p, 0, sizeof(*p));
    p->store = -1;
    const Node *docs[] = { version, locale, inst };
    for (int d = 0; d < 3; d++) {
        if (!docs[d]) continue;
        if (str(docs[d], "PackageIdentifier")) scopy(p->id, sizeof(p->id), str(docs[d], "PackageIdentifier"));
        if (str(docs[d], "PackageVersion")) scopy(p->version, sizeof(p->version), str(docs[d], "PackageVersion"));
    }
    if (locale) {
        scopy(p->name, sizeof(p->name), str(locale, "PackageName"));
        scopy(p->publisher, sizeof(p->publisher), str(locale, "Publisher"));
        scopy(p->moniker, sizeof(p->moniker), str(locale, "Moniker"));
        scopy(p->desc, sizeof(p->desc), str(locale, "ShortDescription"));
        scopy(p->homepage, sizeof(p->homepage), str(locale, "PackageUrl"));
        scopy(p->license, sizeof(p->license), str(locale, "License"));
    }
    if (!p->name[0]) scopy(p->name, sizeof(p->name), p->id);
    scopy(p->source, sizeof(p->source), source);
    Installer def = {0};
    if (inst) read_installer(&def, inst);
    Node *list = inst ? get(inst, "Installers") : NULL;
    for (Node *e = list && list->type == N_LIST ? list->kids : NULL; e && p->ninst < 12; e = e->next) {
        Installer *in = &p->inst[p->ninst++];
        *in = def;
        read_installer(in, e);
    }
}

/* The installer for this machine: x64, else neutral, else x86 (WoW64);
 * a type NovaOS installs */
static const char *g_skip_reason;
static Installer *pick_installer(Package *p)
{
    static const char *order[] = { "x64", "neutral", "x86" };
    g_skip_reason = NULL;
    for (int o = 0; o < 3; o++)
        for (int i = 0; i < p->ninst; i++) {
            Installer *in = &p->inst[i];
            if (!ieq(in->arch, order[o])) continue;
            const char *t = ieq(in->type, "zip") ? in->nested : in->type;
            if (ieq(t, "msix") || ieq(t, "appx") || ieq(t, "msstore")) { g_skip_reason = "MSIX and Store packages need Windows' package deployment"; continue; }
            if (ieq(in->type, "zip") && !t[0]) continue;
            return in;
        }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Versions: numbers compared as numbers, the rest as text
 * ----------------------------------------------------------------------- */
static int vcmp(const char *a, const char *b)
{
    while (*a || *b) {
        if (*a == 'v' || *a == 'V') a++;
        if (*b == 'v' || *b == 'V') b++;
        unsigned long long x = 0, y = 0;
        bool dx = *a >= '0' && *a <= '9', dy = *b >= '0' && *b <= '9';
        while (*a >= '0' && *a <= '9') x = x * 10 + (unsigned)(*a++ - '0');
        while (*b >= '0' && *b <= '9') y = y * 10 + (unsigned)(*b++ - '0');
        if (x != y) return x < y ? -1 : 1;
        if (dx != dy) return dx ? 1 : -1;
        /* the rest of this part: none sorts after a suffix (1.0 > 1.0-beta) */
        const char *ea = a, *eb = b;
        while (*ea && *ea != '.') ea++;
        while (*eb && *eb != '.') eb++;
        size_t la = (size_t)(ea - a), lb = (size_t)(eb - b);
        if (la || lb) {
            if (!la) return 1;
            if (!lb) return -1;
            int c = _strnicmp(a, b, la < lb ? la : lb);
            if (c) return c < 0 ? -1 : 1;
            if (la != lb) return la < lb ? -1 : 1;
        }
        a = ea; b = eb;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Looking packages up in the sources
 * ----------------------------------------------------------------------- */
typedef struct {
    const char *query, *id, *name, *moniker, *version;
    bool exact;
} Filter;

static bool text_match(const char *have, const char *want, bool exact)
{
    if (!have || !have[0]) return false;
    return exact ? ieq(have, want) : istr(have, want) != NULL;
}

/* What of @p @f matches ("" when nothing does) */
static const char *matches(const Package *p, const Filter *f, bool exact)
{
    if (f->id && !text_match(p->id, f->id, exact)) return "";
    if (f->name && !text_match(p->name, f->name, exact)) return "";
    if (f->moniker && !text_match(p->moniker, f->moniker, exact)) return "";
    if (!f->query) return "*";
    if (text_match(p->id, f->query, exact)) return "Id";
    if (text_match(p->name, f->query, exact)) return "Name";
    if (text_match(p->moniker, f->query, exact)) return "Moniker";
    return "";
}

static Package *g_found;
static int g_nfound, g_capfound;

static void found(const Package *p, const char *m)
{
    if (g_nfound == g_capfound) g_found = realloc(g_found, (size_t)(g_capfound = g_capfound * 2 + 8) * sizeof(Package));
    g_found[g_nfound] = *p;
    if (m && strcmp(m, "*")) snprintf(g_found[g_nfound].match, sizeof(g_found[0].match), "%s", m);
    g_nfound++;
}

static void search_store(const Filter *f, bool exact)
{
    load_catalog();
    for (int i = 0; i < g_cat->count; i++) {
        Package p;
        from_store(&p, i);
        const char *m = matches(&p, f, exact);
        if (*m) found(&p, m);
    }
}

/* The community repository's folder for @id: manifests/<letter>/<A>/<B>... */
static void community_path(const char *id, char *out, size_t cap)
{
    size_t n = (size_t)snprintf(out, cap, "manifests/%c/", (char)(id[0] >= 'A' && id[0] <= 'Z' ? id[0] + 32 : id[0]));
    for (const char *p = id; *p && n + 1 < cap; p++) out[n++] = *p == '.' ? '/' : *p;
    out[n] = 0;
}

static bool looks_like_id(const char *s)
{
    return s && strchr(s, '.') && !strchr(s, ' ') && s[0] != '.';
}

static bool g_source_failed;

static void source_failed(const Source *s, const char *why)
{
    fprintf(stderr, "Failed when searching source: %s (%s)\n", s->name, why);
    g_source_failed = true;
}

/* @id's manifests from the community repository (@version, or the latest) */
static bool community_get(const Source *s, const char *id, const char *version, Package *out)
{
    char path[400], url[1200], ver[64] = "";
    community_path(id, path, sizeof(path));
    DWORD st = 0;
    if (version) scopy(ver, sizeof(ver), version);
    else {
        snprintf(url, sizeof(url), COMMUNITY_API "%s", path);
        Node *list = fetch_doc(url, NULL, false, &st);
        if (!list) {
            if (st && st != 404) { char why[48]; snprintf(why, sizeof(why), "server replied %lu", st); source_failed(s, why); }
            else if (!st) source_failed(s, g_http_error[0] ? g_http_error : "no connection");
            return false;
        }
        for (Node *e = list->type == N_LIST ? list->kids : NULL; e; e = e->next) {
            const char *name = str(e, "name"), *type = str(e, "type");
            if (!name || !type || strcmp(type, "dir")) continue;
            if (!((name[0] >= '0' && name[0] <= '9') || ((name[0] == 'v' || name[0] == 'V') && name[1] >= '0' && name[1] <= '9')))
                continue;                               /* (a sub-package's folder) */
            if (!ver[0] || vcmp(name, ver) > 0) scopy(ver, sizeof(ver), name);
        }
        node_free(list);
        if (!ver[0]) return false;
    }
    snprintf(url, sizeof(url), COMMUNITY_RAW "%s/%s/%s.yaml", path, ver, id);
    Node *vdoc = fetch_doc(url, NULL, true, &st);
    if (!vdoc) {
        if (st != 404) source_failed(s, g_http_error[0] ? g_http_error : "manifest download failed");
        return false;
    }
    Node *loc = NULL, *inst = NULL;
    const char *mtype = str(vdoc, "ManifestType");
    if (mtype && !strcmp(mtype, "singleton")) from_manifest(out, vdoc, vdoc, vdoc, s->name);
    else {
        const char *dl = str(vdoc, "DefaultLocale");
        snprintf(url, sizeof(url), COMMUNITY_RAW "%s/%s/%s.locale.%s.yaml", path, ver, id, dl ? dl : "en-US");
        loc = fetch_doc(url, NULL, true, &st);
        snprintf(url, sizeof(url), COMMUNITY_RAW "%s/%s/%s.installer.yaml", path, ver, id);
        inst = fetch_doc(url, NULL, true, &st);
        if (!inst) { source_failed(s, "no installer manifest"); node_free(vdoc); node_free(loc); return false; }
        from_manifest(out, vdoc, loc, inst, s->name);
    }
    node_free(vdoc); node_free(loc); node_free(inst);
    return out->id[0] != 0;
}

static void search_community(const Source *s, const Filter *f)
{
    const char *id = f->id ? f->id : f->query;
    if (!looks_like_id(id)) return;        /* (its full-text index is a SQLite file NovaOS does not read) */
    Package p;
    if (community_get(s, id, f->version, &p) && *matches(&p, f, false)) found(&p, f->id ? "Id" : matches(&p, f, false));
}

/* REST: GET packageManifests/<id> -> one package with every version */
static bool rest_get(const Source *s, const char *id, const char *version, Package *out)
{
    char url[1200];
    snprintf(url, sizeof(url), "%s/packageManifests/%s", s->arg, id);
    DWORD st = 0;
    Node *doc = fetch_doc(url, NULL, false, &st);
    if (!doc) {
        if (st != 404 && st != 204) source_failed(s, g_http_error[0] ? g_http_error : "no package manifest");
        return false;
    }
    Node *data = get(doc, "Data"), *best = NULL;
    Node *versions = get(data, "Versions");
    for (Node *v = versions && versions->type == N_LIST ? versions->kids : NULL; v; v = v->next) {
        const char *pv = str(v, "PackageVersion");
        if (!pv) continue;
        if (version ? ieq(pv, version) : (!best || vcmp(pv, str(best, "PackageVersion")) > 0)) best = v;
    }
    if (best) {
        from_manifest(out, best, get(best, "DefaultLocale"), best, s->name);
        if (!out->id[0]) scopy(out->id, sizeof(out->id), str(data, "PackageIdentifier"));
    }
    node_free(doc);
    return best != NULL;
}

static void rest_search(const Source *s, const Filter *f, bool exact)
{
    char body[1200], url[700];
    const char *field = f->id ? "PackageIdentifier" : f->name ? "PackageName" : f->moniker ? "Moniker" : NULL;
    const char *word = f->id ? f->id : f->name ? f->name : f->moniker ? f->moniker : f->query;
    const char *mt = exact ? "Exact" : "Substring";
    if (!word) snprintf(body, sizeof(body), "{\"MaximumResults\":100}");
    else if (field) snprintf(body, sizeof(body), "{\"MaximumResults\":100,\"Filters\":[{\"PackageMatchField\":\"%s\","
                             "\"RequestMatch\":{\"KeyWord\":\"%s\",\"MatchType\":\"%s\"}}]}", field, word, mt);
    else snprintf(body, sizeof(body), "{\"MaximumResults\":100,\"Query\":{\"KeyWord\":\"%s\",\"MatchType\":\"%s\"}}", word, mt);
    snprintf(url, sizeof(url), "%s/manifestSearch", s->arg);
    DWORD st = 0;
    Node *doc = fetch_doc(url, body, false, &st);
    if (!doc) {
        if (st != 204) source_failed(s, g_http_error[0] ? g_http_error : "search failed");
        return;
    }
    Node *data = get(doc, "Data");
    for (Node *e = data && data->type == N_LIST ? data->kids : NULL; e; e = e->next) {
        Package p;
        memset(&p, 0, sizeof(p));
        p.store = -1;
        scopy(p.id, sizeof(p.id), str(e, "PackageIdentifier"));
        scopy(p.name, sizeof(p.name), str(e, "PackageName"));
        scopy(p.publisher, sizeof(p.publisher), str(e, "Publisher"));
        scopy(p.source, sizeof(p.source), s->name);
        Node *vs = get(e, "Versions");
        for (Node *v = vs && vs->type == N_LIST ? vs->kids : NULL; v; v = v->next) {
            const char *pv = str(v, "PackageVersion");
            if (pv && (!p.version[0] || vcmp(pv, p.version) > 0)) scopy(p.version, sizeof(p.version), pv);
        }
        if (!p.id[0]) continue;
        const char *m = matches(&p, f, exact);
        if (*m) found(&p, m);
        else if (!f->id && !f->name) found(&p, "Moniker");   /* (the source matched it on something else) */
    }
    node_free(doc);
}

/* Every package matching @f in @source (or all sources) */
static void search(const Filter *f, const char *source, bool exact)
{
    g_nfound = 0;
    for (int i = 0; i < g_nsrc; i++) {
        Source *s = &g_src[i];
        if (source && !ieq(source, s->name)) continue;
        if (s->kind == SRC_STORE) search_store(f, exact);
        else if (s->kind == SRC_COMMUNITY) search_community(s, f);
        else rest_search(s, f, exact);
    }
}

/* The full package (its installers) for a search result */
static bool resolve(Package *p, const char *version)
{
    if (p->store >= 0) {
        if (version && !ieq(version, p->version)) return false;
        return true;
    }
    Source *s = find_source(p->source);
    if (!s) return false;
    Package full;
    bool ok = s->kind == SRC_COMMUNITY ? community_get(s, p->id, version, &full) : rest_get(s, p->id, version, &full);
    if (ok) *p = full;
    return ok;
}

/* Exactly one package for install/show/uninstall: an exact identifier, name
 * or moniker first, then a part of one; 0 found, 1 picked, 2 too many */
static int pick(const Filter *f, const char *source, Package *out)
{
    search(f, source, true);
    if (g_nfound > 1) {                         /* the same package in two sources: the App Store's tested one */
        int store = -1, others = 0;
        for (int i = 0; i < g_nfound; i++) {
            if (g_found[i].store >= 0) store = i;
            if (!ieq(g_found[i].id, g_found[0].id)) others++;
        }
        if (!others && store >= 0) { *out = g_found[store]; return 1; }
    }
    if (!g_nfound && !f->exact) search(f, source, false);
    if (g_nfound == 1) { *out = g_found[0]; return 1; }
    return g_nfound ? 2 : 0;
}

/* -----------------------------------------------------------------------
 * Tables
 * ----------------------------------------------------------------------- */
typedef struct { const char *head; int width; } Col;

static int utf8_len(const char *s) { int n = 0; for (; *s; s++) if ((*s & 0xC0) != 0x80) n++; return n; }

static void cell(const char *s, int width, bool last)
{
    s = s ? s : "";
    int n = utf8_len(s);
    if (n > width && !last) {
        int k = 0, chars = 0;
        while (s[k] && chars < width - 1) { k++; while ((s[k] & 0xC0) == 0x80) k++; chars++; }
        printf("%.*s\xE2\x80\xA6 ", k, s);
        return;
    }
    printf("%s", s);
    if (!last) for (int i = n; i <= width; i++) putchar(' ');
}

static void table(Col *cols, int ncols, const char *(*row)(int r, int c), int nrows)
{
    for (int c = 0; c < ncols; c++) {             /* each column as wide as its longest text, within reason */
        int w = utf8_len(cols[c].head);
        for (int r = 0; r < nrows; r++) { int n = utf8_len(row(r, c) ? row(r, c) : ""); if (n > w) w = n; }
        cols[c].width = w > cols[c].width && cols[c].width ? cols[c].width : w;
    }
    int total = 0;
    for (int c = 0; c < ncols; c++) { cell(cols[c].head, cols[c].width, c == ncols - 1); total += cols[c].width + 1; }
    printf("\n");
    for (int i = 0; i < total; i++) putchar('-');
    printf("\n");
    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) cell(row(r, c), cols[c].width, c == ncols - 1);
        printf("\n");
    }
}

/* -----------------------------------------------------------------------
 * Files and processes
 * ----------------------------------------------------------------------- */
static bool exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }
static bool is_dir(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void make_dirs(const char *path)
{
    char p[MAX_PATH];
    scopy(p, sizeof(p), path);
    for (char *s = p + 3; *s; s++)
        if (*s == '\\') { *s = 0; CreateDirectoryA(p, NULL); *s = '\\'; }
    CreateDirectoryA(p, NULL);
}

static bool delete_tree(const char *path)
{
    if (!is_dir(path)) return DeleteFileA(path) || !exists(path);
    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            char sub[MAX_PATH];
            snprintf(sub, sizeof(sub), "%s\\%s", path, fd.cFileName);
            delete_tree(sub);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryA(path) || !exists(path);
}

/* Run @cmd (in @dir) and wait for it; its exit code, or -1 if it did not start */
static long run(const char *app, const char *cmd, const char *dir)
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    char line[4096];
    scopy(line, sizeof(line), cmd);
    if (!CreateProcessA(app, line, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (long)code;
}

static bool sha256_file(const char *path, uint8_t out[32])
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    NovaHash h;
    nova_hash_init(&h, NOVA_SHA256);
    static char chunk[64 * 1024];
    DWORD n;
    while (ReadFile(f, chunk, sizeof(chunk), &n, NULL) && n) nova_hash_update(&h, chunk, n);
    CloseHandle(f);
    nova_hash_final(&h, out);
    return true;
}

static void hex(const uint8_t *d, int n, char *out)
{
    for (int i = 0; i < n; i++) sprintf(out + 2 * i, "%02X", d[i]);
}

/* -----------------------------------------------------------------------
 * What winget installed: HKLM\SOFTWARE\NovaOS\WinGet\Packages\<id>
 * ----------------------------------------------------------------------- */
typedef struct {
    char id[130], name[130], publisher[130], version[64], source[64], type[16];
    char location[MAX_PATH], arp[300], command[64];
} Record;

static bool record_read(const char *id, Record *r)
{
    char key[200];
    snprintf(key, sizeof(key), KEY_PACKAGES "\\%s", id);
    memset(r, 0, sizeof(*r));
    if (!reg_get(HKEY_LOCAL_MACHINE, key, "Version", r->version, sizeof(r->version))) return false;
    scopy(r->id, sizeof(r->id), id);
    reg_get(HKEY_LOCAL_MACHINE, key, "Name", r->name, sizeof(r->name));
    reg_get(HKEY_LOCAL_MACHINE, key, "Publisher", r->publisher, sizeof(r->publisher));
    reg_get(HKEY_LOCAL_MACHINE, key, "Source", r->source, sizeof(r->source));
    reg_get(HKEY_LOCAL_MACHINE, key, "InstallerType", r->type, sizeof(r->type));
    reg_get(HKEY_LOCAL_MACHINE, key, "InstallLocation", r->location, sizeof(r->location));
    reg_get(HKEY_LOCAL_MACHINE, key, "UninstallKey", r->arp, sizeof(r->arp));
    reg_get(HKEY_LOCAL_MACHINE, key, "Command", r->command, sizeof(r->command));
    return true;
}

static void record_write(const Record *r)
{
    char key[200];
    snprintf(key, sizeof(key), KEY_PACKAGES "\\%s", r->id);
    HKEY k;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) != ERROR_SUCCESS) return;
    reg_set(k, "Name", r->name);
    reg_set(k, "Publisher", r->publisher);
    reg_set(k, "Version", r->version);
    reg_set(k, "Source", r->source);
    reg_set(k, "InstallerType", r->type);
    reg_set(k, "InstallLocation", r->location);
    reg_set(k, "UninstallKey", r->arp);
    reg_set(k, "Command", r->command);
    RegCloseKey(k);
}

static void record_delete(const char *id)
{
    char key[200];
    snprintf(key, sizeof(key), KEY_PACKAGES "\\%s", id);
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, key);
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, key);
}

static int records(Record **out)
{
    HKEY k;
    int n = 0, cap = 0;
    *out = NULL;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, KEY_PACKAGES, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    char id[130];
    for (DWORD i = 0;; i++) {
        DWORD len = sizeof(id);
        if (RegEnumKeyExA(k, i, id, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        if (n == cap) *out = realloc(*out, (size_t)(cap = cap * 2 + 8) * sizeof(Record));
        if (record_read(id, &(*out)[n])) n++;
    }
    RegCloseKey(k);
    return n;
}

/* -----------------------------------------------------------------------
 * Installed programs Windows lists (Uninstall keys)
 * ----------------------------------------------------------------------- */
typedef struct { char key[300], name[200], version[64], publisher[130], uninstall[600], quiet[600]; } Arp;
static const struct { HKEY root; const char *path, *label; } g_arp_roots[] = {
    { HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", "Machine\\X64" },
    { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", "Machine\\X86" },
    { HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", "User\\X64" },
};

static int arp_list(Arp **out)
{
    int n = 0, cap = 0;
    *out = NULL;
    for (int r = 0; r < 3; r++) {
        HKEY k;
        if (RegOpenKeyExA(g_arp_roots[r].root, g_arp_roots[r].path, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
        char sub[200];
        for (DWORD i = 0;; i++) {
            DWORD len = sizeof(sub);
            if (RegEnumKeyExA(k, i, sub, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            if (n == cap) *out = realloc(*out, (size_t)(cap = cap * 2 + 16) * sizeof(Arp));
            Arp *a = &(*out)[n];
            memset(a, 0, sizeof(*a));
            char path[400];
            snprintf(path, sizeof(path), "%s\\%s", g_arp_roots[r].path, sub);
            if (!reg_get(g_arp_roots[r].root, path, "DisplayName", a->name, sizeof(a->name))) continue;
            reg_get(g_arp_roots[r].root, path, "DisplayVersion", a->version, sizeof(a->version));
            reg_get(g_arp_roots[r].root, path, "Publisher", a->publisher, sizeof(a->publisher));
            reg_get(g_arp_roots[r].root, path, "UninstallString", a->uninstall, sizeof(a->uninstall));
            reg_get(g_arp_roots[r].root, path, "QuietUninstallString", a->quiet, sizeof(a->quiet));
            snprintf(a->key, sizeof(a->key), "ARP\\%s\\%s", g_arp_roots[r].label, sub);
            n++;
        }
        RegCloseKey(k);
    }
    return n;
}

static bool arp_find(const Arp *list, int n, const char *key)
{
    for (int i = 0; i < n; i++) if (ieq(list[i].key, key)) return true;
    return false;
}

/* -----------------------------------------------------------------------
 * The App Store's install paths (kernel/apps/store.c), in user mode
 * ----------------------------------------------------------------------- */
/* C:\Programs\<exe> with '*' and '**' components (store.c's walk) */
static bool walk(const char *base, const char *p, char *found_path, int depth)
{
    while (*p == '\\') p++;
    if (!*p) { scopy(found_path, MAX_PATH, base); return exists(base); }
    char comp[MAX_PATH];
    int len = 0;
    while (p[len] && p[len] != '\\' && len < MAX_PATH - 1) { comp[len] = p[len]; len++; }
    comp[len] = 0;
    const char *rest = p + len;
    char sub[MAX_PATH];
    if (!strcmp(comp, "**") || (len && comp[len - 1] == '*')) {
        if (!strcmp(comp, "**") && walk(base, rest, found_path, depth)) return true;
        if (depth >= 6) return false;
        char pat[MAX_PATH];
        snprintf(pat, sizeof(pat), "%s\\*", base);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) return false;
        bool ok = false;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
            if (strcmp(comp, "**") && _strnicmp(fd.cFileName, comp, (size_t)len - 1)) continue;
            snprintf(sub, sizeof(sub), "%s\\%s", base, fd.cFileName);
            ok = walk(sub, !strcmp(comp, "**") ? p : rest, found_path, depth + 1);
        } while (!ok && FindNextFileA(h, &fd));
        FindClose(h);
        return ok;
    }
    snprintf(sub, sizeof(sub), "%s\\%s", base, comp);
    return walk(sub, rest, found_path, depth);
}

static bool store_installed(const StoreApp *a, char *exe)
{
    char tmp[MAX_PATH];
    if (!exe) exe = tmp;
    if (a->exe) return walk(a->exe[0] == '\\' ? "C:" : "C:\\Programs", a->exe, exe, 0) && !is_dir(exe);
    if (a->kind == KIND_PORTABLE) {
        snprintf(exe, MAX_PATH, "C:\\Downloads\\%s", a->file);
        return exists(exe);
    }
    return false;
}

static bool ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), e = strlen(ext);
    return n >= e && !_stricmp(s + n - e, ext);
}

/* 7-Zip unpacks @file into @dest (with @files only, if given) */
static long unzip(const char *file, const char *dest, const char *files)
{
    if (!exists(SEVEN_ZIP)) {
        printf("7-Zip is needed to unpack this package: winget install 7zip.7zip -s novaos\n");
        return -2;
    }
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "7z x \"%s\" \"-o%s\" -y%s%s", file, dest, files ? " " : "", files ? files : "");
    return run(SEVEN_ZIP, cmd, NULL);
}

/* An archive of system files unpacked: into System32 and SysWOW64 (store.c's move_system_files) */
static void move_system_files(const StoreApp *a)
{
    char list[512];
    scopy(list, sizeof(list), a->system);
    for (char *f = list, *next; f && *f; f = next) {
        next = strchr(f, ' ');
        if (next) *next++ = 0;
        char *as = strchr(f, '>');
        if (as) *as++ = 0;
        char path[MAX_PATH], to[MAX_PATH];
        snprintf(path, sizeof(path), "C:\\Programs\\%s\\%s", a->dest, f);
        const char *leaf = strrchr(f, '\\');
        bool x86 = !strncmp(f, "x86\\", 4) || !strncmp(f, "x32\\", 4) || strstr(f, "\\x86\\") || strstr(f, "\\x32\\");
        const char *sys = x86 ? "C:\\Windows\\SysWOW64" : "C:\\Windows\\System32";
        if (!leaf || is_dir(path)) continue;
        const char *name = as ? as : leaf + 1;
        snprintf(to, sizeof(to), "%s\\%s", sys, name);
        if (!MoveFileExA(path, to, MOVEFILE_REPLACE_EXISTING)) continue;
        if (strstr(name, "_icd.") && ends_with(name, ".json")) {
            HKEY k;
            if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, x86 ? "SOFTWARE\\WOW6432Node\\Khronos\\Vulkan\\Drivers"
                                                        : "SOFTWARE\\Khronos\\Vulkan\\Drivers",
                                0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) == ERROR_SUCCESS) {
                DWORD zero = 0;
                RegSetValueExA(k, to, 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));
                RegCloseKey(k);
            }
        }
    }
    char top[MAX_PATH];
    snprintf(top, sizeof(top), "C:\\Programs\\%s", a->dest);
    delete_tree(top);                              /* (what is left: the emptied folders) */
}

/* -----------------------------------------------------------------------
 * Installing
 * ----------------------------------------------------------------------- */
static bool g_interactive, g_silent;

static int download(const Installer *in, const char *path, bool check)
{
    printf("Downloading %s\n", in->url);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { printf("Could not create %s\n", path); return (int)E_DOWNLOAD_FAILED; }
    uint8_t sha[32];
    DWORD st = http(in->url, NULL, NULL, f, sha, true);
    CloseHandle(f);
    if (st != 200) {
        if (st) printf("Download failed: the server replied %lu\n", st);
        else printf("Download failed: %s\n", g_http_error[0] ? g_http_error : "no connection");
        DeleteFileA(path);
        return (int)E_DOWNLOAD_FAILED;
    }
    if (check) {
        char got[65];
        hex(sha, 32, got);
        if (_stricmp(got, in->sha)) {
            printf("Installer hash does not match; this cannot be overridden when running as admin\n"
                   "  Expected: %s\n  Actual:   %s\n", in->sha, got);
            DeleteFileA(path);
            return (int)E_HASH_MISMATCH;
        }
        printf("Successfully verified installer hash\n");
    }
    return 0;
}

/* An App Store program, as the Store installs it */
static int install_store(Package *p, Record *r)
{
    const StoreApp *a = &g_cat->apps[p->store];
    char file[MAX_PATH], exe[MAX_PATH];
    snprintf(file, sizeof(file), "C:\\Downloads\\%s", a->file);
    CreateDirectoryA("C:\\Downloads", NULL);
    if (!exists(file)) {
        int e = download(&p->inst[0], file, false);
        if (e) return e;
    } else printf("Using %s (already downloaded)\n", file);
    printf("Starting package install...\n");
    long code = 0;
    if (a->kind == KIND_ARCHIVE) {
        char dest[MAX_PATH];
        snprintf(dest, sizeof(dest), "C:\\Programs\\%s", a->dest);
        if (ends_with(file, ".tar.gz") || ends_with(file, ".tgz") || ends_with(file, ".tar.zst") || ends_with(file, ".tzst")) {
            code = unzip(file, "C:\\Downloads", NULL);          /* 7-Zip takes one layer at a time */
            char tar[MAX_PATH];
            scopy(tar, sizeof(tar), file);
            size_t n = strlen(tar);
            if (ends_with(tar, ".tgz")) strcpy(tar + n - 2, "ar");
            else if (ends_with(tar, ".tzst")) strcpy(tar + n - 4, "ar");
            else if (ends_with(tar, ".zst")) tar[n - 4] = 0;
            else tar[n - 3] = 0;
            if (code == 0) code = unzip(tar, dest, NULL);
            DeleteFileA(tar);
        } else {
            char files[512] = "";
            size_t k = 0;
            for (const char *c = a->system; c && *c && k < sizeof(files) - 1; ) {   /* the files, less any ">name" */
                if (*c == '>') { while (*c && *c != ' ') c++; continue; }
                files[k++] = *c++;
            }
            files[k] = 0;
            code = unzip(file, dest, files[0] ? files : NULL);
        }
        if (code == -2) return (int)E_INSTALL_FAILED;
        if (a->system && code == 0) move_system_files(a);
        scopy(r->location, sizeof(r->location), a->system ? "C:\\Windows\\System32" : dest);
        if (code > 1 || (a->exe && !store_installed(a, exe)) || code < 0) {
            printf("Installation failed: 7-Zip stopped with code %ld\n", code);
            return (int)E_INSTALL_FAILED;
        }
    } else if (a->kind == KIND_SETUP) {
        if (ends_with(file, ".msi")) {
            char cmd[1200];
            snprintf(cmd, sizeof(cmd), "msiexec /i \"%s\"%s", file, g_interactive ? "" : " /qn");
            code = run("C:\\Windows\\System32\\msiexec.exe", cmd, NULL);
        } else {
            char cmd[1200];
            snprintf(cmd, sizeof(cmd), "\"%s\"", file);
            code = run(file, cmd, "C:\\Downloads");
        }
        if (code != 0) {
            if (code < 0) printf("Could not start the installer\n");
            else printf("Installer failed with exit code: %ld\n", code);
            return (int)E_INSTALL_FAILED;
        }
    } else {
        scopy(r->location, sizeof(r->location), file);
    }
    scopy(r->type, sizeof(r->type), p->inst[0].type);
    return 0;
}

/* A portable program: C:\Programs\<command>\<command>.exe, so the
 * Terminal finds it by its command name */
static void portable_command(const Package *p, const Installer *in, char *out, size_t cap)
{
    if (in->alias[0]) { scopy(out, cap, in->alias); return; }
    const char *src = in->nnested ? in->nested_files[0] : in->url;
    const char *leaf = strrchr(src, '/');
    const char *b = strrchr(src, '\\');
    if (b > leaf) leaf = b;
    leaf = leaf ? leaf + 1 : src;
    scopy(out, cap, leaf);
    char *q = strchr(out, '?');
    if (q) *q = 0;
    if (ends_with(out, ".exe")) out[strlen(out) - 4] = 0;
    if (!out[0]) scopy(out, cap, p->id);
}

/* The silent switches for an installer type (winget's defaults) */
static void switches(const Installer *in, const char *type, char *out, size_t cap)
{
    const char *def = "";
    if (ieq(type, "inno")) def = "/SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
    else if (ieq(type, "nullsoft")) def = "/S";
    else if (ieq(type, "burn")) def = "/quiet /norestart";
    else if (ieq(type, "msi") || ieq(type, "wix")) def = "/qn /norestart";
    const char *sw = g_interactive ? (ieq(type, "inno") ? "/SP-" : "")
                   : g_silent ? (in->silent[0] ? in->silent : def)
                   : (in->progress[0] ? in->progress : in->silent[0] ? in->silent : def);
    snprintf(out, cap, "%s%s%s", sw, in->custom[0] && sw[0] ? " " : "", in->custom);
}

/* The Uninstall keys an installer added */
static void arp_new(const Arp *before, int nb, char *out, size_t cap)
{
    Arp *after;
    int na = arp_list(&after);
    out[0] = 0;
    for (int i = 0; i < na && !out[0]; i++)
        if (!arp_find(before, nb, after[i].key)) scopy(out, cap, after[i].key);
    free(after);
}

static int install_manifest(Package *p, Record *r)
{
    Installer *in = pick_installer(p);
    if (!in) {
        printf("No applicable installer found; see logs for more details.%s%s\n",
               g_skip_reason ? "\n  " : "", g_skip_reason ? g_skip_reason : "");
        return (int)E_NO_INSTALLER;
    }
    if (!in->url[0] || !in->sha[0]) { printf("The manifest has no installer address or hash\n"); return (int)E_MANIFEST_FAILED; }
    const char *type = ieq(in->type, "zip") ? in->nested : in->type;
    char dir[MAX_PATH], file[MAX_PATH];
    snprintf(dir, sizeof(dir), "C:\\Downloads\\WinGet\\%s.%s", p->id, p->version);
    make_dirs(dir);
    snprintf(file, sizeof(file), "%s\\%s%s", dir, p->id,
             ieq(in->type, "zip") ? ".zip" : ieq(in->type, "msi") || ieq(in->type, "wix") ? ".msi" : ".exe");
    int e = download(in, file, true);
    if (e) return e;
    printf("Starting package install...\n");
    scopy(r->type, sizeof(r->type), type);
    long code = 0;
    char cmd[2048], sw[600];
    if (ieq(type, "portable")) {
        char command[64], dest[MAX_PATH], target[MAX_PATH];
        portable_command(p, in, command, sizeof(command));
        snprintf(dest, sizeof(dest), "C:\\Programs\\%s", ieq(in->type, "zip") ? p->id : command);
        if (r->location[0] && is_dir(r->location)) delete_tree(r->location);          /* (an upgrade) */
        make_dirs(dest);
        if (ieq(in->type, "zip")) {
            code = unzip(file, dest, NULL);
            if (code != 0) { printf("Could not unpack the package (7-Zip: %ld)\n", code); return (int)E_INSTALL_FAILED; }
            snprintf(target, sizeof(target), "%s\\%s", dest, in->nested_files[0]);
            for (char *c = target; *c; c++) if (*c == '/') *c = '\\';
            if (!exists(target)) { printf("The package has no %s\n", in->nested_files[0]); return (int)E_INSTALL_FAILED; }
        } else {
            snprintf(target, sizeof(target), "%s\\%s.exe", dest, command);
            if (!MoveFileExA(file, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                printf("Could not copy the program to %s\n", target);
                return (int)E_INSTALL_FAILED;
            }
        }
        scopy(r->location, sizeof(r->location), dest);
        scopy(r->command, sizeof(r->command), command);
        printf("Command line alias added: \"%s\" (%s)\n", command, target);
    } else {
        char run_file[MAX_PATH];
        scopy(run_file, sizeof(run_file), file);
        if (ieq(in->type, "zip")) {
            char unpacked[MAX_PATH];
            snprintf(unpacked, sizeof(unpacked), "%s\\unpacked", dir);
            code = unzip(file, unpacked, NULL);
            if (code != 0) { printf("Could not unpack the package (7-Zip: %ld)\n", code); return (int)E_INSTALL_FAILED; }
            snprintf(run_file, sizeof(run_file), "%s\\%s", unpacked, in->nested_files[0]);
            for (char *c = run_file; *c; c++) if (*c == '/') *c = '\\';
        }
        switches(in, type, sw, sizeof(sw));
        Arp *before;
        int nb = arp_list(&before);
        if (ieq(type, "msi") || ieq(type, "wix")) {
            snprintf(cmd, sizeof(cmd), "msiexec /i \"%s\" %s", run_file, sw);
            code = run("C:\\Windows\\System32\\msiexec.exe", cmd, NULL);
        } else {
            if (ieq(type, "exe") && !sw[0] && !g_interactive)
                printf("(the manifest gives no silent switch: the installer may ask questions)\n");
            snprintf(cmd, sizeof(cmd), "\"%s\" %s", run_file, sw);
            code = run(run_file, cmd, dir);
        }
        if (code != 0 && code != 3010 && code != 1641) {         /* (reboot required is success) */
            free(before);
            if (code < 0) printf("Could not start the installer\n");
            else printf("Installer failed with exit code: %ld\n", code);
            return (int)E_INSTALL_FAILED;
        }
        arp_new(before, nb, r->arp, sizeof(r->arp));
        free(before);
        if (code) printf("Restart your PC to finish installation.\n");
    }
    return 0;
}

static int install_package(Package *p, const char *version)
{
    Record r;
    Record old;
    bool had = record_read(p->id, &old);
    memset(&r, 0, sizeof(r));
    if (had) { scopy(r.location, sizeof(r.location), old.location); scopy(r.arp, sizeof(r.arp), old.arp); }
    if (!resolve(p, version)) {
        printf("No version found matching: %s\n", version ? version : p->id);
        return (int)E_NOT_FOUND;
    }
    printf("Found %s [%s] Version %s\n", p->name, p->id, p->version);
    if (p->license[0] && p->store < 0) printf("This application is licensed to you by its owner (%s).\n", p->license);
    if (p->store < 0) printf("Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.\n");
    int e = p->store >= 0 ? install_store(p, &r) : install_manifest(p, &r);
    if (e) return e;
    scopy(r.id, sizeof(r.id), p->id);
    scopy(r.name, sizeof(r.name), p->name);
    scopy(r.publisher, sizeof(r.publisher), p->publisher);
    scopy(r.version, sizeof(r.version), p->version);
    scopy(r.source, sizeof(r.source), p->source);
    record_write(&r);
    printf("Successfully installed %s %s\n", p->id, p->version);
    return 0;
}

/* -----------------------------------------------------------------------
 * Arguments
 * ----------------------------------------------------------------------- */
typedef struct {
    Filter f;
    const char *source, *manifest, *count, *type, *arg;
    bool all, include_unknown, upgrade_available, force;
    const char *pos[4];
    int npos;
} Args;

static bool parse(int argc, char **argv, int from, Args *a)
{
    memset(a, 0, sizeof(*a));
    for (int i = from; i < argc; i++) {
        const char *s = argv[i];
        const char **val = NULL;
        if (ieq(s, "--id")) val = &a->f.id;
        else if (ieq(s, "--name")) val = &a->f.name;
        else if (ieq(s, "--moniker")) val = &a->f.moniker;
        else if (ieq(s, "-q") || ieq(s, "--query")) val = &a->f.query;
        else if (ieq(s, "-v") || ieq(s, "--version")) val = &a->f.version;
        else if (ieq(s, "-s") || ieq(s, "--source")) val = &a->source;
        else if (ieq(s, "-m") || ieq(s, "--manifest")) val = &a->manifest;
        else if (ieq(s, "-n") || ieq(s, "--count")) val = &a->count;
        else if (ieq(s, "-t") || ieq(s, "--type")) val = &a->type;
        else if (ieq(s, "-a") || ieq(s, "--arg")) val = &a->arg;
        else if (ieq(s, "--scope") || ieq(s, "-l") || ieq(s, "--location") || ieq(s, "--locale") ||
                 ieq(s, "-o") || ieq(s, "--log") || ieq(s, "--override") || ieq(s, "--custom") ||
                 ieq(s, "-a") || ieq(s, "--architecture") || ieq(s, "--header") || ieq(s, "--tag") ||
                 ieq(s, "--command") || ieq(s, "--cmd")) { i++; continue; }    /* (accepted; NovaOS has one choice) */
        else if (ieq(s, "-e") || ieq(s, "--exact")) { a->f.exact = true; continue; }
        else if (ieq(s, "-i") || ieq(s, "--interactive")) { g_interactive = true; continue; }
        else if (ieq(s, "-h") || ieq(s, "--silent")) { g_silent = true; continue; }
        else if (ieq(s, "-r") || ieq(s, "--all") || ieq(s, "--recurse")) { a->all = true; continue; }
        else if (ieq(s, "-u") || ieq(s, "--unknown") || ieq(s, "--include-unknown")) { a->include_unknown = true; continue; }
        else if (ieq(s, "--upgrade-available")) { a->upgrade_available = true; continue; }
        else if (ieq(s, "--force")) { a->force = true; continue; }
        else if (s[0] == '-' && s[1] == '-') continue;   /* --accept-package-agreements, --disable-interactivity... */
        else if (a->npos < 4) { a->pos[a->npos++] = s; continue; }
        else return false;
        if (i + 1 >= argc) { printf("Argument value required, but none found: %s\n", s); return false; }
        *val = argv[++i];
    }
    if (a->npos && !a->f.query) a->f.query = a->pos[0];
    return true;
}

/* -----------------------------------------------------------------------
 * Commands
 * ----------------------------------------------------------------------- */
static const char *search_cell(int r, int c)
{
    const Package *p = &g_found[r];
    static char m[64];
    switch (c) {
    case 0: return p->name;
    case 1: return p->id;
    case 2: return p->version;
    case 3: if (!p->match[0] || !p->moniker[0] || !strcmp(p->match, "Id") || !strcmp(p->match, "Name")) return "";
            snprintf(m, sizeof(m), "%s: %s", p->match, p->moniker); return m;
    default: return p->source;
    }
}

static int cmd_search(Args *a)
{
    if (!a->f.query && !a->f.id && !a->f.name && !a->f.moniker) {
        printf("Please specify one of the search options: --query, --id, --name, --moniker\n");
        printf("(or a word: winget search firefox)\n");
    }
    search(&a->f, a->source, a->f.exact);
    if (!g_nfound) {
        printf("No package found matching input criteria.\n");
        return (int)E_NOT_FOUND;
    }
    int n = g_nfound;
    if (a->count && atoi(a->count) > 0 && atoi(a->count) < n) n = atoi(a->count);
    Col cols[] = { { "Name", 36 }, { "Id", 40 }, { "Version", 16 }, { "Match", 24 }, { "Source", 0 } };
    table(cols, 5, search_cell, n);
    return 0;
}

static int cmd_show(Args *a)
{
    Package p;
    int k = pick(&a->f, a->source, &p);
    if (k == 0) { printf("No package found matching input criteria.\n"); return (int)E_NOT_FOUND; }
    if (k == 2) { printf("Multiple packages found matching input criteria. Please refine the input.\n"); return (int)E_MULTIPLE; }
    if (!resolve(&p, a->f.version)) { printf("No version found matching: %s\n", a->f.version); return (int)E_NOT_FOUND; }
    printf("Found %s [%s]\n", p.name, p.id);
    printf("Version: %s\n", p.version);
    if (p.publisher[0]) printf("Publisher: %s\n", p.publisher);
    if (p.moniker[0]) printf("Moniker: %s\n", p.moniker);
    if (p.desc[0]) printf("Description: %s\n", p.desc);
    if (p.homepage[0]) printf("Homepage: %s\n", p.homepage);
    if (p.license[0]) printf("%s: %s\n", p.store >= 0 ? "NovaOS" : "License", p.license);
    printf("Source: %s\n", p.source);
    Installer *in = p.store >= 0 ? &p.inst[0] : pick_installer(&p);
    if (in) {
        printf("Installer:\n");
        printf("  Installer Type: %s%s%s%s\n", in->type, in->nested[0] ? " (" : "", in->nested, in->nested[0] ? ")" : "");
        printf("  Architecture: %s\n", in->arch);
        printf("  Installer Url: %s\n", in->url);
        if (in->sha[0]) printf("  Installer SHA256: %s\n", in->sha);
    } else printf("Installer: none for this PC%s%s\n", g_skip_reason ? ": " : "", g_skip_reason ? g_skip_reason : "");
    return 0;
}

static int cmd_install(Args *a)
{
    if (a->manifest) {
        /* a local manifest: one file (singleton), or a folder of them */
        Node *docs[3] = { 0 };          /* version, locale, installer */
        char path[MAX_PATH];
        if (is_dir(a->manifest)) {
            char pat[MAX_PATH];
            snprintf(pat, sizeof(pat), "%s\\*.yaml", a->manifest);
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    snprintf(path, sizeof(path), "%s\\%s", a->manifest, fd.cFileName);
                    FILE *f = fopen(path, "rb");
                    if (!f) continue;
                    Buf b = {0};
                    char chunk[4096];
                    size_t n;
                    while ((n = fread(chunk, 1, sizeof(chunk), f))) buf_add(&b, chunk, n);
                    fclose(f);
                    Node *d = b.data ? yaml_parse(b.data, b.len) : NULL;
                    free(b.data);
                    const char *t = d ? str(d, "ManifestType") : NULL;
                    int slot = !t ? -1 : !strcmp(t, "installer") ? 2 : !strcmp(t, "defaultLocale") ? 1 :
                               !strcmp(t, "version") ? 0 : !strcmp(t, "singleton") ? 3 : -1;
                    if (slot == 3) { for (int i = 0; i < 3; i++) if (!docs[i]) docs[i] = d; continue; }
                    if (slot >= 0 && !docs[slot]) docs[slot] = d; else node_free(d);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        } else {
            FILE *f = fopen(a->manifest, "rb");
            if (f) {
                Buf b = {0};
                char chunk[4096];
                size_t n;
                while ((n = fread(chunk, 1, sizeof(chunk), f))) buf_add(&b, chunk, n);
                fclose(f);
                Node *d = b.data ? yaml_parse(b.data, b.len) : NULL;
                free(b.data);
                docs[0] = docs[1] = docs[2] = d;
            }
        }
        if (!docs[2] || !str(docs[2], "PackageIdentifier")) {
            printf("Manifest validation failed: no installer manifest in %s\n", a->manifest);
            return (int)E_MANIFEST_FAILED;
        }
        Package p;
        from_manifest(&p, docs[0], docs[1], docs[2], "");
        scopy(p.source, sizeof(p.source), "manifest");
        Record r = {0}, old;
        if (record_read(p.id, &old)) { scopy(r.location, sizeof(r.location), old.location); scopy(r.arp, sizeof(r.arp), old.arp); }
        printf("Found %s [%s] Version %s\n", p.name, p.id, p.version);
        int e = install_manifest(&p, &r);
        if (!e) {
            scopy(r.id, sizeof(r.id), p.id); scopy(r.name, sizeof(r.name), p.name);
            scopy(r.publisher, sizeof(r.publisher), p.publisher); scopy(r.version, sizeof(r.version), p.version);
            scopy(r.source, sizeof(r.source), "");
            record_write(&r);
            printf("Successfully installed %s %s\n", p.id, p.version);
        }
        if (docs[1] != docs[0]) node_free(docs[1]);
        if (docs[2] != docs[0]) node_free(docs[2]);
        node_free(docs[0]);
        return e;
    }
    if (!a->f.query && !a->f.id && !a->f.name && !a->f.moniker) {
        printf("Please specify a package: winget install <query> | --id <id> | -m <manifest>\n");
        return (int)E_INVALID_ARGS;
    }
    Package p;
    int k = pick(&a->f, a->source, &p);
    if (k == 0) {
        printf("No package found matching input criteria.\n");
        return (int)(g_source_failed ? E_SOURCE_FAILED : E_NOT_FOUND);
    }
    if (k == 2) {
        printf("Multiple packages found matching input criteria. Please refine the input.\n");
        for (int i = 0; i < g_nfound; i++) printf("  %s [%s] (%s)\n", g_found[i].name, g_found[i].id, g_found[i].source);
        return (int)E_MULTIPLE;
    }
    Record old;
    if (record_read(p.id, &old) && !a->f.version && p.store < 0) {
        Package latest = p;
        if (resolve(&latest, NULL) && vcmp(latest.version, old.version) <= 0) {
            printf("Found an existing package already installed. Trying to upgrade the installed package...\n"
                   "No available upgrade found.\nNo newer package versions are available from the configured sources.\n");
            return 0;
        }
        p = latest;
    }
    if (p.store >= 0 && store_installed(&g_cat->apps[p.store], NULL)) {
        printf("Found an existing package already installed: %s [%s]\nNo available upgrade found.\n", p.name, p.id);
        return 0;
    }
    return install_package(&p, a->f.version);
}

/* What is installed: winget's records, the App Store's programs and the
 * Uninstall keys, one row each */
typedef struct {
    char name[200], id[300], version[64], available[64], source[64], command[64];
    int store;                 /* catalog index, or -1 */
    bool record;
    char arp[300];
} Row;
static Row *g_rows;
static int g_nrows;

static const char *list_cell(int r, int c)
{
    const Row *w = &g_rows[r];
    return c == 0 ? w->name : c == 1 ? w->id : c == 2 ? w->version : c == 3 ? w->available : w->source;
}

/* The newest version @source has of @id ("" if none) */
static void available(const char *id, const char *source, char *out, size_t cap)
{
    out[0] = 0;
    Source *s = find_source(source);
    if (!s) return;
    if (s->kind == SRC_STORE) {
        load_catalog();
        for (int i = 0; i < g_cat->count; i++) {
            char sid[130];
            store_id(&g_cat->apps[i], sid, sizeof(sid));
            if (ieq(sid, id) && g_cat->apps[i].version) scopy(out, cap, g_cat->apps[i].version);
        }
        return;
    }
    Package p;
    if (s->kind == SRC_COMMUNITY ? community_get(s, id, NULL, &p) : rest_get(s, id, NULL, &p)) scopy(out, cap, p.version);
}

static void gather(bool with_available)
{
    g_nrows = 0;
    Record *rec;
    int nr = records(&rec);
    Arp *arp;
    int na = arp_list(&arp);
    load_catalog();
    g_rows = calloc((size_t)(nr + na + g_cat->count + 1), sizeof(Row));
    bool *arp_used = calloc((size_t)na + 1, sizeof(bool));
    for (int i = 0; i < nr; i++) {
        Record *r = &rec[i];
        /* still there? */
        bool present = r->location[0] ? exists(r->location) : true;
        if (r->arp[0]) present = arp_find(arp, na, r->arp);
        int store = -1;
        for (int c = 0; c < g_cat->count && ieq(r->source, "novaos"); c++) {
            char sid[130];
            store_id(&g_cat->apps[c], sid, sizeof(sid));
            if (ieq(sid, r->id)) { store = c; present = store_installed(&g_cat->apps[c], NULL); }
        }
        if (!present) { record_delete(r->id); continue; }
        Row *w = &g_rows[g_nrows++];
        scopy(w->name, sizeof(w->name), r->name);
        scopy(w->id, sizeof(w->id), r->id);
        scopy(w->version, sizeof(w->version), r->version);
        scopy(w->source, sizeof(w->source), r->source);
        scopy(w->arp, sizeof(w->arp), r->arp);
        scopy(w->command, sizeof(w->command), r->command);
        w->store = store;
        w->record = true;
        for (int k = 0; k < na; k++) if (ieq(arp[k].key, r->arp)) arp_used[k] = true;
    }
    for (int c = 0; c < g_cat->count; c++) {
        const StoreApp *a = &g_cat->apps[c];
        char sid[130];
        store_id(a, sid, sizeof(sid));
        bool listed = false;
        for (int k = 0; k < g_nrows; k++) if (ieq(g_rows[k].id, sid)) listed = true;
        if (listed || !store_installed(a, NULL)) continue;
        Row *w = &g_rows[g_nrows++];
        scopy(w->name, sizeof(w->name), a->name);
        scopy(w->id, sizeof(w->id), sid);
        scopy(w->version, sizeof(w->version), "Unknown");
        scopy(w->source, sizeof(w->source), "novaos");
        w->store = c;
        for (int k = 0; k < na; k++)              /* its installer's Uninstall key: the version */
            if (!arp_used[k] && !_strnicmp(arp[k].name, a->name, strlen(a->name))) {
                arp_used[k] = true;
                if (arp[k].version[0]) scopy(w->version, sizeof(w->version), arp[k].version);
                scopy(w->arp, sizeof(w->arp), arp[k].key);
                break;
            }
    }
    for (int k = 0; k < na; k++) {
        if (arp_used[k]) continue;
        Row *w = &g_rows[g_nrows++];
        scopy(w->name, sizeof(w->name), arp[k].name);
        scopy(w->id, sizeof(w->id), arp[k].key);
        scopy(w->version, sizeof(w->version), arp[k].version[0] ? arp[k].version : "Unknown");
        scopy(w->arp, sizeof(w->arp), arp[k].key);
        w->store = -1;
    }
    if (with_available)
        for (int i = 0; i < g_nrows; i++) {
            Row *w = &g_rows[i];
            if (!w->source[0]) continue;
            char av[64];
            available(w->id, w->source, av, sizeof(av));
            if (av[0] && (ieq(w->version, "Unknown") ? strcmp(av, "Unknown") != 0 : vcmp(av, w->version) > 0))
                scopy(w->available, sizeof(w->available), av);
        }
    free(rec); free(arp); free(arp_used);
}

static bool row_match(const Row *w, const Args *a)
{
    if (a->f.id && !text_match(w->id, a->f.id, a->f.exact)) return false;
    if (a->f.name && !text_match(w->name, a->f.name, a->f.exact)) return false;
    if (a->source && !ieq(w->source, a->source)) return false;
    if (a->f.query && !text_match(w->id, a->f.query, a->f.exact) && !text_match(w->name, a->f.query, a->f.exact) &&
        !text_match(w->command, a->f.query, a->f.exact)) return false;
    return true;
}

static void keep_rows(const Args *a, bool upgradable)
{
    int k = 0;
    for (int i = 0; i < g_nrows; i++) {
        Row *w = &g_rows[i];
        if (!row_match(w, a)) continue;
        if (upgradable && (!w->available[0] || (!a->include_unknown && ieq(w->version, "Unknown")))) continue;
        g_rows[k++] = *w;
    }
    g_nrows = k;
}

static int cmd_list(Args *a)
{
    gather(true);
    keep_rows(a, a->upgrade_available);
    if (!g_nrows) { printf("No installed package found matching input criteria.\n"); return (int)E_NOT_FOUND; }
    Col cols[] = { { "Name", 36 }, { "Id", 40 }, { "Version", 16 }, { "Available", 16 }, { "Source", 0 } };
    table(cols, 5, list_cell, g_nrows);
    return 0;
}

static int cmd_upgrade(Args *a)
{
    gather(true);
    if (!a->all && !a->f.query && !a->f.id && !a->f.name) {      /* the list */
        keep_rows(a, true);
        int unknown = 0;
        if (!g_nrows) {
            printf("No installed package found matching input criteria.\n");
            return 0;
        }
        Col cols[] = { { "Name", 36 }, { "Id", 40 }, { "Version", 16 }, { "Available", 16 }, { "Source", 0 } };
        table(cols, 5, list_cell, g_nrows);
        for (int i = 0; i < g_nrows; i++) if (ieq(g_rows[i].version, "Unknown")) unknown++;
        printf("%d upgrades available.\n", g_nrows);
        (void)unknown;
        return 0;
    }
    keep_rows(a, true);
    if (!g_nrows) {
        printf("No available upgrade found.\nNo newer package versions are available from the configured sources.\n");
        return a->all ? 0 : (int)E_NO_UPGRADE;
    }
    int failed = 0;
    Row *rows = g_rows;
    int n = g_nrows;
    g_rows = NULL;
    for (int i = 0; i < n; i++) {
        Row *w = &rows[i];
        printf("(%d/%d) ", i + 1, n);
        Package p;
        memset(&p, 0, sizeof(p));
        p.store = w->store;
        if (w->store >= 0) from_store(&p, w->store);
        else {
            scopy(p.id, sizeof(p.id), w->id);
            scopy(p.source, sizeof(p.source), w->source);
        }
        if (w->store >= 0) {
            /* the App Store's download replaces the program: unpack or run its installer again */
            const StoreApp *ap = &g_cat->apps[w->store];
            char old[MAX_PATH];
            snprintf(old, sizeof(old), "C:\\Downloads\\%s", ap->file);
            DeleteFileA(old);                    /* (the new catalog's file name may be the same) */
        }
        int e = install_package(&p, NULL);
        if (e) failed++;
    }
    free(rows);
    if (failed) printf("%d package(s) failed to upgrade.\n", failed);
    return failed ? (int)E_INSTALL_FAILED : 0;
}

static int run_uninstall_string(const char *s, bool quiet_known)
{
    char cmd[1200];
    scopy(cmd, sizeof(cmd), s);
    if (istr(cmd, "msiexec")) {              /* MsiExec.exe /I{code} or /X{code}: removal, quietly */
        char *i = (char *)istr(cmd, "/I{");
        if (i) i[1] = 'X';
        if (!g_interactive && !istr(cmd, "/q")) strncat(cmd, " /qn", sizeof(cmd) - strlen(cmd) - 1);
    } else if (!quiet_known && !g_interactive) {
        (void)0;                              /* (only the program knows its own quiet switch) */
    }
    char app[MAX_PATH] = "";
    const char *p = cmd;
    if (*p == '"') {
        const char *e = strchr(p + 1, '"');
        if (e) snprintf(app, sizeof(app), "%.*s", (int)(e - p - 1), p + 1);
    } else {
        const char *e = istr(p, ".exe");
        if (e) snprintf(app, sizeof(app), "%.*s", (int)(e - p + 4), p);
    }
    if (app[0] && !strchr(app, '\\')) {       /* msiexec.exe: the system folder */
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "C:\\Windows\\System32\\%s", app);
        scopy(app, sizeof(app), full);
    }
    return (int)run(app[0] ? app : NULL, cmd, NULL);
}

static int cmd_uninstall(Args *a)
{
    if (!a->f.query && !a->f.id && !a->f.name) {
        printf("Please specify a package: winget uninstall <query> | --id <id>\n");
        return (int)E_INVALID_ARGS;
    }
    gather(false);
    Args exact = *a;
    exact.f.exact = true;
    int k = 0;
    for (int i = 0; i < g_nrows; i++) if (row_match(&g_rows[i], &exact)) g_rows[k++] = g_rows[i];
    if (!k && !a->f.exact) {
        gather(false);
        for (int i = 0; i < g_nrows; i++) if (row_match(&g_rows[i], a)) g_rows[k++] = g_rows[i];
    }
    g_nrows = k;
    if (!k) { printf("No installed package found matching input criteria.\n"); return (int)E_NOT_FOUND; }
    if (k > 1) {
        printf("Multiple installed packages found matching input criteria. Please refine the input.\n");
        Col cols[] = { { "Name", 36 }, { "Id", 40 }, { "Version", 16 }, { "Available", 1 }, { "Source", 0 } };
        table(cols, 5, list_cell, g_nrows);
        return (int)E_MULTIPLE;
    }
    Row *w = &g_rows[0];
    printf("Found %s [%s]\nStarting package uninstall...\n", w->name, w->id);
    Record r;
    bool rec = record_read(w->id, &r);
    int code = 0;
    if (rec && r.location[0] && !r.arp[0] && w->store < 0) {          /* portable or unpacked by winget */
        if (!delete_tree(r.location)) { printf("Could not delete %s\n", r.location); return (int)E_UNINSTALL_FAILED; }
    } else if (w->store >= 0 && !w->arp[0]) {
        const StoreApp *ap = &g_cat->apps[w->store];
        char path[MAX_PATH];
        if (ap->kind == KIND_ARCHIVE && !ap->system) {
            snprintf(path, sizeof(path), "C:\\Programs\\%s", ap->dest);
            if (!delete_tree(path)) { printf("Could not delete %s\n", path); return (int)E_UNINSTALL_FAILED; }
        } else if (ap->kind == KIND_PORTABLE) {
            snprintf(path, sizeof(path), "C:\\Downloads\\%s", ap->file);
            DeleteFileA(path);
        } else {
            printf("%s has no uninstaller NovaOS knows of; the App Store installed it into the system folders.\n", w->name);
            return (int)E_UNINSTALL_FAILED;
        }
    } else if (w->arp[0]) {
        Arp *arp;
        int na = arp_list(&arp);
        const Arp *e = NULL;
        for (int i = 0; i < na; i++) if (ieq(arp[i].key, w->arp)) e = &arp[i];
        if (!e || (!e->uninstall[0] && !e->quiet[0])) {
            printf("The package has no uninstall command.\n");
            free(arp);
            return (int)E_UNINSTALL_FAILED;
        }
        code = run_uninstall_string(e->quiet[0] && !g_interactive ? e->quiet : e->uninstall, e->quiet[0] != 0);
        free(arp);
        if (code != 0 && code != 3010 && code != 1641) {
            if (code < 0) printf("Could not start the uninstaller\n");
            else printf("Uninstall failed with exit code: %d\n", code);
            return (int)E_UNINSTALL_FAILED;
        }
    }
    if (rec) record_delete(w->id);
    printf("Successfully uninstalled\n");
    return 0;
}

/* The App Store's catalog, refreshed as the Store's Refresh does */
static int refresh_store_catalog(void)
{
    char url[600] = STORE_CHANNEL;
    FILE *f = fopen("C:" STORE_CHANNEL_PATH, "rb");
    if (f) {
        if (fgets(url, sizeof(url), f)) {
            size_t n = strlen(url);
            while (n && (url[n - 1] == '\n' || url[n - 1] == '\r' || url[n - 1] == ' ')) url[--n] = 0;
        }
        fclose(f);
        if (strncmp(url, "https://", 8)) scopy(url, sizeof(url), STORE_CHANNEL);
    }
    Buf b = {0};
    DWORD st = http(url, NULL, &b, NULL, NULL, false);
    StoreCatalog *c = st == 200 && b.data ? store_catalog_parse(b.data, (UINT32)b.len) : NULL;
    if (!c) {
        printf("  novaos: could not update (%s)\n", st == 200 ? "the catalog is invalid" : g_http_error[0] ? g_http_error : "no connection");
        free(b.data);
        return 1;
    }
    free(c);
    FILE *o = fopen("C:\\Windows\\AppStore\\catalog.json.tmp", "wb");
    bool ok = o && fwrite(b.data, 1, b.len, o) == b.len;
    if (o) fclose(o);
    ok = ok && MoveFileExA("C:\\Windows\\AppStore\\catalog.json.tmp", "C:" STORE_CATALOG_PATH, MOVEFILE_REPLACE_EXISTING);
    free(b.data);
    printf("  novaos: %s\n", ok ? "Done" : "could not save the catalog");
    return ok ? 0 : 1;
}

static int cmd_source(int argc, char **argv)
{
    const char *sub = argc > 2 ? argv[2] : "list";
    Args a;
    if (!parse(argc, argv, 3, &a)) return (int)E_INVALID_ARGS;
    /* the name: -n/--name or the first word; the URL: -a/--arg or the next word */
    const char *name = NULL;
    for (int i = 3; i < argc; i++) if ((ieq(argv[i], "-n") || ieq(argv[i], "--name")) && i + 1 < argc) name = argv[i + 1];
    int next = 0;
    if (!name && a.npos) name = a.pos[next++];
    if (ieq(sub, "list") || ieq(sub, "ls")) {
        printf("Name   Argument\n");
        printf("--------------------------------------------------------------\n");
        for (int i = 0; i < g_nsrc; i++)
            if (!name || ieq(name, g_src[i].name)) printf("%-6s %s\n", g_src[i].name, g_src[i].arg);
        return 0;
    }
    if (ieq(sub, "update") || ieq(sub, "refresh")) {
        printf("Updating all sources...\n");
        int e = 0;
        for (int i = 0; i < g_nsrc; i++) {
            if (name && !ieq(name, g_src[i].name)) continue;
            if (g_src[i].kind == SRC_STORE) e |= refresh_store_catalog();
            else printf("  %s: Done\n", g_src[i].name);
        }
        return e ? (int)E_SOURCE_FAILED : 0;
    }
    if (ieq(sub, "add")) {
        const char *arg = a.arg ? a.arg : a.npos > next ? a.pos[next] : NULL;
        if (!name || !arg) { printf("Usage: winget source add <name> <url> [-t Microsoft.Rest]\n"); return (int)E_INVALID_ARGS; }
        if (a.type && !ieq(a.type, "Microsoft.Rest")) {
            printf("Only REST sources (Microsoft.Rest) can be added on NovaOS.\n");
            return (int)E_INVALID_ARGS;
        }
        if (find_source(name)) { printf("A source with the given name already exists and refers to a different location.\n"); return (int)E_INVALID_ARGS; }
        printf("Adding source:\n  %s -> %s\n", name, arg);
        char url[700];
        size_t n = strlen(arg);
        snprintf(url, sizeof(url), "%.*s/information", (int)(n && arg[n - 1] == '/' ? n - 1 : n), arg);
        DWORD st = 0;
        Node *info = fetch_doc(url, NULL, false, &st);
        const char *ident = info ? str(get(info, "Data"), "SourceIdentifier") : NULL;
        if (!ident) {
            printf("The source is not a REST source NovaOS can read (%s)\n",
                   st ? (st == 200 ? "no SourceIdentifier" : "server replied with an error") : g_http_error[0] ? g_http_error : "no connection");
            node_free(info);
            return (int)E_SOURCE_FAILED;
        }
        char key[200];
        snprintf(key, sizeof(key), KEY_SOURCES "\\%s", name);
        HKEY k;
        LSTATUS r = RegCreateKeyExA(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
        if (r == ERROR_ACCESS_DENIED) {
            printf("This command requires administrator privileges to execute.\n");
            node_free(info);
            return (int)E_REQUIRES_ADMIN;
        }
        if (r == ERROR_SUCCESS) {
            char clean[600];
            snprintf(clean, sizeof(clean), "%.*s", (int)(n && arg[n - 1] == '/' ? n - 1 : n), arg);
            reg_set(k, "Arg", clean);
            reg_set(k, "Type", "Microsoft.Rest");
            reg_set(k, "Identifier", ident);
            RegCloseKey(k);
        }
        node_free(info);
        printf("Done\n");
        return 0;
    }
    if (ieq(sub, "remove") || ieq(sub, "rm")) {
        Source *s = name ? find_source(name) : NULL;
        if (!s) { printf("Did not find a source named: %s\n", name ? name : ""); return (int)E_INVALID_ARGS; }
        char key[200];
        snprintf(key, sizeof(key), KEY_SOURCES "\\%s", s->name);
        printf("Removing source: %s...\n", s->name);
        if (s->kind == SRC_REST) { RegDeleteTreeA(HKEY_LOCAL_MACHINE, key); RegDeleteKeyA(HKEY_LOCAL_MACHINE, key); }
        else {
            HKEY k;
            if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) == ERROR_SUCCESS) {
                reg_set(k, "Removed", "1");
                RegCloseKey(k);
            }
        }
        printf("Done\n");
        return 0;
    }
    if (ieq(sub, "reset")) {
        if (!a.force && !name) { printf("Resetting all sources requires --force\n"); return (int)E_INVALID_ARGS; }
        RegDeleteTreeA(HKEY_LOCAL_MACHINE, KEY_SOURCES);
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, KEY_SOURCES);
        printf("Resetting all sources...Done\n");
        return 0;
    }
    if (ieq(sub, "export")) {
        for (int i = 0; i < g_nsrc; i++)
            printf("{\"Arg\":\"%s\",\"Name\":\"%s\",\"Type\":\"%s\"}\n", g_src[i].arg, g_src[i].name, g_src[i].type);
        return 0;
    }
    printf("Unrecognized command: 'source %s'\n", sub);
    return (int)E_INVALID_ARGS;
}

static int cmd_hash(Args *a)
{
    if (!a->f.query) { printf("Usage: winget hash <file>\n"); return (int)E_INVALID_ARGS; }
    uint8_t sha[32];
    if (!sha256_file(a->f.query, sha)) { printf("File does not exist: %s\n", a->f.query); return (int)E_INVALID_ARGS; }
    char h[65];
    hex(sha, 32, h);
    for (char *c = h; *c; c++) if (*c >= 'A' && *c <= 'F') *c += 32;
    printf("InstallerSha256: %s\n", h);
    return 0;
}

static void usage(void)
{
    printf("Windows Package Manager v" WINGET_VERSION "\n"
           "Copyright (c) NovaOS contributors (MIT)\n\n"
           "The winget command line utility enables installing applications and other packages from the command line.\n\n"
           "usage: winget  [<command>] [<options>]\n\n"
           "The following commands are available:\n"
           "  install    Installs the given package\n"
           "  show       Shows information about a package\n"
           "  source     Manage sources of packages\n"
           "  search     Find and show basic info of packages\n"
           "  list       Display installed packages\n"
           "  upgrade    Shows and performs available upgrades\n"
           "  uninstall  Uninstalls the given package\n"
           "  hash       Helper to hash installer files\n\n"
           "For more details on a specific command, pass it the help argument. [-?]\n\n"
           "The following options are available:\n"
           "  -v,--version  Display the version of the tool\n"
           "  --info        Display general info of the tool\n\n"
           "Sources: novaos (the App Store's catalog), winget (github.com/microsoft/winget-pkgs,\n"
           "by exact package identifier) and REST sources added with 'winget source add'.\n");
}

int main(int argc, char **argv)
{
    load_sources();
    if (argc < 2 || ieq(argv[1], "-?") || ieq(argv[1], "--help") || ieq(argv[1], "help")) { usage(); return 0; }
    const char *c = argv[1];
    if (ieq(c, "-v") || ieq(c, "--version")) { printf("v" WINGET_VERSION "\n"); return 0; }
    if (ieq(c, "--info")) {
        usage();
        printf("\nWindows: NovaOS\nPackage records: HKLM\\" KEY_PACKAGES "\nDownloads: C:\\Downloads\\WinGet\n\n");
        return cmd_source(2, argv);
    }
    if (ieq(c, "source")) return cmd_source(argc, argv);
    Args a;
    if (!parse(argc, argv, 2, &a)) return (int)E_INVALID_ARGS;
    if (a.source && !find_source(a.source)) { printf("No sources match the given value: %s\n", a.source); return (int)E_INVALID_ARGS; }
    int e;
    if (ieq(c, "search") || ieq(c, "find")) e = cmd_search(&a);
    else if (ieq(c, "show") || ieq(c, "view")) e = cmd_show(&a);
    else if (ieq(c, "install") || ieq(c, "add")) e = cmd_install(&a);
    else if (ieq(c, "list") || ieq(c, "ls")) e = cmd_list(&a);
    else if (ieq(c, "upgrade") || ieq(c, "update")) e = cmd_upgrade(&a);
    else if (ieq(c, "uninstall") || ieq(c, "remove") || ieq(c, "rm")) e = cmd_uninstall(&a);
    else if (ieq(c, "hash")) e = cmd_hash(&a);
    else { printf("Unrecognized command: '%s'\n", c); usage(); return (int)E_INVALID_ARGS; }
    return e;
}
