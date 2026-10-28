/*
 * language.c — Windows.Globalization's languages and regions:
 * ApplicationLanguages (the app's languages and its PrimaryLanguageOverride,
 * kept for the process's life), Language (a BCP-47 tag: its names,
 * script, layout direction and extension subtags), GeographicRegion (a
 * country: its codes, names and currency), the user's
 * GlobalizationPreferences (Windows.System.UserProfile), the
 * CalendarIdentifiers and ClockIdentifiers names, and
 * Fonts.LanguageFontGroup (the fonts Windows recommends for a language's
 * script).
 *
 * Not here yet: ApplicationLanguages.ManifestLanguages are the user's
 * language (not read from the package's manifest); a region's three-digit
 * code is known for the United States alone (kernel32's geography); the
 * preferences cannot be changed (TrySet... answer false), GetForUser and
 * Language.GetMuiCompatibleLanguageListFromLanguageTags are not built.
 */
#include "glob.h"

static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IApplicationLanguagesStatics_ = { 0x75B40847, 0x0A4C, 0x4A92, { 0x95, 0x65, 0xFD, 0x63, 0xC9, 0x5F, 0x7A, 0xED } };
static const GUID IID_IApplicationLanguagesStatics2_ = { 0x1DF0DE4F, 0x072B, 0x4D7B, { 0x8F, 0x06, 0xCB, 0x2D, 0xB4, 0x0F, 0x2B, 0xB5 } };
static const GUID IID_ILanguageFactory_ = { 0x9B0252AC, 0x0C27, 0x44F8, { 0xB7, 0x92, 0x97, 0x93, 0xFB, 0x66, 0xC6, 0x3E } };
static const GUID IID_ILanguageStatics_ = { 0xB23CD557, 0x0865, 0x46D4, { 0x89, 0xB8, 0xD5, 0x9B, 0xE8, 0x99, 0x0F, 0x0D } };
static const GUID IID_ILanguageStatics2_ = { 0x30199F6E, 0x914B, 0x4B2A, { 0x9D, 0x6E, 0xE3, 0xB0, 0xE2, 0x7D, 0xBE, 0x4F } };
static const GUID IID_ILanguageStatics3_ = { 0xD15ECB5A, 0x71DE, 0x5752, { 0x95, 0x42, 0xFA, 0xC5, 0xB4, 0xF2, 0x72, 0x61 } };
static const GUID IID_ILanguage_ = { 0xEA79A752, 0xF7C2, 0x4265, { 0xB1, 0xBD, 0xC4, 0xDE, 0xC4, 0xE4, 0xF0, 0x80 } };
static const GUID IID_ILanguageExtensionSubtags_ = { 0x7D7DAF45, 0x368D, 0x4364, { 0x85, 0x2B, 0xDE, 0xC9, 0x27, 0x03, 0x7B, 0x85 } };
static const GUID IID_ILanguage2_ = { 0x6A47E5B5, 0xD94D, 0x4886, { 0xA4, 0x04, 0xA5, 0xA5, 0xB9, 0xD5, 0xB4, 0x94 } };
static const GUID IID_ILanguage3_ = { 0xC6AF3D10, 0x641A, 0x5BA4, { 0xBB, 0x43, 0x5E, 0x12, 0xAE, 0xD7, 0x59, 0x54 } };
static const GUID IID_IGeographicRegionFactory_ = { 0x53425270, 0x77B4, 0x426B, { 0x85, 0x9F, 0x81, 0xE1, 0x9D, 0x51, 0x25, 0x46 } };
static const GUID IID_IGeographicRegionStatics_ = { 0x29E28974, 0x7AD9, 0x4EF4, { 0x87, 0x99, 0xB3, 0xB4, 0x4F, 0xAD, 0xEC, 0x08 } };
static const GUID IID_IGeographicRegion_ = { 0x01E9A621, 0x4A64, 0x4ED9, { 0x95, 0x4F, 0x9E, 0xDE, 0xB0, 0x7B, 0xD9, 0x03 } };
static const GUID IID_IGlobalizationPreferencesStatics_ = { 0x01BF4326, 0xED37, 0x4E96, { 0xB0, 0xE9, 0xC1, 0x34, 0x0D, 0x1E, 0xA1, 0x58 } };
static const GUID IID_IGlobalizationPreferencesStatics2_ = { 0xFCCE85F1, 0x4300, 0x4CD0, { 0x9C, 0xAC, 0x1A, 0x8E, 0x7B, 0x7E, 0x18, 0xF4 } };
static const GUID IID_IGlobalizationPreferencesStatics3_ = { 0x1E059733, 0x35F5, 0x40D8, { 0xB9, 0xE8, 0xAE, 0xF3, 0xEF, 0x85, 0x6F, 0xCE } };
static const GUID IID_ICalendarIdentifiersStatics_ = { 0x80653F68, 0x2CB2, 0x4C1F, { 0xB5, 0x90, 0xF0, 0xF5, 0x2B, 0xF4, 0xFD, 0x1A } };
static const GUID IID_ICalendarIdentifiersStatics2_ = { 0x7DF4D488, 0x5FD0, 0x42A7, { 0x95, 0xC5, 0x7D, 0x98, 0xD8, 0x23, 0x07, 0x5F } };
static const GUID IID_ICalendarIdentifiersStatics3_ = { 0x2C225423, 0x1FAD, 0x40C0, { 0x93, 0x34, 0xA8, 0xEB, 0x90, 0xDB, 0x04, 0xF5 } };
static const GUID IID_IClockIdentifiersStatics_ = { 0x523805BB, 0x12EC, 0x4F83, { 0xBC, 0x31, 0xB1, 0xB4, 0x37, 0x6B, 0x08, 0x08 } };
static const GUID IID_ILanguageFontGroupFactory_ = { 0xFCAEAC67, 0x4E77, 0x49C7, { 0xB8, 0x56, 0xDD, 0xE9, 0x34, 0xFC, 0x73, 0x5B } };
static const GUID IID_ILanguageFontGroup_ = { 0xF33A7FC3, 0x3A5C, 0x4AEA, { 0xB9, 0xFF, 0xB3, 0x9F, 0xB2, 0x42, 0xF7, 0xF6 } };
static const GUID IID_ILanguageFont_ = { 0xB12E5C3A, 0xB76D, 0x459B, { 0xBE, 0xEB, 0x90, 0x11, 0x51, 0xCD, 0x77, 0xD1 } };

