/*
 * glob.h — what Windows.Globalization.dll's classes share: a small object
 * model (an object of several interfaces, each a "face" pointing back at
 * the object, as one class answers QueryInterface for all of them), the
 * class factories' statics, strings, and the language and region a class
 * resolves its languages to (kernel32's locale data, as everything here is
 * worked out from it).
 */
#ifndef NOVA_GLOB_H
#define NOVA_GLOB_H
#include <windows.h>
#include <roapi.h>
#include <winstring.h>

typedef BYTE boolean_t;
#define VT(o) (*(void ***)(o))
#define E_BOUNDS_ ((HRESULT)0x8000000BL)

/* locale data this needs that NovaOS's headers leave out (winnls.h's values) */
#ifndef LOCALE_SPECIFICDATA
#define LOCALE_SPECIFICDATA 0x00000002
#endif
typedef DWORD LCTYPE;
#ifndef LOCALE_SINTLSYMBOL
#define LOCALE_SINTLSYMBOL 0x15
#endif
#ifndef LOCALE_INEGNUMBER
#define LOCALE_INEGNUMBER 0x1010
#endif
#ifndef LOCALE_SISO3166CTRYNAME
#define LOCALE_SISO3166CTRYNAME 0x5A
#endif
#ifndef LOCALE_SNAN
#define LOCALE_SNAN 0x69
#endif
#ifndef LOCALE_SPOSINFINITY
#define LOCALE_SPOSINFINITY 0x6A
#endif
#ifndef LOCALE_SNEGINFINITY
#define LOCALE_SNEGINFINITY 0x6B
#endif
#define GLOB_SLOCALIZEDDISPLAYNAME 0x02
#define GLOB_SLOCALIZEDCOUNTRYNAME 0x06
#define GLOB_SNATIVECOUNTRYNAME 0x08
#define GLOB_SDAYNAME1 0x2A                 /* Monday; 0x30 Sunday */
#define GLOB_SABBREVDAYNAME1 0x31
#define GLOB_SMONTHNAME1 0x38
#define GLOB_SABBREVMONTHNAME1 0x44
#define GLOB_IGEOID 0x5B
#define GLOB_SISO3166CTRYNAME2 0x68
#define GLOB_SSCRIPTS 0x6C
#define GLOB_SLOCALIZEDLANGUAGENAME 0x6F
#define GLOB_IREADINGLAYOUT 0x70
#define GLOB_SNATIVEDISPLAYNAME 0x73
#define GLOB_SMONTHDAY 0x78
BOOL WINAPI IsValidLocaleName(LPCWSTR name);
int WINAPI GetGeoInfoW(LONG id, DWORD type, LPWSTR buf, int n, LANGID lang);
BOOL WINAPI EnumSystemLocalesEx(BOOL (CALLBACK *fn)(LPWSTR, DWORD, LPARAM), DWORD flags, LPARAM param, LPVOID reserved);

/* ---- objects ------------------------------------------------------- */
#define GLOB_MAX_FACES 10
struct GObj;
typedef struct { const void *const *vt; struct GObj *o; } Face;
typedef struct {
    const WCHAR *name;                       /* its runtime class name */
    UINT32 faces;                            /* how many; faces[0] answers IUnknown/IInspectable */
    const GUID *const *iids;                 /* each face's interface */
    const void *const *const *vts;           /* and its vtable */
    void (*destroy)(struct GObj *);          /* frees what the object holds (not the object) */
    BOOL statics;                            /* a factory: lives as long as the process, never counted */
} GClass;
typedef struct GObj {
    const GClass *cls;
    LONG refs;
    Face face[GLOB_MAX_FACES];
} GObj;

/* every face's IUnknown and IInspectable methods */
HRESULT STDMETHODCALLTYPE glob_qi(Face *f, REFIID iid, void **out);
ULONG STDMETHODCALLTYPE glob_addref(Face *f);
ULONG STDMETHODCALLTYPE glob_release(Face *f);
HRESULT STDMETHODCALLTYPE glob_iids(Face *f, ULONG *n, GUID **iids);
HRESULT STDMETHODCALLTYPE glob_class(Face *f, HSTRING *name);
HRESULT STDMETHODCALLTYPE glob_trust(Face *f, int *t);
#define GLOB_INSP (void *)glob_qi, (void *)glob_addref, (void *)glob_release, (void *)glob_iids, (void *)glob_class, (void *)glob_trust

/* a new object of @cls, @size bytes (its struct starts with a GObj), one
 * reference; NULL when out of memory */
