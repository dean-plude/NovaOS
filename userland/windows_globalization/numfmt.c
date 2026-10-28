/*
 * numfmt.c — Windows.Globalization.NumberFormatting's DecimalFormatter and
 * CurrencyFormatter: numbers as text in a language's way (its decimal and
 * group separators, grouping, negative pattern; a currency's symbol or code,
 * its pattern and digits) and back (Parse...), with the options Windows has:
 * least integer, fraction and significant digits, grouping, a decimal point
 * always shown, a signed zero, a numeral system and a number rounder
 * (INumberRounder, or ApplyRoundingForCurrency's algorithm).
 *
 * A number is first a decimal digit string (Dec: doubles by the shortest
 * text that reads back as the same double, as Windows shows them), so
 * rounding and padding are exact.
 *
 * Not here yet: PercentFormatter, PermilleFormatter, the rounder classes
 * (IncrementNumberRounder, SignificantDigitsNumberRounder) and
 * NumeralSystemTranslator; the default numeral system is Latn for every
 * language.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "glob.h"

static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IDecimalFormatterFactory_ = { 0x0D018C9A, 0xE393, 0x46B8, { 0xB8, 0x30, 0x7A, 0x69, 0xC8, 0xF8, 0x9F, 0xBB } };
static const GUID IID_ICurrencyFormatterFactory_ = { 0x86C7537E, 0xB938, 0x4AA2, { 0x84, 0xB0, 0x2C, 0x33, 0xDC, 0x5B, 0x14, 0x50 } };
static const GUID IID_INumberFormatter_ = { 0xA5007C49, 0x7676, 0x4DB7, { 0x86, 0x31, 0x1B, 0x6F, 0xF2, 0x65, 0xCA, 0xA9 } };
static const GUID IID_INumberFormatterOptions_ = { 0x80332D21, 0xAEE1, 0x4A39, { 0xBA, 0xA2, 0x07, 0xED, 0x8C, 0x96, 0xDA, 0xF6 } };
static const GUID IID_INumberFormatter2_ = { 0xD4A8C1F0, 0x80D0, 0x4B0D, { 0xA8, 0x9E, 0x88, 0x2C, 0x1E, 0x8F, 0x83, 0x10 } };
static const GUID IID_INumberParser_ = { 0xE6659412, 0x4A13, 0x4A53, { 0x83, 0xA1, 0x39, 0x2F, 0xBE, 0x4C, 0xFF, 0x9F } };
static const GUID IID_ISignificantDigitsOption_ = { 0x1D4DFCDD, 0x2D43, 0x4EE8, { 0xBB, 0xF1, 0xC1, 0xB2, 0x6A, 0x71, 0x1A, 0x58 } };
static const GUID IID_INumberRounderOption_ = { 0x3B088433, 0x646F, 0x4EFE, { 0x8D, 0x48, 0x66, 0xEB, 0x2E, 0x49, 0xE7, 0x36 } };
static const GUID IID_ISignedZeroOption_ = { 0xFD1CDD31, 0x0A3C, 0x49C4, { 0xA6, 0x42, 0x96, 0xA1, 0x56, 0x4F, 0x4F, 0x30 } };
static const GUID IID_ICurrencyFormatter_ = { 0x11730CA5, 0x4B00, 0x41B2, { 0xB3, 0x32, 0x73, 0xB1, 0x2A, 0x49, 0x7D, 0x54 } };
static const GUID IID_ICurrencyFormatter2_ = { 0x072C2F1D, 0xE7BA, 0x4197, { 0x92, 0x0E, 0x24, 0x7C, 0x92, 0xF7, 0xDE, 0xA6 } };
static const GUID IID_IPropertyValueStatics_ = { 0x629BDBC8, 0xD932, 0x4FF4, { 0x96, 0xB9, 0x8D, 0x96, 0xC5, 0xC1, 0xE8, 0x58 } };
/* IReference<Int64>, <UInt64>, <Double> */
static const GUID IID_IReferenceInt64_ = { 0x4DDA9E24, 0xE69F, 0x5C6A, { 0xA0, 0xA6, 0x93, 0x42, 0x73, 0x65, 0xAF, 0x2A } };
static const GUID IID_IReferenceUInt64_ = { 0x6755E376, 0x53BB, 0x568B, { 0xA1, 0x1D, 0x17, 0x23, 0x98, 0x68, 0x30, 0x9E } };
static const GUID IID_IReferenceDouble_ = { 0x2F2D6C29, 0x5473, 0x5F3E, { 0x92, 0xE7, 0x96, 0x57, 0x2B, 0xB9, 0x90, 0xE2 } };

