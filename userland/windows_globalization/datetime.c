/*
 * datetime.c — Windows.Globalization.DateTimeFormatting.DateTimeFormatter:
 * a DateTime as text in a language's way, from a template ("longdate",
 * "shorttime", "month day", "dayofweek.abbreviated hour minute" ...), a
 * pattern ("{month.full} {day.integer}, {year.full}") or the Include...
 * formats (YearFormat, MonthFormat ...), in the Gregorian calendar, on the
 * 12- or 24-hour clock, in the machine's time zone or UTC.
 *
 * A template picks the language's own picture (kernel32's long or short
 * date, year-month, month-day, long or short time; Win32's dddd, MMMM, yy,
 * h, tt ...) and keeps the parts it asks for; a picture and a pattern are
 * both a list of fields and literal text here, which Patterns gives back in
 * the pattern syntax.
 *
 * Not here yet: calendars other than the Gregorian, time zones other than
 * UTC and the machine's, and a template's parts in a language's own order
 * when its picture has none of them together (they are put in template
 * order).
 */
#include <wchar.h>
#include "glob.h"

static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IDateTimeFormatterFactory_ = { 0xEC8D8A53, 0x1A2E, 0x412D, { 0x88, 0x15, 0x3B, 0x74, 0x5F, 0xB1, 0xA2, 0xA0 } };
static const GUID IID_IDateTimeFormatterStatics_ = { 0xBFCDE7C0, 0xDF4C, 0x4A2E, { 0x90, 0x12, 0xF4, 0x7D, 0xAF, 0x3F, 0x12, 0x12 } };
static const GUID IID_IDateTimeFormatter_ = { 0x95EECA10, 0x73E0, 0x4E4B, { 0xA1, 0x83, 0x3D, 0x6A, 0xD0, 0xBA, 0x35, 0xEC } };
static const GUID IID_IDateTimeFormatter2_ = { 0x27C91A86, 0xBDAA, 0x4FD0, { 0x9E, 0x36, 0x67, 0x1D, 0x5A, 0xA5, 0xEE, 0x03 } };
#define LOCALE_RETURN_GENITIVE_NAMES_ 0x10000000u

enum { T_LIT, T_ERA, T_YEAR, T_MONTH, T_DAY, T_DOW, T_PERIOD, T_HOUR, T_MINUTE, T_SECOND, T_ZONE };
enum { S_FULL, S_ABBR, S_INT };
typedef struct { BYTE kind, style, width; WCHAR lit[24]; } Tok;
#define MAX_TOKS 48
/* what a formatter includes: YearFormat None 0 Default 1 Abbreviated 2 Full 3;
 * MonthFormat ... Numeric 4; DayFormat, HourFormat, MinuteFormat,
 * SecondFormat None 0 Default 1; DayOfWeekFormat as YearFormat */
typedef struct { int year, month, day, dow, hour, minute, second, zone; } Parts;

typedef struct {
    GObj base;
    GLangs langs;
    WCHAR *template_;
    WCHAR numeral[16];
    BOOL clock24;
    Parts parts;
    Tok toks[MAX_TOKS];
    int ntoks;
} Fmt;

