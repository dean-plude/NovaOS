/*
 * calendar.c — Windows.Globalization.Calendar: a date and time in the
 * Gregorian calendar (years 1 to 9999), in the machine's time zone or UTC,
 * read and changed field by field (era, year, month, day, period, hour,
 * minute, second, nanosecond), added to, compared, and its fields as text
 * in its language (kernel32's month and day names, its era and AM/PM
 * designators) and numeral system; with the 12- or 24-hour clock.
 *
 * Also the DateTime arithmetic datetime.c shares: a DateTime (100 ns ticks
 * since 1601, UTC) split into fields and back, in either zone.
 *
 * Not here yet: the other calendar systems (Hebrew, Hijri, Japanese ...;
 * ChangeCalendarSystem refuses them) and time zones other than UTC and the
 * machine's own (a zone is named by its Windows name, not an IANA one).
 */
#include "glob.h"

static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_ICalendarFactory_ = { 0x83F58412, 0xE56B, 0x4C75, { 0xA6, 0x6E, 0x0F, 0x63, 0xD5, 0x77, 0x58, 0xA6 } };
static const GUID IID_ICalendarFactory2_ = { 0xB44B378C, 0xCA7E, 0x4590, { 0x9E, 0x72, 0xEA, 0x2B, 0xEC, 0x1A, 0x51, 0x15 } };
static const GUID IID_ICalendar_ = { 0xCA30221D, 0x86D9, 0x40FB, { 0xA2, 0x6B, 0xD4, 0x4E, 0xB7, 0xCF, 0x08, 0xEA } };
static const GUID IID_ITimeZoneOnCalendar_ = { 0xBB3C25E5, 0x46CF, 0x4317, { 0xA3, 0xF5, 0x02, 0x62, 0x1A, 0xD5, 0x44, 0x78 } };

#define TICKS_SECOND 10000000LL
#define TICKS_DAY (86400LL * TICKS_SECOND)
#define DAYS_1601 (-134774LL)          /* 1601-01-01 counted from 1970-01-01 */