typedef struct {
    GObj base;
    GLangs langs;
    WCHAR locale[GLOB_TAG];       /* whose separators and patterns: the resolved language's */
    int integer_digits, fraction_digits, significant_digits;
    BOOL grouped, point_always, zero_signed;
    WCHAR numeral[16];
    void *rounder;                /* INumberRounder, or NULL */
    BOOL currency;
    WCHAR code[4];                /* ISO 4217 */
    int mode;                     /* CurrencyFormatterMode: UseSymbol 0, UseCurrencyCode 1 */
    int rounding;                 /* RoundingAlgorithm ApplyRoundingForCurrency set (0 none) */
} Num;

/* ---- decimal digit strings ----------------------------------------- */
#define DEC_MAX 400
typedef struct {
    BOOL neg, inf, nan;
    char d[DEC_MAX];              /* significant digits, no leading zeros ("" is zero) */
    int n;                        /* how many */
    int point;                    /* the value is 0.d * 10^point */
} Dec;
static void dec_trim(Dec *x)
{
    int lead = 0;
    while (lead < x->n && x->d[lead] == '0') lead++;
    if (lead) { memmove(x->d, x->d + lead, x->n - lead); x->n -= lead; x->point -= lead; }
    while (x->n && x->d[x->n - 1] == '0') x->n--;
    if (!x->n) x->point = 0;
}
static void dec_u64(Dec *x, UINT64 v, BOOL neg)
{
    char t[24];
    int k = 0;
    ZeroMemory(x, sizeof *x);
    x->neg = neg;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = 0; i < k; i++) x->d[i] = t[k - 1 - i];
    x->n = x->point = k;
    dec_trim(x);
}
static void dec_i64(Dec *x, INT64 v) { dec_u64(x, v < 0 ? (UINT64)0 - (UINT64)v : (UINT64)v, v < 0); }
/* the shortest digits that read back as @v */
static void dec_double(Dec *x, double v)
{
    char t[64];
    ZeroMemory(x, sizeof *x);
    if (v != v) { x->nan = TRUE; return; }
    x->neg = v < 0 || (v == 0 && 1 / v < 0);
    if (v == 1.0 / 0.0 || v == -1.0 / 0.0) { x->inf = TRUE; return; }
    if (v == 0) return;
    for (int p = 1; p <= 17; p++) {
        snprintf(t, sizeof t, "%.*e", p - 1, v);
        if (strtod(t, 0) == v || p == 17) break;
    }
    /* t: [-]d[.ddd]e[+-]xx */
    const char *c = t;
    if (*c == '-') c++;
    int exp = 0;
    for (; *c && *c != 'e'; c++)
        if (*c >= '0' && *c <= '9') x->d[x->n++] = *c;
    if (*c == 'e') exp = atoi(c + 1);
    x->point = exp + 1;
    dec_trim(x);
}
static BOOL dec_zero(const Dec *x) { return !x->n && !x->inf && !x->nan; }
/* round to @frac fraction digits by RoundingAlgorithm @alg */
static void dec_round(Dec *x, int frac, int alg)
{
    int keep = x->point + frac;   /* digits kept */
    if (x->inf || x->nan || alg <= 0 || alg > 10 || keep >= x->n) return;
    /* the first digit dropped (an implied 0 when every digit goes) and whether any after it is not 0 */
    int first = keep >= 0 ? x->d[keep] - '0' : 0;
    BOOL rest = FALSE;
    for (int i = keep >= 0 ? keep + 1 : 0; i < x->n; i++) if (x->d[i] != '0') rest = TRUE;
    BOOL some = first || rest, above = first > 5 || (first == 5 && rest), tie = first == 5 && !rest;
    int last = keep > 0 ? x->d[keep - 1] - '0' : 0;
    BOOL up = FALSE;              /* the kept magnitude goes up one unit */
    switch (alg) {
    case 1: up = x->neg && some; break;                    /* RoundDown: toward -infinity */
    case 2: up = !x->neg && some; break;                   /* RoundUp: toward +infinity */
    case 3: up = FALSE; break;                             /* RoundTowardsZero */
    case 4: up = some; break;                              /* RoundAwayFromZero */
    case 5: up = above || (tie && x->neg); break;          /* RoundHalfDown: a tie toward -infinity */
    case 6: up = above || (tie && !x->neg); break;         /* RoundHalfUp: a tie toward +infinity */
    case 7: up = above; break;                             /* RoundHalfTowardsZero */
    case 8: up = above || tie; break;                      /* RoundHalfAwayFromZero */
    case 9: up = above || (tie && (last & 1)); break;      /* RoundHalfToEven */
    case 10: up = above || (tie && !(last & 1)); break;    /* RoundHalfToOdd */
    }
    if (keep <= 0) {
        if (up) { x->d[0] = '1'; x->n = 1; x->point = 1 - frac; }   /* one unit of the last place kept */
        else { x->n = 0; x->point = 0; }
        return;
    }
    x->n = keep;
    if (up) {
        int i = keep - 1;
        while (i >= 0 && x->d[i] == '9') x->d[i--] = '0';
        if (i >= 0) x->d[i]++;
        else { memmove(x->d + 1, x->d, x->n); x->d[0] = '1'; x->n++; x->point++; }
    }
    dec_trim(x);
}

