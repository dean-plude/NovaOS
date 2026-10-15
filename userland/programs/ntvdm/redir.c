/* redir.c — drive C: for DOS through its network redirector interface
 * (see redir.h).  The structures are MS-DOS 4.00's (v4.0/src/INC/SF.INC,
 * CURDIR.INC, and the swappable data area of DOS/MS_DATA.ASM). */
#include "pcbios.h"

#include <string.h>

/* the swappable data area (INT 21h AX=5D06h's DS:SI) */
#define SDA_DMA 0x00C                           /* DWORD: the disk transfer address */
#define SDA_CURDRV 0x016                        /* BYTE: the current drive (0 = A:) */
#define SDA_SATTRIB 0x24D                       /* BYTE: the attributes a search finds */
#define SDA_WFP 0x292                           /* WORD: offset in DOS's segment of the full name */
#define SDA_REN_WFP 0x294                       /* WORD: the same for RENAME's new name */
#define SDA_USER_SP 0x264                       /* WORD, WORD: the caller's registers (SS:SP) */
#define SDA_EXTOPEN 0x2D4                       /* WORD: INT 21h AH=6Ch's action (DX) */
#define SDA_SAVE_DI 0x2D9                       /* WORD, WORD: INT 21h AH=57h's ES:DI */
#define SDA_SAVE_CX 0x2DF                       /* WORD: and its CX */

/* a system file table entry */
#define SF_REF 0x00
#define SF_MODE 0x02
#define SF_ATTR 0x04
#define SF_FLAGS 0x05
#define SF_DEVPTR 0x07
#define SF_FIRCLUS 0x0B
#define SF_TIME 0x0D
#define SF_DATE 0x0F
#define SF_SIZE 0x11
#define SF_POS 0x15
#define SF_FSDA 0x19                            /* (the file system's own: our file's index + 1) */
#define SF_DIRSEC 0x1B
#define SF_DIRPOS 0x1F
#define SF_NAME 0x20
#define SF_IFS 0x37

/* a current directory structure (88 bytes each, A: first) */
#define CDS_SIZE 0x58
#define CDS_FLAGS 0x43
#define CDS_DEVPTR 0x45
#define CDS_ID 0x49
#define CDS_END 0x4F
#define CDS_IFS 0x52
#define CDS_NET 0xC000                          /* network (DOS 4: installable file system), in use */

#define E_FILE 0x02
#define E_PATH 0x03
#define E_HANDLES 0x04
#define E_ACCESS 0x05
#define E_FUNC 0x01
#define E_NO_MORE 0x12
#define E_EXISTS 0x50

#define A_RO 0x01
#define A_HIDDEN 0x02
#define A_SYSTEM 0x04
#define A_LABEL 0x08
#define A_DIR 0x10

#define AX c->r.w[CPU86_AX]
#define BX c->r.w[CPU86_BX]
#define CX c->r.w[CPU86_CX]
#define DX c->r.w[CPU86_DX]
#define DI c->r.w[CPU86_DI]
#define SI c->r.w[CPU86_SI]

static uint16_t rd16(Cpu86 *c, uint16_t s, uint16_t o) { return cpu86_rd16(c, s, o); }
static uint32_t rd32(Cpu86 *c, uint16_t s, uint16_t o) { return rd16(c, s, o) | (uint32_t)rd16(c, s, (uint16_t)(o + 2)) << 16; }
static void wr32(Cpu86 *c, uint16_t s, uint16_t o, uint32_t x)
{
    cpu86_wr16(c, s, o, (uint16_t)x);
    cpu86_wr16(c, s, (uint16_t)(o + 2), (uint16_t)(x >> 16));
}

static void rd_str(Cpu86 *c, uint16_t s, uint16_t o, char *out, int cap)
{
    int i = 0;
    for (; i < cap - 1; i++) if (!(out[i] = (char)cpu86_rd8(c, s, (uint16_t)(o + i)))) break;
    out[i] = 0;
}

static uint8_t sda8(Vdm *v, uint16_t f) { return cpu86_rd8(&v->cpu, v->redir.sda_seg, (uint16_t)(v->redir.sda_off + f)); }
static uint16_t sda16(Vdm *v, uint16_t f) { return rd16(&v->cpu, v->redir.sda_seg, (uint16_t)(v->redir.sda_off + f)); }

/* the far pointer in the swappable data area at @f */
static void sda_ptr(Vdm *v, uint16_t f, uint16_t *seg, uint16_t *off)
{
    *off = sda16(v, f);
    *seg = sda16(v, (uint16_t)(f + 2));
}

