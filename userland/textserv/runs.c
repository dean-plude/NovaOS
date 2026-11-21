/*
 * runs.c — rich text's character formats: each character of a rich text
 * (TXTBIT_RICHTEXT, a XAML RichEditBox's) has a format, an index into a
 * small table of the formats in use (TS.fmts).  A format is what it sets
 * over the default character format (the host's, or EM_SETCHARFORMAT's
 * SCF_DEFAULT): bold, italic, underline, strikethrough and the text
 * colour, so a change of the default shows through wherever a format does
 * not set that property.  Text put in takes the format of the text it
 * replaces, or of the character before it, or the one EM_SETCHARFORMAT
 * gave the caret (insfmt) until the caret moves, as Rich Edit's does.
 *
 * Plain text (a TextBox's) has one format, the default, and a change to
 * the selection's format changes it.  A rich text's characters can have a
 * face and a size of their own (XAML text input, part 9): each distinct
 * face, size, bold and italic is a font key (fmt_key; 0 the default's), a
 * font of GDI's or the host's DirectWrite face (dwtext.c), and a line is
 * as tall as its tallest font, its runs sharing one baseline (textserv.c's
 * layout).  Underline and strikethrough are bars drawn under and through
 * the text.  Written from the Rich Edit documentation (CHARFORMAT2,
 * EM_SETCHARFORMAT, EM_GETCHARFORMAT).
 */
#include "tsint.h"

/* ---- the table ---------------------------------------------------------- */
BOOL fmt_reserve(TS *t, LONG cap)
{
    BYTE *f = alloc(cap);
    if (!f) return FALSE;
    if (t->fmt) { memcpy(f, t->fmt, t->len); release(t->fmt); }
    t->fmt = f;
    return TRUE;
}

int fmt_at(TS *t, LONG cp)
{
    /* (plain text shows one format: the formats a rich text had are not
     * drawn once the host turns TXTBIT_RICHTEXT off) */
    return (RICH(t) && t->fmt && cp >= 0 && cp < t->len) ? t->fmt[cp] : 0;
}

/* the format whose settings are @f's: found, or added (0, the default,
 * when the table is full) */
static int intern(TS *t, const RUNFMT *f)
{
    int i;
    RUNFMT g;
    memset(&g, 0, sizeof(g));                     /* (padding and the face's tail: compared whole) */
    g.mask = f->mask & FMT_MASK;
    g.effects = f->effects & g.mask;
    g.color = f->color;
    g.align = f->align;
    g.ind.set = f->ind.set;
    memcpy(g.ind.v, f->ind.v, sizeof(g.ind.v));
    if (!(g.mask & CFM_COLOR) || (g.effects & CFE_AUTOCOLOR)) g.color = 0;
    if (g.mask & CFM_SIZE) g.size = f->size > 0 ? f->size : 1;
    if (g.mask & CFM_FACE)
        for (i = 0; i < 31 && f->face[i]; i++) g.face[i] = f->face[i];
    for (i = 0; i < 3; i++) if (!(g.ind.set & (1 << i))) g.ind.v[i] = 0;
    if (!g.mask && !g.align && !g.ind.set) return 0;
    if (!t->nfmts) t->nfmts = 1;                  /* (fmts[0]: the default) */
    for (i = 1; i < t->nfmts; i++)
        if (!memcmp(&t->fmts[i], &g, sizeof(g))) return i;
    if (t->nfmts >= MAXFMT) return 0;
    t->fmts[t->nfmts] = g;
    t->keyof[t->nfmts] = 0;
    return t->nfmts++;
}

/* format @idx with @cf's properties (its dwMask's) set */
static int merge_cf(TS *t, int idx, const CHARFORMATW *cf)
{
    RUNFMT f = t->fmts[idx];
    DWORD mask = cf->dwMask & FMT_MASK;
    if (!idx) memset(&f, 0, sizeof(f));
    f.mask |= mask;
    f.effects = (f.effects & ~mask) | (cf->dwEffects & mask);
    if (mask & CFM_COLOR) f.color = cf->crTextColor;
    if (mask & CFM_SIZE) f.size = cf->yHeight;
    if (mask & CFM_FACE) { memset(f.face, 0, sizeof(f.face)); memcpy(f.face, cf->szFaceName, 31 * sizeof(WCHAR)); }
    return intern(t, &f);
}