/* ---- days and fields (Howard Hinnant's civil calendar algorithms) --- */
static INT64 days_from_civil(INT64 y, int m, int d)
{
    y -= m <= 2;
    INT64 era = (y >= 0 ? y : y - 399) / 400;
    INT64 yoe = y - era * 400;
    INT64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    INT64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void civil_from_days(INT64 z, int *y, int *m, int *d)
{
    z += 719468;
    INT64 era = (z >= 0 ? z : z - 146096) / 146097;
    INT64 doe = z - era * 146097;
    INT64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    INT64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    INT64 mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}
static INT64 floor_div(INT64 a, INT64 b) { INT64 q = a / b; return (a % b && (a < 0) != (b < 0)) ? q - 1 : q; }
int glob_days_in_month(int year, int month)
{
    static const int n[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    BOOL leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : n[(month - 1) % 12];
}
void glob_split(INT64 ticks, GTime *t)
{
    INT64 days = floor_div(ticks, TICKS_DAY), rest = ticks - days * TICKS_DAY;
    civil_from_days(days + DAYS_1601, &t->year, &t->month, &t->day);
    t->dow = (int)(((days + DAYS_1601) % 7 + 7 + 4) % 7);          /* 1970-01-01 was a Thursday */
    t->hour = (int)(rest / (3600 * TICKS_SECOND));
    t->minute = (int)(rest / (60 * TICKS_SECOND) % 60);
    t->second = (int)(rest / TICKS_SECOND % 60);
    t->frac = rest % TICKS_SECOND;
}
INT64 glob_join(const GTime *t)
{
    return (days_from_civil(t->year, t->month, t->day) - DAYS_1601) * TICKS_DAY +
           ((INT64)t->hour * 3600 + t->minute * 60 + t->second) * TICKS_SECOND + t->frac;
}

/* ---- time zones ---------------------------------------------------- */
INT64 glob_to_zone(INT64 utc, BOOL utc_zone)
{
    TIME_ZONE_INFORMATION tz;
    if (utc_zone) return utc;
    if (utc >= 0) {
        FILETIME ft = { (DWORD)utc, (DWORD)(utc >> 32) };
        SYSTEMTIME st, lt;
        if (FileTimeToSystemTime(&ft, &st) && SystemTimeToTzSpecificLocalTime(0, &st, &lt) && SystemTimeToFileTime(&lt, &ft))
            return ((INT64)ft.dwHighDateTime << 32 | ft.dwLowDateTime) + utc % 10000;   /* (SYSTEMTIME keeps milliseconds) */
    }
    GetTimeZoneInformation(&tz);
    return utc - (INT64)tz.Bias * 60 * TICKS_SECOND;
}
INT64 glob_from_zone(INT64 local, BOOL utc_zone)
{
    if (utc_zone) return local;
    INT64 utc = local - (glob_to_zone(local, FALSE) - local);
    return local - (glob_to_zone(utc, FALSE) - utc);              /* the offset at that instant */
}
static INT64 now_utc(void)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return (INT64)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
}

/* ---- Calendar ------------------------------------------------------ */
typedef struct {
    GObj base;
    GLangs langs;
    WCHAR numeral[16];
    BOOL clock24;
    BOOL utc;                     /* its zone: UTC, else the machine's */
    INT64 local;                  /* the date and time in its zone, ticks since 1601 */
} Cal;
#define MIN_LOCAL ((days_from_civil(1, 1, 1) - DAYS_1601) * TICKS_DAY)
#define MAX_LOCAL ((days_from_civil(9999, 12, 31) - DAYS_1601 + 1) * TICKS_DAY - 1)
static const GClass cal_class;
static Cal *cal_of(void *face) { return OBJ(Cal, face); }
static void fields(Cal *c, GTime *t) { glob_split(c->local, t); }
static HRESULT set_fields(Cal *c, GTime *t)
{
    if (t->year < 1 || t->year > 9999) return E_INVALIDARG;
    c->local = glob_join(t);
    return S_OK;
}
static HRESULT set_local(Cal *c, INT64 v)
{
    if (v < MIN_LOCAL || v > MAX_LOCAL) return E_INVALIDARG;
    c->local = v;
    return S_OK;
}

/* text */
static HRESULT number(Cal *c, INT64 v, int min_digits, HSTRING *out)
{
    WCHAR s[32], t[32];
    int k = 0, n = 0;
    BOOL neg = v < 0;
    UINT64 u = neg ? (UINT64)0 - (UINT64)v : (UINT64)v;
    do { t[n++] = (WCHAR)('0' + u % 10); u /= 10; } while (u);
    while (n < min_digits && n < 30) t[n++] = '0';
    if (neg) s[k++] = '-';
    while (n) s[k++] = t[--n];
    s[k] = 0;
    glob_digits(s, c->numeral);
    return glob_str(s, out);
}
/* a name of @full (and its abbreviation @abbr), by ideal length: 0 the
 * abbreviation, else the full name if it fits */
static HRESULT name(Cal *c, LCTYPE full, LCTYPE abbr, int ideal, BOOL use_ideal, HSTRING *out)
{
    WCHAR f[80], a[80];
    glob_info(c->langs.resolved, full, f, 80);
    if (!use_ideal) return glob_str(f, out);
    glob_info(c->langs.resolved, abbr, a, 80);
    if (!a[0] || (ideal > 0 && lstrlenW(f) <= ideal)) return glob_str(f, out);
    return glob_str(a, out);
}
#define LOCALE_RETURN_GENITIVE_NAMES_ 0x10000000u

/* getters and setters by field */
#define GETTER(fn, expr) static HRESULT STDMETHODCALLTYPE fn(Face *f, INT32 *v) \
    { Cal *c = cal_of(f); GTime t; fields(c, &t); (void)t; (void)c; if (!v) return E_POINTER; *v = (INT32)(expr); return S_OK; }
#define CONST_GETTER(fn, value) static HRESULT STDMETHODCALLTYPE fn(Face *f, INT32 *v) { (void)f; if (!v) return E_POINTER; *v = (value); return S_OK; }

static HRESULT STDMETHODCALLTYPE c_clone(Face *f, void **out);
static HRESULT STDMETHODCALLTYPE c_min(Face *f) { cal_of(f)->local = MIN_LOCAL; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_max(Face *f) { cal_of(f)->local = MAX_LOCAL; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_languages(Face *f, void **v) { return glob_langs_view(&cal_of(f)->langs, v); }
static HRESULT STDMETHODCALLTYPE c_get_numeral(Face *f, HSTRING *v) { return glob_str(cal_of(f)->numeral, v); }
static HRESULT STDMETHODCALLTYPE c_put_numeral(Face *f, HSTRING v)
{
    const WCHAR *s = glob_raw(v);
    if (!glob_numeral_zero(s) || lstrlenW(s) >= 16) return E_INVALIDARG;
    lstrcpyW(cal_of(f)->numeral, *s ? s : L"Latn");
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_get_system(Face *f, HSTRING *v) { (void)f; return glob_str(L"GregorianCalendar", v); }
static HRESULT STDMETHODCALLTYPE c_change_system(Face *f, HSTRING v)
{
    (void)f;
    return lstrcmpW(glob_raw(v), L"GregorianCalendar") ? E_INVALIDARG : S_OK;
}
static HRESULT STDMETHODCALLTYPE c_get_clock(Face *f, HSTRING *v) { return glob_str(cal_of(f)->clock24 ? L"24HourClock" : L"12HourClock", v); }
static HRESULT clock_of(const WCHAR *s, BOOL *clock24)
{
    if (!lstrcmpW(s, L"24HourClock")) *clock24 = TRUE;
    else if (!lstrcmpW(s, L"12HourClock")) *clock24 = FALSE;
    else return E_INVALIDARG;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_change_clock(Face *f, HSTRING v) { return clock_of(glob_raw(v), &cal_of(f)->clock24); }
static HRESULT STDMETHODCALLTYPE c_get_datetime(Face *f, INT64 *v)
{
    Cal *c = cal_of(f);
    if (!v) return E_POINTER;
    *v = glob_from_zone(c->local, c->utc);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_set_datetime(Face *f, INT64 v) { Cal *c = cal_of(f); return set_local(c, glob_to_zone(v, c->utc)); }
static HRESULT STDMETHODCALLTYPE c_now(Face *f) { Cal *c = cal_of(f); c->local = glob_to_zone(now_utc(), c->utc); return S_OK; }

/* era: the Gregorian calendar's one, A.D. */
CONST_GETTER(c_first_era, 1) CONST_GETTER(c_last_era, 1) CONST_GETTER(c_eras, 1) CONST_GETTER(c_era, 1)
static HRESULT STDMETHODCALLTYPE c_put_era(Face *f, INT32 v) { (void)f; return v == 1 ? S_OK : E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE c_add_eras(Face *f, INT32 v) { (void)f; return v == 0 ? S_OK : E_INVALIDARG; }
static HRESULT era_text(Cal *c, HSTRING *out)
{
    WCHAR s[32];
    if (!GetCalendarInfoEx(c->langs.resolved, CAL_GREGORIAN, 0, CAL_SERASTRING, s, 32, 0) || !s[0]) lstrcpyW(s, L"A.D.");
    return glob_str(s, out);
}
static HRESULT STDMETHODCALLTYPE c_era_text(Face *f, HSTRING *v) { return era_text(cal_of(f), v); }
static HRESULT STDMETHODCALLTYPE c_era_text_n(Face *f, INT32 n, HSTRING *v) { (void)n; return era_text(cal_of(f), v); }
CONST_GETTER(c_first_year_era, 1) CONST_GETTER(c_last_year_era, 9999) CONST_GETTER(c_years_era, 9999)

/* year */
GETTER(c_year, t.year)
static HRESULT STDMETHODCALLTYPE c_put_year(Face *f, INT32 v)
{
    Cal *c = cal_of(f);
    GTime t;
    fields(c, &t);
    if (v < 1 || v > 9999) return E_INVALIDARG;
    t.year = v;
    if (t.day > glob_days_in_month(t.year, t.month)) t.day = glob_days_in_month(t.year, t.month);
    return set_fields(c, &t);
}
static HRESULT add_months(Cal *c, INT64 months)
{
    GTime t;
    fields(c, &t);
    INT64 m = (INT64)t.year * 12 + (t.month - 1) + months;
    if (m < 12 || m >= 10000LL * 12) return E_INVALIDARG;
    t.year = (int)(m / 12);
    t.month = (int)(m % 12) + 1;
    if (t.day > glob_days_in_month(t.year, t.month)) t.day = glob_days_in_month(t.year, t.month);
    return set_fields(c, &t);
}
static HRESULT STDMETHODCALLTYPE c_add_years(Face *f, INT32 v) { return add_months(cal_of(f), (INT64)v * 12); }
static HRESULT STDMETHODCALLTYPE c_year_text(Face *f, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.year, 1, v); }
static HRESULT STDMETHODCALLTYPE c_year_truncated(Face *f, INT32 n, HSTRING *v)
{
    GTime t;
    INT64 mod = 1;
    fields(cal_of(f), &t);
    if (n < 1) return E_INVALIDARG;
    for (int i = 0; i < n && i < 9; i++) mod *= 10;
    return number(cal_of(f), t.year % mod, n < 9 ? n : 9, v);
}
static HRESULT STDMETHODCALLTYPE c_year_padded(Face *f, INT32 n, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.year, n, v); }

/* month */
CONST_GETTER(c_first_month, 1) CONST_GETTER(c_last_month, 12) CONST_GETTER(c_months, 12)
GETTER(c_month, t.month)
static HRESULT STDMETHODCALLTYPE c_put_month(Face *f, INT32 v)
{
    Cal *c = cal_of(f);
    GTime t;
    fields(c, &t);
    if (v < 1 || v > 12) return E_INVALIDARG;
    t.month = v;
    if (t.day > glob_days_in_month(t.year, t.month)) t.day = glob_days_in_month(t.year, t.month);
    return set_fields(c, &t);
}
static HRESULT STDMETHODCALLTYPE c_add_months(Face *f, INT32 v) { return add_months(cal_of(f), v); }
static HRESULT month_text(Face *f, BOOL solo, int ideal, BOOL use_ideal, HSTRING *v)
{
    GTime t;
    fields(cal_of(f), &t);
    LCTYPE gen = solo ? 0 : LOCALE_RETURN_GENITIVE_NAMES_;
    return name(cal_of(f), (GLOB_SMONTHNAME1 + t.month - 1) | gen, GLOB_SABBREVMONTHNAME1 + t.month - 1, ideal, use_ideal, v);
}
static HRESULT STDMETHODCALLTYPE c_month_text(Face *f, HSTRING *v) { return month_text(f, FALSE, 0, FALSE, v); }
static HRESULT STDMETHODCALLTYPE c_month_text_n(Face *f, INT32 n, HSTRING *v) { return month_text(f, FALSE, n, TRUE, v); }
static HRESULT STDMETHODCALLTYPE c_month_solo(Face *f, HSTRING *v) { return month_text(f, TRUE, 0, FALSE, v); }
static HRESULT STDMETHODCALLTYPE c_month_solo_n(Face *f, INT32 n, HSTRING *v) { return month_text(f, TRUE, n, TRUE, v); }
static HRESULT STDMETHODCALLTYPE c_month_numeric(Face *f, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.month, 1, v); }
static HRESULT STDMETHODCALLTYPE c_month_padded(Face *f, INT32 n, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.month, n, v); }

/* adding time */
static HRESULT add_ticks(Cal *c, INT64 count, INT64 unit)
{
    /* (an overflow of count * unit is out of range anyway) */
    if (count > (MAX_LOCAL - MIN_LOCAL) / unit || count < -(MAX_LOCAL - MIN_LOCAL) / unit) return E_INVALIDARG;
    return set_local(c, c->local + count * unit);
}
static HRESULT STDMETHODCALLTYPE c_add_weeks(Face *f, INT32 v) { return add_ticks(cal_of(f), v, 7 * TICKS_DAY); }

/* day */
CONST_GETTER(c_first_day, 1)
GETTER(c_last_day, glob_days_in_month(t.year, t.month))
GETTER(c_days, glob_days_in_month(t.year, t.month))
GETTER(c_day, t.day)
static HRESULT STDMETHODCALLTYPE c_put_day(Face *f, INT32 v)
{
    Cal *c = cal_of(f);
    GTime t;
    fields(c, &t);
    if (v < 1 || v > glob_days_in_month(t.year, t.month)) return E_INVALIDARG;
    t.day = v;
    return set_fields(c, &t);
}
static HRESULT STDMETHODCALLTYPE c_add_days(Face *f, INT32 v) { return add_ticks(cal_of(f), v, TICKS_DAY); }
static HRESULT STDMETHODCALLTYPE c_day_text(Face *f, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.day, 1, v); }
static HRESULT STDMETHODCALLTYPE c_day_padded(Face *f, INT32 n, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.day, n, v); }
GETTER(c_dow, t.dow)
static HRESULT dow_text(Face *f, int ideal, BOOL use_ideal, HSTRING *v)
{
    GTime t;
    fields(cal_of(f), &t);
    int i = t.dow ? t.dow - 1 : 6;                 /* LOCALE_SDAYNAME1 is Monday */
    return name(cal_of(f), GLOB_SDAYNAME1 + i, GLOB_SABBREVDAYNAME1 + i, ideal, use_ideal, v);
}
static HRESULT STDMETHODCALLTYPE c_dow_text(Face *f, HSTRING *v) { return dow_text(f, 0, FALSE, v); }
static HRESULT STDMETHODCALLTYPE c_dow_text_n(Face *f, INT32 n, HSTRING *v) { return dow_text(f, n, TRUE, v); }

/* period (AM and PM on the 12-hour clock; the whole day on the 24-hour one) */
CONST_GETTER(c_first_period, 1)
static HRESULT STDMETHODCALLTYPE c_last_period(Face *f, INT32 *v) { if (!v) return E_POINTER; *v = cal_of(f)->clock24 ? 1 : 2; return S_OK; }
GETTER(c_period, c->clock24 ? 1 : t.hour < 12 ? 1 : 2)
static HRESULT STDMETHODCALLTYPE c_put_period(Face *f, INT32 v)
{
    Cal *c = cal_of(f);
    GTime t;
    fields(c, &t);
    if (v < 1 || v > (c->clock24 ? 1 : 2)) return E_INVALIDARG;
    if (!c->clock24) t.hour = t.hour % 12 + (v == 2 ? 12 : 0);
    return set_fields(c, &t);
}
static HRESULT STDMETHODCALLTYPE c_add_periods(Face *f, INT32 v)
{
    Cal *c = cal_of(f);
    return add_ticks(c, v, c->clock24 ? TICKS_DAY : TICKS_DAY / 2);
}
static HRESULT period_text(Face *f, HSTRING *v)
{
    GTime t;
    WCHAR s[32];
    fields(cal_of(f), &t);
    glob_info(cal_of(f)->langs.resolved, t.hour < 12 ? LOCALE_S1159 : LOCALE_S2359, s, 32);
    return glob_str(s, v);
}
static HRESULT STDMETHODCALLTYPE c_period_text(Face *f, HSTRING *v) { return period_text(f, v); }
static HRESULT STDMETHODCALLTYPE c_period_text_n(Face *f, INT32 n, HSTRING *v) { (void)n; return period_text(f, v); }

/* hour: 12, 1 ... 11 in a period of the 12-hour clock; 0 ... 23 */
static HRESULT STDMETHODCALLTYPE c_first_hour(Face *f, INT32 *v) { if (!v) return E_POINTER; *v = cal_of(f)->clock24 ? 0 : 12; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_last_hour(Face *f, INT32 *v) { if (!v) return E_POINTER; *v = cal_of(f)->clock24 ? 23 : 11; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_hours(Face *f, INT32 *v) { if (!v) return E_POINTER; *v = cal_of(f)->clock24 ? 24 : 12; return S_OK; }
static int hour_of(Cal *c, const GTime *t) { return c->clock24 ? t->hour : t->hour % 12 ? t->hour % 12 : 12; }
GETTER(c_hour, hour_of(c, &t))
static HRESULT STDMETHODCALLTYPE c_put_hour(Face *f, INT32 v)
{
    Cal *c = cal_of(f);
    GTime t;
    fields(c, &t);
    if (c->clock24) {
        if (v < 0 || v > 23) return E_INVALIDARG;
        t.hour = v;
    } else {
        if (v < 1 || v > 12) return E_INVALIDARG;
        t.hour = v % 12 + (t.hour >= 12 ? 12 : 0);
    }
    return set_fields(c, &t);
}
static HRESULT STDMETHODCALLTYPE c_add_hours(Face *f, INT32 v) { return add_ticks(cal_of(f), v, 3600 * TICKS_SECOND); }
static HRESULT STDMETHODCALLTYPE c_hour_text(Face *f, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), hour_of(cal_of(f), &t), 1, v); }
static HRESULT STDMETHODCALLTYPE c_hour_padded(Face *f, INT32 n, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), hour_of(cal_of(f), &t), n, v); }