/* the full name DOS works on ("C:\DIR\NAME.EXT"): the first, or RENAME's second */
static void wfp(Vdm *v, int second, char *out, int cap)
{
    rd_str(&v->cpu, v->redir.sda_seg, sda16(v, second ? SDA_REN_WFP : SDA_WFP), out, cap);
}

/* ---- names: 8.3 for DOS, the host's own on the host ---- */

static char upper(char ch) { return ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch; }

static int name_char(unsigned char ch)          /* allowed in a FAT 8.3 name */
{
    return ch > ' ' && ch < 0x7F && !strchr("\"*+,./:;<=>?[\\]|", ch);
}

/* @name as 8.3 ("README.TXT"); 0 if it does not fit as it is */
static int fits_83(const char *name, char out[13])
{
    const char *dot = strrchr(name, '.');
    int base = dot ? (int)(dot - name) : (int)strlen(name), ext = dot ? (int)strlen(dot + 1) : 0;
    if (!strcmp(name, ".") || !strcmp(name, "..")) { strcpy(out, name); return 1; }
    if (base < 1 || base > 8 || ext > 3 || (dot && !ext)) return 0;
    int o = 0;
    for (const char *s = name; *s; s++) {
        if (s == dot) { out[o++] = '.'; continue; }
        if (!name_char((unsigned char)*s)) return 0;
        out[o++] = upper(*s);
    }
    out[o] = 0;
    return 1;
}

/* the alias a name that does not fit gets, before its number: up to six
 * characters of the name and three of the extension ("LONGNA", "TXT") */
static void alias_stem(const char *name, char base[9], char ext[4])
{
    const char *dot = strrchr(name, '.');
    if (dot == name) dot = NULL;                /* (".config": all name) */
    int b = 0, e = 0;
    for (const char *s = name; *s && s != dot && b < 6; s++)
        if (name_char((unsigned char)*s)) base[b++] = upper(*s);
        else if (*s != ' ' && *s != '.') base[b++] = '_';
    if (!b) base[b++] = '_';
    base[b] = 0;
    for (const char *s = dot ? dot + 1 : ""; *s && e < 3; s++)
        if (name_char((unsigned char)*s)) ext[e++] = upper(*s);
        else if (*s != ' ' && *s != '.') ext[e++] = '_';
    ext[e] = 0;
}

/* "NAME.EXT" as a directory entry has it: eleven characters, padded with
 * spaces; '*' fills the rest of its part with '?' */
static void to_fcb(const char *name, char fcb[11])
{
    memset(fcb, ' ', 11);
    if (!strcmp(name, ".") || !strcmp(name, "..")) { memcpy(fcb, name, strlen(name)); return; }
    int i = 0, part = 0;
    for (const char *s = name; *s; s++) {
        if (*s == '.' && !part) { part = 1; i = 8; continue; }
        int end = part ? 11 : 8;
        if (*s == '*') { while (i < end) fcb[i++] = '?'; continue; }
        if (i < end) fcb[i++] = upper(*s);
    }
}

static int fcb_match(const char *pattern, const char *fcb)
{
    for (int i = 0; i < 11; i++) if (pattern[i] != '?' && pattern[i] != fcb[i]) return 0;
    return 1;
}

/* Walking a host directory, each entry with the 8.3 name DOS sees: its
 * own when it fits, else an alias numbered in the order the host lists
 * them ("LONGNA~1.TXT", "LONGNA~2.TXT") */
typedef int (*EntryFn)(void *arg, const VdmFile *f, const char *dos);

typedef struct {
    EntryFn fn;
    void *arg;
    int nstems;
    struct { char stem[12]; int n; } stems[32];   /* (six of the name, a dot, three: "ROGUE-.TXT") */
} Walk;

