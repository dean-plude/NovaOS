/* wrtglobtest.exe — the Windows.Globalization classes Windows Calculator
 * asks for (Windows.Globalization.dll), through RoGetActivationFactory:
 * ApplicationLanguages (en-US, then a PrimaryLanguageOverride first);
 * Language (a tag's spelling, names, script, layout direction, extension
 * subtags; IsWellFormed); GeographicRegion (the user's: US, USA, USD; DE);
 * GlobalizationPreferences; CalendarIdentifiers and ClockIdentifiers;
 * DecimalFormatter (Calculator's digits 0-9, grouping, fraction digits,
 * negative numbers, German separators, parsing back); CurrencyFormatter
 * (dollars by symbol and by code, yen's no decimals, rounding);
 * Calendar (a leap day's fields and names, adding years and days, the
 * 12- and 24-hour clocks, cloning and comparing) and DateTimeFormatter
 * (longdate, shorttime, a pattern, Patterns, Include...). */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <roapi.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define VT(o, i, T) ((T)((void **)*(void **)(o))[i])
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(void *);
typedef HRESULT (STDMETHODCALLTYPE *QI_t)(void *, const GUID *, void **);
typedef HRESULT (STDMETHODCALLTYPE *Get_t)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *PutS_t)(void *, HSTRING);
typedef HRESULT (STDMETHODCALLTYPE *PutI_t)(void *, INT32);
typedef HRESULT (STDMETHODCALLTYPE *GetS_t)(void *, HSTRING, void *);
typedef HRESULT (STDMETHODCALLTYPE *FmtU_t)(void *, UINT64, HSTRING *);
typedef HRESULT (STDMETHODCALLTYPE *FmtI_t)(void *, INT64, HSTRING *);
typedef HRESULT (STDMETHODCALLTYPE *FmtD_t)(void *, double, HSTRING *);
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)

