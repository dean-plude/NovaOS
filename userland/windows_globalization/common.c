/*
 * Windows.Globalization.dll — the Windows Runtime's globalization classes,
 * as Windows has them, worked out from kernel32's locale data:
 *
 *   language.c  ApplicationLanguages, Language, GeographicRegion,
 *               CalendarIdentifiers, ClockIdentifiers, Fonts.LanguageFontGroup
 *               and Windows.System.UserProfile.GlobalizationPreferences
 *   numfmt.c    NumberFormatting.DecimalFormatter and CurrencyFormatter
 *   calendar.c  Calendar (the Gregorian calendar)
 *   datetime.c  DateTimeFormatting.DateTimeFormatter
 *
 * This file: the objects' common methods, strings, language lists, numeral
 * systems and DllGetActivationFactory.  (Windows keeps LanguageFontGroup in
 * Windows.Globalization.Fonts.dll and GlobalizationPreferences in
 * Windows.Globalization.dll too.)
 */
#include "glob.h"

int _fltused = 0x9875;      /* floating point in use (numbers, a font's scale; the compiler references it) */
static const GUID IID_IUnknown_ = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_ = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_ = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
HRESULT nova_strvec_new(const WCHAR *const *items, UINT32 count, void **out);   /* userland/winrtmap */

/* ---- objects ------------------------------------------------------- */
HRESULT STDMETHODCALLTYPE glob_qi(Face *f, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    GObj *o = f->o;
    *out = 0;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_))
        *out = &o->face[0];
    else
        for (UINT32 i = 0; i < o->cls->faces; i++)
            if (IsEqualGUID(iid, o->cls->iids[i])) { *out = &o->face[i]; break; }
    if (!*out) return E_NOINTERFACE;
    glob_addref(f);
    return S_OK;
}
ULONG STDMETHODCALLTYPE glob_addref(Face *f)
{
    return f->o->cls->statics ? 2 : (ULONG)InterlockedIncrement(&f->o->refs);
}
ULONG STDMETHODCALLTYPE glob_release(Face *f)
{
    GObj *o = f->o;
    if (o->cls->statics) return 1;
    LONG r = InterlockedDecrement(&o->refs);
    if (!r) {
        if (o->cls->destroy) o->cls->destroy(o);
        HeapFree(GetProcessHeap(), 0, o);
    }
    return (ULONG)r;
}
HRESULT STDMETHODCALLTYPE glob_iids(Face *f, ULONG *n, GUID **iids)
{
    GObj *o = f->o;
    if (!n || !iids) return E_POINTER;
    *iids = CoTaskMemAlloc(o->cls->faces * sizeof(GUID));
    if (!*iids) { *n = 0; return E_OUTOFMEMORY; }
    for (UINT32 i = 0; i < o->cls->faces; i++) (*iids)[i] = *o->cls->iids[i];
    *n = o->cls->faces;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE glob_class(Face *f, HSTRING *name) { return glob_str(f->o->cls->name, name); }
HRESULT STDMETHODCALLTYPE glob_trust(Face *f, int *t) { (void)f; if (!t) return E_POINTER; *t = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE glob_no_activate(Face *f, void **out) { (void)f; if (out) *out = 0; return E_NOTIMPL; }

static void faces_init(GObj *o, const GClass *cls)
{
    o->cls = cls;
    o->refs = 1;
    for (UINT32 i = 0; i < cls->faces; i++) { o->face[i].vt = cls->vts[i]; o->face[i].o = o; }
}
void *glob_new(const GClass *cls, SIZE_T size)
{
    GObj *o = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (o) faces_init(o, cls);
    return o;
}
void glob_static_init(GObj *o, const GClass *cls) { if (!o->cls) faces_init(o, cls); }
void *glob_out(void *obj, UINT32 i)
{
    GObj *o = obj;
    glob_addref(&o->face[i]);
    return &o->face[i];
}

/* ---- strings ------------------------------------------------------- */
HRESULT glob_str(const WCHAR *s, HSTRING *out)
{
    if (!out) return E_POINTER;
    *out = 0;
    return s && *s ? WindowsCreateString(s, (UINT32)lstrlenW(s), out) : S_OK;
}
const WCHAR *glob_raw(HSTRING s)
{
    const WCHAR *r = WindowsGetStringRawBuffer(s, 0);
    return r ? r : L"";
}
WCHAR *glob_dup(const WCHAR *s)
{
    if (!s) s = L"";
    WCHAR *d = HeapAlloc(GetProcessHeap(), 0, (lstrlenW(s) + 1) * sizeof(WCHAR));
    if (d) lstrcpyW(d, s);
    return d;
}
void glob_free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
UINT32 glob_iterable(void *iterable, WCHAR **out, UINT32 cap)
{
    void *it = 0;
    UINT32 n = 0;
    if (!iterable) return 0;
    /* IIterable<String>.First, then IIterator's Current, HasCurrent, MoveNext */
    if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, void **))VT(iterable)[6])(iterable, &it)) || !it) return 0;
    boolean_t has = 0;
    ((HRESULT (STDMETHODCALLTYPE *)(void *, boolean_t *))VT(it)[7])(it, &has);
    while (has && n < cap) {
        HSTRING s = 0;
        if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))VT(it)[6])(it, &s))) break;
        if ((out[n] = glob_dup(glob_raw(s)))) n++;
        WindowsDeleteString(s);
        has = 0;
        if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, boolean_t *))VT(it)[8])(it, &has))) break;
    }
    glob_unref(it);
    return n;
}
HRESULT glob_strvec(const WCHAR *const *items, UINT32 n, void **out)
{
    if (!out) return E_POINTER;
    return nova_strvec_new(items, n, out);
}