/* ---- tokens -------------------------------------------------------- */
static void add_lit(Fmt *m, const WCHAR *s, int n)
{
    while (n > 0) {
        Tok *t = m->ntoks ? &m->toks[m->ntoks - 1] : 0;
        if (!t || t->kind != T_LIT || lstrlenW(t->lit) >= 23) {
            if (m->ntoks >= MAX_TOKS) return;
            t = &m->toks[m->ntoks++];
            ZeroMemory(t, sizeof *t);
            t->kind = T_LIT;
        }
        int k = lstrlenW(t->lit);
        while (n > 0 && k < 23) { t->lit[k++] = *s++; n--; }
        t->lit[k] = 0;
    }
}
static void add_tok(Fmt *m, int kind, int style, int width)
{
    if (m->ntoks >= MAX_TOKS) return;
    Tok *t = &m->toks[m->ntoks++];
    ZeroMemory(t, sizeof *t);
    t->kind = (BYTE)kind;
    t->style = (BYTE)style;
    t->width = (BYTE)width;
}
/* a Win32 date or time picture (dddd, MMMM d, yyyy; h:mm:ss tt) as tokens */
static void parse_picture(Fmt *m, const WCHAR *p)
{
    while (*p) {
        WCHAR c = *p;
        int n = 1;
        if (c == '\'') {
            const WCHAR *s = ++p;
            while (*p && *p != '\'') p++;
            if (p == s && *p) add_lit(m, L"'", 1);         /* '' is a quote */
            else add_lit(m, s, (int)(p - s));
            if (*p) p++;
            continue;
        }
        while (p[n] == c) n++;
        switch (c) {
        case 'd': if (n >= 3) add_tok(m, T_DOW, n == 3 ? S_ABBR : S_FULL, 0); else add_tok(m, T_DAY, S_INT, n); break;
        case 'M': if (n >= 3) add_tok(m, T_MONTH, n == 3 ? S_ABBR : S_FULL, 0); else add_tok(m, T_MONTH, S_INT, n); break;
        case 'y': add_tok(m, T_YEAR, n <= 2 ? S_ABBR : S_FULL, 0); break;
        case 'g': add_tok(m, T_ERA, S_ABBR, 0); break;
        case 'h': case 'H': add_tok(m, T_HOUR, S_INT, n > 2 ? 2 : n); break;
        case 'm': add_tok(m, T_MINUTE, S_INT, n > 2 ? 2 : n); break;
        case 's': add_tok(m, T_SECOND, S_INT, n > 2 ? 2 : n); break;
        case 't': add_tok(m, T_PERIOD, S_ABBR, 0); break;
        default: add_lit(m, p, n); break;
        }
        p += n;
    }
}
/* a pattern: {field.style(n)} and literal text; FALSE when it names no field Windows has */
static BOOL parse_pattern(Fmt *m, const WCHAR *p)
{
    static const struct { const WCHAR *name; int kind; } names[] = {
        { L"era", T_ERA }, { L"year", T_YEAR }, { L"month", T_MONTH }, { L"day", T_DAY }, { L"dayofweek", T_DOW },
        { L"period", T_PERIOD }, { L"hour", T_HOUR }, { L"minute", T_MINUTE }, { L"second", T_SECOND }, { L"timezone", T_ZONE },
    };
    while (*p) {
        if (*p != '{') {
            const WCHAR *s = p;
            while (*p && *p != '{') p++;
            add_lit(m, s, (int)(p - s));
            continue;
        }
        const WCHAR *s = ++p;
        while (*p && *p != '}') p++;
        if (!*p) return FALSE;
        WCHAR f[64];
        int n = (int)(p - s);
        if (n >= 64) return FALSE;
        lstrcpynW(f, s, n + 1);
        p++;
        if (!lstrcmpiW(f, L"openbrace")) { add_lit(m, L"{", 1); continue; }
        if (!lstrcmpiW(f, L"closebrace")) { add_lit(m, L"}", 1); continue; }
        /* field[.solo][.style][(n)] */
        int width = 0;
        WCHAR *paren = 0;
        for (WCHAR *c = f; *c; c++) if (*c == '(') { paren = c; break; }
        if (paren) { *paren = 0; for (WCHAR *c = paren + 1; *c >= '0' && *c <= '9'; c++) width = width * 10 + (*c - '0'); }
        WCHAR *style = 0;
        for (WCHAR *c = f; *c; c++) if (*c == '.') { *c = 0; style = c + 1; break; }
        if (style && !wcsncmp(style, L"solo", 4)) style = style[4] == '.' ? style + 5 : 0;
        int kind = -1;
        for (UINT32 i = 0; i < sizeof names / sizeof *names; i++) if (!lstrcmpiW(f, names[i].name)) kind = names[i].kind;
        if (kind < 0) return FALSE;
        int st = kind == T_DAY || kind == T_HOUR || kind == T_MINUTE || kind == T_SECOND ? S_INT : S_FULL;
        if (style) {
            if (!lstrcmpiW(style, L"full")) st = S_FULL;
            else if (!lstrcmpiW(style, L"abbreviated")) st = S_ABBR;
            else if (!lstrcmpiW(style, L"integer")) st = S_INT;
            else return FALSE;
        }
        if (kind == T_YEAR && st == S_INT) st = width && width <= 2 ? S_ABBR : S_FULL;
        if (kind == T_YEAR && st == S_ABBR) width = 2;
        add_tok(m, kind, st, width > 9 ? 9 : width);
    }
    return TRUE;
}
/* the Include... formats of what the tokens show */
static void parts_of_tokens(Fmt *m)
{
    Parts *p = &m->parts;
    ZeroMemory(p, sizeof *p);
    for (int i = 0; i < m->ntoks; i++) {
        Tok *t = &m->toks[i];
        switch (t->kind) {
        case T_YEAR: p->year = t->style == S_ABBR ? 2 : 3; break;
        case T_MONTH: p->month = t->style == S_FULL ? 3 : t->style == S_ABBR ? 2 : 4; break;
        case T_DAY: p->day = 1; break;
        case T_DOW: p->dow = t->style == S_ABBR ? 2 : 3; break;
        case T_HOUR: p->hour = 1; break;
        case T_MINUTE: p->minute = 1; break;
        case T_SECOND: p->second = 1; break;
        case T_ZONE: p->zone = 1; break;
        }
    }
}