int fmt_insertion(TS *t, LONG a, LONG b)
{
    if (!RICH(t)) return 0;
    if (t->insfmt >= 0 && a == selmin(t)) return t->insfmt;
    if (a < b) return fmt_at(t, a);
    if (a > 0) return fmt_at(t, a - 1);
    return fmt_at(t, b);
}

/* ---- a format's values -------------------------------------------------- */
static DWORD default_effects(TS *t)
{
    DWORD e = t->cf.cf.dwEffects & (CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT);
    if (t->cf.cf.cbSize >= sizeof(CHARFORMAT2W) && t->cf.wWeight >= 600) e |= CFE_BOLD;
    if ((t->cf.cf.dwEffects & CFE_AUTOCOLOR) || !(t->cf.cf.dwMask & CFM_COLOR)) e |= CFE_AUTOCOLOR;
    return e;
}

DWORD fmt_effects(TS *t, int idx)
{
    DWORD d = default_effects(t);
    const RUNFMT *f = &t->fmts[idx];
    if (!idx || idx >= t->nfmts) return d;
    return (d & ~f->mask) | (f->effects & f->mask);
}

COLORREF fmt_color(TS *t, int idx, COLORREF autocolor)
{
    const RUNFMT *f = &t->fmts[idx];
    if (idx && idx < t->nfmts && (f->mask & CFM_COLOR)) return (f->effects & CFE_AUTOCOLOR) ? autocolor : f->color;
    return (default_effects(t) & CFE_AUTOCOLOR) ? autocolor : t->cf.cf.crTextColor;
}

LONG fmt_size(TS *t, int idx)
{
    const RUNFMT *f = &t->fmts[idx];
    if (idx && idx < t->nfmts && (f->mask & CFM_SIZE)) return f->size;
    return t->cf.cf.yHeight;
}

const WCHAR *fmt_face(TS *t, int idx)
{
    const RUNFMT *f = &t->fmts[idx];
    if (idx && idx < t->nfmts && (f->mask & CFM_FACE) && f->face[0]) return f->face;
    return t->cf.cf.szFaceName;
}

static BOOL same_face(const WCHAR *a, const WCHAR *b)
{
    int i;
    for (i = 0; i < 32; i++) {
        WCHAR x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return FALSE;
        if (!x) return TRUE;
    }
    return TRUE;
}

/* the font a format's characters are drawn in: its key in the fonts in use
 * (TS.fkeys), found or added; key 0 is the default format's own font (its
 * weight may be other than bold's), and a format with the default's
 * bold, italic, size and face has it.  When the table is full, the
 * default's. */
int fmt_key(TS *t, int idx)
{
    DWORD e;
    BYTE bi;
    LONG size;
    const WCHAR *face;
    int k;
    if (idx < 0 || idx >= MAXFMT) idx = 0;
    if (t->keyof[idx]) return t->keyof[idx] - 1;
    e = fmt_effects(t, idx);
    bi = (BYTE)(((e & CFE_BOLD) ? 1 : 0) | ((e & CFE_ITALIC) ? 2 : 0));
    size = fmt_size(t, idx);
    face = fmt_face(t, idx);
    if (!t->nfkeys) {                             /* (key 0: the default's) */
        DWORD d = fmt_effects(t, 0);
        memset(&t->fkeys[0], 0, sizeof(t->fkeys[0]));
        t->fkeys[0].bi = (BYTE)(((d & CFE_BOLD) ? 1 : 0) | ((d & CFE_ITALIC) ? 2 : 0));
        t->fkeys[0].size = t->cf.cf.yHeight;
        memcpy(t->fkeys[0].face, t->cf.cf.szFaceName, 31 * sizeof(WCHAR));
        t->nfkeys = 1;
    }
    for (k = 0; k < t->nfkeys; k++)
        if (t->fkeys[k].bi == bi && t->fkeys[k].size == size && same_face(t->fkeys[k].face, face)) break;
    if (k == t->nfkeys) {
        if (k == MAXFONTS) k = 0;
        else {
            memset(&t->fkeys[k], 0, sizeof(t->fkeys[k]));
            t->fkeys[k].bi = bi;
            t->fkeys[k].size = size;
            memcpy(t->fkeys[k].face, face, 31 * sizeof(WCHAR));
            t->nfkeys++;
        }
    }
    t->keyof[idx] = (BYTE)(k + 1);
    return k;
}