static const GUID IID_IActivationFactory_T = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IApplicationLanguagesStatics_T = { 0x75B40847, 0x0A4C, 0x4A92, { 0x95, 0x65, 0xFD, 0x63, 0xC9, 0x5F, 0x7A, 0xED } };
static const GUID IID_ILanguageFactory_T = { 0x9B0252AC, 0x0C27, 0x44F8, { 0xB7, 0x92, 0x97, 0x93, 0xFB, 0x66, 0xC6, 0x3E } };
static const GUID IID_ILanguageStatics_T = { 0xB23CD557, 0x0865, 0x46D4, { 0x89, 0xB8, 0xD5, 0x9B, 0xE8, 0x99, 0x0F, 0x0D } };
static const GUID IID_ILanguage2_T = { 0x6A47E5B5, 0xD94D, 0x4886, { 0xA4, 0x04, 0xA5, 0xA5, 0xB9, 0xD5, 0xB4, 0x94 } };
static const GUID IID_ILanguageExtensionSubtags_T = { 0x7D7DAF45, 0x368D, 0x4364, { 0x85, 0x2B, 0xDE, 0xC9, 0x27, 0x03, 0x7B, 0x85 } };
static const GUID IID_IGeographicRegionFactory_T = { 0x53425270, 0x77B4, 0x426B, { 0x85, 0x9F, 0x81, 0xE1, 0x9D, 0x51, 0x25, 0x46 } };
static const GUID IID_IGeographicRegionStatics_T = { 0x29E28974, 0x7AD9, 0x4EF4, { 0x87, 0x99, 0xB3, 0xB4, 0x4F, 0xAD, 0xEC, 0x08 } };
static const GUID IID_IGlobalizationPreferencesStatics_T = { 0x01BF4326, 0xED37, 0x4E96, { 0xB0, 0xE9, 0xC1, 0x34, 0x0D, 0x1E, 0xA1, 0x58 } };
static const GUID IID_ICalendarIdentifiersStatics_T = { 0x80653F68, 0x2CB2, 0x4C1F, { 0xB5, 0x90, 0xF0, 0xF5, 0x2B, 0xF4, 0xFD, 0x1A } };
static const GUID IID_IClockIdentifiersStatics_T = { 0x523805BB, 0x12EC, 0x4F83, { 0xBC, 0x31, 0xB1, 0xB4, 0x37, 0x6B, 0x08, 0x08 } };
static const GUID IID_IDecimalFormatterFactory_T = { 0x0D018C9A, 0xE393, 0x46B8, { 0xB8, 0x30, 0x7A, 0x69, 0xC8, 0xF8, 0x9F, 0xBB } };
static const GUID IID_ICurrencyFormatterFactory_T = { 0x86C7537E, 0xB938, 0x4AA2, { 0x84, 0xB0, 0x2C, 0x33, 0xDC, 0x5B, 0x14, 0x50 } };
static const GUID IID_INumberFormatterOptions_T = { 0x80332D21, 0xAEE1, 0x4A39, { 0xBA, 0xA2, 0x07, 0xED, 0x8C, 0x96, 0xDA, 0xF6 } };
static const GUID IID_INumberFormatter2_T = { 0xD4A8C1F0, 0x80D0, 0x4B0D, { 0xA8, 0x9E, 0x88, 0x2C, 0x1E, 0x8F, 0x83, 0x10 } };
static const GUID IID_INumberParser_T = { 0xE6659412, 0x4A13, 0x4A53, { 0x83, 0xA1, 0x39, 0x2F, 0xBE, 0x4C, 0xFF, 0x9F } };
static const GUID IID_ICurrencyFormatter2_T = { 0x072C2F1D, 0xE7BA, 0x4197, { 0x92, 0x0E, 0x24, 0x7C, 0x92, 0xF7, 0xDE, 0xA6 } };
static const GUID IID_ICalendar_T = { 0xCA30221D, 0x86D9, 0x40FB, { 0xA2, 0x6B, 0xD4, 0x4E, 0xB7, 0xCF, 0x08, 0xEA } };
static const GUID IID_ITimeZoneOnCalendar_T = { 0xBB3C25E5, 0x46CF, 0x4317, { 0xA3, 0xF5, 0x02, 0x62, 0x1A, 0xD5, 0x44, 0x78 } };
static const GUID IID_IDateTimeFormatterFactory_T = { 0xEC8D8A53, 0x1A2E, 0x412D, { 0x88, 0x15, 0x3B, 0x74, 0x5F, 0xB1, 0xA2, 0xA0 } };
static const GUID IID_IDateTimeFormatter2_T = { 0x27C91A86, 0xBDAA, 0x4FD0, { 0x9E, 0x36, 0x67, 0x1D, 0x5A, 0xA5, 0xEE, 0x03 } };
static const GUID IID_IIterableString_T = { 0xE2FCC7C1, 0x3BFC, 0x5A0B, { 0xB2, 0xB0, 0x72, 0xE7, 0x69, 0xD1, 0xCB, 0x7E } };
#define LEAP_DAY 133536836960000000LL      /* 2024-02-29 12:34:56 UTC, a Thursday */

static HSTRING hs(const WCHAR *s) { HSTRING h = 0; WindowsCreateString(s, (UINT32)wcslen(s), &h); return h; }
static const WCHAR *raw(HSTRING h) { const WCHAR *r = WindowsGetStringRawBuffer(h, 0); return r ? r : L""; }
/* @h is @s; @h is freed */
static int is(HSTRING h, const WCHAR *s)
{
    int r = !wcscmp(raw(h), s);
    if (!r) printf("wrtglobtest: got \"%ls\", wanted \"%ls\"\n", raw(h), s);
    WindowsDeleteString(h);
    return r;
}
static HRESULT factory(const WCHAR *cls, const GUID *iid, void **out)
{
    HSTRING s = hs(cls);
    HRESULT hr = RoGetActivationFactory(s, iid, out);
    WindowsDeleteString(s);
    if (hr != S_OK) printf("wrtglobtest: RoGetActivationFactory(%ls): 0x%08lX\n", cls, (unsigned long)hr);
    return hr;
}
/* a string getter's value */
static HSTRING str(void *o, int slot) { HSTRING s = 0; if (o) VT(o, slot, Get_t)(o, &s); return s; }
/* an IVectorView<String>'s first item and size */
static HSTRING first(void *view, UINT32 *size)
{
    HSTRING s = 0;
    UINT32 n = 0;
    if (!view) { if (size) *size = 0; return 0; }
    VT(view, 7, Get_t)(view, &n);
    if (n) VT(view, 6, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32, HSTRING *))(view, 0, &s);
    if (size) *size = n;
    return s;
}
static void *qi(void *o, const GUID *iid) { void *r = 0; if (o) VT(o, 0, QI_t)(o, iid, &r); return r; }