static int walk_entry(void *arg, const VdmFile *f)
{
    Walk *w = arg;
    char dos[13];
    if (!fits_83(f->name, dos)) {
        char base[9], ext[4], stem[12];
        alias_stem(f->name, base, ext);
        memcpy(stem, base, 7);
        strcat(stem, ".");
        strcat(stem, ext);
        int k = 0, n;
        while (k < w->nstems && strcmp(w->stems[k].stem, stem)) k++;
        if (k < w->nstems) n = ++w->stems[k].n;
        else {
            n = 1;
            if (k < 32) { strcpy(w->stems[k].stem, stem); w->stems[k].n = 1; w->nstems++; }
        }
        char num[8];
        int nl = 0;
        for (int x = n; x; x /= 10) nl++;
        for (int i = nl - 1, x = n; i >= 0; i--, x /= 10) num[i] = (char)('0' + x % 10);
        num[nl] = 0;
        int keep = 8 - 1 - nl;                  /* (the base shortens as the number grows) */
        if (keep > (int)strlen(base)) keep = (int)strlen(base);
        memcpy(dos, base, (size_t)keep);
        dos[keep] = '~';
        memcpy(dos + keep + 1, num, (size_t)nl + 1);
        if (ext[0]) { strcat(dos, "."); strcat(dos, ext); }
    }
    return w->fn(w->arg, f, dos);
}

static int walk(Vdm *v, const char *dir, EntryFn fn, void *arg)
{
    Walk w;
    w.fn = fn;
    w.arg = arg;
    w.nstems = 0;
    return v->redir.fs.list(v->redir.fs.ctx, dir, walk_entry, &w);
}

static void join(char *path, const char *name)
{
    size_t n = strlen(path);
    if (n && n + 1 < VDM_PATH) path[n++] = '\\';
    size_t k = strlen(name);
    if (n + k >= VDM_PATH) k = VDM_PATH - 1 - n;
    memcpy(path + n, name, k);
    path[n + k] = 0;
}

typedef struct { const char *want; int by_host; VdmFile f; int found; char dos[13]; } Find;

static int find_entry(void *arg, const VdmFile *f, const char *dos)
{
    Find *x = arg;
    int hit = 1;
    if (x->by_host) {
        for (const char *a = x->want, *b = f->name; hit && (*a || *b); a++, b++) hit = upper(*a) == upper(*b);
    } else hit = !strcmp(dos, x->want);
    if (!hit) return 0;
    x->f = *f;
    strcpy(x->dos, dos);
    x->found = 1;
    return 1;
}

/* the host path of DOS's full name @name ("C:\DIR\FILE.TXT"); 0, or
 * E_PATH when a directory on the way is not there.  *@missing: the last
 * part is not there (@host then ends in DOS's name for it, to create) */
static int resolve(Vdm *v, const char *name, char *host, int *missing, VdmFile *f)
{
    host[0] = 0;
    *missing = 0;
    if (!name[0] || name[1] != ':' || name[2] != '\\') return E_PATH;
    const char *s = name + 3;
    if (f) {
        memset(f, 0, sizeof(*f));
        f->attr = A_DIR;
    }
    while (*s) {
        char part[13];
        int n = 0;
        while (*s && *s != '\\') { if (n < 12) part[n++] = upper(*s); s++; }
        part[n] = 0;
        while (*s == '\\') s++;
        if (!n) continue;
        Find x;
        memset(&x, 0, sizeof(x));
        x.want = part;
        walk(v, host, find_entry, &x);
        if (!x.found) {
            if (*s) return E_PATH;
            join(host, part);
            *missing = 1;
            return 0;
        }
        if (*s && !(x.f.attr & A_DIR)) return E_PATH;
        join(host, x.f.name);
        if (f) *f = x.f;
    }
    return 0;
}

int vdm_redir_dos_path(Vdm *v, const char *host, char *out, int cap)
{
    char dir[VDM_PATH] = "";
    int o = 0;
    if (cap < 4) return 0;
    out[o++] = 'C'; out[o++] = ':'; out[o++] = '\\';
    out[o] = 0;
    for (const char *s = host; *s;) {
        char part[256];
        int n = 0;
        while (*s && *s != '\\' && *s != '/') { if (n < 255) part[n++] = *s; s++; }
        part[n] = 0;
        while (*s == '\\' || *s == '/') s++;
        if (!n) continue;
        Find x;
        memset(&x, 0, sizeof(x));
        x.want = part;
        x.by_host = 1;
        walk(v, dir, find_entry, &x);
        if (!x.found) return 0;
        int k = (int)strlen(x.dos);
        if (o + k + 2 > cap) return 0;
        if (out[o - 1] != '\\') out[o++] = '\\';
        memcpy(out + o, x.dos, (size_t)k + 1);
        o += k;
        join(dir, x.f.name);
    }
    return 1;
}

/* ---- the calls ---- */