/* a font key's height and ascent in pixels: the host's DirectWrite face's
 * (dwtext.c), or GDI's font's on @dc (or a screen one), measured once */
void fmt_metrics(TS *t, HDC dc, int key, LONG *h, LONG *asc)
{
    FONTKEY *k = &t->fkeys[key];
    if (key <= 0 || key >= t->nfkeys) { *h = t->lineh > 0 ? t->lineh : 16; *asc = t->lineh > 0 ? t->ascent : 12; return; }
    if (!k->h) {
        if (!dw_metrics(t, key, &k->h, &k->asc)) {
            TEXTMETRICW tm;
            HDC own = 0;
            HGDIOBJ old;
            if (!dc) dc = own = CreateCompatibleDC(0);
            old = SelectObject(dc, fmt_font(t, key));
            memset(&tm, 0, sizeof(tm));
            GetTextMetricsW(dc, &tm);
            SelectObject(dc, old);
            if (own) DeleteDC(own);
            k->h = tm.tmHeight > 0 ? tm.tmHeight : 16;
            k->asc = tm.tmAscent > 0 ? tm.tmAscent : 12;
        }
        if (k->h <= 0) k->h = 1;
    }
    *h = k->h;
    *asc = k->asc;
}

/* ---- changing and reading formats --------------------------------------- */
BOOL fmt_apply(TS *t, LONG a, LONG b, DWORD mask, DWORD effects, COLORREF color)
{
    CHARFORMATW cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = mask & ~(CFM_SIZE | CFM_FACE);
    cf.dwEffects = effects;
    cf.crTextColor = color;
    return fmt_apply_cf(t, a, b, &cf);
}

BOOL fmt_apply_cf(TS *t, LONG a, LONG b, const CHARFORMATW *cf)
{
    LONG i;
    int from = -1, to = 0;
    DWORD mask = cf->dwMask & FMT_MASK;
    if (!mask) return TRUE;
    if ((mask & CFM_SIZE) && cf->yHeight <= 0) return FALSE;
    if (a > b) { LONG x = a; a = b; b = x; }
    if (a < 0) a = 0;
    if (b > t->len) b = t->len;
    if (!RICH(t)) {                               /* (plain text: the one format, the default) */
        CHARFORMAT2W d;
        memset(&d, 0, sizeof(d));
        memcpy(&d, cf, sizeof(CHARFORMATW));
        d.cf.cbSize = sizeof(d);
        d.cf.dwMask = mask;
        set_default_format(t, &d.cf);
        return TRUE;
    }
    if (a == b) {                                 /* (the caret's: what typing there takes) */
        t->insfmt = merge_cf(t, fmt_insertion(t, a, b), cf);
        return TRUE;
    }
    if (!t->fmt) return FALSE;
    t->typing = FALSE;
    save_undo(t);
    for (i = a; i < b; i++) {
        if (t->fmt[i] != from) { from = t->fmt[i]; to = merge_cf(t, from, cf); }
        t->fmt[i] = (BYTE)to;
    }
    t->dirty = TRUE;
    t->modified = TRUE;
    redraw(t);
    return TRUE;
}

void fmt_query(TS *t, LONG a, LONG b, CHARFORMAT2W *out)
{
    DWORD e, same = FMT_MASK;
    COLORREF c;
    LONG i;
    int first;
    if (a > b) { LONG x = a; a = b; b = x; }
    if (a < 0) a = 0;
    if (b > t->len) b = t->len;
    memcpy(out, &t->cf, sizeof(*out));
    out->cf.cbSize = sizeof(*out);
    first = (a == b || !RICH(t)) ? fmt_insertion(t, a, b) : fmt_at(t, a);
    e = fmt_effects(t, first);
    c = fmt_color(t, first, 0);
    same |= CFM_SIZE | CFM_FACE;
    if (RICH(t)) {
        int prev = first;
        for (i = a + 1; i < b && same; i++) {
            int k = fmt_at(t, i);
            DWORD ek;
            if (k == prev) continue;
            prev = k;
            ek = fmt_effects(t, k);
            same &= ~((e ^ ek) & (CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT));
            if (((e ^ ek) & CFE_AUTOCOLOR) || (!(e & CFE_AUTOCOLOR) && fmt_color(t, k, 0) != c)) same &= ~CFM_COLOR;
            if (fmt_size(t, k) != fmt_size(t, first)) same &= ~CFM_SIZE;
            if (!same_face(fmt_face(t, k), fmt_face(t, first))) same &= ~CFM_FACE;
        }
    }
    out->cf.yHeight = fmt_size(t, first);
    memcpy(out->cf.szFaceName, fmt_face(t, first), sizeof(out->cf.szFaceName));
    out->cf.szFaceName[31] = 0;
    if (!(same & CFM_FACE)) out->cf.szFaceName[0] = 0;
    out->cf.dwMask = same;
    out->cf.dwEffects = (out->cf.dwEffects & ~(CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT | CFE_AUTOCOLOR)) | (e & same);
    out->cf.crTextColor = (e & CFE_AUTOCOLOR) ? 0 : c;
    if (out->wWeight || (same & CFM_BOLD)) out->wWeight = (e & CFE_BOLD) ? 700 : 400;
}