static const void *const factory_vt[] = { GLOB_INSP, (void *)glob_no_activate };

BOOL glob_24h(const WCHAR *locale)
{
    WCHAR f[80];
    glob_info(locale, LOCALE_STIMEFORMAT, f, 80);
    for (const WCHAR *p = f; *p; p++) {
        if (*p == '\'') { while (p[1] && p[1] != '\'') p++; if (p[1]) p++; continue; }
        if (*p == 'H') return TRUE;
        if (*p == 'h') return FALSE;
    }
    return FALSE;
}

/* ---- ApplicationLanguages ------------------------------------------ */
static HRESULT STDMETHODCALLTYPE al_get_override(Face *f, HSTRING *v)
{
    (void)f;
    WCHAR t[GLOB_TAG];
    glob_get_override(t);
    return glob_str(t, v);
}
static BOOL well_formed(const WCHAR *t);
static HRESULT STDMETHODCALLTYPE al_put_override(Face *f, HSTRING v)
{
    (void)f;
    const WCHAR *t = glob_raw(v);
    if (*t && (!well_formed(t) || lstrlenW(t) >= GLOB_TAG)) return E_INVALIDARG;
    glob_set_override(t);
    return S_OK;
}
static HRESULT app_view(BOOL with_override, void **v)
{
    WCHAR list[4][GLOB_TAG];
    const WCHAR *items[4];
    UINT32 n;
    if (with_override) n = glob_app_languages(list, 4);
    else { glob_user_locale(list[0]); n = 1; }
    for (UINT32 i = 0; i < n; i++) items[i] = list[i];
    return glob_strvec(items, n, v);
}
static HRESULT STDMETHODCALLTYPE al_languages(Face *f, void **v) { (void)f; return app_view(TRUE, v); }
static HRESULT STDMETHODCALLTYPE al_manifest(Face *f, void **v) { (void)f; return app_view(FALSE, v); }
static HRESULT STDMETHODCALLTYPE al_for_user(Face *f, void *user, void **v) { (void)f; (void)user; return app_view(TRUE, v); }
static const void *const al_vt[] = { GLOB_INSP, (void *)al_get_override, (void *)al_put_override, (void *)al_languages, (void *)al_manifest };
static const void *const al2_vt[] = { GLOB_INSP, (void *)al_for_user };
static const GUID *const al_iids[] = { &IID_IActivationFactory_, &IID_IApplicationLanguagesStatics_, &IID_IApplicationLanguagesStatics2_ };
static const void *const *const al_vts[] = { factory_vt, al_vt, al2_vt };
static const GClass al_class = { L"Windows.Globalization.ApplicationLanguages", 3, al_iids, al_vts, 0, TRUE };
static GObj g_al;