static void dir_entry(Cpu86 *c, uint16_t seg, uint16_t off, const char *dos, const VdmFile *f)
{
    char fcb[11];
    to_fcb(dos, fcb);
    for (int i = 0; i < 11; i++) cpu86_wr8(c, seg, (uint16_t)(off + i), (uint8_t)fcb[i]);
    cpu86_wr8(c, seg, (uint16_t)(off + 11), f->attr);
    for (int i = 12; i < 22; i++) cpu86_wr8(c, seg, (uint16_t)(off + i), 0);
    cpu86_wr16(c, seg, (uint16_t)(off + 22), f->time);
    cpu86_wr16(c, seg, (uint16_t)(off + 24), f->date);
    cpu86_wr16(c, seg, (uint16_t)(off + 26), 0);
    wr32(c, seg, (uint16_t)(off + 28), f->attr & A_DIR ? 0 : f->size);
}

typedef struct {
    Vdm *v;
    char pattern[11];
    uint8_t attr;
    int index, from;                            /* the entry being looked at; the first that may match */
    int hit;
    VdmFile f;
    char dos[13];
} Search;

static int search_entry(void *arg, const VdmFile *f, const char *dos)
{
    Search *s = arg;
    int i = s->index++;
    char fcb[11];
    if (i < s->from) return 0;
    if (f->attr & (A_HIDDEN | A_SYSTEM | A_DIR) & ~s->attr) return 0;
    to_fcb(dos, fcb);
    if (!fcb_match(s->pattern, fcb)) return 0;
    s->f = *f;
    strcpy(s->dos, dos);
    s->hit = i + 1;
    return 1;
}

/* the next entry of search slot @slot from entry @from on: the search
 * data block and the directory entry at the disk transfer address */
static int search(Vdm *v, int slot, const char *pattern, uint8_t attr, int from)
{
    Cpu86 *c = &v->cpu;
    const char *dir = v->redir.search[slot].dir;
    Search s;
    memset(&s, 0, sizeof(s));
    s.v = v;
    memcpy(s.pattern, pattern, 11);
    s.attr = attr;
    s.from = from;
    if (dir[0]) {                               /* "." and "..", as a FAT subdirectory has them */
        static const char *const dots[2] = { ".", ".." };
        for (int i = 0; i < 2 && !s.hit; i++) {
            VdmFile d;
            memset(&d, 0, sizeof(d));
            strcpy(d.name, dots[i]);
            d.attr = A_DIR;
            search_entry(&s, &d, dots[i]);
        }
    }
    if (!s.hit) walk(v, dir, search_entry, &s);
    if (!s.hit) return E_NO_MORE;
    uint16_t seg, off;
    sda_ptr(v, SDA_DMA, &seg, &off);
    cpu86_wr8(c, seg, off, 0x80 | 3);           /* (a network search, drive C:) */
    for (int i = 0; i < 11; i++) cpu86_wr8(c, seg, (uint16_t)(off + 1 + i), (uint8_t)pattern[i]);
    cpu86_wr8(c, seg, (uint16_t)(off + 12), attr);
    cpu86_wr16(c, seg, (uint16_t)(off + 13), (uint16_t)s.hit);
    cpu86_wr8(c, seg, (uint16_t)(off + 15), (uint8_t)slot);
    cpu86_wr16(c, seg, (uint16_t)(off + 16), v->redir.search[slot].tag);
    for (int i = 18; i < 21; i++) cpu86_wr8(c, seg, (uint16_t)(off + i), 0);
    dir_entry(c, seg, (uint16_t)(off + 21), s.dos, &s.f);
    return 0;
}

static int find_first(Vdm *v)
{
    char name[128], host[VDM_PATH];
    wfp(v, 0, name, sizeof(name));
    uint8_t attr = sda8(v, SDA_SATTRIB);
    if (attr == A_LABEL) return E_NO_MORE;      /* (the drive has no label) */
    char *last = strrchr(name, '\\');
    if (!last) return E_PATH;
    char pattern[11];
    to_fcb(last + 1, pattern);
    last[1] = 0;
    int missing;
    VdmFile d;
    int e = resolve(v, name, host, &missing, &d);
    if (e || missing || !(d.attr & A_DIR)) return E_PATH;
    VdmRedir *r = &v->redir;
    int slot = 0;
    for (int i = 1; i < VDM_SEARCHES; i++)       /* (the oldest slot) */
        if ((uint16_t)(r->search_tag - r->search[i].tag) > (uint16_t)(r->search_tag - r->search[slot].tag)) slot = i;
    strcpy(r->search[slot].dir, host);
    r->search[slot].tag = ++r->search_tag;
    return search(v, slot, pattern, attr, 0);
}