BOOL fmt_toggle(TS *t, DWORD effect)
{
    CHARFORMAT2W cf;
    BOOL on;
    fmt_query(t, selmin(t), selmax(t), &cf);
    on = (cf.cf.dwMask & effect) && (cf.cf.dwEffects & effect);
    return fmt_apply(t, selmin(t), selmax(t), effect, on ? 0 : effect, 0);
}

/* ---- paragraphs' alignment ---------------------------------------------
 * A paragraph's format is its mark's, as Rich Edit's is: the '\r' that ends
 * it carries the alignment in its character format (RUNFMT.align), so a
 * paragraph split by Enter keeps it on both sides and two paragraphs
 * joined take the later one's.  The final paragraph has no mark in the
 * text: its alignment is lastalign.  Plain text has one paragraph format,
 * lastalign, for every paragraph.  0 everywhere means the default's
 * (defalign: the host's TxGetParaFormat, or EM_SETPARAFORMAT
 * SPF_SETDEFAULT, which XAML's TextBox.TextAlignment sends).  Justified
 * paragraphs draw left-aligned. */
static LONG mark_of(TS *t, LONG cp)
{
    if (cp < 0) cp = 0;
    while (cp < t->len && t->text[cp] != L'\r') cp++;
    return cp;
}

static int own_at_mark(TS *t, LONG e)
{
    if (RICH(t) && e < t->len) {
        int idx = fmt_at(t, e);
        return (idx && idx < t->nfmts) ? t->fmts[idx].align : 0;
    }
    return t->lastalign;
}

int para_own(TS *t, LONG cp) { return own_at_mark(t, mark_of(t, cp)); }

int para_align(TS *t, LONG cp)
{
    int a = para_own(t, cp);
    return a ? a : (t->defalign ? t->defalign : PFA_LEFT);
}

int fmt_with_align(TS *t, int idx, int align)
{
    RUNFMT f = t->fmts[idx];
    if (!idx || idx >= t->nfmts) memset(&f, 0, sizeof(f));
    if (f.align == align) return idx;
    f.align = align;
    return intern(t, &f);
}

/* calls @fn on the mark of each paragraph [a, b) touches (one, when a == b) */
static void each_para(TS *t, LONG a, LONG b, void (*fn)(TS *, LONG, void *), void *ctx)
{
    LONG e;
    if (a > b) { LONG x = a; a = b; b = x; }
    if (a < 0) a = 0;
    if (b > t->len) b = t->len;
    for (e = mark_of(t, a);; e = mark_of(t, e + 1)) {
        fn(t, e, ctx);
        if (e >= t->len || e + 1 >= b) break;
    }
}

typedef struct { int first; BOOL same; } PQ;
static void query_one(TS *t, LONG e, void *ctx)
{
    PQ *q = ctx;
    int a = own_at_mark(t, e);
    if (!a) a = t->defalign ? t->defalign : PFA_LEFT;
    if (!q->first) q->first = a;
    else if (q->first != a) q->same = FALSE;
}

int para_query(TS *t, LONG a, LONG b)
{
    PQ q = { 0, TRUE };
    if (!RICH(t)) return para_align(t, 0);
    each_para(t, a, b, query_one, &q);
    return q.same ? q.first : 0;
}

static void apply_one(TS *t, LONG e, void *ctx)
{
    int align = *(int *)ctx;
    if (e < t->len) t->fmt[e] = (BYTE)fmt_with_align(t, t->fmt[e], align);
    else t->lastalign = align;
}