/* ---- from parts to tokens ------------------------------------------ */
static BOOL wanted(const Parts *p, int kind)
{
    switch (kind) {
    case T_YEAR: return p->year != 0;
    case T_MONTH: return p->month != 0;
    case T_DAY: return p->day != 0;
    case T_DOW: return p->dow != 0;
    case T_HOUR: case T_PERIOD: return p->hour != 0;
    case T_MINUTE: return p->minute != 0;
    case T_SECOND: return p->second != 0;
    }
    return FALSE;
}
/* the fields of @picture @p asks for, each with the literal before it
 * (the first's dropped), in the styles @p asks for */
static void filtered(Fmt *m, const WCHAR *picture, const Parts *p)
{
    Fmt all;
    all.ntoks = 0;
    parse_picture(&all, picture);
    int kept = 0, last_lit = -1;
    BOOL seen[T_ZONE + 1] = { 0 };
    for (int i = 0; i < all.ntoks; i++) {
        Tok *t = &all.toks[i];
        if (t->kind == T_LIT) { last_lit = i; continue; }
        if (!wanted(p, t->kind) || seen[t->kind]) { last_lit = -1; continue; }
        if (t->kind == T_PERIOD && m->clock24) { last_lit = -1; continue; }
        if (kept && last_lit >= 0) add_lit(m, all.toks[last_lit].lit, lstrlenW(all.toks[last_lit].lit));
        Tok c = *t;
        if (c.kind == T_YEAR) c.style = p->year == 2 ? S_ABBR : p->year == 3 ? S_FULL : c.style;
        if (c.kind == T_MONTH) {
            if (p->month == 3) c.style = S_FULL;
            else if (p->month == 2) c.style = S_ABBR;
            else if (p->month == 4 && c.style != S_INT) { c.style = S_INT; c.width = 1; }
        }
        if (c.kind == T_DOW) c.style = p->dow == 2 ? S_ABBR : p->dow == 3 ? S_FULL : c.style;
        add_tok(m, c.kind, c.style, c.width);
        seen[c.kind] = TRUE;
        kept++;
        last_lit = -1;
    }
    /* what the picture lacks goes at the end */
    static const int order[] = { T_DOW, T_MONTH, T_DAY, T_YEAR, T_HOUR, T_MINUTE, T_SECOND, T_PERIOD };
    for (UINT32 i = 0; i < sizeof order / sizeof *order; i++) {
        int k = order[i];
        if (!wanted(p, k) || seen[k] || (k == T_PERIOD && m->clock24)) continue;
        if (k == T_PERIOD && !seen[T_HOUR]) continue;
        if (kept) add_lit(m, k == T_MINUTE || k == T_SECOND ? L":" : L" ", 1);
        int style = k == T_DAY || k == T_HOUR ? S_INT : k == T_MINUTE || k == T_SECOND ? S_INT : S_FULL;
        int width = k == T_MINUTE || k == T_SECOND ? 2 : k == T_DAY || k == T_HOUR ? 1 : 0;
        if (k == T_YEAR) style = p->year == 2 ? S_ABBR : S_FULL;
        if (k == T_MONTH) { style = p->month == 2 ? S_ABBR : p->month == 4 ? S_INT : S_FULL; width = style == S_INT; }
        if (k == T_DOW) style = p->dow == 2 ? S_ABBR : S_FULL;
        if (k == T_PERIOD) style = S_ABBR;
        add_tok(m, k, style, width);
        seen[k] = TRUE;
        kept++;
    }
}
static void build(Fmt *m, BOOL longdate, BOOL shortdate, BOOL longtime, BOOL shorttime)
{
    const WCHAR *loc = m->langs.resolved;
    Parts *p = &m->parts;
    WCHAR pic[96];
    m->ntoks = 0;
    BOOL date = p->year || p->month || p->day || p->dow, time = p->hour || p->minute || p->second;
    if (date) {
        LCTYPE which = LOCALE_SLONGDATE;
        if (shortdate) which = LOCALE_SSHORTDATE;
        else if (!longdate) {
            BOOL named = p->month == 2 || p->month == 3;
            if (p->month && p->year && !p->day && !p->dow) which = LOCALE_SYEARMONTH;
            else if (p->month && p->day && !p->year && !p->dow && named) which = GLOB_SMONTHDAY;
            else if (p->month && !named && !p->dow) which = LOCALE_SSHORTDATE;
        }
        glob_info(loc, which, pic, 96);
        if (!pic[0]) lstrcpyW(pic, which == LOCALE_SSHORTDATE ? L"M/d/yyyy" : L"dddd, MMMM d, yyyy");
        filtered(m, pic, p);
    }
    if (time) {
        glob_info(loc, shorttime && !longtime ? LOCALE_SSHORTTIME : LOCALE_STIMEFORMAT, pic, 96);
        if (!pic[0]) lstrcpyW(pic, L"h:mm:ss tt");
        if (m->ntoks) add_lit(m, L" ", 1);
        filtered(m, pic, p);
    }
    if (p->zone) {
        if (m->ntoks) add_lit(m, L" ", 1);
        add_tok(m, T_ZONE, S_ABBR, 0);
    }
}
/* a template: its words as parts; FALSE when it has a word Windows does not */
static BOOL parse_template(Fmt *m, const WCHAR *s)
{
    Parts *p = &m->parts;
    BOOL ld = FALSE, sd = FALSE, lt = FALSE, st = FALSE;
    ZeroMemory(p, sizeof *p);
    WCHAR w[40];
    while (*s) {
        while (*s == ' ') s++;
        int n = 0;
        while (s[n] && s[n] != ' ') n++;
        if (!n) break;
        if (n >= 40) return FALSE;
        lstrcpynW(w, s, n + 1);
        s += n;
        if (!lstrcmpiW(w, L"longdate")) { ld = TRUE; p->year = 3; p->month = 3; p->day = 1; p->dow = 3; }
        else if (!lstrcmpiW(w, L"shortdate")) { sd = TRUE; p->year = 1; p->month = 1; p->day = 1; }
        else if (!lstrcmpiW(w, L"longtime")) { lt = TRUE; p->hour = p->minute = p->second = 1; }
        else if (!lstrcmpiW(w, L"shorttime")) { st = TRUE; p->hour = p->minute = 1; }
        else if (!lstrcmpiW(w, L"year")) p->year = 1;
        else if (!lstrcmpiW(w, L"year.full")) p->year = 3;
        else if (!lstrcmpiW(w, L"year.abbreviated")) p->year = 2;
        else if (!lstrcmpiW(w, L"month")) p->month = 1;
        else if (!lstrcmpiW(w, L"month.full")) p->month = 3;
        else if (!lstrcmpiW(w, L"month.abbreviated")) p->month = 2;
        else if (!lstrcmpiW(w, L"month.numeric")) p->month = 4;
        else if (!lstrcmpiW(w, L"day")) p->day = 1;
        else if (!lstrcmpiW(w, L"dayofweek")) p->dow = 1;
        else if (!lstrcmpiW(w, L"dayofweek.full")) p->dow = 3;
        else if (!lstrcmpiW(w, L"dayofweek.abbreviated")) p->dow = 2;
        else if (!lstrcmpiW(w, L"hour")) p->hour = 1;
        else if (!lstrcmpiW(w, L"minute")) p->minute = 1;
        else if (!lstrcmpiW(w, L"second")) p->second = 1;
        else if (!lstrcmpiW(w, L"timezone") || !lstrcmpiW(w, L"timezone.full") || !lstrcmpiW(w, L"timezone.abbreviated")) p->zone = 1;
        else return FALSE;
    }
    if (!(p->year || p->month || p->day || p->dow || p->hour || p->minute || p->second || p->zone)) return FALSE;
    /* a month with a day and no year reads as a name (April 25) unless numbers were asked for */
    if (p->month == 1 && !p->year && p->day) p->month = 3;
    if (p->month == 1 && p->year && !p->day) p->month = 3;
    build(m, ld, sd, lt, st);
    return TRUE;
}