static int find_next(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    uint16_t seg, off;
    sda_ptr(v, SDA_DMA, &seg, &off);
    int slot = cpu86_rd8(c, seg, (uint16_t)(off + 15));
    if (slot >= VDM_SEARCHES || v->redir.search[slot].tag != rd16(c, seg, (uint16_t)(off + 16))) return E_NO_MORE;
    char pattern[11];
    for (int i = 0; i < 11; i++) pattern[i] = (char)cpu86_rd8(c, seg, (uint16_t)(off + 1 + i));
    return search(v, slot, pattern, cpu86_rd8(c, seg, (uint16_t)(off + 12)), rd16(c, seg, (uint16_t)(off + 13)));
}

/* the SFT ES:DI made into an open file of ours */
static void fill_sft(Vdm *v, int idx, const char *dos_name, const VdmFile *f)
{
    Cpu86 *c = &v->cpu;
    uint16_t s = c->sreg[CPU86_ES], o = DI;
    char fcb[11];
    to_fcb(dos_name, fcb);
    cpu86_wr8(c, s, (uint16_t)(o + SF_ATTR), f->attr);
    cpu86_wr16(c, s, (uint16_t)(o + SF_FLAGS), 0x8042);     /* remote, clean, drive C: */
    wr32(c, s, (uint16_t)(o + SF_DEVPTR), (uint32_t)0xF000 << 16 | VDM_IFS_AT);
    cpu86_wr16(c, s, (uint16_t)(o + SF_FIRCLUS), 0);
    cpu86_wr16(c, s, (uint16_t)(o + SF_TIME), f->time);
    cpu86_wr16(c, s, (uint16_t)(o + SF_DATE), f->date);
    wr32(c, s, (uint16_t)(o + SF_SIZE), f->size);
    wr32(c, s, (uint16_t)(o + SF_POS), 0);
    cpu86_wr16(c, s, (uint16_t)(o + SF_FSDA), (uint16_t)(idx + 1));
    wr32(c, s, (uint16_t)(o + SF_DIRSEC), 0);
    cpu86_wr8(c, s, (uint16_t)(o + SF_DIRPOS), 0);
    for (int i = 0; i < 11; i++) cpu86_wr8(c, s, (uint16_t)(o + SF_NAME + i), (uint8_t)fcb[i]);
    wr32(c, s, (uint16_t)(o + SF_IFS), (uint32_t)0xF000 << 16 | VDM_IFS_AT);
}

/* open (or create, by @how) DOS's file into the SFT at ES:DI */
static int open_file(Vdm *v, const char *name, const char *host, int how, uint8_t attr)
{
    VdmRedir *r = &v->redir;
    int idx = 0;
    while (idx < VDM_FILES && r->files[idx]) idx++;
    if (idx == VDM_FILES) return E_HANDLES;
    void *h = NULL;
    int e = r->fs.open(r->fs.ctx, host, how, &h);
    if (e) return e;
    r->files[idx] = h;
    VdmFile f;
    memset(&f, 0, sizeof(f));
    if (r->fs.stat(r->fs.ctx, host, &f)) f.attr = attr;
    if (how & (VDMFS_CREATE | VDMFS_NEW)) { f.size = 0; f.attr = (uint8_t)(attr | 0x20); }
    const char *base = strrchr(name, '\\');
    fill_sft(v, idx, base ? base + 1 : name, &f);
    return 0;
}

/* the file of the SFT at ES:DI; NULL if it is not ours */
static void **sft_file(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    int idx = rd16(c, c->sreg[CPU86_ES], (uint16_t)(DI + SF_FSDA)) - 1;
    return idx >= 0 && idx < VDM_FILES && v->redir.files[idx] ? &v->redir.files[idx] : NULL;
}