BOOL para_apply(TS *t, LONG a, LONG b, int align)
{
    if (align < PFA_LEFT || align > PFA_JUSTIFY) return FALSE;
    if (para_query(t, a, b) == align) return TRUE;
    t->typing = FALSE;
    save_undo(t);
    if (!RICH(t) || !t->fmt) t->lastalign = align;   /* (plain text: the one paragraph format) */
    else each_para(t, a, b, apply_one, &align);
    t->dirty = TRUE;
    t->modified = TRUE;
    redraw(t);
    return TRUE;
}

void para_refresh(TS *t)
{
    const PARAFORMAT *pf = 0;
    if (t->host && HOST(t)->TxGetParaFormat(t->host, &pf) == S_OK && pf) {
        if ((pf->dwMask & PFM_ALIGNMENT) && pf->wAlignment >= PFA_LEFT && pf->wAlignment <= PFA_JUSTIFY)
            t->defalign = pf->wAlignment;
        if (pf->dwMask & PFM_STARTINDENT) t->defind[IND_START] = pf->dxStartIndent;
        if (pf->dwMask & PFM_RIGHTINDENT) t->defind[IND_RIGHT] = pf->dxRightIndent;
        if (pf->dwMask & PFM_OFFSET) t->defind[IND_OFFSET] = pf->dxOffset;
    }
    if (!RICH(t)) {                               /* (plain text's one paragraph format is the default) */
        t->lastalign = 0;
        memset(&t->lastind, 0, sizeof(t->lastind));
    }
    t->dirty = TRUE;
}

/* ---- paragraphs' indents (XAML text input, part 8) ----------------------
 * Kept as the alignment is: on the paragraph's mark (RUNFMT.ind), the
 * final paragraph's in lastind, plain text's one set in lastind, and those
 * a paragraph does not set its own are the default's (defind: the host's
 * TxGetParaFormat, or EM_SETPARAFORMAT SPF_SETDEFAULT).  In twips, as
 * PARAFORMAT's: the first line starts dxStartIndent from the left, the
 * others dxOffset from it, and every line ends dxRightIndent before the
 * right; a line never starts left of the text's left edge. */
static INDENTS own_ind_at_mark(TS *t, LONG e)
{
    INDENTS none;
    if (RICH(t) && e < t->len) {
        int idx = fmt_at(t, e);
        if (idx && idx < t->nfmts) return t->fmts[idx].ind;
        memset(&none, 0, sizeof(none));
        return none;
    }
    return t->lastind;
}

static void resolve_ind(TS *t, const INDENTS *own, LONG v[3])
{
    int i;
    for (i = 0; i < 3; i++) v[i] = (own->set & (1 << i)) ? own->v[i] : t->defind[i];
}

void para_indents(TS *t, LONG cp, LONG v[3])
{
    INDENTS own = own_ind_at_mark(t, mark_of(t, cp));
    resolve_ind(t, &own, v);
}

void para_px(TS *t, LONG cp, LONG *first, LONG *rest, LONG *right)
{
    LONG v[3];
    para_indents(t, cp, v);
    *first = v[IND_START] > 0 ? v[IND_START] / 15 : 0;          /* (1440 twips an inch, 96 pixels) */
    *rest = v[IND_START] + v[IND_OFFSET] > 0 ? (v[IND_START] + v[IND_OFFSET]) / 15 : 0;
    *right = v[IND_RIGHT] > 0 ? v[IND_RIGHT] / 15 : 0;
}

int fmt_with_para(TS *t, int idx, LONG cp)
{
    LONG e = mark_of(t, cp);
    RUNFMT f = t->fmts[idx];
    if (!idx || idx >= t->nfmts) memset(&f, 0, sizeof(f));
    f.align = own_at_mark(t, e);
    f.ind = own_ind_at_mark(t, e);
    return intern(t, &f);
}

typedef struct { LONG first[3]; DWORD same; BOOL any; } IQ;
static void query_ind_one(TS *t, LONG e, void *ctx)
{
    IQ *q = ctx;
    INDENTS own = own_ind_at_mark(t, e);
    LONG v[3];
    int i;
    resolve_ind(t, &own, v);
    if (!q->any) { memcpy(q->first, v, sizeof(v)); q->any = TRUE; return; }
    for (i = 0; i < 3; i++) if (v[i] != q->first[i]) q->same &= ~(1u << i);
}