/* minute, second, nanosecond */
#define TIME_FIELD(get, put, add, text, padded, field, lo, hi, unit) \
    GETTER(get, t.field) \
    static HRESULT STDMETHODCALLTYPE put(Face *f, INT32 v) \
    { Cal *c = cal_of(f); GTime t; fields(c, &t); if (v < (lo) || v > (hi)) return E_INVALIDARG; t.field = v; return set_fields(c, &t); } \
    static HRESULT STDMETHODCALLTYPE add(Face *f, INT32 v) { return add_ticks(cal_of(f), v, unit); } \
    static HRESULT STDMETHODCALLTYPE text(Face *f, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.field, 1, v); } \
    static HRESULT STDMETHODCALLTYPE padded(Face *f, INT32 n, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.field, n, v); }
TIME_FIELD(c_minute, c_put_minute, c_add_minutes, c_minute_text, c_minute_padded, minute, 0, 59, 60 * TICKS_SECOND)
TIME_FIELD(c_second, c_put_second, c_add_seconds, c_second_text, c_second_padded, second, 0, 59, TICKS_SECOND)
GETTER(c_nanosecond, t.frac * 100)
static HRESULT STDMETHODCALLTYPE c_put_nanosecond(Face *f, INT32 v)
{
    Cal *c = cal_of(f);
    GTime t;
    fields(c, &t);
    if (v < 0 || v > 999999999) return E_INVALIDARG;
    t.frac = v / 100;
    return set_fields(c, &t);
}
static HRESULT STDMETHODCALLTYPE c_add_nanoseconds(Face *f, INT32 v) { return add_ticks(cal_of(f), v / 100, 1); }
static HRESULT STDMETHODCALLTYPE c_ns_text(Face *f, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.frac * 100, 1, v); }
static HRESULT STDMETHODCALLTYPE c_ns_padded(Face *f, INT32 n, HSTRING *v) { GTime t; fields(cal_of(f), &t); return number(cal_of(f), t.frac * 100, n, v); }