static int open_call(Vdm *v, int create, int ext)
{
    Cpu86 *c = &v->cpu;
    char name[128], host[VDM_PATH];
    wfp(v, 0, name, sizeof(name));
    uint16_t param = rd16(c, c->sreg[CPU86_SS], (uint16_t)(c->r.w[CPU86_SP] + 6));
    int mode = cpu86_rd8(c, c->sreg[CPU86_ES], (uint16_t)(DI + SF_MODE)) & 3;
    if (mode == 3) return E_ACCESS;
    int missing;
    VdmFile f;
    int e = resolve(v, name, host, &missing, &f);
    if (e) return e;
    if (!missing && (f.attr & A_DIR)) return E_ACCESS;
    if (ext) {                                  /* INT 21h AH=6Ch: what to do comes in DX */
        uint16_t action = sda16(v, SDA_EXTOPEN);
        uint8_t attr = (uint8_t)param;
        if (!missing) {
            if ((action & 0x0F) == 0) return E_EXISTS;
            if ((action & 0x0F) == 2) {
                if (f.attr & A_RO) return E_ACCESS;
                e = open_file(v, name, host, VDMFS_RW | VDMFS_CREATE, attr);
                if (!e) CX = 3;                 /* replaced */
                return e;
            }
            if ((f.attr & A_RO) && mode) return E_ACCESS;
            e = open_file(v, name, host, mode, f.attr);
            if (!e) CX = 1;                     /* opened */
            return e;
        }
        if ((action & 0xF0) != 0x10) return E_FILE;
        if (attr & (A_DIR | A_LABEL)) return E_ACCESS;
        e = open_file(v, name, host, VDMFS_RW | VDMFS_NEW, attr);
        if (!e) CX = 2;                         /* created */
        return e;
    }
    if (create) {
        uint8_t attr = (uint8_t)param;
        if (attr & (A_DIR | A_LABEL)) return E_ACCESS;
        if (!missing && (f.attr & A_RO)) return E_ACCESS;
        return open_file(v, name, host, VDMFS_RW | VDMFS_CREATE, attr);
    }
    if (missing) return E_FILE;
    if ((f.attr & A_RO) && mode) return E_ACCESS;
    return open_file(v, name, host, mode, f.attr);
}

static int close_call(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    uint16_t s = c->sreg[CPU86_ES];
    uint16_t ref = rd16(c, s, (uint16_t)(DI + SF_REF));
    if (ref && ref != 0xFFFF) cpu86_wr16(c, s, (uint16_t)(DI + SF_REF), --ref);
    void **h = sft_file(v);
    if (!h) return 0;
    if (!ref) {
        v->redir.fs.close(v->redir.fs.ctx, *h);
        *h = NULL;
        cpu86_wr16(c, s, (uint16_t)(DI + SF_FSDA), 0);
    }
    return 0;
}

static uint8_t io_buf[65536];

static int read_call(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    uint16_t s = c->sreg[CPU86_ES];
    void **h = sft_file(v);
    if (!h) return E_ACCESS;
    if ((cpu86_rd8(c, s, (uint16_t)(DI + SF_MODE)) & 3) == VDMFS_WRITE) return E_ACCESS;
    uint32_t pos = rd32(c, s, (uint16_t)(DI + SF_POS)), got = 0;
    int e = v->redir.fs.read(v->redir.fs.ctx, *h, pos, io_buf, CX, &got);
    if (e) return e;
    uint16_t seg, off;
    sda_ptr(v, SDA_DMA, &seg, &off);
    for (uint32_t i = 0; i < got; i++) cpu86_wr8(c, seg, (uint16_t)(off + i), io_buf[i]);
    wr32(c, s, (uint16_t)(DI + SF_POS), pos + got);
    CX = (uint16_t)got;
    return 0;
}

static int write_call(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    uint16_t s = c->sreg[CPU86_ES];
    void **h = sft_file(v);
    if (!h) return E_ACCESS;
    if ((cpu86_rd8(c, s, (uint16_t)(DI + SF_MODE)) & 3) == VDMFS_READ) return E_ACCESS;
    uint32_t pos = rd32(c, s, (uint16_t)(DI + SF_POS)), size = rd32(c, s, (uint16_t)(DI + SF_SIZE));
    uint16_t seg, off;
    sda_ptr(v, SDA_DMA, &seg, &off);
    for (uint32_t i = 0; i < CX; i++) io_buf[i] = cpu86_rd8(c, seg, (uint16_t)(off + i));
    int e = v->redir.fs.write(v->redir.fs.ctx, *h, pos, io_buf, CX);
    if (e) return e;
    if (!CX) size = pos;                        /* (a write of nothing: the file ends here) */
    pos += CX;
    if (pos > size) size = pos;
    wr32(c, s, (uint16_t)(DI + SF_POS), pos);
    wr32(c, s, (uint16_t)(DI + SF_SIZE), size);
    return 0;
}

#define DELETE_MAX 128

typedef struct { char pattern[11]; int n, err; char names[DELETE_MAX][256]; } Delete;