/* ---- the formatter's locale ---------------------------------------- */
static void info(Num *m, LCTYPE type, WCHAR *out, int cap) { glob_info(m->locale, type, out, cap); }
static const WCHAR *zero_decimal_currencies = L"BIF CLP DJF GNF ISK JPY KMF KRW PYG RWF UGX UYI VND VUV XAF XOF XPF ";
static const WCHAR *three_decimal_currencies = L"BHD IQD JOD KWD LYD OMR TND ";
static BOOL in_list(const WCHAR *list, const WCHAR *code)
{
    for (const WCHAR *p = list; *p; p += 4)
        if (p[0] == code[0] && p[1] == code[1] && p[2] == code[2]) return TRUE;
    return FALSE;
}
static int currency_digits(Num *m)
{
    WCHAR own[16];
    info(m, LOCALE_SINTLSYMBOL, own, 16);
    if (!lstrcmpW(own, m->code)) return glob_info_int(m->locale, LOCALE_ICURRDIGITS, 2);
    if (in_list(zero_decimal_currencies, m->code)) return 0;
    if (in_list(three_decimal_currencies, m->code)) return 3;
    return 2;
}

/* ---- formatting ---------------------------------------------------- */
#define OUT_MAX 512
static void put(WCHAR *out, int *k, const WCHAR *s) { while (*s && *k < OUT_MAX - 1) out[(*k)++] = *s++; out[*k] = 0; }
static void putc_(WCHAR *out, int *k, WCHAR c) { if (*k < OUT_MAX - 1) out[(*k)++] = c; out[*k] = 0; }
/* the number's digits, separators and decimal point, without sign or symbol */
static void body(Num *m, const Dec *x, WCHAR *out)
{
    WCHAR dec[8], grp[8], grouping[16];
    int k = 0;
    out[0] = 0;
    info(m, m->currency ? LOCALE_SMONDECIMALSEP : LOCALE_SDECIMAL, dec, 8);
    info(m, m->currency ? LOCALE_SMONTHOUSANDSEP : LOCALE_STHOUSAND, grp, 8);
    info(m, m->currency ? LOCALE_SMONGROUPING : LOCALE_SGROUPING, grouping, 16);
    if (!dec[0]) lstrcpyW(dec, L".");
    /* integer digits: those before the point, at least integer_digits */
    int int_n = x->point > 0 ? x->point : 0;
    int frac_n = x->n - x->point > 0 ? x->n - x->point : 0;
    /* at least significant_digits from the first one not 0 (a zero's 0 counts): more fraction 0s */
    if (m->significant_digits > x->n) {
        int extra = x->n ? m->significant_digits - x->n : m->significant_digits - 1;
        if (x->n && x->point > x->n) extra -= x->point - x->n;     /* 1200: its integer 0s count */
        if (extra > 0) frac_n += extra;
    }
    if (frac_n < m->fraction_digits) frac_n = m->fraction_digits;
    int int_shown = int_n > m->integer_digits ? int_n : m->integer_digits;
    char ib[DEC_MAX + 64];
    int ik = 0;
    for (int i = int_shown - 1; i >= 0 && ik < (int)sizeof ib - 1; i--) {
        int pos = x->point - 1 - i;          /* index into d of the 10^i digit */
        ib[ik++] = pos >= 0 && pos < x->n ? x->d[pos] : '0';
    }
    /* groups, from the right: grouping "3;0" (threes), "3;2;0" (three then twos), "3" (one group) */
    int sizes[8], ns = 0, repeat = 0;
    for (const WCHAR *g = grouping; *g && ns < 8;) {
        int v = 0;
        while (*g >= '0' && *g <= '9') v = v * 10 + (*g++ - '0');
        if (*g == ';') g++;
        if (!v) { repeat = 1; break; }
        sizes[ns++] = v;
    }
    BOOL mark[DEC_MAX + 64] = { 0 };     /* a separator before ib[i] */
    if (m->grouped && ns && grp[0]) {
        int at = ik, s = 0;
        for (;;) {
            int size = sizes[s < ns ? s : ns - 1];
            if (s >= ns && !repeat) break;
            at -= size;
            if (at <= 0) break;
            mark[at] = TRUE;
            s++;
        }
    }
    for (int i = 0; i < ik; i++) {
        if (mark[i]) put(out, &k, grp);
        putc_(out, &k, (WCHAR)ib[i]);
    }
    if (frac_n || m->point_always) put(out, &k, dec);
    for (int i = 0; i < frac_n; i++) {
        int pos = x->point + i;
        putc_(out, &k, pos >= 0 && pos < x->n ? (WCHAR)x->d[pos] : '0');
    }
    glob_digits(out, m->numeral);
}
static void format_dec(Num *m, Dec *x, WCHAR *out)
{
    WCHAR num[OUT_MAX], neg[8];
    int k = 0;
    out[0] = 0;
    if (x->nan) { info(m, LOCALE_SNAN, out, OUT_MAX); if (!out[0]) lstrcpyW(out, L"NaN"); return; }
    if (x->inf) {
        info(m, x->neg ? LOCALE_SNEGINFINITY : LOCALE_SPOSINFINITY, out, OUT_MAX);
        if (!out[0]) lstrcpyW(out, x->neg ? L"-\x221E" : L"\x221E");
        return;
    }
    if (m->currency) dec_round(x, m->fraction_digits, m->rounding);
    body(m, x, num);
    BOOL negative = x->neg && (!dec_zero(x) || m->zero_signed);
    info(m, LOCALE_SNEGATIVESIGN, neg, 8);
    if (!neg[0]) lstrcpyW(neg, L"-");
    if (!m->currency) {
        /* LOCALE_INEGNUMBER: 0 (1.1), 1 -1.1, 2 - 1.1, 3 1.1-, 4 1.1 - */
        int p = negative ? glob_info_int(m->locale, LOCALE_INEGNUMBER, 1) : -1;
        if (p == 0) { putc_(out, &k, '('); put(out, &k, num); putc_(out, &k, ')'); }
        else if (p == 2) { put(out, &k, neg); putc_(out, &k, ' '); put(out, &k, num); }
        else if (p == 3) { put(out, &k, num); put(out, &k, neg); }
        else if (p == 4) { put(out, &k, num); putc_(out, &k, ' '); put(out, &k, neg); }
        else { if (negative) put(out, &k, neg); put(out, &k, num); }
        return;
    }
    WCHAR sym[16], own[16];
    info(m, LOCALE_SINTLSYMBOL, own, 16);
    if (m->mode == 0 && !lstrcmpW(own, m->code)) info(m, LOCALE_SCURRENCY, sym, 16);
    else lstrcpyW(sym, m->code);
    BOOL code = !lstrcmpW(sym, m->code);    /* a code always stands apart from the number */
    if (!negative) {
        /* LOCALE_ICURRENCY: 0 $1, 1 1$, 2 $ 1, 3 1 $ */
        int p = glob_info_int(m->locale, LOCALE_ICURRENCY, 0);
        if (code && p < 2) p += 2;
        if (p == 0 || p == 2) { put(out, &k, sym); if (p == 2) putc_(out, &k, ' '); put(out, &k, num); }
        else { put(out, &k, num); if (p == 3) putc_(out, &k, ' '); put(out, &k, sym); }
        return;
    }
    /* LOCALE_INEGCURR's 16 patterns: $ the symbol, - the sign, 1 the number */
    static const WCHAR *const neg_patterns[16] = {
        L"($1)", L"-$1", L"$-1", L"$1-", L"(1$)", L"-1$", L"1-$", L"1$-",
        L"-1 $", L"-$ 1", L"1 $-", L"$ 1-", L"$ -1", L"1- $", L"($ 1)", L"(1 $)",
    };
    static const int spaced[8] = { 14, 9, 12, 11, 15, 8, 13, 10 };   /* each of 0-7 with the symbol apart */
    int p = glob_info_int(m->locale, LOCALE_INEGCURR, 0);
    if (p < 0 || p > 15) p = 0;
    if (code && p < 8) p = spaced[p];
    for (const WCHAR *c = neg_patterns[p]; *c; c++) {
        if (*c == '$') put(out, &k, sym);
        else if (*c == '-') put(out, &k, neg);
        else if (*c == '1') put(out, &k, num);
        else putc_(out, &k, *c);
    }
}
static HRESULT format_double(Num *m, double v, HSTRING *s)
{
    WCHAR out[OUT_MAX];
    Dec x;
    if (!s) return E_POINTER;
    if (m->rounder) {
        double r = v;
        /* INumberRounder.RoundDouble */
        if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, double, double *))VT(m->rounder)[11])(m->rounder, v, &r))) v = r;
    }
    dec_double(&x, v);
    format_dec(m, &x, out);
    return glob_str(out, s);
}
static HRESULT format_i64(Num *m, INT64 v, HSTRING *s)
{
    WCHAR out[OUT_MAX];
    Dec x;
    if (!s) return E_POINTER;
    if (m->rounder) {
        INT64 r = v;
        if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, INT64, INT64 *))VT(m->rounder)[8])(m->rounder, v, &r))) v = r;
    }
    dec_i64(&x, v);
    format_dec(m, &x, out);
    return glob_str(out, s);
}
static HRESULT format_u64(Num *m, UINT64 v, HSTRING *s)
{
    WCHAR out[OUT_MAX];
    Dec x;
    if (!s) return E_POINTER;
    if (m->rounder) {
        UINT64 r = v;
        if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, UINT64, UINT64 *))VT(m->rounder)[9])(m->rounder, v, &r))) v = r;
    }
    dec_u64(&x, v, FALSE);
    format_dec(m, &x, out);
    return glob_str(out, s);
}
static HRESULT STDMETHODCALLTYPE f_int(Face *f, INT64 v, HSTRING *s) { return format_i64(OBJ(Num, f), v, s); }
static HRESULT STDMETHODCALLTYPE f_uint(Face *f, UINT64 v, HSTRING *s) { return format_u64(OBJ(Num, f), v, s); }
static HRESULT STDMETHODCALLTYPE f_double(Face *f, double v, HSTRING *s) { return format_double(OBJ(Num, f), v, s); }