static void *g_langs;          /* ApplicationLanguages' statics */
/* an IIterable<String> of one language (the app's languages with it as the override) */
static void *one_language(const WCHAR *tag)
{
    void *view = 0, *it;
    HSTRING t = hs(tag);
    VT(g_langs, 7, PutS_t)(g_langs, t);
    WindowsDeleteString(t);
    VT(g_langs, 8, Get_t)(g_langs, &view);
    it = qi(view, &IID_IIterableString_T);
    RELEASE(view);
    VT(g_langs, 7, PutS_t)(g_langs, 0);
    return it;
}

static void languages(void)
{
    void *view = 0;
    UINT32 n = 0;
    CHECK("ApplicationLanguages' statics", factory(L"Windows.Globalization.ApplicationLanguages", &IID_IApplicationLanguagesStatics_T, &g_langs) == S_OK);
    if (!g_langs) return;
    VT(g_langs, 8, Get_t)(g_langs, &view);
    CHECK("Languages: en-US", is(first(view, &n), L"en-US") && n == 1);
    RELEASE(view);
    HSTRING fr = hs(L"fr-FR");
    CHECK("PrimaryLanguageOverride fr-FR", VT(g_langs, 7, PutS_t)(g_langs, fr) == S_OK);
    WindowsDeleteString(fr);
    CHECK("read back", is(str(g_langs, 6), L"fr-FR"));
    VT(g_langs, 8, Get_t)(g_langs, &view);
    CHECK("Languages: the override first, then en-US", is(first(view, &n), L"fr-FR") && n == 2);
    RELEASE(view);
    VT(g_langs, 7, PutS_t)(g_langs, 0);
    CHECK("override cleared", WindowsGetStringLen(str(g_langs, 6)) == 0);
    HSTRING bad = hs(L"not a tag");
    CHECK("a malformed override: refused", VT(g_langs, 7, PutS_t)(g_langs, bad) == E_INVALIDARG);
    WindowsDeleteString(bad);

    void *lf = 0, *ls = 0, *lang = 0, *l2 = 0, *ext = 0, *subs = 0;
    factory(L"Windows.Globalization.Language", &IID_ILanguageFactory_T, &lf);
    factory(L"Windows.Globalization.Language", &IID_ILanguageStatics_T, &ls);
    if (!lf || !ls) { RELEASE(lf); RELEASE(ls); return; }
    HSTRING tag = hs(L"en-us-u-ca-gregory");
    VT(lf, 6, GetS_t)(lf, tag, &lang);
    WindowsDeleteString(tag);
    CHECK("Language(en-us-u-ca-gregory)", lang != 0);
    CHECK("its tag as Windows spells it", is(str(lang, 6), L"en-US-u-ca-gregory"));
    HSTRING dn = str(lang, 7);
    CHECK("DisplayName: English (United States)", wcsstr(raw(dn), L"English") != 0);
    WindowsDeleteString(dn);
    CHECK("Script: Latn", is(str(lang, 9), L"Latn"));
    int dir = -1;
    l2 = qi(lang, &IID_ILanguage2_T);
    if (l2) VT(l2, 6, Get_t)(l2, &dir);
    CHECK("LayoutDirection: Ltr", dir == 0);
    ext = qi(lang, &IID_ILanguageExtensionSubtags_T);
    HSTRING u = hs(L"u");
    if (ext) VT(ext, 6, GetS_t)(ext, u, &subs);
    WindowsDeleteString(u);
    CHECK("GetExtensionSubtags(u): ca, gregory", is(first(subs, &n), L"ca") && n == 2);
    RELEASE(subs); RELEASE(ext); RELEASE(l2); RELEASE(lang);
    tag = hs(L"ar-SA");
    VT(lf, 6, GetS_t)(lf, tag, &lang);
    WindowsDeleteString(tag);
    dir = -1;
    l2 = qi(lang, &IID_ILanguage2_T);
    if (l2) VT(l2, 6, Get_t)(l2, &dir);
    CHECK("ar-SA: Rtl, Arab", dir == 1 && is(str(lang, 9), L"Arab"));
    RELEASE(l2); RELEASE(lang);
    BYTE ok = 0, nope = 1;
    HSTRING good = hs(L"zh-Hant-TW"), worse = hs(L"1x-");
    VT(ls, 6, GetS_t)(ls, good, &ok);
    VT(ls, 6, GetS_t)(ls, worse, &nope);
    CHECK("IsWellFormed", ok == 1 && nope == 0);
    tag = hs(L"-");
    CHECK("Language(-): refused", VT(lf, 6, GetS_t)(lf, tag, &lang) == E_INVALIDARG && !lang);
    WindowsDeleteString(tag);
    WindowsDeleteString(good);
    WindowsDeleteString(worse);
    RELEASE(lf); RELEASE(ls);
}