static int delete_entry(void *arg, const VdmFile *f, const char *dos)
{
    Delete *d = arg;
    char fcb[11];
    to_fcb(dos, fcb);
    if ((f->attr & (A_DIR | A_HIDDEN | A_SYSTEM)) || !fcb_match(d->pattern, fcb)) return 0;
    if (f->attr & A_RO) { d->err = E_ACCESS; return 0; }
    if (d->n < DELETE_MAX) strcpy(d->names[d->n++], f->name);
    return 0;
}

/* (the names first, then the files: the listing would change under the walk) */
static int delete_call(Vdm *v)
{
    char name[128], dir[VDM_PATH];
    wfp(v, 0, name, sizeof(name));
    char *last = strrchr(name, '\\');
    if (!last) return E_PATH;
    static Delete d;
    memset(&d, 0, sizeof(d));
    to_fcb(last + 1, d.pattern);
    last[1] = 0;
    int missing;
    VdmFile f;
    if (resolve(v, name, dir, &missing, &f) || missing || !(f.attr & A_DIR)) return E_PATH;
    walk(v, dir, delete_entry, &d);
    int done = 0;
    for (int i = 0; i < d.n; i++) {
        char host[VDM_PATH];
        strcpy(host, dir);
        join(host, d.names[i]);
        int e = v->redir.fs.remove(v->redir.fs.ctx, host);
        if (e) d.err = e;
        else done++;
    }
    return done ? 0 : d.err ? d.err : E_FILE;
}

static int path_call(Vdm *v, int what)
{
    Cpu86 *c = &v->cpu;
    char name[128], host[VDM_PATH];
    wfp(v, 0, name, sizeof(name));
    int missing;
    VdmFile f;
    int e = resolve(v, name, host, &missing, &f);
    if (e) return e;
    VdmRedir *r = &v->redir;
    switch (what) {
    case 0x01:                                  /* remove directory */
        if (missing || !(f.attr & A_DIR) || !host[0]) return E_PATH;
        return r->fs.rmdir(r->fs.ctx, host) ? E_ACCESS : 0;
    case 0x03:                                  /* make directory */
        if (!missing) return E_ACCESS;
        return r->fs.mkdir(r->fs.ctx, host) ? E_ACCESS : 0;
    case 0x05:                                  /* change directory: is it one? */
        return missing || !(f.attr & A_DIR) ? E_PATH : 0;
    case 0x0E:                                  /* set attributes (the host keeps its own) */
        return missing ? E_FILE : 0;
    case 0x0F:                                  /* get attributes */
        if (missing) return E_FILE;
        AX = f.attr;
        BX = (uint16_t)(f.size >> 16);
        DI = (uint16_t)f.size;
        CX = f.time;
        DX = f.date;
        return 0;
    case 0x11: {                                /* rename */
        char to[128], to_host[VDM_PATH];
        if (missing) return E_FILE;
        if (strchr(name, '?') || !host[0]) return E_ACCESS;
        wfp(v, 1, to, sizeof(to));
        int to_missing;
        e = resolve(v, to, to_host, &to_missing, NULL);
        if (e) return e;
        if (!to_missing) return E_ACCESS;
        return r->fs.rename(r->fs.ctx, host, to_host) ? E_ACCESS : 0;
    }
    }
    return E_FUNC;
}

static int seek_end_call(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    uint16_t s = c->sreg[CPU86_ES];
    if (!sft_file(v)) return E_ACCESS;
    int32_t at = (int32_t)(rd32(c, s, (uint16_t)(DI + SF_SIZE)) + ((uint32_t)CX << 16 | DX));
    if (at < 0) return 0x19;                    /* seek error */
    wr32(c, s, (uint16_t)(DI + SF_POS), (uint32_t)at);
    AX = (uint16_t)at;
    DX = (uint16_t)((uint32_t)at >> 16);
    return 0;
}

/* INT 21h AX=5702h-5704h, a file's extended attributes: as DOS 4 does for
 * its own disks, none (an empty list, 2 bytes) */
static int xa_call(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    int type = c->r.b[6];                       /* (BL) */
    if (type != 2 && type != 3) return 0;
    uint16_t cx = sda16(v, SDA_SAVE_CX);
    if (cx == 1) return 0;
    if (cx) cpu86_wr16(c, sda16(v, SDA_SAVE_DI + 2), sda16(v, SDA_SAVE_DI), 0);
    cpu86_wr16(c, sda16(v, SDA_USER_SP + 2), (uint16_t)(sda16(v, SDA_USER_SP) + 4), 2);     /* (the caller's CX) */
    return 0;
}