/* ---- Language ------------------------------------------------------ */
static BOOL alnum(WCHAR c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
static BOOL alpha(WCHAR c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
/* BCP-47's shape: subtags of 1 to 8 letters and digits, the first all
 * letters (2 to 8, or the private-use x / grandfathered i) */
static BOOL well_formed(const WCHAR *t)
{
    int len = 0, first = 1;
    if (!t || !*t) return FALSE;
    for (const WCHAR *p = t;; p++) {
        if (*p == '-' || !*p) {
            if (!len || len > 8) return FALSE;
            if (first) {
                if (len == 1 && !(p[-1] == 'x' || p[-1] == 'X' || p[-1] == 'i' || p[-1] == 'I')) return FALSE;
                first = 0;
            }
            if (!*p) return TRUE;
            len = 0;
            continue;
        }
        if (!alnum(*p) || (first && !alpha(*p))) return FALSE;
        len++;
    }
}
/* as Windows spells a tag: language lower case, script title case,
 * region upper case, the rest (after a singleton) lower case */
static void canonical(const WCHAR *in, WCHAR out[GLOB_TAG])
{
    int i = 0, sub = 0, ext = 0;
    lstrcpynW(out, in, GLOB_TAG);
    while (out[i]) {
        int s = i, len = 0;
        while (out[i] && out[i] != '-') { i++; len++; }
        BOOL all_alpha = TRUE;
        for (int k = s; k < i; k++) if (!alpha(out[k])) all_alpha = FALSE;
        for (int k = s; k < i; k++) if (out[k] >= 'A' && out[k] <= 'Z') out[k] = (WCHAR)(out[k] + 32);
        if (len == 1) ext = 1;
        if (!ext && sub) {
            if (len == 4 && all_alpha) out[s] = (WCHAR)(out[s] - 32);
            else if (len == 2 && all_alpha) { out[s] = (WCHAR)(out[s] - 32); out[s + 1] = (WCHAR)(out[s + 1] - 32); }
        }
        sub++;
        if (out[i]) i++;
    }
}
typedef struct {
    GObj base;
    WCHAR tag[GLOB_TAG];
    WCHAR locale[GLOB_TAG];       /* the locale kernel32 resolves it to, "" none */
} Language;
static HRESULT STDMETHODCALLTYPE l_tag(Face *f, HSTRING *v) { return glob_str(OBJ(Language, f)->tag, v); }
static HRESULT name_or_tag(Language *l, LCTYPE type, HSTRING *v)
{
    WCHAR s[160];
    s[0] = 0;
    if (l->locale[0]) glob_info(l->locale, type, s, 160);
    return glob_str(s[0] ? s : l->tag, v);
}
static HRESULT STDMETHODCALLTYPE l_display(Face *f, HSTRING *v) { return name_or_tag(OBJ(Language, f), GLOB_SLOCALIZEDDISPLAYNAME, v); }
static HRESULT STDMETHODCALLTYPE l_native(Face *f, HSTRING *v) { return name_or_tag(OBJ(Language, f), GLOB_SNATIVEDISPLAYNAME, v); }
static HRESULT STDMETHODCALLTYPE l_abbreviated(Face *f, HSTRING *v) { return name_or_tag(OBJ(Language, f), GLOB_SLOCALIZEDLANGUAGENAME, v); }
static void script_of(Language *l, WCHAR out[8])
{
    out[0] = 0;
    /* the tag's own script subtag (zh-Hant-TW), else the locale's first script */
    for (const WCHAR *p = l->tag; *p; p++)
        if (*p == '-') {
            const WCHAR *s = p + 1;
            int n = 0;
            while (s[n] && s[n] != '-') n++;
            if (n == 1) break;
            if (n == 4 && alpha(s[0])) { lstrcpynW(out, s, 5); return; }
        }
    if (l->locale[0]) {
        WCHAR all[64];
        glob_info(l->locale, GLOB_SSCRIPTS, all, 64);
        if (lstrlenW(all) >= 4) lstrcpynW(out, all, 5);
    }
}
static HRESULT STDMETHODCALLTYPE l_script(Face *f, HSTRING *v)
{
    WCHAR s[8];
    script_of(OBJ(Language, f), s);
    return glob_str(s, v);
}
/* LanguageLayoutDirection Ltr 0, Rtl 1, TtbLtr 2, TtbRtl 3 from
 * LOCALE_IREADINGLAYOUT's 0 LTR, 1 RTL, 2 columns right to left, 3 left to right */
static HRESULT STDMETHODCALLTYPE l_layout(Face *f, int *v)
{
    Language *l = OBJ(Language, f);
    if (!v) return E_POINTER;
    int r = l->locale[0] ? glob_info_int(l->locale, GLOB_IREADINGLAYOUT, 0) : 0;
    *v = r == 2 ? 3 : r == 3 ? 2 : r;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE l_subtags(Face *f, HSTRING singleton, void **v)
{
    Language *l = OBJ(Language, f);
    const WCHAR *one = glob_raw(singleton);
    WCHAR copy[GLOB_TAG], *items[GLOB_TAG / 2];
    UINT32 n = 0;
    if (!v) return E_POINTER;
    if (lstrlenW(one) != 1) return E_INVALIDARG;
    lstrcpyW(copy, l->tag);
    /* the subtags after "-<singleton>-", up to the next singleton */
    BOOL in = FALSE;
    for (WCHAR *p = copy; *p;) {
        WCHAR *s = p;
        while (*p && *p != '-') p++;
        if (*p) *p++ = 0;
        if (lstrlenW(s) == 1) { if (in) break; in = !lstrcmpiW(s, one); continue; }
        if (in) items[n++] = s;
    }
    return glob_strvec((const WCHAR *const *)items, n, v);
}
static const void *const lang_vt[] = { GLOB_INSP, (void *)l_tag, (void *)l_display, (void *)l_native, (void *)l_script };
static const void *const lang_ext_vt[] = { GLOB_INSP, (void *)l_subtags };
static const void *const lang2_vt[] = { GLOB_INSP, (void *)l_layout };
static const void *const lang3_vt[] = { GLOB_INSP, (void *)l_abbreviated };
static const GUID *const lang_iids[] = { &IID_ILanguage_, &IID_ILanguageExtensionSubtags_, &IID_ILanguage2_, &IID_ILanguage3_ };
static const void *const *const lang_vts[] = { lang_vt, lang_ext_vt, lang2_vt, lang3_vt };
static const GClass lang_class = { L"Windows.Globalization.Language", 4, lang_iids, lang_vts, 0, FALSE };

static HRESULT STDMETHODCALLTYPE lf_create(Face *f, HSTRING tag, void **v)
{
    (void)f;
    const WCHAR *t = glob_raw(tag);
    if (!v) return E_POINTER;
    *v = 0;
    if (!well_formed(t) || lstrlenW(t) >= GLOB_TAG) return E_INVALIDARG;
    Language *l = glob_new(&lang_class, sizeof *l);
    if (!l) return E_OUTOFMEMORY;
    canonical(t, l->tag);
    glob_resolve(l->tag, l->locale);
    *v = &l->base.face[0];
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ls_well_formed(Face *f, HSTRING tag, boolean_t *v)
{
    (void)f;
    if (!v) return E_POINTER;
    *v = well_formed(glob_raw(tag));
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ls_input_tag(Face *f, HSTRING *v)
{
    (void)f;
    WCHAR t[GLOB_TAG];
    glob_user_locale(t);
    return glob_str(t, v);
}
static HRESULT STDMETHODCALLTYPE ls_set_input(Face *f, HSTRING tag, boolean_t *v) { (void)f; (void)tag; if (!v) return E_POINTER; *v = FALSE; return S_OK; }
static HRESULT STDMETHODCALLTYPE ls_mui(Face *f, void *tags, void **v) { (void)f; (void)tags; if (v) *v = 0; return E_NOTIMPL; }
static const void *const lf_vt[] = { GLOB_INSP, (void *)lf_create };
static const void *const ls_vt[] = { GLOB_INSP, (void *)ls_well_formed, (void *)ls_input_tag };
static const void *const ls2_vt[] = { GLOB_INSP, (void *)ls_set_input };
static const void *const ls3_vt[] = { GLOB_INSP, (void *)ls_mui };
static const GUID *const lf_iids[] = { &IID_IActivationFactory_, &IID_ILanguageFactory_, &IID_ILanguageStatics_,
                                       &IID_ILanguageStatics2_, &IID_ILanguageStatics3_ };
static const void *const *const lf_vts[] = { factory_vt, lf_vt, ls_vt, ls2_vt, ls3_vt };
static const GClass lf_class = { L"Windows.Globalization.Language", 5, lf_iids, lf_vts, 0, TRUE };
static GObj g_lf;

/* ---- GeographicRegion ---------------------------------------------- */
typedef struct {
    GObj base;
    WCHAR locale[GLOB_TAG];       /* a locale of that country */
    WCHAR code[4];
} Region;
static HRESULT STDMETHODCALLTYPE r_code(Face *f, HSTRING *v) { return glob_str(OBJ(Region, f)->code, v); }
static HRESULT r_info(Face *f, LCTYPE type, HSTRING *v)
{
    WCHAR s[160];
    glob_info(OBJ(Region, f)->locale, type, s, 160);
    return glob_str(s, v);
}
static HRESULT STDMETHODCALLTYPE r_code3(Face *f, HSTRING *v) { return r_info(f, GLOB_SISO3166CTRYNAME2, v); }
static HRESULT STDMETHODCALLTYPE r_digits(Face *f, HSTRING *v)
{
    WCHAR s[8];
    int geo = glob_info_int(OBJ(Region, f)->locale, GLOB_IGEOID, 0);
    if (!geo || !GetGeoInfoW(geo, 8 /* GEO_ISO_UN_NUMBER */, s, 8, 0)) s[0] = 0;
    return glob_str(s, v);
}
static HRESULT STDMETHODCALLTYPE r_display(Face *f, HSTRING *v) { return r_info(f, GLOB_SLOCALIZEDCOUNTRYNAME, v); }
static HRESULT STDMETHODCALLTYPE r_native(Face *f, HSTRING *v) { return r_info(f, GLOB_SNATIVECOUNTRYNAME, v); }
static HRESULT STDMETHODCALLTYPE r_currencies(Face *f, void **v)
{
    WCHAR c[16];
    const WCHAR *items[1] = { c };
    glob_info(OBJ(Region, f)->locale, LOCALE_SINTLSYMBOL, c, 16);
    return glob_strvec(items, c[0] ? 1 : 0, v);
}
static const void *const region_vt[] = { GLOB_INSP, (void *)r_code, (void *)r_code, (void *)r_code3, (void *)r_digits,
                                         (void *)r_display, (void *)r_native, (void *)r_currencies };
static const GUID *const region_iids[] = { &IID_IGeographicRegion_ };
static const void *const *const region_vts[] = { region_vt };
static const GClass region_class = { L"Windows.Globalization.GeographicRegion", 1, region_iids, region_vts, 0, FALSE };
static HRESULT region_new(const WCHAR *code, void **v)
{
    WCHAR user[GLOB_TAG], loc[GLOB_TAG];
    if (!v) return E_POINTER;
    *v = 0;
    glob_user_locale(user);
    if (!glob_region_locale(code, user, loc)) return E_INVALIDARG;
    Region *r = glob_new(&region_class, sizeof *r);
    if (!r) return E_OUTOFMEMORY;
    lstrcpyW(r->locale, loc);
    glob_info(loc, LOCALE_SISO3166CTRYNAME, r->code, 4);
    *v = &r->base.face[0];
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE rf_activate(Face *f, void **v)
{
    (void)f;
    WCHAR code[4];
    glob_user_region(code);
    return region_new(code, v);
}
static HRESULT STDMETHODCALLTYPE rf_create(Face *f, HSTRING code, void **v) { (void)f; return region_new(glob_raw(code), v); }
static HRESULT STDMETHODCALLTYPE rs_supported(Face *f, HSTRING code, boolean_t *v)
{
    (void)f;
    WCHAR loc[GLOB_TAG];
    if (!v) return E_POINTER;
    *v = glob_region_locale(glob_raw(code), 0, loc);
    return S_OK;
}
static const void *const rfa_vt[] = { GLOB_INSP, (void *)rf_activate };
static const void *const rf_vt[] = { GLOB_INSP, (void *)rf_create };
static const void *const rs_vt[] = { GLOB_INSP, (void *)rs_supported };
static const GUID *const rf_iids[] = { &IID_IActivationFactory_, &IID_IGeographicRegionFactory_, &IID_IGeographicRegionStatics_ };
static const void *const *const rf_vts[] = { rfa_vt, rf_vt, rs_vt };
static const GClass rf_class = { L"Windows.Globalization.GeographicRegion", 3, rf_iids, rf_vts, 0, TRUE };
static GObj g_rf;

/* ---- GlobalizationPreferences -------------------------------------- */
static HRESULT one(const WCHAR *s, void **v) { return glob_strvec(&s, 1, v); }
static HRESULT STDMETHODCALLTYPE gp_calendars(Face *f, void **v) { (void)f; return one(L"GregorianCalendar", v); }
static HRESULT STDMETHODCALLTYPE gp_clocks(Face *f, void **v)
{
    (void)f;
    WCHAR loc[GLOB_TAG];
    glob_user_locale(loc);
    return one(glob_24h(loc) ? L"24HourClock" : L"12HourClock", v);
}
static HRESULT STDMETHODCALLTYPE gp_currencies(Face *f, void **v)
{
    (void)f;
    WCHAR loc[GLOB_TAG], c[16];
    glob_user_locale(loc);
    glob_info(loc, LOCALE_SINTLSYMBOL, c, 16);
    return one(c[0] ? c : L"USD", v);
}
static HRESULT STDMETHODCALLTYPE gp_languages(Face *f, void **v)
{
    (void)f;
    WCHAR loc[GLOB_TAG];
    glob_user_locale(loc);
    return one(loc, v);
}
static HRESULT STDMETHODCALLTYPE gp_region(Face *f, HSTRING *v)
{
    (void)f;
    WCHAR code[4];
    glob_user_region(code);
    return glob_str(code, v);
}
/* LOCALE_IFIRSTDAYOFWEEK counts from Monday (0), DayOfWeek from Sunday */
static HRESULT STDMETHODCALLTYPE gp_week_start(Face *f, int *v)
{
    (void)f;
    WCHAR loc[GLOB_TAG];
    if (!v) return E_POINTER;
    glob_user_locale(loc);
    *v = (glob_info_int(loc, LOCALE_IFIRSTDAYOFWEEK, 6) + 1) % 7;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gp_try_set(Face *f, void *x, boolean_t *v) { (void)f; (void)x; if (!v) return E_POINTER; *v = FALSE; return S_OK; }
static HRESULT STDMETHODCALLTYPE gp_for_user(Face *f, void *user, void **v) { (void)f; (void)user; if (v) *v = 0; return E_NOTIMPL; }
static const void *const gp_vt[] = { GLOB_INSP, (void *)gp_calendars, (void *)gp_clocks, (void *)gp_currencies,
                                     (void *)gp_languages, (void *)gp_region, (void *)gp_week_start };
static const void *const gp2_vt[] = { GLOB_INSP, (void *)gp_try_set, (void *)gp_try_set };
static const void *const gp3_vt[] = { GLOB_INSP, (void *)gp_for_user };
static const GUID *const gp_iids[] = { &IID_IActivationFactory_, &IID_IGlobalizationPreferencesStatics_,
                                       &IID_IGlobalizationPreferencesStatics2_, &IID_IGlobalizationPreferencesStatics3_ };
static const void *const *const gp_vts[] = { factory_vt, gp_vt, gp2_vt, gp3_vt };
static const GClass gp_class = { L"Windows.System.UserProfile.GlobalizationPreferences", 4, gp_iids, gp_vts, 0, TRUE };
static GObj g_gp;

/* ---- CalendarIdentifiers, ClockIdentifiers ------------------------- */
#define NAME_FN(fn, s) static HRESULT STDMETHODCALLTYPE fn(Face *f, HSTRING *v) { (void)f; return glob_str(s, v); }
NAME_FN(ci_gregorian, L"GregorianCalendar") NAME_FN(ci_hebrew, L"HebrewCalendar") NAME_FN(ci_hijri, L"HijriCalendar")
NAME_FN(ci_japanese, L"JapaneseCalendar") NAME_FN(ci_julian, L"JulianCalendar") NAME_FN(ci_korean, L"KoreanCalendar")
NAME_FN(ci_taiwan, L"TaiwanCalendar") NAME_FN(ci_thai, L"ThaiCalendar") NAME_FN(ci_umalqura, L"UmAlQuraCalendar")
NAME_FN(ci_persian, L"PersianCalendar") NAME_FN(ci_chinese_lunar, L"ChineseLunarCalendar")
NAME_FN(ci_japanese_lunar, L"JapaneseLunarCalendar") NAME_FN(ci_korean_lunar, L"KoreanLunarCalendar")
NAME_FN(ci_taiwan_lunar, L"TaiwanLunarCalendar") NAME_FN(ci_vietnamese_lunar, L"VietnameseLunarCalendar")
NAME_FN(clk_12, L"12HourClock") NAME_FN(clk_24, L"24HourClock")
static const void *const ci_vt[] = { GLOB_INSP, (void *)ci_gregorian, (void *)ci_hebrew, (void *)ci_hijri, (void *)ci_japanese,
                                     (void *)ci_julian, (void *)ci_korean, (void *)ci_taiwan, (void *)ci_thai, (void *)ci_umalqura };
static const void *const ci2_vt[] = { GLOB_INSP, (void *)ci_persian };
static const void *const ci3_vt[] = { GLOB_INSP, (void *)ci_chinese_lunar, (void *)ci_japanese_lunar, (void *)ci_korean_lunar,
                                      (void *)ci_taiwan_lunar, (void *)ci_vietnamese_lunar };
static const GUID *const ci_iids[] = { &IID_IActivationFactory_, &IID_ICalendarIdentifiersStatics_,
                                       &IID_ICalendarIdentifiersStatics2_, &IID_ICalendarIdentifiersStatics3_ };
static const void *const *const ci_vts[] = { factory_vt, ci_vt, ci2_vt, ci3_vt };
static const GClass ci_class = { L"Windows.Globalization.CalendarIdentifiers", 4, ci_iids, ci_vts, 0, TRUE };
static GObj g_ci;
static const void *const clk_vt[] = { GLOB_INSP, (void *)clk_12, (void *)clk_24 };
static const GUID *const clk_iids[] = { &IID_IActivationFactory_, &IID_IClockIdentifiersStatics_ };
static const void *const *const clk_vts[] = { factory_vt, clk_vt };
static const GClass clk_class = { L"Windows.Globalization.ClockIdentifiers", 2, clk_iids, clk_vts, 0, TRUE };
static GObj g_clk;

/* ---- Fonts.LanguageFontGroup --------------------------------------- */
typedef struct {
    GObj base;
    const WCHAR *family;
    UINT16 weight;
} LanguageFont;
static HRESULT STDMETHODCALLTYPE lfont_family(Face *f, HSTRING *v) { return glob_str(OBJ(LanguageFont, f)->family, v); }
static HRESULT STDMETHODCALLTYPE lfont_weight(Face *f, UINT16 *v) { if (!v) return E_POINTER; *v = OBJ(LanguageFont, f)->weight; return S_OK; }
static HRESULT STDMETHODCALLTYPE lfont_stretch(Face *f, int *v) { (void)f; if (!v) return E_POINTER; *v = 5; return S_OK; }   /* FontStretch.Normal */
static HRESULT STDMETHODCALLTYPE lfont_style(Face *f, int *v) { (void)f; if (!v) return E_POINTER; *v = 0; return S_OK; }     /* FontStyle.Normal */
static HRESULT STDMETHODCALLTYPE lfont_scale(Face *f, double *v) { (void)f; if (!v) return E_POINTER; *v = 100.0; return S_OK; }
static const void *const lfont_vt[] = { GLOB_INSP, (void *)lfont_family, (void *)lfont_weight, (void *)lfont_stretch,
                                        (void *)lfont_style, (void *)lfont_scale };
static const GUID *const lfont_iids[] = { &IID_ILanguageFont_ };
static const void *const *const lfont_vts[] = { lfont_vt };
static const GClass lfont_class = { L"Windows.Globalization.Fonts.LanguageFont", 1, lfont_iids, lfont_vts, 0, FALSE };

/* the user-interface font Windows recommends for a script (Segoe UI for
 * the rest) */
static const struct { const WCHAR *script, *ui; } g_script_fonts[] = {
    { L"Jpan", L"Yu Gothic UI" }, { L"Hans", L"Microsoft YaHei UI" }, { L"Hant", L"Microsoft JhengHei UI" },
    { L"Kore", L"Malgun Gothic" }, { L"Thai", L"Leelawadee UI" }, { L"Laoo", L"Leelawadee UI" },
    { L"Khmr", L"Leelawadee UI" }, { L"Deva", L"Nirmala UI" }, { L"Beng", L"Nirmala UI" }, { L"Taml", L"Nirmala UI" },
    { L"Telu", L"Nirmala UI" }, { L"Knda", L"Nirmala UI" }, { L"Mlym", L"Nirmala UI" }, { L"Gujr", L"Nirmala UI" },
    { L"Guru", L"Nirmala UI" }, { L"Orya", L"Nirmala UI" }, { L"Ethi", L"Ebrima" }, { L"Mong", L"Mongolian Baiti" },
};
typedef struct {
    GObj base;
    const WCHAR *ui;              /* the script's UI family */
} FontGroup;
enum { UI_TEXT, UI_HEADING, UI_TITLE, UI_CAPTION, UI_NOTIFICATION, DOC_TRADITIONAL, DOC_MODERN, DOC_HEADING,
       FIXED_WIDTH, DOC_ALT1, DOC_ALT2 };
static HRESULT font(Face *f, int which, void **v)
{
    FontGroup *g = OBJ(FontGroup, f);
    BOOL latin = !lstrcmpW(g->ui, L"Segoe UI");
    if (!v) return E_POINTER;
    LanguageFont *l = glob_new(&lfont_class, sizeof *l);
    *v = l ? &l->base.face[0] : 0;
    if (!l) return E_OUTOFMEMORY;
    l->family = g->ui;
    l->weight = 400;
    switch (which) {
    case UI_HEADING: case UI_TITLE: if (latin) l->family = L"Segoe UI Light"; l->weight = 300; break;
    case UI_NOTIFICATION: l->weight = 600; break;
    case DOC_TRADITIONAL: case DOC_ALT2: if (latin) l->family = L"Times New Roman"; break;
    case DOC_MODERN: case DOC_ALT1: if (latin) l->family = L"Calibri"; break;
    case DOC_HEADING: if (latin) l->family = L"Cambria"; break;
    case FIXED_WIDTH: l->family = L"Consolas"; break;
    }
    return S_OK;
}
#define FONT_FN(fn, w) static HRESULT STDMETHODCALLTYPE fn(Face *f, void **v) { return font(f, w, v); }
FONT_FN(fg_ui_text, UI_TEXT) FONT_FN(fg_ui_heading, UI_HEADING) FONT_FN(fg_ui_title, UI_TITLE)
FONT_FN(fg_ui_caption, UI_CAPTION) FONT_FN(fg_ui_notification, UI_NOTIFICATION) FONT_FN(fg_traditional, DOC_TRADITIONAL)
FONT_FN(fg_modern, DOC_MODERN) FONT_FN(fg_doc_heading, DOC_HEADING) FONT_FN(fg_fixed, FIXED_WIDTH)
FONT_FN(fg_alt1, DOC_ALT1) FONT_FN(fg_alt2, DOC_ALT2)
static const void *const fg_vt[] = { GLOB_INSP, (void *)fg_ui_text, (void *)fg_ui_heading, (void *)fg_ui_title, (void *)fg_ui_caption,
                                     (void *)fg_ui_notification, (void *)fg_traditional, (void *)fg_modern, (void *)fg_doc_heading,
                                     (void *)fg_fixed, (void *)fg_alt1, (void *)fg_alt2 };
static const GUID *const fg_iids[] = { &IID_ILanguageFontGroup_ };
static const void *const *const fg_vts[] = { fg_vt };
static const GClass fg_class = { L"Windows.Globalization.Fonts.LanguageFontGroup", 1, fg_iids, fg_vts, 0, FALSE };
static HRESULT STDMETHODCALLTYPE fgf_create(Face *f, HSTRING tag, void **v)
{
    (void)f;
    const WCHAR *t = glob_raw(tag);
    if (!v) return E_POINTER;
    *v = 0;
    if (!well_formed(t) || lstrlenW(t) >= GLOB_TAG) return E_INVALIDARG;
    Language l;
    WCHAR script[8];
    canonical(t, l.tag);
    glob_resolve(l.tag, l.locale);
    script_of(&l, script);
    /* a language with no script of its own (ja, zh-CN) goes by its locale's */
    if (!lstrcmpW(script, L"Hani") || !lstrcmpW(script, L"Hira") || !lstrcmpW(script, L"Kana")) lstrcpyW(script, L"Jpan");
    if (!lstrcmpW(script, L"Hang")) lstrcpyW(script, L"Kore");
    if (!lstrcmpiW(l.locale, L"zh-TW") || !lstrcmpiW(l.locale, L"zh-HK") || !lstrcmpiW(l.locale, L"zh-MO")) lstrcpyW(script, L"Hant");
    else if (l.locale[0] == 'z' && l.locale[1] == 'h' && (!l.locale[2] || l.locale[2] == '-')) lstrcpyW(script, L"Hans");
    FontGroup *g = glob_new(&fg_class, sizeof *g);
    if (!g) return E_OUTOFMEMORY;
    g->ui = L"Segoe UI";
    for (UINT32 i = 0; i < sizeof g_script_fonts / sizeof *g_script_fonts; i++)
        if (!lstrcmpW(script, g_script_fonts[i].script)) g->ui = g_script_fonts[i].ui;
    *v = &g->base.face[0];
    return S_OK;
}
static const void *const fgf_vt[] = { GLOB_INSP, (void *)fgf_create };
static const GUID *const fgf_iids[] = { &IID_IActivationFactory_, &IID_ILanguageFontGroupFactory_ };
static const void *const *const fgf_vts[] = { factory_vt, fgf_vt };
static const GClass fgf_class = { L"Windows.Globalization.Fonts.LanguageFontGroup", 2, fgf_iids, fgf_vts, 0, TRUE };
static GObj g_fgf;

HRESULT glob_language_factory(const WCHAR *cls, void **out)
{
    static const struct { const WCHAR *name; GObj *obj; const GClass *cls; } classes[] = {
        { L"Windows.Globalization.ApplicationLanguages", &g_al, &al_class },
        { L"Windows.Globalization.Language", &g_lf, &lf_class },
        { L"Windows.Globalization.GeographicRegion", &g_rf, &rf_class },
        { L"Windows.System.UserProfile.GlobalizationPreferences", &g_gp, &gp_class },
        { L"Windows.Globalization.CalendarIdentifiers", &g_ci, &ci_class },
        { L"Windows.Globalization.ClockIdentifiers", &g_clk, &clk_class },
        { L"Windows.Globalization.Fonts.LanguageFontGroup", &g_fgf, &fgf_class },
    };
    for (UINT32 i = 0; i < sizeof classes / sizeof *classes; i++)
        if (!lstrcmpW(cls, classes[i].name)) {
            glob_static_init(classes[i].obj, classes[i].cls);
            *out = &classes[i].obj->face[0];
            return S_OK;
        }
    return CLASS_E_CLASSNOTAVAILABLE;
}