static void regions(void)
{
    void *af = 0, *rf = 0, *rs = 0, *r = 0, *cur = 0, *gp = 0, *ci = 0, *clk = 0;
    UINT32 n = 0;
    factory(L"Windows.Globalization.GeographicRegion", &IID_IActivationFactory_T, &af);
    factory(L"Windows.Globalization.GeographicRegion", &IID_IGeographicRegionFactory_T, &rf);
    factory(L"Windows.Globalization.GeographicRegion", &IID_IGeographicRegionStatics_T, &rs);
    if (af) VT(af, 6, Get_t)(af, &r);
    CHECK("GeographicRegion(): the user's", r != 0);
    if (r) {
        CHECK("Code US", is(str(r, 6), L"US"));
        CHECK("CodeThreeLetter USA", is(str(r, 8), L"USA"));
        CHECK("CodeThreeDigit 840", is(str(r, 9), L"840"));
        VT(r, 12, Get_t)(r, &cur);
        CHECK("CurrenciesInUse: USD", is(first(cur, &n), L"USD") && n == 1);
        RELEASE(cur);
        RELEASE(r);
    }
    HSTRING de = hs(L"DE");
    if (rf) VT(rf, 6, GetS_t)(rf, de, &r);
    WindowsDeleteString(de);
    if (r) {
        CHECK("GeographicRegion(DE): DEU", is(str(r, 8), L"DEU"));
        VT(r, 12, Get_t)(r, &cur);
        CHECK("its currency: EUR", is(first(cur, &n), L"EUR"));
        RELEASE(cur);
        RELEASE(r);
    } else CHECK("GeographicRegion(DE)", 0);
    BYTE yes = 0, no = 1;
    HSTRING fra = hs(L"FRA"), zz = hs(L"ZZ");
    if (rs) { VT(rs, 6, GetS_t)(rs, fra, &yes); VT(rs, 6, GetS_t)(rs, zz, &no); }
    CHECK("IsSupported(FRA), not ZZ", yes == 1 && no == 0);
    WindowsDeleteString(fra);
    WindowsDeleteString(zz);
    RELEASE(af); RELEASE(rf); RELEASE(rs);

    factory(L"Windows.System.UserProfile.GlobalizationPreferences", &IID_IGlobalizationPreferencesStatics_T, &gp);
    if (gp) {
        void *v = 0;
        CHECK("HomeGeographicRegion US", is(str(gp, 10), L"US"));
        VT(gp, 8, Get_t)(gp, &v);
        CHECK("Currencies: USD", is(first(v, 0), L"USD"));
        RELEASE(v);
        VT(gp, 6, Get_t)(gp, &v);
        CHECK("Calendars: GregorianCalendar", is(first(v, 0), L"GregorianCalendar"));
        RELEASE(v);
        VT(gp, 7, Get_t)(gp, &v);
        CHECK("Clocks: 12HourClock", is(first(v, 0), L"12HourClock"));
        RELEASE(v);
        int dow = -1;
        VT(gp, 11, Get_t)(gp, &dow);
        CHECK("WeekStartsOn Sunday", dow == 0);
        RELEASE(gp);
    } else CHECK("GlobalizationPreferences", 0);
    factory(L"Windows.Globalization.CalendarIdentifiers", &IID_ICalendarIdentifiersStatics_T, &ci);
    CHECK("CalendarIdentifiers.Gregorian", ci && is(str(ci, 6), L"GregorianCalendar"));
    factory(L"Windows.Globalization.ClockIdentifiers", &IID_IClockIdentifiersStatics_T, &clk);
    CHECK("ClockIdentifiers", clk && is(str(clk, 6), L"12HourClock") && is(str(clk, 7), L"24HourClock"));
    RELEASE(ci); RELEASE(clk);
}