/* ---- parsing ------------------------------------------------------- */
/* @text as plain ASCII "[-]digits[.digits]": the language's separators,
 * sign patterns, numeral system digits (and a currency's symbol or code)
 * taken out; FALSE when it is not a number */
static BOOL normalize(Num *m, const WCHAR *text, char *out, int cap)
{
    WCHAR dec[8], grp[8], neg[8], pos[8], sym[16], own[16];
    int k = 0, digits = 0;
    BOOL negative = FALSE, paren = FALSE, point = FALSE;
    info(m, m->currency ? LOCALE_SMONDECIMALSEP : LOCALE_SDECIMAL, dec, 8);
    info(m, m->currency ? LOCALE_SMONTHOUSANDSEP : LOCALE_STHOUSAND, grp, 8);
    info(m, LOCALE_SNEGATIVESIGN, neg, 8);
    info(m, LOCALE_SPOSITIVESIGN, pos, 8);
    if (!dec[0]) lstrcpyW(dec, L".");
    if (!neg[0]) lstrcpyW(neg, L"-");
    sym[0] = 0;
    if (m->currency) {
        info(m, LOCALE_SINTLSYMBOL, own, 16);
        if (!lstrcmpW(own, m->code)) info(m, LOCALE_SCURRENCY, sym, 16);
    }
    WCHAR zero = glob_numeral_zero(m->numeral);
    out[k++] = '+';
    for (const WCHAR *p = text; *p;) {
        int n;
        if (*p >= '0' && *p <= '9') { if (k >= cap - 1) return FALSE; out[k++] = (char)*p++; digits++; continue; }
        if (zero && *p >= zero && *p <= zero + 9) { if (k >= cap - 1) return FALSE; out[k++] = (char)('0' + (*p++ - zero)); digits++; continue; }
        if (*p == ' ' || *p == 0xA0 || *p == 0x202F) { p++; continue; }
        if ((n = lstrlenW(dec)) && !wcsncmp(p, dec, n)) {
            if (point) return FALSE;
            point = TRUE;
            if (k >= cap - 1) return FALSE;
            out[k++] = '.';
            p += n;
            continue;
        }
        if (grp[0] && (n = lstrlenW(grp)) && !wcsncmp(p, grp, n) && !point && digits) { p += n; continue; }
        if ((n = lstrlenW(neg)) && !wcsncmp(p, neg, n)) { if (negative) return FALSE; negative = TRUE; p += n; continue; }
        if (pos[0] && (n = lstrlenW(pos)) && !wcsncmp(p, pos, n)) { p += n; continue; }
        if (*p == '(' && !paren && !digits) { paren = TRUE; negative = !negative; p++; continue; }
        if (*p == ')' && paren) { paren = FALSE; p++; continue; }
        if (m->currency && sym[0] && (n = lstrlenW(sym)) && !wcsncmp(p, sym, n)) { p += n; continue; }
        if (m->currency && !wcsncmp(p, m->code, 3)) { p += 3; continue; }
        return FALSE;
    }
    if (!digits || paren) return FALSE;
    out[0] = negative ? '-' : '+';
    out[k] = 0;
    return TRUE;
}
static HRESULT box(int slot, const GUID *iid, const void *value, void **out)
{
    static const WCHAR name[] = L"Windows.Foundation.PropertyValue";
    HSTRING_HEADER h;
    HSTRING s;
    void *statics = 0, *b = 0;
    HRESULT hr = WindowsCreateStringReference(name, (UINT32)(sizeof name / sizeof *name - 1), &h, &s);
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(s, &IID_IPropertyValueStatics_, &statics);
    if (FAILED(hr)) return hr;
    if (slot == 15) hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, double, void **))VT(statics)[slot])(statics, *(const double *)value, &b);
    else hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, UINT64, void **))VT(statics)[slot])(statics, *(const UINT64 *)value, &b);
    glob_unref(statics);
    if (SUCCEEDED(hr)) hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, REFIID, void **))VT(b)[0])(b, iid, out);
    glob_unref(b);
    return hr;
}
/* digits and an optional all-zero fraction as a whole number */
static BOOL parse_whole(const char *t, UINT64 *v, BOOL *neg)
{
    *neg = *t == '-';
    *v = 0;
    for (t++; *t && *t != '.'; t++) {
        if (*v > (~(UINT64)0 - (UINT64)(*t - '0')) / 10) return FALSE;
        *v = *v * 10 + (UINT64)(*t - '0');
    }
    if (*t == '.') for (t++; *t; t++) if (*t != '0') return FALSE;
    return TRUE;
}
static HRESULT STDMETHODCALLTYPE p_int(Face *f, HSTRING text, void **out)
{
    char t[DEC_MAX];
    UINT64 v;
    BOOL neg;
    if (!out) return E_POINTER;
    *out = 0;
    if (!normalize(OBJ(Num, f), glob_raw(text), t, DEC_MAX) || !parse_whole(t, &v, &neg)) return S_OK;
    if (neg ? v > (UINT64)1 << 63 : v > ((UINT64)1 << 63) - 1) return S_OK;
    INT64 r = neg ? (INT64)(0 - v) : (INT64)v;
    return box(12, &IID_IReferenceInt64_, &r, out);    /* CreateInt64 */
}
static HRESULT STDMETHODCALLTYPE p_uint(Face *f, HSTRING text, void **out)
{
    char t[DEC_MAX];
    UINT64 v;
    BOOL neg;
    if (!out) return E_POINTER;
    *out = 0;
    if (!normalize(OBJ(Num, f), glob_raw(text), t, DEC_MAX) || !parse_whole(t, &v, &neg) || (neg && v)) return S_OK;
    return box(13, &IID_IReferenceUInt64_, &v, out);   /* CreateUInt64 */
}
static HRESULT STDMETHODCALLTYPE p_double(Face *f, HSTRING text, void **out)
{
    char t[DEC_MAX];
    if (!out) return E_POINTER;
    *out = 0;
    if (!normalize(OBJ(Num, f), glob_raw(text), t, DEC_MAX)) return S_OK;
    double v = strtod(t, 0);
    return box(15, &IID_IReferenceDouble_, &v, out);   /* CreateDouble */
}