/* ---- formatting ---------------------------------------------------- */
#define OUT_MAX 256
static void put(WCHAR *out, int *k, const WCHAR *s) { while (*s && *k < OUT_MAX - 1) out[(*k)++] = *s++; out[*k] = 0; }
static void put_num(Fmt *m, WCHAR *out, int *k, int v, int width)
{
    WCHAR t[16], s[16];
    int n = 0, j = 0;
    do { t[n++] = (WCHAR)('0' + v % 10); v /= 10; } while (v && n < 15);
    while (n < width && n < 15) t[n++] = '0';
    while (n) s[j++] = t[--n];
    s[j] = 0;
    glob_digits(s, m->numeral);
    put(out, k, s);
}
static HRESULT format(Fmt *m, INT64 utc, BOOL utc_zone, HSTRING *v)
{
    const WCHAR *loc = m->langs.resolved;
    WCHAR out[OUT_MAX], s[80];
    int k = 0;
    GTime t;
    if (!v) return E_POINTER;
    glob_split(glob_to_zone(utc, utc_zone), &t);
    if (t.year < 1 || t.year > 9999) return E_INVALIDARG;
    out[0] = 0;
    BOOL has_day = FALSE;
    for (int i = 0; i < m->ntoks; i++) if (m->toks[i].kind == T_DAY) has_day = TRUE;
    for (int i = 0; i < m->ntoks; i++) {
        Tok *x = &m->toks[i];
        switch (x->kind) {
        case T_LIT: put(out, &k, x->lit); break;
        case T_ERA:
            if (!GetCalendarInfoEx(loc, CAL_GREGORIAN, 0, CAL_SERASTRING, s, 80, 0) || !s[0]) lstrcpyW(s, L"A.D.");
            put(out, &k, s);
            break;
        case T_YEAR: put_num(m, out, &k, x->style == S_ABBR ? t.year % 100 : t.year, x->style == S_ABBR ? 2 : x->width); break;
        case T_MONTH:
            if (x->style == S_INT) { put_num(m, out, &k, t.month, x->width); break; }
            glob_info(loc, x->style == S_FULL ? (GLOB_SMONTHNAME1 + t.month - 1) | (has_day ? LOCALE_RETURN_GENITIVE_NAMES_ : 0)
                                              : GLOB_SABBREVMONTHNAME1 + t.month - 1, s, 80);
            put(out, &k, s);
            break;
        case T_DAY: put_num(m, out, &k, t.day, x->width); break;
        case T_DOW: {
            int d = t.dow ? t.dow - 1 : 6;
            glob_info(loc, (x->style == S_ABBR ? GLOB_SABBREVDAYNAME1 : GLOB_SDAYNAME1) + d, s, 80);
            put(out, &k, s);
            break;
        }
        case T_PERIOD:
            glob_info(loc, t.hour < 12 ? LOCALE_S1159 : LOCALE_S2359, s, 80);
            put(out, &k, s);
            break;
        case T_HOUR: put_num(m, out, &k, m->clock24 ? t.hour : t.hour % 12 ? t.hour % 12 : 12, x->width); break;
        case T_MINUTE: put_num(m, out, &k, t.minute, x->width); break;
        case T_SECOND: put_num(m, out, &k, t.second, x->width); break;
        case T_ZONE: {
            TIME_ZONE_INFORMATION tz;
            if (utc_zone) { put(out, &k, L"UTC"); break; }
            GetTimeZoneInformation(&tz);
            put(out, &k, tz.StandardName[0] ? tz.StandardName : L"UTC");
            break;
        }
        }
    }
    return glob_str(out, v);
}
/* the tokens in the pattern syntax */
static void pattern_text(Fmt *m, WCHAR *out, int cap)
{
    int k = 0;
    out[0] = 0;
    for (int i = 0; i < m->ntoks; i++) {
        Tok *x = &m->toks[i];
        WCHAR f[48];
        f[0] = 0;
        switch (x->kind) {
        case T_LIT:
            for (const WCHAR *c = x->lit; *c && k < cap - 16; c++) {
                if (*c == '{') { lstrcpyW(out + k, L"{openbrace}"); k += 11; }
                else if (*c == '}') { lstrcpyW(out + k, L"{closebrace}"); k += 12; }
                else out[k++] = *c;
            }
            out[k] = 0;
            continue;
        case T_ERA: lstrcpyW(f, L"{era.abbreviated}"); break;
        case T_YEAR: lstrcpyW(f, x->style == S_ABBR ? L"{year.abbreviated}" : L"{year.full}"); break;
        case T_MONTH: lstrcpyW(f, x->style == S_FULL ? L"{month.full}" : x->style == S_ABBR ? L"{month.abbreviated}" :
                                  x->width >= 2 ? L"{month.integer(2)}" : L"{month.integer}"); break;
        case T_DAY: lstrcpyW(f, x->width >= 2 ? L"{day.integer(2)}" : L"{day.integer}"); break;
        case T_DOW: lstrcpyW(f, x->style == S_ABBR ? L"{dayofweek.abbreviated}" : L"{dayofweek.full}"); break;
        case T_PERIOD: lstrcpyW(f, L"{period.abbreviated}"); break;
        case T_HOUR: lstrcpyW(f, x->width >= 2 ? L"{hour.integer(2)}" : L"{hour.integer}"); break;
        case T_MINUTE: lstrcpyW(f, L"{minute.integer(2)}"); break;
        case T_SECOND: lstrcpyW(f, L"{second.integer(2)}"); break;
        case T_ZONE: lstrcpyW(f, L"{timezone.abbreviated}"); break;
        }
        if (k + lstrlenW(f) < cap) { lstrcpyW(out + k, f); k += lstrlenW(f); }
    }
}

