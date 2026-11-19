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
    RUNFMT g = *f;
    g.mask &= FMT_MASK;
    g.effects &= g.mask;
    if (!(g.mask & CFM_COLOR) || (g.effects & CFE_AUTOCOLOR)) g.color = 0;
    if (!g.mask) return 0;
    if (!t->nfmts) t->nfmts = 1;                  /* (fmts[0]: the default) */
    for (i = 1; i < t->nfmts; i++)
        if (t->fmts[i].mask == g.mask && t->fmts[i].effects == g.effects && t->fmts[i].color == g.color) return i;
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