DWORD para_query_ind(TS *t, LONG a, LONG b, LONG v[3])
{
    IQ q;
    memset(&q, 0, sizeof(q));
    q.same = 7;
    if (!RICH(t)) { para_indents(t, 0, v); return 7; }
    each_para(t, a, b, query_ind_one, &q);
    memcpy(v, q.first, sizeof(q.first));
    return q.same;
}

typedef struct { DWORD set; const LONG *v; } IA;
static void apply_ind_one(TS *t, LONG e, void *ctx)
{
    IA *x = ctx;
    INDENTS own = own_ind_at_mark(t, e);
    int i;
    for (i = 0; i < 3; i++)
        if (x->set & (1u << i)) { own.set |= (BYTE)(1 << i); own.v[i] = x->v[i]; }
    if (e < t->len) {
        RUNFMT f = t->fmts[t->fmt[e]];
        if (!t->fmt[e] || t->fmt[e] >= t->nfmts) memset(&f, 0, sizeof(f));
        f.ind = own;
        t->fmt[e] = (BYTE)intern(t, &f);
    } else t->lastind = own;
}

BOOL para_apply_ind(TS *t, LONG a, LONG b, DWORD set, const LONG v[3])
{
    LONG cur[3];
    IA x;
    int i;
    DWORD same;
    set &= 7;
    if (!set) return TRUE;
    same = para_query_ind(t, a, b, cur);
    for (i = 0; i < 3; i++) if ((set & (1u << i)) && (!(same & (1u << i)) || cur[i] != v[i])) break;
    if (i == 3) return TRUE;                      /* (nothing changes) */
    t->typing = FALSE;
    save_undo(t);
    x.set = set;
    x.v = v;
    if (!RICH(t) || !t->fmt) {                    /* (plain text: the one paragraph format) */
        for (i = 0; i < 3; i++)
            if (set & (1u << i)) { t->lastind.set |= (BYTE)(1 << i); t->lastind.v[i] = v[i]; }
    } else each_para(t, a, b, apply_ind_one, &x);
    t->dirty = TRUE;
    t->modified = TRUE;
    redraw(t);
    return TRUE;
}

/* ---- fonts and bars ------------------------------------------------------ */
/* GDI's font of @key: its face, size, bold and italic (t->font when the
 * default's own) */
HFONT fmt_font(TS *t, int key)
{
    LOGFONTW lf;
    const FONTKEY *k;
    if (key <= 0 || key >= t->nfkeys) return t->font;
    if (t->fonts[key]) return t->fonts[key];
    k = &t->fkeys[key];
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -(k->size * 96 + 720) / 1440;   /* twips at 96 dpi */
    if (!lf.lfHeight) lf.lfHeight = -1;
    lf.lfWeight = (k->bi & 1) ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (k->bi & 2) != 0;
    lf.lfCharSet = DEFAULT_CHARSET;
    memcpy(lf.lfFaceName, k->face, sizeof(lf.lfFaceName));
    lf.lfFaceName[31] = 0;
    t->fonts[key] = CreateFontIndirectW(&lf);
    return t->fonts[key] ? t->fonts[key] : t->font;
}

/* (after the default format changes: the fonts, their keys and metrics
 * are found again) */
void fmt_free_fonts(TS *t)
{
    int i;
    for (i = 0; i < MAXFONTS; i++) {
        if (t->fonts[i]) DeleteObject(t->fonts[i]);
        t->fonts[i] = 0;
    }
    t->nfkeys = 0;
    memset(t->keyof, 0, sizeof(t->keyof));
}

/* underline and strikethrough over [x0, x1) of a run in font @key whose
 * baseline is at @base: @bar fills each (left, top, right, bottom) */
void fmt_decorate(TS *t, int key, DWORD effects, LONG x0, LONG x1, LONG base, void (*bar)(void *, LONG, LONG, LONG, LONG), void *ctx)
{
    LONG h, asc, th, u;
    if (x1 <= x0 || !(effects & (CFE_UNDERLINE | CFE_STRIKEOUT))) return;
    fmt_metrics(t, 0, key, &h, &asc);
    th = (h + 12) / 24;
    if (th < 1) th = 1;
    if (effects & CFE_UNDERLINE) {
        u = base + th;
        if (u + th > base - asc + h) u = base - asc + h - th;
        bar(ctx, x0, u, x1, u + th);
    }
    if (effects & CFE_STRIKEOUT) {
        u = base - asc / 3;
        bar(ctx, x0, u, x1, u + th);
    }
}