/* ---- options ------------------------------------------------------- */
static HRESULT STDMETHODCALLTYPE o_languages(Face *f, void **v) { return glob_langs_view(&OBJ(Num, f)->langs, v); }
static HRESULT STDMETHODCALLTYPE o_region(Face *f, HSTRING *v) { return glob_str(OBJ(Num, f)->langs.region, v); }
#define INT_OPTION(get, set, field) \
    static HRESULT STDMETHODCALLTYPE get(Face *f, INT32 *v) { if (!v) return E_POINTER; *v = OBJ(Num, f)->field; return S_OK; } \
    static HRESULT STDMETHODCALLTYPE set(Face *f, INT32 v) { if (v < 0 || v > 64) return E_INVALIDARG; OBJ(Num, f)->field = v; return S_OK; }
#define BOOL_OPTION(get, set, field) \
    static HRESULT STDMETHODCALLTYPE get(Face *f, boolean_t *v) { if (!v) return E_POINTER; *v = (boolean_t)OBJ(Num, f)->field; return S_OK; } \
    static HRESULT STDMETHODCALLTYPE set(Face *f, boolean_t v) { OBJ(Num, f)->field = v != 0; return S_OK; }
INT_OPTION(o_get_int, o_put_int, integer_digits)
INT_OPTION(o_get_frac, o_put_frac, fraction_digits)
INT_OPTION(o_get_sig, o_put_sig, significant_digits)
BOOL_OPTION(o_get_grouped, o_put_grouped, grouped)
BOOL_OPTION(o_get_point, o_put_point, point_always)
BOOL_OPTION(o_get_zero, o_put_zero, zero_signed)
static HRESULT STDMETHODCALLTYPE o_get_numeral(Face *f, HSTRING *v) { return glob_str(OBJ(Num, f)->numeral, v); }
static HRESULT STDMETHODCALLTYPE o_put_numeral(Face *f, HSTRING v)
{
    const WCHAR *s = glob_raw(v);
    if (!glob_numeral_zero(s) || lstrlenW(s) >= 16) return E_INVALIDARG;
    lstrcpyW(OBJ(Num, f)->numeral, *s ? s : L"Latn");
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE o_resolved(Face *f, HSTRING *v) { return glob_str(OBJ(Num, f)->langs.resolved, v); }
static HRESULT STDMETHODCALLTYPE o_get_rounder(Face *f, void **v)
{
    if (!v) return E_POINTER;
    *v = OBJ(Num, f)->rounder;
    glob_ref(*v);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE o_put_rounder(Face *f, void *r)
{
    Num *m = OBJ(Num, f);
    glob_ref(r);
    glob_unref(m->rounder);
    m->rounder = r;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_get_currency(Face *f, HSTRING *v) { return glob_str(OBJ(Num, f)->code, v); }
static BOOL valid_code(const WCHAR *c)
{
    if (lstrlenW(c) != 3) return FALSE;
    for (int i = 0; i < 3; i++) if (!((c[i] >= 'A' && c[i] <= 'Z') || (c[i] >= 'a' && c[i] <= 'z'))) return FALSE;
    return TRUE;
}
static void set_code(Num *m, const WCHAR *c)
{
    for (int i = 0; i < 3; i++) m->code[i] = (WCHAR)(c[i] >= 'a' ? c[i] - 32 : c[i]);
    m->code[3] = 0;
    m->fraction_digits = currency_digits(m);
}
static HRESULT STDMETHODCALLTYPE c_put_currency(Face *f, HSTRING v)
{
    const WCHAR *c = glob_raw(v);
    if (!valid_code(c)) return E_INVALIDARG;
    set_code(OBJ(Num, f), c);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_get_mode(Face *f, int *v) { if (!v) return E_POINTER; *v = OBJ(Num, f)->mode; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_put_mode(Face *f, int v) { if (v != 0 && v != 1) return E_INVALIDARG; OBJ(Num, f)->mode = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_apply_rounding(Face *f, int alg)
{
    Num *m = OBJ(Num, f);
    if (alg < 0 || alg > 10) return E_INVALIDARG;
    m->rounding = alg;
    m->fraction_digits = currency_digits(m);
    return S_OK;
}

static const void *const fmt_vt[] = { GLOB_INSP, (void *)f_int, (void *)f_uint, (void *)f_double };
static const void *const opt_vt[] = { GLOB_INSP, (void *)o_languages, (void *)o_region, (void *)o_get_int, (void *)o_put_int,
                                      (void *)o_get_frac, (void *)o_put_frac, (void *)o_get_grouped, (void *)o_put_grouped,
                                      (void *)o_get_point, (void *)o_put_point, (void *)o_get_numeral, (void *)o_put_numeral,
                                      (void *)o_resolved, (void *)o_region };
static const void *const parser_vt[] = { GLOB_INSP, (void *)p_int, (void *)p_uint, (void *)p_double };
static const void *const sig_vt[] = { GLOB_INSP, (void *)o_get_sig, (void *)o_put_sig };
static const void *const rounder_vt[] = { GLOB_INSP, (void *)o_get_rounder, (void *)o_put_rounder };
static const void *const zero_vt[] = { GLOB_INSP, (void *)o_get_zero, (void *)o_put_zero };
static const void *const cur_vt[] = { GLOB_INSP, (void *)c_get_currency, (void *)c_put_currency };
static const void *const cur2_vt[] = { GLOB_INSP, (void *)c_get_mode, (void *)c_put_mode, (void *)c_apply_rounding };

static void num_destroy(GObj *o)
{
    Num *m = (Num *)o;
    glob_langs_free(&m->langs);
    glob_unref(m->rounder);
}
static const GUID *const dec_iids[] = { &IID_INumberFormatter_, &IID_INumberFormatterOptions_, &IID_INumberFormatter2_,
                                        &IID_INumberParser_, &IID_ISignificantDigitsOption_, &IID_INumberRounderOption_,
                                        &IID_ISignedZeroOption_ };
static const void *const *const dec_vts[] = { fmt_vt, opt_vt, fmt_vt, parser_vt, sig_vt, rounder_vt, zero_vt };
static const GClass dec_class = { L"Windows.Globalization.NumberFormatting.DecimalFormatter", 7, dec_iids, dec_vts, num_destroy, FALSE };
static const GUID *const cur_iids[] = { &IID_ICurrencyFormatter_, &IID_INumberParser_, &IID_INumberFormatter2_,
                                        &IID_INumberFormatter_, &IID_INumberFormatterOptions_, &IID_ICurrencyFormatter2_,
                                        &IID_ISignificantDigitsOption_, &IID_INumberRounderOption_, &IID_ISignedZeroOption_ };
static const void *const *const cur_vts[] = { cur_vt, parser_vt, fmt_vt, fmt_vt, opt_vt, cur2_vt, sig_vt, rounder_vt, zero_vt };
static const GClass cur_class = { L"Windows.Globalization.NumberFormatting.CurrencyFormatter", 9, cur_iids, cur_vts, num_destroy, FALSE };

static HRESULT num_new(BOOL currency, const WCHAR *code, void *languages, HSTRING region, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (currency && !valid_code(code)) return E_INVALIDARG;
    Num *m = glob_new(currency ? &cur_class : &dec_class, sizeof *m);
    if (!m) return E_OUTOFMEMORY;
    HRESULT hr = glob_langs(&m->langs, languages, region);
    if (FAILED(hr)) { glob_free(m); return hr; }
    lstrcpyW(m->locale, m->langs.resolved);
    m->integer_digits = 1;
    lstrcpyW(m->numeral, L"Latn");
    m->currency = currency;
    if (currency) { m->grouped = TRUE; set_code(m, code); }
    *out = &m->base.face[0];
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE df_activate(Face *f, void **v) { (void)f; return num_new(FALSE, 0, 0, 0, v); }
static HRESULT STDMETHODCALLTYPE df_create(Face *f, void *languages, HSTRING region, void **v)
{
    (void)f;
    if (!languages) return E_INVALIDARG;
    return num_new(FALSE, 0, languages, region, v);
}
static HRESULT STDMETHODCALLTYPE cf_code(Face *f, HSTRING code, void **v) { (void)f; return num_new(TRUE, glob_raw(code), 0, 0, v); }
static HRESULT STDMETHODCALLTYPE cf_context(Face *f, HSTRING code, void *languages, HSTRING region, void **v)
{
    (void)f;
    if (!languages) return E_INVALIDARG;
    return num_new(TRUE, glob_raw(code), languages, region, v);
}
static const void *const dfa_vt[] = { GLOB_INSP, (void *)df_activate };
static const void *const df_vt[] = { GLOB_INSP, (void *)df_create };
static const GUID *const df_iids[] = { &IID_IActivationFactory_, &IID_IDecimalFormatterFactory_ };
static const void *const *const df_vts[] = { dfa_vt, df_vt };
static const GClass df_class = { L"Windows.Globalization.NumberFormatting.DecimalFormatter", 2, df_iids, df_vts, 0, TRUE };
static GObj g_df;
static const void *const cfa_vt[] = { GLOB_INSP, (void *)glob_no_activate };
static const void *const cf_vt[] = { GLOB_INSP, (void *)cf_code, (void *)cf_context };
static const GUID *const cf_iids[] = { &IID_IActivationFactory_, &IID_ICurrencyFormatterFactory_ };
static const void *const *const cf_vts[] = { cfa_vt, cf_vt };
static const GClass cf_class = { L"Windows.Globalization.NumberFormatting.CurrencyFormatter", 2, cf_iids, cf_vts, 0, TRUE };
static GObj g_cf;

HRESULT glob_number_factory(const WCHAR *cls, void **out)
{
    if (!lstrcmpW(cls, df_class.name)) { glob_static_init(&g_df, &df_class); *out = &g_df.face[0]; return S_OK; }
    if (!lstrcmpW(cls, cf_class.name)) { glob_static_init(&g_cf, &cf_class); *out = &g_cf.face[0]; return S_OK; }
    return CLASS_E_CLASSNOTAVAILABLE;
}