/* ---- languages and regions ----------------------------------------- */
void glob_user_locale(WCHAR out[GLOB_TAG])
{
    if (!GetUserDefaultLocaleName(out, GLOB_TAG)) lstrcpyW(out, L"en-US");
    /* a sort suffix (de-DE_phoneb) is not part of a language tag */
    for (WCHAR *p = out; *p; p++)
        if (*p == '_') { *p = 0; break; }
}
BOOL glob_resolve(const WCHAR *tag, WCHAR out[GLOB_TAG])
{
    WCHAR base[GLOB_TAG];
    out[0] = 0;
    if (!tag || !*tag) return FALSE;
    /* without its extensions and private use (en-US-u-ca-gregory: en-US) */
    lstrcpynW(base, tag, GLOB_TAG);
    for (WCHAR *p = base; *p; p++)
        if (*p == '-' && p[1] && (!p[2] || p[2] == '-')) { *p = 0; break; }
    if (!base[0] || !ResolveLocaleName(base, out, GLOB_TAG) || !out[0]) return FALSE;
    return TRUE;
}
static SRWLOCK g_lang_lock = SRWLOCK_INIT;
static WCHAR g_override[GLOB_TAG];
void glob_set_override(const WCHAR *tag)
{
    AcquireSRWLockExclusive(&g_lang_lock);
    lstrcpynW(g_override, tag ? tag : L"", GLOB_TAG);
    ReleaseSRWLockExclusive(&g_lang_lock);
}
void glob_get_override(WCHAR out[GLOB_TAG])
{
    AcquireSRWLockShared(&g_lang_lock);
    lstrcpyW(out, g_override);
    ReleaseSRWLockShared(&g_lang_lock);
}
UINT32 glob_app_languages(WCHAR out[][GLOB_TAG], UINT32 cap)
{
    UINT32 n = 0;
    WCHAR user[GLOB_TAG];
    if (!cap) return 0;
    glob_get_override(out[0]);
    if (out[0][0]) n++;
    glob_user_locale(user);
    if (n < cap && lstrcmpiW(user, out[0])) lstrcpyW(out[n++], user);
    return n;
}
void glob_info(const WCHAR *name, LCTYPE type, WCHAR *out, int cap)
{
    if (!GetLocaleInfoEx(name, type, out, cap)) out[0] = 0;
}
int glob_info_int(const WCHAR *name, LCTYPE type, int dflt)
{
    DWORD v = 0;
    if (GetLocaleInfoEx(name, type | LOCALE_RETURN_NUMBER, (LPWSTR)&v, sizeof v / sizeof(WCHAR))) return (int)v;
    return dflt;
}
void glob_user_region(WCHAR out[4])
{
    WCHAR loc[GLOB_TAG];
    glob_user_locale(loc);
    glob_info(loc, LOCALE_SISO3166CTRYNAME, out, 4);
    if (!out[0]) lstrcpyW(out, L"US");
}
typedef struct { const WCHAR *code; WCHAR *out; } RegionFind;
static BOOL CALLBACK region_enum(LPWSTR name, DWORD flags, LPARAM p)
{
    (void)flags;
    RegionFind *f = (RegionFind *)p;
    const WCHAR *dash = 0;
    WCHAR three[8];
    for (const WCHAR *c = name; *c; c++) if (*c == '-') dash = c;
    if (!dash) return TRUE;
    if (lstrlenW(f->code) == 3) {          /* a three-letter code (USA): the locale's ISO 3166 alpha-3 */
        if (lstrlenW(dash + 1) != 2) return TRUE;
        glob_info(name, GLOB_SISO3166CTRYNAME2, three, 8);
        if (lstrcmpiW(three, f->code)) return TRUE;
    } else if (lstrcmpiW(dash + 1, f->code)) return TRUE;
    lstrcpynW(f->out, name, GLOB_TAG);
    return FALSE;
}
BOOL glob_region_locale(const WCHAR *code, const WCHAR *lang, WCHAR out[GLOB_TAG])
{
    WCHAR tag[GLOB_TAG], base[GLOB_TAG];
    out[0] = 0;
    if (!code || lstrlenW(code) < 2 || lstrlenW(code) > 3) return FALSE;
    /* the language's own locale there (en + US: en-US) */
    lstrcpynW(base, lang ? lang : L"en", GLOB_TAG);
    for (WCHAR *p = base; *p; p++) if (*p == '-') { *p = 0; break; }
    if (lstrlenW(code) == 2 && lstrlenW(base) + 4 <= GLOB_TAG) {
        lstrcpyW(tag, base);
        lstrcatW(tag, L"-");
        lstrcatW(tag, code);
        if (IsValidLocaleName(tag)) { lstrcpyW(out, tag); return TRUE; }
    }
    RegionFind f = { code, out };
    EnumSystemLocalesEx(region_enum, LOCALE_SPECIFICDATA, (LPARAM)&f, 0);
    return out[0] != 0;
}
HRESULT glob_langs(GLangs *l, void *iterable, HSTRING region)
{
    WCHAR *given[16];
    UINT32 n = 0;
    ZeroMemory(l, sizeof *l);
    if (iterable) {
        n = glob_iterable(iterable, given, 16);
        if (!n) return E_INVALIDARG;               /* Windows refuses an empty list */
    } else {
        WCHAR app[4][GLOB_TAG];
        UINT32 k = glob_app_languages(app, 4);
        for (UINT32 i = 0; i < k; i++)
            if ((given[n] = glob_dup(app[i]))) n++;
    }
    l->list = HeapAlloc(GetProcessHeap(), 0, (n ? n : 1) * sizeof(WCHAR *));
    if (!l->list) { while (n) glob_free(given[--n]); return E_OUTOFMEMORY; }
    for (UINT32 i = 0; i < n; i++) l->list[i] = given[i];
    l->count = n;
    for (UINT32 i = 0; i < n && !l->resolved[0]; i++) glob_resolve(l->list[i], l->resolved);
    if (!l->resolved[0]) lstrcpyW(l->resolved, L"en-US");
    const WCHAR *r = region ? glob_raw(region) : L"";
    if (*r) {
        WCHAR loc[GLOB_TAG];
        if (!glob_region_locale(r, l->resolved, loc)) { glob_langs_free(l); return E_INVALIDARG; }
        glob_info(loc, LOCALE_SISO3166CTRYNAME, l->region, 4);
    }
    if (!l->region[0]) glob_info(l->resolved, LOCALE_SISO3166CTRYNAME, l->region, 4);
    if (!l->region[0]) glob_user_region(l->region);
    return S_OK;
}
void glob_langs_free(GLangs *l)
{
    for (UINT32 i = 0; i < l->count; i++) glob_free(l->list[i]);
    glob_free(l->list);
    l->list = 0;
    l->count = 0;
}
HRESULT glob_langs_view(const GLangs *l, void **out)
{
    return glob_strvec((const WCHAR *const *)l->list, l->count, out);
}