static HSTRING fmt_u(void *f2, UINT64 v) { HSTRING s = 0; VT(f2, 7, FmtU_t)(f2, v, &s); return s; }
static HSTRING fmt_i(void *f2, INT64 v) { HSTRING s = 0; VT(f2, 6, FmtI_t)(f2, v, &s); return s; }
static HSTRING fmt_d(void *f2, double v) { HSTRING s = 0; VT(f2, 8, FmtD_t)(f2, v, &s); return s; }
static void numbers(void)
{
    void *af = 0, *df = 0, *d = 0, *f2 = 0, *opt = 0, *parser = 0, *box = 0;
    factory(L"Windows.Globalization.NumberFormatting.DecimalFormatter", &IID_IActivationFactory_T, &af);
    factory(L"Windows.Globalization.NumberFormatting.DecimalFormatter", &IID_IDecimalFormatterFactory_T, &df);
    if (af) VT(af, 6, Get_t)(af, &d);
    CHECK("DecimalFormatter()", d != 0);
    f2 = qi(d, &IID_INumberFormatter2_T);
    opt = qi(d, &IID_INumberFormatterOptions_T);
    if (f2 && opt) {
        /* as Calculator's LocalizationSettings reads its digits */
        VT(opt, 11, PutI_t)(opt, 0);
        VT(opt, 15, PutI_t)(opt, 0);
        int digits = 0;
        WCHAR want[2] = { 0, 0 };
        for (int i = 0; i < 10; i++) { want[0] = (WCHAR)('0' + i); digits += is(fmt_u(f2, (UINT64)i), want); }
        CHECK("FormatUInt 0-9", digits == 10);
        CHECK("ResolvedLanguage en-US", is(str(opt, 18), L"en-US"));
        CHECK("FormatDouble(1234.5)", is(fmt_d(f2, 1234.5), L"1234.5"));
        CHECK("FormatDouble(0.1)", is(fmt_d(f2, 0.1), L"0.1"));
        CHECK("FormatInt(-42)", is(fmt_i(f2, -42), L"-42"));
        VT(opt, 13, PutI_t)(opt, 1);                 /* IsGrouped */
        CHECK("grouped", is(fmt_d(f2, 1234567.25), L"1,234,567.25"));
        VT(opt, 11, PutI_t)(opt, 2);                 /* FractionDigits */
        CHECK("two fraction digits", is(fmt_d(f2, 1234.5), L"1,234.50"));
        CHECK("an integer's too", is(fmt_u(f2, 18446744073709551615ULL), L"18,446,744,073,709,551,615.00"));
        VT(opt, 9, PutI_t)(opt, 3);                  /* IntegerDigits */
        CHECK("three integer digits", is(fmt_d(f2, 0.5), L"000.50"));
        parser = qi(d, &IID_INumberParser_T);
        HSTRING t = hs(L"-1,234.5");
        if (parser) VT(parser, 8, GetS_t)(parser, t, &box);
        WindowsDeleteString(t);
        double back = 0;
        if (box) VT(box, 6, Get_t)(box, &back);
        CHECK("ParseDouble(-1,234.5)", back == -1234.5);
        RELEASE(box);
        t = hs(L"12abc");
        if (parser) VT(parser, 8, GetS_t)(parser, t, &box);
        WindowsDeleteString(t);
        CHECK("ParseDouble(12abc): none", !box);
        RELEASE(parser);
    }
    RELEASE(f2); RELEASE(opt); RELEASE(d);
    void *de = one_language(L"de-DE");
    HSTRING region = hs(L"DE");
    if (df && de) VT(df, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void *, HSTRING, void **))(df, de, region, &d);
    WindowsDeleteString(region);
    CHECK("DecimalFormatter([de-DE], DE)", d != 0);
    f2 = qi(d, &IID_INumberFormatter2_T);
    opt = qi(d, &IID_INumberFormatterOptions_T);
    if (f2 && opt) {
        VT(opt, 13, PutI_t)(opt, 1);
        CHECK("German separators", is(fmt_d(f2, 1234.5), L"1.234,5"));
        CHECK("ResolvedGeographicRegion DE", is(str(opt, 19), L"DE"));
    }
    RELEASE(f2); RELEASE(opt); RELEASE(d); RELEASE(de); RELEASE(af); RELEASE(df);

    void *cf = 0, *c = 0, *c2 = 0;
    factory(L"Windows.Globalization.NumberFormatting.CurrencyFormatter", &IID_ICurrencyFormatterFactory_T, &cf);
    HSTRING usd = hs(L"USD"), jpy = hs(L"JPY");
    if (cf) VT(cf, 6, GetS_t)(cf, usd, &c);
    CHECK("CurrencyFormatter(USD)", c != 0);
    f2 = qi(c, &IID_INumberFormatter2_T);
    c2 = qi(c, &IID_ICurrencyFormatter2_T);
    if (f2 && c2) {
        CHECK("$1,234.50", is(fmt_d(f2, 1234.5), L"$1,234.50"));
        HSTRING neg = fmt_d(f2, -3);
        CHECK("-3: the sign and $3.00", wcsstr(raw(neg), L"$3.00") && (wcschr(raw(neg), '-') || wcschr(raw(neg), '(')));
        WindowsDeleteString(neg);
        VT(c2, 7, PutI_t)(c2, 1);                    /* Mode: UseCurrencyCode */
        CHECK("by code", is(fmt_d(f2, 1234.5), L"USD 1,234.50"));
        VT(c2, 7, PutI_t)(c2, 0);
        VT(c2, 8, PutI_t)(c2, 8);                    /* ApplyRoundingForCurrency(RoundHalfAwayFromZero) */
        CHECK("rounded half away from zero", is(fmt_d(f2, 2.345), L"$2.35") && is(fmt_d(f2, 2.344), L"$2.34"));
    }
    RELEASE(f2); RELEASE(c2); RELEASE(c);
    if (cf) VT(cf, 6, GetS_t)(cf, jpy, &c);
    f2 = qi(c, &IID_INumberFormatter2_T);
    CHECK("yen: no decimals", f2 && is(fmt_u(f2, 1234), L"JPY 1,234"));
    RELEASE(f2); RELEASE(c);
    HSTRING bad = hs(L"US");
    CHECK("CurrencyFormatter(US): refused", cf && VT(cf, 6, GetS_t)(cf, bad, &c) == E_INVALIDARG);
    WindowsDeleteString(bad);
    WindowsDeleteString(usd);
    WindowsDeleteString(jpy);
    RELEASE(cf);
}

