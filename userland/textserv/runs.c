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
 * the selection's format changes it.  The face and the size are the
 * default's everywhere: one line height (a later part lets them vary).
 * Bold and italic are fonts (GDI's, or the host's DirectWrite faces:
 * dwtext.c); underline and strikethrough are bars drawn under and through
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
    memset(&g, 0, sizeof(g));                     /* (INDENTS' padding: compared whole) */
    g.mask = f->mask;
    g.effects = f->effects;
    g.color = f->color;
    g.align = f->align;
    g.ind.set = f->ind.set;
    memcpy(g.ind.v, f->ind.v, sizeof(g.ind.v));
    g.mask &= FMT_MASK;
    g.effects &= g.mask;
    if (!(g.mask & CFM_COLOR) || (g.effects & CFE_AUTOCOLOR)) g.color = 0;
    for (i = 0; i < 3; i++) if (!(g.ind.set & (1 << i))) g.ind.v[i] = 0;
    if (!g.mask && !g.align && !g.ind.set) return 0;
    if (!t->nfmts) t->nfmts = 1;                  /* (fmts[0]: the default) */
    for (i = 1; i < t->nfmts; i++)
        if (t->fmts[i].mask == g.mask && t->fmts[i].effects == g.effects && t->fmts[i].color == g.color
            && t->fmts[i].align == g.align && !memcmp(&t->fmts[i].ind, &g.ind, sizeof(g.ind))) return i;
    if (t->nfmts >= MAXFMT) return 0;
    t->fmts[t->nfmts] = g;
    return t->nfmts++;
}

/* format @idx with @mask's properties set to @effects and @color */
static int merge(TS *t, int idx, DWORD mask, DWORD effects, COLORREF color)
{
    RUNFMT f = t->fmts[idx];
    if (!idx) memset(&f, 0, sizeof(f));
    f.mask |= mask;
    f.effects = (f.effects & ~mask) | (effects & mask);
    if (mask & CFM_COLOR) f.color = color;
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

int fmt_key(TS *t, int idx)
{
    DWORD e = fmt_effects(t, idx);
    return ((e & CFE_BOLD) ? 1 : 0) | ((e & CFE_ITALIC) ? 2 : 0);
}

/* ---- changing and reading formats --------------------------------------- */
BOOL fmt_apply(TS *t, LONG a, LONG b, DWORD mask, DWORD effects, COLORREF color)
{
    LONG i;
    int from = -1, to = 0;
    mask &= FMT_MASK;
    if (!mask) return TRUE;
    if (a > b) { LONG x = a; a = b; b = x; }
    if (a < 0) a = 0;
    if (b > t->len) b = t->len;
    if (!RICH(t)) {                               /* (plain text: the one format, the default) */
        CHARFORMAT2W cf;
        memset(&cf, 0, sizeof(cf));
        cf.cf.cbSize = sizeof(cf);
        cf.cf.dwMask = mask;
        cf.cf.dwEffects = effects;
        cf.cf.crTextColor = color;
        set_default_format(t, &cf.cf);
        return TRUE;
    }
    if (a == b) {                                 /* (the caret's: what typing there takes) */
        t->insfmt = merge(t, fmt_insertion(t, a, b), mask, effects, color);
        return TRUE;
    }
    if (!t->fmt) return FALSE;
    t->typing = FALSE;
    save_undo(t);
    for (i = a; i < b; i++) {
        if (t->fmt[i] != from) { from = t->fmt[i]; to = merge(t, from, mask, effects, color); }
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
        }
    }
    out->cf.dwMask = same | CFM_FACE | CFM_SIZE;
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
/* GDI's font of @key: the default's face and size, bold and italic as the
 * key says (t->font when the default's own) */
HFONT fmt_font(TS *t, int key)
{
    LOGFONTW lf;
    if (key == fmt_key(t, 0) && t->font) return t->font;
    if (t->fonts[key]) return t->fonts[key];
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -(t->cf.cf.yHeight * 96 + 720) / 1440;   /* twips at 96 dpi */
    lf.lfWeight = (key & 1) ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (key & 2) != 0;
    lf.lfCharSet = DEFAULT_CHARSET;
    memcpy(lf.lfFaceName, t->cf.cf.szFaceName, sizeof(lf.lfFaceName));
    lf.lfFaceName[31] = 0;
    t->fonts[key] = CreateFontIndirectW(&lf);
    return t->fonts[key] ? t->fonts[key] : t->font;
}

void fmt_free_fonts(TS *t)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (t->fonts[i]) DeleteObject(t->fonts[i]);
        t->fonts[i] = 0;
    }
}

/* underline and strikethrough over [x0, x1) of the line at @y: @bar fills
 * each (left, top, right, bottom) */
void fmt_decorate(TS *t, DWORD effects, LONG x0, LONG x1, LONG y, void (*bar)(void *, LONG, LONG, LONG, LONG), void *ctx)
{
    LONG th = (t->lineh + 12) / 24, u;
    if (x1 <= x0) return;
    if (th < 1) th = 1;
    if (effects & CFE_UNDERLINE) {
        u = y + t->ascent + th;
        if (u + th > y + t->lineh) u = y + t->lineh - th;
        bar(ctx, x0, u, x1, u + th);
    }
    if (effects & CFE_STRIKEOUT) {
        u = y + t->ascent - t->ascent / 3;
        bar(ctx, x0, u, x1, u + th);
    }
}