/* ---- numeral systems ----------------------------------------------- */
/* the decimal ones Windows.Globalization names (NumeralSystemIdentifiers)
 * whose digits are ten consecutive code points */
static const struct { const WCHAR *name; WCHAR zero; } g_numerals[] = {
    { L"Latn", '0' }, { L"Arab", 0x0660 }, { L"ArabExt", 0x06F0 }, { L"Bali", 0x1B50 }, { L"Beng", 0x09E6 },
    { L"Cham", 0xAA50 }, { L"Deva", 0x0966 }, { L"FullWide", 0xFF10 }, { L"Gujr", 0x0AE6 }, { L"Guru", 0x0A66 },
    { L"Java", 0xA9D0 }, { L"Kali", 0xA900 }, { L"Khmr", 0x17E0 }, { L"Knda", 0x0CE6 }, { L"Lana", 0x1A80 },
    { L"LanaTham", 0x1A90 }, { L"Laoo", 0x0ED0 }, { L"Lepc", 0x1C40 }, { L"Limb", 0x1946 }, { L"Mlym", 0x0D66 },
    { L"Mong", 0x1810 }, { L"Mtei", 0xABF0 }, { L"Mymr", 0x1040 }, { L"MymrShan", 0x1090 }, { L"Nkoo", 0x07C0 },
    { L"Olck", 0x1C50 }, { L"Orya", 0x0B66 }, { L"Saur", 0xA8D0 }, { L"Sund", 0x1BB0 }, { L"Talu", 0x19D0 },
    { L"TamlDec", 0x0BE6 }, { L"Telu", 0x0C66 }, { L"Thai", 0x0E50 }, { L"Tibt", 0x0F20 }, { L"Vaii", 0xA620 },
};
WCHAR glob_numeral_zero(const WCHAR *system)
{
    if (!system || !*system) return '0';
    for (UINT32 i = 0; i < sizeof g_numerals / sizeof *g_numerals; i++)
        if (!lstrcmpiW(system, g_numerals[i].name)) return g_numerals[i].zero;
    return 0;
}
void glob_digits(WCHAR *s, const WCHAR *system)
{
    WCHAR zero = glob_numeral_zero(system);
    if (!zero || zero == '0') return;
    for (; *s; s++)
        if (*s >= '0' && *s <= '9') *s = (WCHAR)(zero + (*s - '0'));
}

/* ---- the DLL's classes --------------------------------------------- */
__declspec(dllexport) HRESULT WINAPI DllGetActivationFactory(HSTRING cls, void **factory)
{
    if (!factory) return E_POINTER;
    *factory = 0;
    const WCHAR *n = glob_raw(cls);
    HRESULT hr = glob_language_factory(n, factory);
    if (hr == CLASS_E_CLASSNOTAVAILABLE) hr = glob_number_factory(n, factory);
    if (hr == CLASS_E_CLASSNOTAVAILABLE) hr = glob_calendar_factory(n, factory);
    if (hr == CLASS_E_CLASSNOTAVAILABLE) hr = glob_datetime_factory(n, factory);
    return hr;
}
__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