static int space_call(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    uint64_t total = 0, avail = 0;
    v->redir.fs.space(v->redir.fs.ctx, &total, &avail);
    uint64_t clusters = total / 32768, free = avail / 32768;
    if (clusters > 0xFFFE) clusters = 0xFFFE;   /* (DOS's numbers are 16 bits: 2 GB at most) */
    if (free > clusters) free = clusters;
    AX = 0xF840;                                /* 64 sectors a cluster, a hard disk */
    BX = (uint16_t)clusters;
    CX = 512;
    DX = (uint16_t)free;
    return 0;
}

int vdm_redir_call(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    if (!v->redir.hooked) return 0;
    int e;
    switch (c->r.b[0]) {
    case 0x00: c->r.b[0] = 0xFF; return 1;      /* installed */
    case 0x01: case 0x03: case 0x05: case 0x0E: case 0x0F: case 0x11:
        e = path_call(v, c->r.b[0]); break;
    case 0x06: e = close_call(v); break;
    case 0x07: case 0x0A: case 0x0B: case 0x1D: case 0x20: case 0x22:
        e = 0; break;                           /* commit, lock, unlock, end of process, flush: nothing to do */
    case 0x08: e = read_call(v); break;
    case 0x09: e = write_call(v); break;
    case 0x0C: e = space_call(v); break;
    case 0x13: e = delete_call(v); break;
    case 0x16: e = open_call(v, 0, 0); break;
    case 0x17: e = open_call(v, 1, 0); break;
    case 0x1B: e = find_first(v); break;
    case 0x1C: e = find_next(v); break;
    case 0x21: e = seek_end_call(v); break;
    case 0x2D: e = xa_call(v); break;
    case 0x2E: e = open_call(v, 0, 1); break;
    default: return 0;                          /* (names without a drive, printers: DOS's) */
    }
    if (e) AX = (uint16_t)e;
    vdm_iret_flag(v, CPU86_CF, e != 0);
    return 1;
}

int vdm_redir_install(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    VdmRedir *r = &v->redir;
    uint16_t lol = c->sreg[CPU86_ES], lo = BX;
    r->sda_seg = c->sreg[CPU86_DS];
    r->sda_off = SI;
    if (!r->on || cpu86_rd8(c, lol, (uint16_t)(lo + 0x21)) < 3) return 0;   /* (LASTDRIVE below C) */
    uint32_t cds = rd32(c, lol, (uint16_t)(lo + 0x16));
    uint16_t s = (uint16_t)(cds >> 16), o = (uint16_t)(cds + 2 * CDS_SIZE);
    const char *dir = r->cwd[0] ? r->cwd : "C:\\";
    for (int i = 0; i < CDS_FLAGS; i++)
        cpu86_wr8(c, s, (uint16_t)(o + i), (uint8_t)(i < (int)strlen(dir) ? dir[i] : 0));
    cpu86_wr16(c, s, (uint16_t)(o + CDS_FLAGS), CDS_NET);
    wr32(c, s, (uint16_t)(o + CDS_DEVPTR), 0);
    wr32(c, s, (uint16_t)(o + CDS_ID), 0xFFFFFFFF);
    cpu86_wr16(c, s, (uint16_t)(o + CDS_ID + 4), 0);
    cpu86_wr16(c, s, (uint16_t)(o + CDS_END), 2);       /* ("C:" can not be backed out of) */
    cpu86_wr8(c, s, (uint16_t)(o + CDS_END + 2), 0);
    wr32(c, s, (uint16_t)(o + CDS_IFS), (uint32_t)0xF000 << 16 | VDM_IFS_AT);
    cpu86_wr16(c, s, (uint16_t)(o + CDS_IFS + 4), 0);
    r->hooked = 1;
    return 1;
}

void vdm_drive_c(Vdm *v, const VdmFs *fs, const char *cwd)
{
    VdmRedir *r = &v->redir;
    r->fs = *fs;
    r->on = 1;
    char dos[68];
    if (cwd && vdm_redir_dos_path(v, cwd, dos, (int)sizeof(dos))) strcpy(r->cwd, dos);
}

int vdm_run_program(Vdm *v, const char *program, const char *args)
{
    VdmRedir *r = &v->redir;
    if (!r->on || !vdm_redir_dos_path(v, program, r->run, (int)sizeof(r->run))) return 0;
    size_t n = strlen(args);
    if (n >= sizeof(r->args)) n = sizeof(r->args) - 1;
    memcpy(r->args, args, n);
    r->args[n] = 0;
    return 1;
}