void *glob_new(const GClass *cls, SIZE_T size);
/* the object's face @i, with a reference (an out parameter's value) */
void *glob_out(void *obj, UINT32 i);
/* a static object's faces made (a factory, set up once) */
void glob_static_init(GObj *o, const GClass *cls);
/* the face's object, as its class's struct */
#define OBJ(T, f) ((T *)((Face *)(f))->o)
static inline void glob_unref(void *p) { if (p) ((ULONG (STDMETHODCALLTYPE *)(void *))VT(p)[2])(p); }
static inline void glob_ref(void *p) { if (p) ((ULONG (STDMETHODCALLTYPE *)(void *))VT(p)[1])(p); }

/* the factories' common IActivationFactory.ActivateInstance, for a class
 * with no default constructor */
HRESULT STDMETHODCALLTYPE glob_no_activate(Face *f, void **out);

/* ---- strings ------------------------------------------------------- */
HRESULT glob_str(const WCHAR *s, HSTRING *out);       /* an HSTRING copy; E_POINTER without @out */
const WCHAR *glob_raw(HSTRING s);                      /* never NULL */
WCHAR *glob_dup(const WCHAR *s);                       /* on the process heap */
void glob_free(void *p);
/* @n strings from an IIterable<String> into @out (glob_dup'd), at most @cap; the count */
UINT32 glob_iterable(void *iterable, WCHAR **out, UINT32 cap);
/* a new IVectorView<String> of @n strings */
HRESULT glob_strvec(const WCHAR *const *items, UINT32 n, void **out);

/* ---- languages and regions ----------------------------------------- */
#define GLOB_TAG 86
/* the user's locale (GetUserDefaultLocaleName): en-US */
void glob_user_locale(WCHAR out[GLOB_TAG]);
/* a language tag as a locale kernel32 knows (ResolveLocaleName): FALSE
 * when it knows none */
BOOL glob_resolve(const WCHAR *tag, WCHAR out[GLOB_TAG]);
/* the application's languages (ApplicationLanguages.Languages): the
 * override first, then the user's; returns the count */
UINT32 glob_app_languages(WCHAR out[][GLOB_TAG], UINT32 cap);
/* ApplicationLanguages.PrimaryLanguageOverride ("" when none) */
void glob_set_override(const WCHAR *tag);
void glob_get_override(WCHAR out[GLOB_TAG]);
/* a class's languages: those given (NULL: the application's), and the
 * first a locale is known for (its resolved language, en-US when none) */
typedef struct {
    WCHAR **list;            /* glob_dup'd */
    UINT32 count;
    WCHAR resolved[GLOB_TAG];
    WCHAR region[4];         /* the region the class was given, or the resolved locale's */
} GLangs;
HRESULT glob_langs(GLangs *l, void *iterable, HSTRING region);
void glob_langs_free(GLangs *l);
HRESULT glob_langs_view(const GLangs *l, void **out);
/* @type of locale @name as text (GetLocaleInfoEx); "" when it has none */
void glob_info(const WCHAR *name, LCTYPE type, WCHAR *out, int cap);
int glob_info_int(const WCHAR *name, LCTYPE type, int dflt);
/* the user's region code (US) and a locale for a region: @lang-@code
 * when kernel32 knows it, else the first locale of that country */
void glob_user_region(WCHAR out[4]);
/* whether a locale's clock is the 24-hour one (its LOCALE_STIMEFORMAT has H) */
BOOL glob_24h(const WCHAR *locale);
BOOL glob_region_locale(const WCHAR *code, const WCHAR *lang, WCHAR out[GLOB_TAG]);

/* ---- digits -------------------------------------------------------- */
/* the zero of a numeral system (Latn, Arab, Deva ...): 0 when unknown */
WCHAR glob_numeral_zero(const WCHAR *system);
/* ASCII digits in @s turned into @system's, in place */
void glob_digits(WCHAR *s, const WCHAR *system);

/* ---- each file's classes ------------------------------------------- */
HRESULT glob_language_factory(const WCHAR *cls, void **out);   /* language.c */
HRESULT glob_number_factory(const WCHAR *cls, void **out);     /* numfmt.c */
HRESULT glob_calendar_factory(const WCHAR *cls, void **out);   /* calendar.c */
HRESULT glob_datetime_factory(const WCHAR *cls, void **out);   /* datetime.c */

/* ---- time (calendar.c, for datetime.c too) ------------------------- */
/* a Windows.Foundation.DateTime (100 ns ticks since 1601, UTC) in a time
 * zone (UTC, or the machine's local zone): the ticks there */
INT64 glob_to_zone(INT64 utc, BOOL utc_zone);
INT64 glob_from_zone(INT64 local, BOOL utc_zone);
typedef struct { int year, month, day, dow, hour, minute, second; INT64 frac; } GTime;   /* frac: ticks below a second */
void glob_split(INT64 ticks, GTime *t);
INT64 glob_join(const GTime *t);
int glob_days_in_month(int year, int month);

#endif
