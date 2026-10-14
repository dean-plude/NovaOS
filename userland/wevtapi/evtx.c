/*
 * evtx.c — events saved as a Windows .evtx file (EvtExportLog, Event
 * Viewer's "Save All Events As")
 *
 * The layout is the one Windows Vista and later write (documented by
 * libevtx's "Windows XML Event Log (EVTX) format"): a 4096-byte file
 * header ("ElfFile"), then 64 KB chunks ("ElfChnk", a 512-byte header with
 * CRC32 checksums), each holding whole event records ("**", size, record
 * ID, time written, the event as binary XML, the size again).  Each event
 * is its Event XML (wevtapi.c's evt_xml) in binary XML: elements,
 * attributes and text written out in full, names kept once per chunk,
 * without the templates Windows uses to share an event's shape.
 */
#include <windows.h>
#include <string.h>
#include "wevtapi.h"

#define CHUNK     0x10000
#define CHUNK_HDR 0x200
#define MAX_NAMES 96

static DWORD g_crc[256];

static DWORD crc32(const BYTE *p, DWORD n, DWORD crc)
{
    if (!g_crc[1])
        for (DWORD i = 0; i < 256; i++) {
            DWORD c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            g_crc[i] = c;
        }
    crc = ~crc;
    while (n--) crc = g_crc[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

typedef struct {
    BYTE  *b;                       /* the chunk */
    DWORD  pos;
    BOOL   over;                    /* the record did not fit */
    struct { const WCHAR *s; int n; DWORD off; } names[MAX_NAMES];
    int    nnames;
} Ck;

static void put(Ck *c, const void *p, DWORD n)
{
    if (c->pos + n > CHUNK) { c->over = TRUE; return; }
    memcpy(c->b + c->pos, p, n);
    c->pos += n;
}
static void put8(Ck *c, BYTE v) { put(c, &v, 1); }
static void put16(Ck *c, WORD v) { put(c, &v, 2); }
static void put32(Ck *c, DWORD v) { put(c, &v, 4); }
static void at32(Ck *c, DWORD where, DWORD v) { if (where + 4 <= CHUNK) memcpy(c->b + where, &v, 4); }

/* A name: its offset if this chunk has it, else the name itself, here */
static void name(Ck *c, const WCHAR *s, int n)
{
    for (int i = 0; i < c->nnames; i++)
        if (c->names[i].n == n && !memcmp(c->names[i].s, s, sizeof(WCHAR) * (size_t)n)) { put32(c, c->names[i].off); return; }
    DWORD off = c->pos + 4;
    put32(c, off);
    WORD hash = 0;
    DWORD h = 0;
    for (int i = 0; i < n; i++) h = h * 65599 + s[i];
    hash = (WORD)h;
    put32(c, 0);                    /* (next name with this hash: none) */
    put16(c, hash);
    put16(c, (WORD)n);
    put(c, s, sizeof(WCHAR) * (DWORD)n);
    put16(c, 0);
    if (c->nnames < MAX_NAMES) {
        c->names[c->nnames].s = s;              /* (points into the event's XML, which outlives the record) */
        c->names[c->nnames].n = n;
        c->names[c->nnames].off = off;
        c->nnames++;
    }
}

/* Text with &lt; and the like undone, as a value token (a UTF-16 string) */
static void text(Ck *c, const WCHAR *s, int n)
{
    WCHAR *t = evt_alloc(sizeof(WCHAR) * (size_t)(n + 1));
    int k = 0;
    if (!t) { c->over = TRUE; return; }
    for (int i = 0; i < n; i++) {
        if (s[i] == '&') {
            static const struct { const WCHAR *e; WCHAR ch; } ent[] = {
                { L"&lt;", '<' }, { L"&gt;", '>' }, { L"&amp;", '&' }, { L"&apos;", '\'' }, { L"&quot;", '"' } };
            int done = 0;
            for (int j = 0; j < 5 && !done; j++) {
                int l = lstrlenW(ent[j].e);
                if (i + l <= n && !memcmp(s + i, ent[j].e, sizeof(WCHAR) * (size_t)l)) { t[k++] = ent[j].ch; i += l - 1; done = 1; }
            }
            if (done) continue;
        }
        t[k++] = s[i];
    }
    if (k > 0xFFFF) k = 0xFFFF;
    put8(c, 0x05);                  /* value text */
    put8(c, 0x01);                  /* a UTF-16 string */
    put16(c, (WORD)k);
    put(c, t, sizeof(WCHAR) * (DWORD)k);
    evt_free(t);
}

static BOOL is_name(WCHAR ch) { return ch && ch != ' ' && ch != '>' && ch != '/' && ch != '=' && ch != '<'; }

/* The element at *@p ('<'), with what is in it */
static void element(Ck *c, const WCHAR **p)
{
    const WCHAR *s = *p + 1, *nm = s;
    while (is_name(*s)) s++;
    int nl = (int)(s - nm);
    struct { const WCHAR *n, *v; int nl, vl; } at[8];
    int na = 0;
    for (;;) {                                      /* the attributes: name='value' */
        while (*s == ' ') s++;
        if (!*s || *s == '>' || *s == '/') break;
        const WCHAR *an = s;
        while (is_name(*s)) s++;
        int anl = (int)(s - an);
        if (*s != '=' || (s[1] != '\'' && s[1] != '"')) { c->over = TRUE; return; }
        WCHAR q = s[1];
        const WCHAR *v = s + 2, *e = v;
        while (*e && *e != q) e++;
        if (!*e) { c->over = TRUE; return; }
        if (na < 8) { at[na].n = an; at[na].nl = anl; at[na].v = v; at[na].vl = (int)(e - v); na++; }
        s = e + 1;
    }
    put8(c, na ? 0x41 : 0x01);                      /* open start element (with attributes) */
    put16(c, 0xFFFF);                               /* (dependency: none) */
    DWORD size_at = c->pos;
    put32(c, 0);
    name(c, nm, nl);
    if (na) {
        DWORD list_at = c->pos;
        put32(c, 0);
        for (int i = 0; i < na; i++) {
            put8(c, i + 1 < na ? 0x46 : 0x06);      /* attribute (more follow) */
            name(c, at[i].n, at[i].nl);
            text(c, at[i].v, at[i].vl);
        }
        at32(c, list_at, c->pos - list_at - 4);
    }
    if (*s == '/') {                                /* <x/> */
        put8(c, 0x03);                              /* close empty element */
        *p = s + 2;
    } else {
        put8(c, 0x02);                              /* close start element */
        s++;
        for (;;) {
            const WCHAR *t = s;
            while (*s && *s != '<') s++;
            if (s > t) text(c, t, (int)(s - t));
            if (!*s) { c->over = TRUE; return; }
            if (s[1] == '/') {                      /* </x> */
                while (*s && *s != '>') s++;
                if (*s) s++;
                break;
            }
            element(c, &s);
            if (c->over) return;
        }
        put8(c, 0x04);                              /* end element */
        *p = s;
    }
    at32(c, size_at, c->pos - size_at - 4);
}

/* One event record; FALSE if it does not fit what is left of the chunk */
static BOOL record(Ck *c, const Event *e)
{
    Sb x = { 0 };
    evt_xml(e, &x, FALSE);
    if (x.oom || !x.p) { evt_free(x.p); return TRUE; }      /* (skipped) */
    DWORD start = c->pos;
    int names_before = c->nnames;
    c->over = FALSE;
    put32(c, 0x00002A2A);
    put32(c, 0);
    ULONGLONG id = e->rec.RecordNumber, t = (ULONGLONG)e->rec.TimeWritten * 10000000ULL + 116444736000000000ULL;
    put(c, &id, 8);
    put(c, &t, 8);
    static const BYTE frag[4] = { 0x0F, 0x01, 0x01, 0x00 };    /* fragment header, binary XML 1.1 */
    put(c, frag, 4);
    const WCHAR *p = x.p;
    element(c, &p);
    put8(c, 0x00);                                  /* end of stream */
    DWORD size = c->pos - start + 4;
    put32(c, size);
    BOOL fit = !c->over;
    if (fit) at32(c, start + 4, size);
    else {                                          /* (taken back: the next chunk) */
        memset(c->b + start, 0, CHUNK - start);
        c->pos = start;
        c->nnames = names_before;
    }
    /* the names point into this event's XML: forget them with it */
    if (fit) c->nnames = names_before;
    evt_free(x.p);
    return fit;
}

static void put64_at(BYTE *b, DWORD off, ULONGLONG v) { memcpy(b + off, &v, 8); }
static void put32_at(BYTE *b, DWORD off, DWORD v) { memcpy(b + off, &v, 4); }

/* Close a chunk: its header and checksums */
static void finish(BYTE *b, DWORD free_at, DWORD last_at, ULONGLONG first, ULONGLONG last)
{
    memcpy(b, "ElfChnk", 8);
    put64_at(b, 8, first);
    put64_at(b, 16, last);
    put64_at(b, 24, first);
    put64_at(b, 32, last);
    put32_at(b, 40, 128);
    put32_at(b, 44, last_at);
    put32_at(b, 48, free_at);
    put32_at(b, 52, crc32(b + CHUNK_HDR, free_at - CHUNK_HDR, 0));
    DWORD crc = crc32(b, 120, 0);
    crc = crc32(b + 128, CHUNK_HDR - 128, crc);
    put32_at(b, 124, crc);
}

BOOL evtx_write(const WCHAR *path, const Event *const *events, DWORD n)
{
    DWORD cap = 4, count = 0;
    BYTE **chunks = evt_alloc(sizeof(BYTE *) * cap);
    if (!chunks) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    Ck c;
    memset(&c, 0, sizeof(c));
    ULONGLONG first = 0, last = 0, next_id = 1;
    DWORD last_at = 0;
    for (DWORD i = 0; i <= n; i++) {
        if (!c.b && i < n) {
            c.b = evt_alloc(CHUNK);
            if (!c.b) break;
            c.pos = CHUNK_HDR;
            c.nnames = 0;
            first = 0;
        }
        DWORD at = c.pos;
        if (i < n && record(&c, events[i])) {
            if (c.pos > at) {
                if (!first) first = events[i]->rec.RecordNumber;
                last = events[i]->rec.RecordNumber;
                last_at = at;
                next_id = last + 1;
            }
            continue;
        }
        if (c.b && c.pos > CHUNK_HDR) {             /* full (or the end): the chunk is done */
            finish(c.b, c.pos, last_at, first, last);
            if (count == cap) {
                BYTE **more = evt_alloc(sizeof(BYTE *) * cap * 2);
                if (!more) break;
                memcpy(more, chunks, sizeof(BYTE *) * cap);
                evt_free(chunks);
                chunks = more;
                cap *= 2;
            }
            chunks[count++] = c.b;
            c.b = NULL;
            if (i < n) i--;                         /* (this event again, in a new chunk) */
        } else if (c.b && i < n) {
            /* an event too big for an empty chunk: left out */
        }
    }
    evt_free(c.b);

    BYTE hdr[4096];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "ElfFile", 8);
    put64_at(hdr, 8, 0);
    put64_at(hdr, 16, count ? count - 1 : 0);
    put64_at(hdr, 24, next_id);
    put32_at(hdr, 32, 128);
    hdr[36] = 1;                                    /* version 3.1 */
    hdr[38] = 3;
    hdr[40] = 0x00; hdr[41] = 0x10;                 /* header block 4096 */
    hdr[42] = (BYTE)count; hdr[43] = (BYTE)(count >> 8);
    put32_at(hdr, 124, crc32(hdr, 120, 0));

    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    BOOL ok = f != INVALID_HANDLE_VALUE;
    DWORD w;
    if (ok) ok = WriteFile(f, hdr, sizeof(hdr), &w, NULL) && w == sizeof(hdr);
    for (DWORD i = 0; ok && i < count; i++) ok = WriteFile(f, chunks[i], CHUNK, &w, NULL) && w == CHUNK;
    DWORD err = GetLastError();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    for (DWORD i = 0; i < count; i++) evt_free(chunks[i]);
    evt_free(chunks);
    if (!ok) SetLastError(err);
    return ok;
}