/* another calendar: its instant (ours directly, any other by GetDateTime) */
static BOOL is_ours(void *face);
static HRESULT instant_of(void *other, INT64 *v)
{
    if (!other) return E_INVALIDARG;
    if (is_ours(other)) { Cal *o = cal_of(other); *v = glob_from_zone(o->local, o->utc); return S_OK; }
    return ((HRESULT (STDMETHODCALLTYPE *)(void *, INT64 *))VT(other)[16])(other, v);
}
static int cmp(INT64 a, INT64 b) { return a < b ? -1 : a > b; }
static HRESULT STDMETHODCALLTYPE c_compare(Face *f, void *other, INT32 *v)
{
    INT64 mine, theirs;
    HRESULT hr;
    if (!v) return E_POINTER;
    c_get_datetime(f, &mine);
    if (FAILED(hr = instant_of(other, &theirs))) return hr;
    *v = cmp(mine, theirs);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_compare_datetime(Face *f, INT64 other, INT32 *v)
{
    INT64 mine;
    if (!v) return E_POINTER;
    c_get_datetime(f, &mine);
    *v = cmp(mine, other);
    return S_OK;
}
static HRESULT langs_copy(GLangs *to, const GLangs *from)
{
    *to = *from;
    to->list = HeapAlloc(GetProcessHeap(), 0, (from->count ? from->count : 1) * sizeof(WCHAR *));
    if (!to->list) { to->count = 0; return E_OUTOFMEMORY; }
    for (UINT32 i = 0; i < from->count; i++) to->list[i] = glob_dup(from->list[i]);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE c_copy_to(Face *f, void *other)
{
    Cal *c = cal_of(f);
    if (!other) return E_INVALIDARG;
    if (is_ours(other)) {
        Cal *o = cal_of(other);
        if (o == c) return S_OK;
        GLangs l;
        if (FAILED(langs_copy(&l, &c->langs))) return E_OUTOFMEMORY;
        glob_langs_free(&o->langs);
        o->langs = l;
        lstrcpyW(o->numeral, c->numeral);
        o->clock24 = c->clock24;
        o->utc = c->utc;
        o->local = c->local;
        return S_OK;
    }
    INT64 v;
    c_get_datetime(f, &v);
    return ((HRESULT (STDMETHODCALLTYPE *)(void *, INT64))VT(other)[17])(other, v);    /* SetDateTime */
}
CONST_GETTER(c_first_minute, 0) CONST_GETTER(c_last_minute, 59) CONST_GETTER(c_minutes, 60)
CONST_GETTER(c_first_second, 0) CONST_GETTER(c_last_second, 59) CONST_GETTER(c_seconds, 60)
static HRESULT STDMETHODCALLTYPE c_resolved(Face *f, HSTRING *v) { return glob_str(cal_of(f)->langs.resolved, v); }
static HRESULT STDMETHODCALLTYPE c_dst(Face *f, boolean_t *v)
{
    Cal *c = cal_of(f);
    TIME_ZONE_INFORMATION tz;
    if (!v) return E_POINTER;
    *v = FALSE;
    if (c->utc) return S_OK;
    GetTimeZoneInformation(&tz);
    INT64 utc = glob_from_zone(c->local, FALSE);
    *v = c->local - utc != -(INT64)tz.Bias * 60 * TICKS_SECOND;
    return S_OK;
}

/* ITimeZoneOnCalendar */
static void zone_name(Cal *c, WCHAR out[64])
{
    TIME_ZONE_INFORMATION tz;
    DWORD r = GetTimeZoneInformation(&tz);
    if (c->utc || (!tz.Bias && r != TIME_ZONE_ID_DAYLIGHT && !tz.StandardName[0])) { lstrcpyW(out, L"UTC"); return; }
    lstrcpynW(out, tz.StandardName[0] ? tz.StandardName : L"UTC", 32);
}
static HRESULT STDMETHODCALLTYPE z_get(Face *f, HSTRING *v) { WCHAR s[64]; zone_name(cal_of(f), s); return glob_str(s, v); }
static BOOL is_utc_name(const WCHAR *s)
{
    return !lstrcmpiW(s, L"UTC") || !lstrcmpiW(s, L"Etc/UTC") || !lstrcmpiW(s, L"Etc/GMT") || !lstrcmpiW(s, L"GMT") ||
           !lstrcmpiW(s, L"Etc/Universal") || !lstrcmpiW(s, L"Etc/Zulu");
}
static HRESULT change_zone(Cal *c, const WCHAR *s)
{
    WCHAR mine[64];
    BOOL utc;
    if (is_utc_name(s)) utc = TRUE;
    else {
        Cal local = *c;
        local.utc = FALSE;
        zone_name(&local, mine);
        if (lstrcmpiW(s, mine)) return E_INVALIDARG;
        utc = FALSE;
    }
    INT64 instant = glob_from_zone(c->local, c->utc);     /* the same instant, in the new zone */
    c->utc = utc;
    c->local = glob_to_zone(instant, utc);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE z_change(Face *f, HSTRING v) { return change_zone(cal_of(f), glob_raw(v)); }
static HRESULT STDMETHODCALLTYPE z_text(Face *f, HSTRING *v)
{
    Cal *c = cal_of(f);
    TIME_ZONE_INFORMATION tz;
    boolean_t dst = 0;
    if (c->utc) return glob_str(L"UTC", v);
    GetTimeZoneInformation(&tz);
    c_dst(f, &dst);
    const WCHAR *n = dst ? tz.DaylightName : tz.StandardName;
    return glob_str(n[0] ? n : L"UTC", v);
}
static HRESULT STDMETHODCALLTYPE z_text_n(Face *f, INT32 n, HSTRING *v) { (void)n; return z_text(f, v); }

static const void *const cal_vt[] = { GLOB_INSP,
    (void *)c_clone, (void *)c_min, (void *)c_max, (void *)c_languages, (void *)c_get_numeral, (void *)c_put_numeral,
    (void *)c_get_system, (void *)c_change_system, (void *)c_get_clock, (void *)c_change_clock,
    (void *)c_get_datetime, (void *)c_set_datetime, (void *)c_now,
    (void *)c_first_era, (void *)c_last_era, (void *)c_eras, (void *)c_era, (void *)c_put_era, (void *)c_add_eras,
    (void *)c_era_text, (void *)c_era_text_n, (void *)c_first_year_era, (void *)c_last_year_era, (void *)c_years_era,
    (void *)c_year, (void *)c_put_year, (void *)c_add_years, (void *)c_year_text, (void *)c_year_truncated, (void *)c_year_padded,
    (void *)c_first_month, (void *)c_last_month, (void *)c_months, (void *)c_month, (void *)c_put_month, (void *)c_add_months,
    (void *)c_month_text, (void *)c_month_text_n, (void *)c_month_solo, (void *)c_month_solo_n, (void *)c_month_numeric,
    (void *)c_month_padded, (void *)c_add_weeks,
    (void *)c_first_day, (void *)c_last_day, (void *)c_days, (void *)c_day, (void *)c_put_day, (void *)c_add_days,
    (void *)c_day_text, (void *)c_day_padded,
    (void *)c_dow, (void *)c_dow_text, (void *)c_dow_text_n, (void *)c_dow_text, (void *)c_dow_text_n,
    (void *)c_first_period, (void *)c_last_period, (void *)c_last_period, (void *)c_period, (void *)c_put_period,
    (void *)c_add_periods, (void *)c_period_text, (void *)c_period_text_n,
    (void *)c_first_hour, (void *)c_last_hour, (void *)c_hours, (void *)c_hour, (void *)c_put_hour, (void *)c_add_hours,
    (void *)c_hour_text, (void *)c_hour_padded,
    (void *)c_minute, (void *)c_put_minute, (void *)c_add_minutes, (void *)c_minute_text, (void *)c_minute_padded,
    (void *)c_second, (void *)c_put_second, (void *)c_add_seconds, (void *)c_second_text, (void *)c_second_padded,
    (void *)c_nanosecond, (void *)c_put_nanosecond, (void *)c_add_nanoseconds, (void *)c_ns_text, (void *)c_ns_padded,
    (void *)c_compare, (void *)c_compare_datetime, (void *)c_copy_to,
    (void *)c_first_minute, (void *)c_last_minute, (void *)c_minutes,
    (void *)c_first_second, (void *)c_last_second, (void *)c_seconds,
    (void *)c_resolved, (void *)c_dst };
static BOOL is_ours(void *face) { return *(const void *const **)face == cal_vt; }
static const void *const zone_vt[] = { GLOB_INSP, (void *)z_get, (void *)z_change, (void *)z_text, (void *)z_text_n };
static void cal_destroy(GObj *o) { glob_langs_free(&((Cal *)o)->langs); }
static const GUID *const cal_iids[] = { &IID_ICalendar_, &IID_ITimeZoneOnCalendar_ };
static const void *const *const cal_vts[] = { cal_vt, zone_vt };
static const GClass cal_class = { L"Windows.Globalization.Calendar", 2, cal_iids, cal_vts, cal_destroy, FALSE };

static HRESULT STDMETHODCALLTYPE c_clone(Face *f, void **out)
{
    Cal *c = cal_of(f);
    if (!out) return E_POINTER;
    *out = 0;
    Cal *n = glob_new(&cal_class, sizeof *n);
    if (!n) return E_OUTOFMEMORY;
    if (FAILED(langs_copy(&n->langs, &c->langs))) { glob_free(n); return E_OUTOFMEMORY; }
    lstrcpyW(n->numeral, c->numeral);
    n->clock24 = c->clock24;
    n->utc = c->utc;
    n->local = c->local;
    *out = &n->base.face[0];
    return S_OK;
}

static HRESULT cal_new(void *languages, HSTRING calendar, HSTRING clock, HSTRING zone, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (calendar && lstrcmpW(glob_raw(calendar), L"GregorianCalendar")) return E_INVALIDARG;
    Cal *c = glob_new(&cal_class, sizeof *c);
    if (!c) return E_OUTOFMEMORY;
    HRESULT hr = glob_langs(&c->langs, languages, 0);
    if (FAILED(hr)) { glob_free(c); return hr; }
    lstrcpyW(c->numeral, L"Latn");
    c->clock24 = glob_24h(c->langs.resolved);
    if (clock) hr = clock_of(glob_raw(clock), &c->clock24);
    c->local = glob_to_zone(now_utc(), FALSE);
    if (SUCCEEDED(hr) && zone) hr = change_zone(c, glob_raw(zone));
    if (FAILED(hr)) { glob_unref(&c->base.face[0]); return hr; }
    *out = &c->base.face[0];
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cf_activate(Face *f, void **v) { (void)f; return cal_new(0, 0, 0, 0, v); }
static HRESULT STDMETHODCALLTYPE cf_default(Face *f, void *languages, void **v)
{
    (void)f;
    if (!languages) return E_INVALIDARG;
    return cal_new(languages, 0, 0, 0, v);
}
static HRESULT STDMETHODCALLTYPE cf_create(Face *f, void *languages, HSTRING calendar, HSTRING clock, void **v)
{
    (void)f;
    if (!languages) return E_INVALIDARG;
    return cal_new(languages, calendar, clock, 0, v);
}
static HRESULT STDMETHODCALLTYPE cf_create_zone(Face *f, void *languages, HSTRING calendar, HSTRING clock, HSTRING zone, void **v)
{
    (void)f;
    if (!languages) return E_INVALIDARG;
    return cal_new(languages, calendar, clock, zone, v);
}
static const void *const cfa_vt[] = { GLOB_INSP, (void *)cf_activate };
static const void *const cf_vt[] = { GLOB_INSP, (void *)cf_default, (void *)cf_create };
static const void *const cf2_vt[] = { GLOB_INSP, (void *)cf_create_zone };
static const GUID *const cf_iids[] = { &IID_IActivationFactory_, &IID_ICalendarFactory_, &IID_ICalendarFactory2_ };
static const void *const *const cf_vts[] = { cfa_vt, cf_vt, cf2_vt };
static const GClass cf_class = { L"Windows.Globalization.Calendar", 3, cf_iids, cf_vts, 0, TRUE };
static GObj g_cf;

HRESULT glob_calendar_factory(const WCHAR *cls, void **out)
{
    if (lstrcmpW(cls, cf_class.name)) return CLASS_E_CLASSNOTAVAILABLE;
    glob_static_init(&g_cf, &cf_class);
    *out = &g_cf.face[0];
    return S_OK;
}