static INT32 geti(void *o, int slot) { INT32 v = -1; VT(o, slot, Get_t)(o, &v); return v; }
static void calendar(void)
{
    void *af = 0, *cal = 0, *zone = 0, *clone = 0;
    factory(L"Windows.Globalization.Calendar", &IID_IActivationFactory_T, &af);
    void *insp = 0;
    if (af) VT(af, 6, Get_t)(af, &insp);
    cal = qi(insp, &IID_ICalendar_T);
    RELEASE(insp);
    CHECK("Calendar()", cal != 0);
    if (!cal) { RELEASE(af); return; }
    CHECK("GregorianCalendar, 12HourClock", is(str(cal, 12), L"GregorianCalendar") && is(str(cal, 14), L"12HourClock"));
    zone = qi(cal, &IID_ITimeZoneOnCalendar_T);
    HSTRING utc = hs(L"UTC");
    CHECK("ChangeTimeZone(UTC)", zone && VT(zone, 7, PutS_t)(zone, utc) == S_OK);
    WindowsDeleteString(utc);
    CHECK("GetTimeZone", zone && is(str(zone, 6), L"UTC"));
    VT(cal, 17, HRESULT (STDMETHODCALLTYPE *)(void *, INT64))(cal, LEAP_DAY);
    CHECK("2024-02-29", geti(cal, 30) == 2024 && geti(cal, 39) == 2 && geti(cal, 52) == 29);
    CHECK("Thursday", geti(cal, 57) == 4 && is(str(cal, 58), L"Thursday"));
    CHECK("February", is(str(cal, 42), L"February"));
    CHECK("days in February 2024", geti(cal, 51) == 29);
    CHECK("12:34:56 PM", geti(cal, 73) == 12 && geti(cal, 65) == 2 && geti(cal, 78) == 34 && geti(cal, 83) == 56);
    INT64 back = 0;
    VT(cal, 16, Get_t)(cal, &back);
    CHECK("GetDateTime: the same instant", back == LEAP_DAY);
    VT(cal, 6, Get_t)(cal, &clone);
    INT32 order = 9;
    if (clone) VT(cal, 93, HRESULT (STDMETHODCALLTYPE *)(void *, void *, INT32 *))(cal, clone, &order);
    CHECK("Clone compares equal", order == 0);
    VT(cal, 32, PutI_t)(cal, 1);                     /* AddYears */
    CHECK("a year on: 2025-02-28", geti(cal, 30) == 2025 && geti(cal, 39) == 2 && geti(cal, 52) == 28);
    VT(cal, 54, PutI_t)(cal, 1);                     /* AddDays */
    CHECK("a day on: March 1", geti(cal, 39) == 3 && geti(cal, 52) == 1);
    if (clone) VT(cal, 93, HRESULT (STDMETHODCALLTYPE *)(void *, void *, INT32 *))(cal, clone, &order);
    CHECK("now later than the clone", order == 1);
    HSTRING h24 = hs(L"24HourClock");
    VT(cal, 15, PutS_t)(cal, h24);
    WindowsDeleteString(h24);
    VT(cal, 74, PutI_t)(cal, 23);                    /* Hour */
    CHECK("24-hour clock: hour 23, one period", geti(cal, 73) == 23 && geti(cal, 63) == 1);
    CHECK("Day 31 of March", VT(cal, 53, PutI_t)(cal, 31) == S_OK && geti(cal, 52) == 31);
    CHECK("no April 31", VT(cal, 40, PutI_t)(cal, 4) == S_OK && geti(cal, 52) == 30 && VT(cal, 53, PutI_t)(cal, 31) == E_INVALIDARG);
    RELEASE(clone); RELEASE(zone); RELEASE(cal); RELEASE(af);
}