/* ---- IDateTimeFormatter -------------------------------------------- */
static Fmt *fmt_of(void *face) { return OBJ(Fmt, face); }
static HRESULT STDMETHODCALLTYPE d_languages(Face *f, void **v) { return glob_langs_view(&fmt_of(f)->langs, v); }
static HRESULT STDMETHODCALLTYPE d_region(Face *f, HSTRING *v) { return glob_str(fmt_of(f)->langs.region, v); }
static HRESULT STDMETHODCALLTYPE d_calendar(Face *f, HSTRING *v) { (void)f; return glob_str(L"GregorianCalendar", v); }
static HRESULT STDMETHODCALLTYPE d_clock(Face *f, HSTRING *v) { return glob_str(fmt_of(f)->clock24 ? L"24HourClock" : L"12HourClock", v); }
static HRESULT STDMETHODCALLTYPE d_get_numeral(Face *f, HSTRING *v) { return glob_str(fmt_of(f)->numeral, v); }
static HRESULT STDMETHODCALLTYPE d_put_numeral(Face *f, HSTRING v)
{
    const WCHAR *s = glob_raw(v);
    if (!glob_numeral_zero(s) || lstrlenW(s) >= 16) return E_INVALIDARG;
    lstrcpyW(fmt_of(f)->numeral, *s ? s : L"Latn");
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE d_patterns(Face *f, void **v)
{
    WCHAR p[512];
    const WCHAR *items[1] = { p };
    pattern_text(fmt_of(f), p, 512);
    return glob_strvec(items, 1, v);
}
static HRESULT STDMETHODCALLTYPE d_template(Face *f, HSTRING *v) { return glob_str(fmt_of(f)->template_, v); }
static HRESULT STDMETHODCALLTYPE d_format(Face *f, INT64 dt, HSTRING *v) { return format(fmt_of(f), dt, FALSE, v); }
static HRESULT STDMETHODCALLTYPE d_format_zone(Face *f, INT64 dt, HSTRING zone, HSTRING *v)
{
    const WCHAR *z = glob_raw(zone);
    BOOL utc = !lstrcmpiW(z, L"UTC") || !lstrcmpiW(z, L"Etc/UTC") || !lstrcmpiW(z, L"Etc/GMT") || !lstrcmpiW(z, L"GMT");
    return format(fmt_of(f), dt, utc, v);
}
#define PART_GETTER(fn, field) static HRESULT STDMETHODCALLTYPE fn(Face *f, int *v) { if (!v) return E_POINTER; *v = fmt_of(f)->parts.field; return S_OK; }
PART_GETTER(d_year, year) PART_GETTER(d_month, month) PART_GETTER(d_dow, dow) PART_GETTER(d_day, day)
PART_GETTER(d_hour, hour) PART_GETTER(d_minute, minute) PART_GETTER(d_second, second)
static HRESULT STDMETHODCALLTYPE d_resolved(Face *f, HSTRING *v) { return glob_str(fmt_of(f)->langs.resolved, v); }
static const void *const fmt_vt[] = { GLOB_INSP, (void *)d_languages, (void *)d_region, (void *)d_calendar, (void *)d_clock,
                                      (void *)d_get_numeral, (void *)d_put_numeral, (void *)d_patterns, (void *)d_template,
                                      (void *)d_format, (void *)d_year, (void *)d_month, (void *)d_dow, (void *)d_day,
                                      (void *)d_hour, (void *)d_minute, (void *)d_second, (void *)d_resolved, (void *)d_region };
static const void *const fmt2_vt[] = { GLOB_INSP, (void *)d_format_zone };
static void fmt_destroy(GObj *o) { Fmt *m = (Fmt *)o; glob_langs_free(&m->langs); glob_free(m->template_); }
static const GUID *const fmt_iids[] = { &IID_IDateTimeFormatter_, &IID_IDateTimeFormatter2_ };
static const void *const *const fmt_vts[] = { fmt_vt, fmt2_vt };
static const GClass fmt_class = { L"Windows.Globalization.DateTimeFormatting.DateTimeFormatter", 2, fmt_iids, fmt_vts, fmt_destroy, FALSE };

/* a formatter: from @template (a template or a pattern), else from @parts */
static HRESULT fmt_new(const WCHAR *template_, const Parts *parts, void *languages, HSTRING region, HSTRING calendar,
                       HSTRING clock, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (calendar && lstrcmpW(glob_raw(calendar), L"GregorianCalendar")) return E_INVALIDARG;
    Fmt *m = glob_new(&fmt_class, sizeof *m);
    if (!m) return E_OUTOFMEMORY;
    HRESULT hr = glob_langs(&m->langs, languages, region);
    if (FAILED(hr)) { glob_free(m); return hr; }
    lstrcpyW(m->numeral, L"Latn");
    m->clock24 = glob_24h(m->langs.resolved);
    if (clock) {
        const WCHAR *c = glob_raw(clock);
        if (!lstrcmpW(c, L"24HourClock")) m->clock24 = TRUE;
        else if (!lstrcmpW(c, L"12HourClock")) m->clock24 = FALSE;
        else hr = E_INVALIDARG;
    }
    if (SUCCEEDED(hr)) {
        if (template_) {
            BOOL pattern = FALSE;
            for (const WCHAR *c = template_; *c; c++) if (*c == '{') pattern = TRUE;
            if (pattern) { if (parse_pattern(m, template_)) parts_of_tokens(m); else hr = E_INVALIDARG; }
            else if (!parse_template(m, template_)) hr = E_INVALIDARG;
            if (SUCCEEDED(hr) && !(m->template_ = glob_dup(template_))) hr = E_OUTOFMEMORY;
        } else {
            m->parts = *parts;
            if (parts->year < 0 || parts->year > 3 || parts->month < 0 || parts->month > 4 || parts->day < 0 || parts->day > 1 ||
                parts->dow < 0 || parts->dow > 3 || parts->hour < 0 || parts->hour > 1 || parts->minute < 0 || parts->minute > 1 ||
                parts->second < 0 || parts->second > 1 ||
                !(parts->year || parts->month || parts->day || parts->dow || parts->hour || parts->minute || parts->second))
                hr = E_INVALIDARG;
            else {
                build(m, parts->dow && parts->day && parts->month && parts->year, FALSE, parts->second != 0, FALSE);
                /* the template that says the same */
                WCHAR t[160] = { 0 };
                static const WCHAR *const yn[] = { 0, L"year", L"year.abbreviated", L"year.full" };
                static const WCHAR *const mn[] = { 0, L"month", L"month.abbreviated", L"month.full", L"month.numeric" };
                static const WCHAR *const wn[] = { 0, L"dayofweek", L"dayofweek.abbreviated", L"dayofweek.full" };
                const WCHAR *words[8];
                int n = 0;
                if (parts->month) words[n++] = mn[parts->month];
                if (parts->day) words[n++] = L"day";
                if (parts->year) words[n++] = yn[parts->year];
                if (parts->dow) words[n++] = wn[parts->dow];
                if (parts->hour) words[n++] = L"hour";
                if (parts->minute) words[n++] = L"minute";
                if (parts->second) words[n++] = L"second";
                for (int i = 0; i < n; i++) { if (i) lstrcatW(t, L" "); lstrcatW(t, words[i]); }
                if (!(m->template_ = glob_dup(t))) hr = E_OUTOFMEMORY;
            }
        }
    }
    if (FAILED(hr)) { glob_unref(&m->base.face[0]); return hr; }
    *out = &m->base.face[0];
    return S_OK;
}

/* ---- its factory and statics --------------------------------------- */
static HRESULT STDMETHODCALLTYPE ff_template(Face *f, HSTRING t, void **v) { (void)f; return fmt_new(glob_raw(t), 0, 0, 0, 0, 0, v); }
static HRESULT STDMETHODCALLTYPE ff_template_langs(Face *f, HSTRING t, void *langs, void **v)
{
    (void)f;
    if (!langs) return E_INVALIDARG;
    return fmt_new(glob_raw(t), 0, langs, 0, 0, 0, v);
}
static HRESULT STDMETHODCALLTYPE ff_template_context(Face *f, HSTRING t, void *langs, HSTRING region, HSTRING cal, HSTRING clock, void **v)
{
    (void)f;
    if (!langs) return E_INVALIDARG;
    return fmt_new(glob_raw(t), 0, langs, region, cal, clock, v);
}
static HRESULT STDMETHODCALLTYPE ff_date(Face *f, int y, int m, int d, int w, void **v)
{
    (void)f;
    Parts p = { y, m, d, w, 0, 0, 0, 0 };
    return fmt_new(0, &p, 0, 0, 0, 0, v);
}
static HRESULT STDMETHODCALLTYPE ff_time(Face *f, int h, int mi, int s, void **v)
{
    (void)f;
    Parts p = { 0, 0, 0, 0, h, mi, s, 0 };
    return fmt_new(0, &p, 0, 0, 0, 0, v);
}
static HRESULT STDMETHODCALLTYPE ff_datetime_langs(Face *f, int y, int m, int d, int w, int h, int mi, int s, void *langs, void **v)
{
    (void)f;
    Parts p = { y, m, d, w, h, mi, s, 0 };
    if (!langs) return E_INVALIDARG;
    return fmt_new(0, &p, langs, 0, 0, 0, v);
}
static HRESULT STDMETHODCALLTYPE ff_datetime_context(Face *f, int y, int m, int d, int w, int h, int mi, int s, void *langs,
                                                     HSTRING region, HSTRING cal, HSTRING clock, void **v)
{
    (void)f;
    Parts p = { y, m, d, w, h, mi, s, 0 };
    if (!langs) return E_INVALIDARG;
    return fmt_new(0, &p, langs, region, cal, clock, v);
}
static HRESULT STDMETHODCALLTYPE fs_long_date(Face *f, void **v) { (void)f; return fmt_new(L"longdate", 0, 0, 0, 0, 0, v); }
static HRESULT STDMETHODCALLTYPE fs_long_time(Face *f, void **v) { (void)f; return fmt_new(L"longtime", 0, 0, 0, 0, 0, v); }
static HRESULT STDMETHODCALLTYPE fs_short_date(Face *f, void **v) { (void)f; return fmt_new(L"shortdate", 0, 0, 0, 0, 0, v); }
static HRESULT STDMETHODCALLTYPE fs_short_time(Face *f, void **v) { (void)f; return fmt_new(L"shorttime", 0, 0, 0, 0, 0, v); }
static const void *const ffa_vt[] = { GLOB_INSP, (void *)glob_no_activate };
static const void *const ff_vt[] = { GLOB_INSP, (void *)ff_template, (void *)ff_template_langs, (void *)ff_template_context,
                                     (void *)ff_date, (void *)ff_time, (void *)ff_datetime_langs, (void *)ff_datetime_context };
static const void *const fs_vt[] = { GLOB_INSP, (void *)fs_long_date, (void *)fs_long_time, (void *)fs_short_date, (void *)fs_short_time };
static const GUID *const ff_iids[] = { &IID_IActivationFactory_, &IID_IDateTimeFormatterFactory_, &IID_IDateTimeFormatterStatics_ };
static const void *const *const ff_vts[] = { ffa_vt, ff_vt, fs_vt };
static const GClass ff_class = { L"Windows.Globalization.DateTimeFormatting.DateTimeFormatter", 3, ff_iids, ff_vts, 0, TRUE };
static GObj g_ff;

HRESULT glob_datetime_factory(const WCHAR *cls, void **out)
{
    if (lstrcmpW(cls, ff_class.name)) return CLASS_E_CLASSNOTAVAILABLE;
    glob_static_init(&g_ff, &ff_class);
    *out = &g_ff.face[0];
    return S_OK;
}