static HSTRING format_utc(void *fmt)
{
    void *f2 = qi(fmt, &IID_IDateTimeFormatter2_T);
    HSTRING s = 0, utc = hs(L"UTC");
    if (f2) VT(f2, 6, HRESULT (STDMETHODCALLTYPE *)(void *, INT64, HSTRING, HSTRING *))(f2, LEAP_DAY, utc, &s);
    WindowsDeleteString(utc);
    RELEASE(f2);
    return s;
}
static void *formatter(void *ff, const WCHAR *t)
{
    void *f = 0;
    HSTRING s = hs(t);
    VT(ff, 6, GetS_t)(ff, s, &f);
    WindowsDeleteString(s);
    if (!f) printf("wrtglobtest: DateTimeFormatter(%ls) refused\n", t);
    return f;
}
static void dates(void)
{
    void *ff = 0, *f = 0, *pats = 0;
    factory(L"Windows.Globalization.DateTimeFormatting.DateTimeFormatter", &IID_IDateTimeFormatterFactory_T, &ff);
    if (!ff) return;
    f = formatter(ff, L"longdate");
    CHECK("longdate", f && is(format_utc(f), L"Thursday, February 29, 2024"));
    CHECK("its IncludeYear Full, IncludeDayOfWeek Full", f && geti(f, 15) == 3 && geti(f, 17) == 3);
    CHECK("its Template", f && is(str(f, 13), L"longdate"));
    RELEASE(f);
    f = formatter(ff, L"shortdate");
    if (f) VT(f, 12, Get_t)(f, &pats);
    CHECK("shortdate: Patterns", is(first(pats, 0), L"{month.integer}/{day.integer}/{year.full}"));
    CHECK("shortdate", f && is(format_utc(f), L"2/29/2024"));
    RELEASE(pats); RELEASE(f);
    f = formatter(ff, L"shorttime");
    CHECK("shorttime", f && is(format_utc(f), L"12:34 PM"));
    RELEASE(f);
    f = formatter(ff, L"month day");
    CHECK("month day", f && is(format_utc(f), L"February 29"));
    RELEASE(f);
    f = formatter(ff, L"{dayofweek.abbreviated} {month.abbreviated} {day.integer(2)} {hour.integer}:{minute.integer(2)}");
    CHECK("a pattern", f && is(format_utc(f), L"Thu Feb 29 12:34"));
    RELEASE(f);
    HSTRING bad = hs(L"fortnight");
    CHECK("an unknown template: refused", VT(ff, 6, GetS_t)(ff, bad, &f) == E_INVALIDARG && !f);
    WindowsDeleteString(bad);
    RELEASE(ff);
}

int main(void)
{
    RoInitialize(1);
    languages();
    regions();
    numbers();
    calendar();
    dates();
    RELEASE(g_langs);
    printf("wrtglobtest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    return fail != 0;
}
