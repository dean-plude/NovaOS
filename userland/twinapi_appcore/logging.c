/*
 * logging.c — Windows.Foundation.Diagnostics' logging classes, which
 * Windows serves from its Windows Runtime base (wincorlib's neighbours) and
 * apps use for their TraceLogging events: LoggingChannel (an event
 * provider: a name and its ETW provider id), LoggingChannelOptions (the
 * provider's group), LoggingOptions (an event's keywords, tags, task,
 * opcode and activity ids), LoggingFields (an event's payload) and
 * LoggingActivity (a start/stop pair on a channel).  Calculator's
 * TraceLogger makes one channel, "MicrosoftCalculator", and a LoggingFields
 * for each event it reports.
 *
 * On Windows a channel's events reach whichever ETW session enables its
 * provider.  NovaOS has no ETW sessions, so no provider is ever enabled:
 * a channel's Enabled (and ILoggingTarget's IsEnabled*) answers false,
 * LoggingEnabled takes handlers and is never raised, and LogEvent,
 * LogMessage and LogValuePair succeed and go nowhere, as they do on Windows
 * while nobody listens.  A LoggingFields keeps no fields for the same
 * reason: every Add* (all 115 of ILoggingFields' methods) succeeds and
 * drops its value.  The options classes keep what they are given.  A
 * channel made without an id has the null GUID (Windows hashes the name
 * into one; nothing here would see it).
 *
 * twinapi.appcore.dll's DllGetActivationFactory hands the five class names
 * to logging_factory; the registry's ActivatableClassId keys
 * (kernel/um/um_registry.c) name that DLL.
 */
#include <windows.h>
#include <roapi.h>

typedef BYTE boolean_t;
WINOLEAPI_(HRESULT) CoCreateGuid(GUID *g);

static const GUID IID_IUnknown_l       = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_l   = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_l   = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IActivationFactory_l = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IClosable_l      = { 0x30D5A829, 0x7FA4, 0x4026, { 0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E } };
/* Windows.Foundation.Diagnostics (IIDs as windows-rs 0.58 lists them) */
static const GUID IID_ILoggingActivity_ = { 0xBC032941, 0xB766, 0x4CB5, { 0x98, 0x48, 0x97, 0xAC, 0x6B, 0xA6, 0xD6, 0x0C } };
static const GUID IID_ILoggingActivity2_ = { 0x26C29808, 0x6322, 0x456A, { 0xAF, 0x82, 0x80, 0xC8, 0x64, 0x2F, 0x17, 0x8B } };
static const GUID IID_ILoggingActivityFactory_ = { 0x6B33B483, 0xE10A, 0x4C58, { 0x97, 0xD5, 0x10, 0xFB, 0x45, 0x10, 0x74, 0xFB } };
static const GUID IID_ILoggingChannel_ = { 0xE9A50343, 0x11D7, 0x4F01, { 0xB5, 0xCA, 0xCF, 0x49, 0x52, 0x78, 0xC0, 0xA8 } };
static const GUID IID_ILoggingChannel2_ = { 0x9F4C3CF3, 0x0BAC, 0x45A5, { 0x9E, 0x33, 0xBA, 0xF3, 0xF3, 0xA2, 0x46, 0xA5 } };
static const GUID IID_ILoggingChannelFactory_ = { 0x4EDC5B9C, 0xAF80, 0x4A9B, { 0xB0, 0xDC, 0x39, 0x8F, 0x9A, 0xE5, 0x20, 0x7B } };
static const GUID IID_ILoggingChannelFactory2_ = { 0x4C6EF5DD, 0x3B27, 0x4DC9, { 0x99, 0xF0, 0x29, 0x9C, 0x6E, 0x46, 0x03, 0xA1 } };
static const GUID IID_ILoggingChannelOptions_ = { 0xC3E847FF, 0x0EBB, 0x4A53, { 0x8C, 0x54, 0xDE, 0xC2, 0x49, 0x26, 0xCB, 0x2C } };
static const GUID IID_ILoggingChannelOptionsFactory_ = { 0xA93151DA, 0x7FAF, 0x4191, { 0x87, 0x55, 0x5E, 0x86, 0xDC, 0x65, 0xD8, 0x96 } };
static const GUID IID_ILoggingFields_ = { 0xD7F6B7AF, 0x762D, 0x4579, { 0x83, 0xBD, 0x52, 0xC2, 0x3B, 0xC3, 0x33, 0xBC } };
static const GUID IID_ILoggingOptions_ = { 0x90BC7850, 0x0192, 0x4F5D, { 0xAC, 0x26, 0x00, 0x6A, 0xDA, 0xCA, 0x12, 0xD8 } };
static const GUID IID_ILoggingOptionsFactory_ = { 0xD713C6CB, 0x98AB, 0x464B, { 0x9F, 0x22, 0xA3, 0x26, 0x84, 0x78, 0x36, 0x8A } };
static const GUID IID_ILoggingTarget_ = { 0x65F16C35, 0xE388, 0x4E26, { 0xB1, 0x7A, 0xF5, 0x1C, 0xD3, 0xA8, 0x39, 0x16 } };

/* Every object here is one Obj; each interface it answers for is an Iface
 * (a vtable and the way back to its object).  Factories are static Objs. */
enum { K_CHANNEL, K_CHANNEL_OPTIONS, K_OPTIONS, K_FIELDS, K_ACTIVITY,
       K_CHANNEL_FACTORY, K_CHANNEL_OPTIONS_FACTORY, K_OPTIONS_FACTORY, K_FIELDS_FACTORY, K_ACTIVITY_FACTORY, K_COUNT };
#define MAX_IFACES 4
typedef struct Obj Obj;
typedef struct { const void *const *vt; Obj *self; } Iface;
struct Obj {
    Iface ifs[MAX_IFACES];
    LONG refs;                  /* -1: static */
    int kind;
    HSTRING name;               /* a channel's or an activity's */
    GUID id;                    /* a channel's provider id or an activity's id; the channel options' group */
    Obj *channel;               /* an activity's */
    INT64 keywords;             /* a LoggingOptions' */
    INT32 tags, opcode;
    INT16 task;
    GUID activity, related;
};
#define SELF(p) (((Iface *)(p))->self)
typedef struct { const GUID *iid; const void *const *vt; } IfaceDesc;
static const IfaceDesc *g_descs[K_COUNT];
static const WCHAR *const g_names[K_COUNT] = {
    L"Windows.Foundation.Diagnostics.LoggingChannel", L"Windows.Foundation.Diagnostics.LoggingChannelOptions",
    L"Windows.Foundation.Diagnostics.LoggingOptions", L"Windows.Foundation.Diagnostics.LoggingFields",
    L"Windows.Foundation.Diagnostics.LoggingActivity",
    L"Windows.Foundation.Diagnostics.LoggingChannel", L"Windows.Foundation.Diagnostics.LoggingChannelOptions",
    L"Windows.Foundation.Diagnostics.LoggingOptions", L"Windows.Foundation.Diagnostics.LoggingFields",
    L"Windows.Foundation.Diagnostics.LoggingActivity",
};

static HRESULT STDMETHODCALLTYPE o_qi(void *p, REFIID iid, void **out)
{
    Obj *o = SELF(p);
    if (!out) return E_POINTER;
    *out = 0;
    if (IsEqualGUID(iid, &IID_IUnknown_l) || IsEqualGUID(iid, &IID_IInspectable_l) || IsEqualGUID(iid, &IID_IAgileObject_l))
        *out = &o->ifs[0];
    else
        for (int i = 0; i < MAX_IFACES && g_descs[o->kind][i].iid; i++)
            if (IsEqualGUID(iid, g_descs[o->kind][i].iid)) { *out = &o->ifs[i]; break; }
    if (!*out) return E_NOINTERFACE;
    if (o->refs >= 0) InterlockedIncrement(&o->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE o_addref(void *p)
{
    Obj *o = SELF(p);
    return o->refs < 0 ? 2 : (ULONG)InterlockedIncrement(&o->refs);
}
static ULONG STDMETHODCALLTYPE o_release(void *p)
{
    Obj *o = SELF(p);
    if (o->refs < 0) return 1;
    LONG n = InterlockedDecrement(&o->refs);
    if (!n) {
        if (o->name) WindowsDeleteString(o->name);
        if (o->channel) o_release(&o->channel->ifs[0]);
        HeapFree(GetProcessHeap(), 0, o);
    }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE o_iids(void *p, ULONG *n, GUID **iids) { (void)p; if (!n || !iids) return E_POINTER; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE o_class(void *p, HSTRING *out)
{
    const WCHAR *s = g_names[SELF(p)->kind];
    UINT32 n = 0;
    if (!out) return E_POINTER;
    while (s[n]) n++;
    return WindowsCreateString(s, n, out);
}
static HRESULT STDMETHODCALLTYPE o_trust(void *p, int *t) { (void)p; if (!t) return E_POINTER; *t = 0; return S_OK; }
#define OBJ_FNS (void *)o_qi, (void *)o_addref, (void *)o_release, (void *)o_iids, (void *)o_class, (void *)o_trust

static HRESULT obj_new(int kind, Obj **out)
{
    Obj *o = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *o);
    *out = o;
    if (!o) return E_OUTOFMEMORY;
    o->refs = 1;
    o->kind = kind;
    for (int i = 0; i < MAX_IFACES && g_descs[kind][i].iid; i++) { o->ifs[i].vt = g_descs[kind][i].vt; o->ifs[i].self = o; }
    return S_OK;
}
/* a new object, handed out by its first interface (the reference obj_new made) */
static HRESULT obj_out(Obj *o, void **out)
{
    *out = &o->ifs[0];
    return S_OK;
}

/* ---- shared method bodies --------------------------------------------- */
static HRESULT STDMETHODCALLTYPE get_name(void *p, HSTRING *v) { if (!v) return E_POINTER; return WindowsDuplicateString(SELF(p)->name, v); }
static HRESULT STDMETHODCALLTYPE get_id(void *p, GUID *v) { if (!v) return E_POINTER; *v = SELF(p)->id; return S_OK; }
static HRESULT STDMETHODCALLTYPE get_false(void *p, boolean_t *v) { (void)p; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE get_false_l(void *p, int level, boolean_t *v) { (void)level; return get_false(p, v); }
static HRESULT STDMETHODCALLTYPE get_false_lk(void *p, int level, INT64 kw, boolean_t *v) { (void)level; (void)kw; return get_false(p, v); }
static HRESULT STDMETHODCALLTYPE close_(void *p) { (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE drop_s(void *p, HSTRING s) { (void)p; (void)s; return S_OK; }
static HRESULT STDMETHODCALLTYPE drop_si(void *p, HSTRING s, int a) { (void)p; (void)s; (void)a; return S_OK; }
static HRESULT STDMETHODCALLTYPE drop_sii(void *p, HSTRING s, int a, int b) { (void)p; (void)s; (void)a; (void)b; return S_OK; }
static HRESULT STDMETHODCALLTYPE drop_sp(void *p, HSTRING s, void *f) { (void)p; (void)s; (void)f; return S_OK; }
static HRESULT STDMETHODCALLTYPE drop_spi(void *p, HSTRING s, void *f, int l) { (void)p; (void)s; (void)f; (void)l; return S_OK; }
static HRESULT STDMETHODCALLTYPE drop_spip(void *p, HSTRING s, void *f, int l, void *o) { (void)p; (void)s; (void)f; (void)l; (void)o; return S_OK; }

/* ILoggingTarget's StartActivity*: an activity on this channel (or on an
 * activity's channel) */
static HRESULT start_activity(void *p, HSTRING name, void **out)
{
    Obj *on = SELF(p), *a;
    if (!out) return E_POINTER;
    *out = 0;
    if (!name) return E_INVALIDARG;
    HRESULT hr = obj_new(K_ACTIVITY, &a);
    if (FAILED(hr)) return hr;
    Obj *ch = on->kind == K_ACTIVITY ? on->channel : on;
    if (ch) { a->channel = ch; o_addref(&ch->ifs[0]); }
    if (FAILED(hr = WindowsDuplicateString(name, &a->name))) { o_release(&a->ifs[0]); return hr; }
    CoCreateGuid(&a->id);
    return obj_out(a, out);
}
static HRESULT STDMETHODCALLTYPE ta_start(void *p, HSTRING n, void **o) { return start_activity(p, n, o); }
static HRESULT STDMETHODCALLTYPE ta_start_f(void *p, HSTRING n, void *f, void **o) { (void)f; return start_activity(p, n, o); }
static HRESULT STDMETHODCALLTYPE ta_start_fl(void *p, HSTRING n, void *f, int l, void **o) { (void)f; (void)l; return start_activity(p, n, o); }
static HRESULT STDMETHODCALLTYPE ta_start_flo(void *p, HSTRING n, void *f, int l, void *op, void **o) { (void)f; (void)l; (void)op; return start_activity(p, n, o); }
static const void *const target_vt[] = { OBJ_FNS, (void *)get_false, (void *)get_false_l, (void *)get_false_lk,
    (void *)drop_s, (void *)drop_sp, (void *)drop_spi, (void *)drop_spip,
    (void *)ta_start, (void *)ta_start_f, (void *)ta_start_fl, (void *)ta_start_flo };
static const void *const closable_vt[] = { OBJ_FNS, (void *)close_ };

/* ---- LoggingChannel --------------------------------------------------- */
static HRESULT STDMETHODCALLTYPE ch_level(void *p, int *v) { (void)p; if (!v) return E_POINTER; *v = 0; return S_OK; }  /* Verbose */
static LONG g_tokens;
static HRESULT STDMETHODCALLTYPE ch_enabled_add(void *p, void *h, EventRegistrationToken *t)
{
    (void)p;
    if (!h || !t) return E_INVALIDARG;
    t->value = InterlockedIncrement(&g_tokens);   /* never raised: no session will enable the provider */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ch_enabled_remove(void *p, EventRegistrationToken t) { (void)p; (void)t; return S_OK; }
static const void *const channel_vt[] = { OBJ_FNS, (void *)get_name, (void *)get_false, (void *)ch_level,
    (void *)drop_s, (void *)drop_si, (void *)drop_si, (void *)drop_sii, (void *)ch_enabled_add, (void *)ch_enabled_remove };
static const void *const channel2_vt[] = { OBJ_FNS, (void *)get_id };
static const IfaceDesc channel_ifs[] = { { &IID_ILoggingChannel_, channel_vt }, { &IID_ILoggingChannel2_, channel2_vt },
                                         { &IID_ILoggingTarget_, target_vt }, { &IID_IClosable_l, closable_vt } };

static HRESULT channel_make(HSTRING name, const GUID *id, void **out)
{
    Obj *c;
    if (!out) return E_POINTER;
    *out = 0;
    if (!name) return E_INVALIDARG;
    HRESULT hr = obj_new(K_CHANNEL, &c);
    if (FAILED(hr)) return hr;
    if (FAILED(hr = WindowsDuplicateString(name, &c->name))) { o_release(&c->ifs[0]); return hr; }
    if (id) c->id = *id;
    return obj_out(c, out);
}
static HRESULT STDMETHODCALLTYPE chf_activate(void *p, void **o) { (void)p; if (o) *o = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE chf_create(void *p, HSTRING n, void **o) { (void)p; return channel_make(n, 0, o); }
static HRESULT STDMETHODCALLTYPE chf_create_opts(void *p, HSTRING n, void *opts, void **o) { (void)p; (void)opts; return channel_make(n, 0, o); }
static HRESULT STDMETHODCALLTYPE chf_create_opts_id(void *p, HSTRING n, void *opts, GUID id, void **o) { (void)p; (void)opts; return channel_make(n, &id, o); }
static const void *const chf_vt[] = { OBJ_FNS, (void *)chf_activate };
static const void *const chf1_vt[] = { OBJ_FNS, (void *)chf_create };
static const void *const chf2_vt[] = { OBJ_FNS, (void *)chf_create_opts, (void *)chf_create_opts_id };
static const IfaceDesc chf_ifs[] = { { &IID_IActivationFactory_l, chf_vt }, { &IID_ILoggingChannelFactory_, chf1_vt },
                                     { &IID_ILoggingChannelFactory2_, chf2_vt }, { 0 } };

/* ---- LoggingChannelOptions -------------------------------------------- */
static HRESULT STDMETHODCALLTYPE co_put_group(void *p, GUID g) { SELF(p)->id = g; return S_OK; }
static const void *const chopt_vt[] = { OBJ_FNS, (void *)get_id, (void *)co_put_group };
static const IfaceDesc chopt_ifs[] = { { &IID_ILoggingChannelOptions_, chopt_vt }, { 0 } };
static HRESULT chopt_make(const GUID *group, void **out)
{
    Obj *o;
    if (!out) return E_POINTER;
    *out = 0;
    HRESULT hr = obj_new(K_CHANNEL_OPTIONS, &o);
    if (FAILED(hr)) return hr;
    if (group) o->id = *group;
    return obj_out(o, out);
}
static HRESULT STDMETHODCALLTYPE cof_activate(void *p, void **o) { (void)p; return chopt_make(0, o); }
static HRESULT STDMETHODCALLTYPE cof_create(void *p, GUID g, void **o) { (void)p; return chopt_make(&g, o); }
static const void *const cof_vt[] = { OBJ_FNS, (void *)cof_activate };
static const void *const cof1_vt[] = { OBJ_FNS, (void *)cof_create };
static const IfaceDesc cof_ifs[] = { { &IID_IActivationFactory_l, cof_vt }, { &IID_ILoggingChannelOptionsFactory_, cof1_vt }, { 0 } };

/* ---- LoggingOptions --------------------------------------------------- */
static HRESULT STDMETHODCALLTYPE op_kw(void *p, INT64 *v) { if (!v) return E_POINTER; *v = SELF(p)->keywords; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_put_kw(void *p, INT64 v) { SELF(p)->keywords = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_tags(void *p, INT32 *v) { if (!v) return E_POINTER; *v = SELF(p)->tags; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_put_tags(void *p, INT32 v) { SELF(p)->tags = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_task(void *p, INT16 *v) { if (!v) return E_POINTER; *v = SELF(p)->task; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_put_task(void *p, INT16 v) { SELF(p)->task = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_opcode(void *p, INT32 *v) { if (!v) return E_POINTER; *v = SELF(p)->opcode; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_put_opcode(void *p, INT32 v) { SELF(p)->opcode = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_act(void *p, GUID *v) { if (!v) return E_POINTER; *v = SELF(p)->activity; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_put_act(void *p, GUID v) { SELF(p)->activity = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_rel(void *p, GUID *v) { if (!v) return E_POINTER; *v = SELF(p)->related; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_put_rel(void *p, GUID v) { SELF(p)->related = v; return S_OK; }
static const void *const opt_vt[] = { OBJ_FNS, (void *)op_kw, (void *)op_put_kw, (void *)op_tags, (void *)op_put_tags,
    (void *)op_task, (void *)op_put_task, (void *)op_opcode, (void *)op_put_opcode,
    (void *)op_act, (void *)op_put_act, (void *)op_rel, (void *)op_put_rel };
static const IfaceDesc opt_ifs[] = { { &IID_ILoggingOptions_, opt_vt }, { 0 } };
static HRESULT opt_make(INT64 kw, void **out)
{
    Obj *o;
    if (!out) return E_POINTER;
    *out = 0;
    HRESULT hr = obj_new(K_OPTIONS, &o);
    if (FAILED(hr)) return hr;
    o->keywords = kw;
    return obj_out(o, out);
}
static HRESULT STDMETHODCALLTYPE opf_activate(void *p, void **o) { (void)p; return opt_make(0, o); }
static HRESULT STDMETHODCALLTYPE opf_create(void *p, INT64 kw, void **o) { (void)p; return opt_make(kw, o); }
static const void *const opf_vt[] = { OBJ_FNS, (void *)opf_activate };
static const void *const opf1_vt[] = { OBJ_FNS, (void *)opf_create };
static const IfaceDesc opf_ifs[] = { { &IID_IActivationFactory_l, opf_vt }, { &IID_ILoggingOptionsFactory_, opf1_vt }, { 0 } };

/* ---- LoggingFields: each method takes the field's name and its value(s),
 * which no session reads, so each drops them.  The stubs differ only in
 * how many argument words they take after `this` (an x86 stdcall callee
 * pops its arguments: a GUID or Rect is four words, an 8-byte value or a
 * Point or Size two, anything else one); the list follows ILoggingFields'
 * order (windows-rs 0.58's ILoggingFields_Vtbl) ---------------------------- */
typedef UINT32 W;
static HRESULT STDMETHODCALLTYPE lf_drop0(void *p) { (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE lf_drop1(void *p, W a) { (void)p; (void)a; return S_OK; }
static HRESULT STDMETHODCALLTYPE lf_drop2(void *p, W a, W b) { (void)p; (void)a; (void)b; return S_OK; }
static HRESULT STDMETHODCALLTYPE lf_drop3(void *p, W a, W b, W c) { (void)p; (void)a; (void)b; (void)c; return S_OK; }
static HRESULT STDMETHODCALLTYPE lf_drop4(void *p, W a, W b, W c, W d) { (void)p; (void)a; (void)b; (void)c; (void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE lf_drop5(void *p, W a, W b, W c, W d, W e) { (void)p; (void)a; (void)b; (void)c; (void)d; (void)e; return S_OK; }
static HRESULT STDMETHODCALLTYPE lf_drop6(void *p, W a, W b, W c, W d, W e, W f) { (void)p; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return S_OK; }
static HRESULT STDMETHODCALLTYPE lf_drop7(void *p, W a, W b, W c, W d, W e, W f, W g) { (void)p; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; return S_OK; }
static const void *const fields_vt[] = { OBJ_FNS,
    (void *)lf_drop0,   /* Clear */
    (void *)lf_drop1,   /* BeginStruct */
    (void *)lf_drop2,   /* BeginStructWithTags */
    (void *)lf_drop0,   /* EndStruct */
    (void *)lf_drop1,   /* AddEmpty */
    (void *)lf_drop2,   /* AddEmptyWithFormat */
    (void *)lf_drop3,   /* AddEmptyWithFormatAndTags */
    (void *)lf_drop2,   /* AddUInt8 */
    (void *)lf_drop3,   /* AddUInt8WithFormat */
    (void *)lf_drop4,   /* AddUInt8WithFormatAndTags */
    (void *)lf_drop3,   /* AddUInt8Array */
    (void *)lf_drop4,   /* AddUInt8ArrayWithFormat */
    (void *)lf_drop5,   /* AddUInt8ArrayWithFormatAndTags */
    (void *)lf_drop2,   /* AddInt16 */
    (void *)lf_drop3,   /* AddInt16WithFormat */
    (void *)lf_drop4,   /* AddInt16WithFormatAndTags */
    (void *)lf_drop3,   /* AddInt16Array */
    (void *)lf_drop4,   /* AddInt16ArrayWithFormat */
    (void *)lf_drop5,   /* AddInt16ArrayWithFormatAndTags */
    (void *)lf_drop2,   /* AddUInt16 */
    (void *)lf_drop3,   /* AddUInt16WithFormat */
    (void *)lf_drop4,   /* AddUInt16WithFormatAndTags */
    (void *)lf_drop3,   /* AddUInt16Array */
    (void *)lf_drop4,   /* AddUInt16ArrayWithFormat */
    (void *)lf_drop5,   /* AddUInt16ArrayWithFormatAndTags */
    (void *)lf_drop2,   /* AddInt32 */
    (void *)lf_drop3,   /* AddInt32WithFormat */
    (void *)lf_drop4,   /* AddInt32WithFormatAndTags */
    (void *)lf_drop3,   /* AddInt32Array */
    (void *)lf_drop4,   /* AddInt32ArrayWithFormat */
    (void *)lf_drop5,   /* AddInt32ArrayWithFormatAndTags */
    (void *)lf_drop2,   /* AddUInt32 */
    (void *)lf_drop3,   /* AddUInt32WithFormat */
    (void *)lf_drop4,   /* AddUInt32WithFormatAndTags */
    (void *)lf_drop3,   /* AddUInt32Array */
    (void *)lf_drop4,   /* AddUInt32ArrayWithFormat */
    (void *)lf_drop5,   /* AddUInt32ArrayWithFormatAndTags */
    (void *)lf_drop3,   /* AddInt64 */
    (void *)lf_drop4,   /* AddInt64WithFormat */
    (void *)lf_drop5,   /* AddInt64WithFormatAndTags */
    (void *)lf_drop3,   /* AddInt64Array */
    (void *)lf_drop4,   /* AddInt64ArrayWithFormat */
    (void *)lf_drop5,   /* AddInt64ArrayWithFormatAndTags */
    (void *)lf_drop3,   /* AddUInt64 */
    (void *)lf_drop4,   /* AddUInt64WithFormat */
    (void *)lf_drop5,   /* AddUInt64WithFormatAndTags */
    (void *)lf_drop3,   /* AddUInt64Array */
    (void *)lf_drop4,   /* AddUInt64ArrayWithFormat */
    (void *)lf_drop5,   /* AddUInt64ArrayWithFormatAndTags */
    (void *)lf_drop2,   /* AddSingle */
    (void *)lf_drop3,   /* AddSingleWithFormat */
    (void *)lf_drop4,   /* AddSingleWithFormatAndTags */
    (void *)lf_drop3,   /* AddSingleArray */
    (void *)lf_drop4,   /* AddSingleArrayWithFormat */
    (void *)lf_drop5,   /* AddSingleArrayWithFormatAndTags */
    (void *)lf_drop3,   /* AddDouble */
    (void *)lf_drop4,   /* AddDoubleWithFormat */
    (void *)lf_drop5,   /* AddDoubleWithFormatAndTags */
    (void *)lf_drop3,   /* AddDoubleArray */
    (void *)lf_drop4,   /* AddDoubleArrayWithFormat */
    (void *)lf_drop5,   /* AddDoubleArrayWithFormatAndTags */
    (void *)lf_drop2,   /* AddChar16 */
    (void *)lf_drop3,   /* AddChar16WithFormat */
    (void *)lf_drop4,   /* AddChar16WithFormatAndTags */
    (void *)lf_drop3,   /* AddChar16Array */
    (void *)lf_drop4,   /* AddChar16ArrayWithFormat */
    (void *)lf_drop5,   /* AddChar16ArrayWithFormatAndTags */
    (void *)lf_drop2,   /* AddBoolean */
    (void *)lf_drop3,   /* AddBooleanWithFormat */
    (void *)lf_drop4,   /* AddBooleanWithFormatAndTags */
    (void *)lf_drop3,   /* AddBooleanArray */
    (void *)lf_drop4,   /* AddBooleanArrayWithFormat */
    (void *)lf_drop5,   /* AddBooleanArrayWithFormatAndTags */
    (void *)lf_drop2,   /* AddString */
    (void *)lf_drop3,   /* AddStringWithFormat */
    (void *)lf_drop4,   /* AddStringWithFormatAndTags */
    (void *)lf_drop3,   /* AddStringArray */
    (void *)lf_drop4,   /* AddStringArrayWithFormat */
    (void *)lf_drop5,   /* AddStringArrayWithFormatAndTags */
    (void *)lf_drop5,   /* AddGuid */
    (void *)lf_drop6,   /* AddGuidWithFormat */
    (void *)lf_drop7,   /* AddGuidWithFormatAndTags */
    (void *)lf_drop3,   /* AddGuidArray */
    (void *)lf_drop4,   /* AddGuidArrayWithFormat */
    (void *)lf_drop5,   /* AddGuidArrayWithFormatAndTags */
    (void *)lf_drop3,   /* AddDateTime */
    (void *)lf_drop4,   /* AddDateTimeWithFormat */
    (void *)lf_drop5,   /* AddDateTimeWithFormatAndTags */
    (void *)lf_drop3,   /* AddDateTimeArray */
    (void *)lf_drop4,   /* AddDateTimeArrayWithFormat */
    (void *)lf_drop5,   /* AddDateTimeArrayWithFormatAndTags */
    (void *)lf_drop3,   /* AddTimeSpan */
    (void *)lf_drop4,   /* AddTimeSpanWithFormat */
    (void *)lf_drop5,   /* AddTimeSpanWithFormatAndTags */
    (void *)lf_drop3,   /* AddTimeSpanArray */
    (void *)lf_drop4,   /* AddTimeSpanArrayWithFormat */
    (void *)lf_drop5,   /* AddTimeSpanArrayWithFormatAndTags */
    (void *)lf_drop3,   /* AddPoint */
    (void *)lf_drop4,   /* AddPointWithFormat */
    (void *)lf_drop5,   /* AddPointWithFormatAndTags */
    (void *)lf_drop3,   /* AddPointArray */
    (void *)lf_drop4,   /* AddPointArrayWithFormat */
    (void *)lf_drop5,   /* AddPointArrayWithFormatAndTags */
    (void *)lf_drop3,   /* AddSize */
    (void *)lf_drop4,   /* AddSizeWithFormat */
    (void *)lf_drop5,   /* AddSizeWithFormatAndTags */
    (void *)lf_drop3,   /* AddSizeArray */
    (void *)lf_drop4,   /* AddSizeArrayWithFormat */
    (void *)lf_drop5,   /* AddSizeArrayWithFormatAndTags */
    (void *)lf_drop5,   /* AddRect */
    (void *)lf_drop6,   /* AddRectWithFormat */
    (void *)lf_drop7,   /* AddRectWithFormatAndTags */
    (void *)lf_drop3,   /* AddRectArray */
    (void *)lf_drop4,   /* AddRectArrayWithFormat */
    (void *)lf_drop5,   /* AddRectArrayWithFormatAndTags */
};
static const IfaceDesc fields_ifs[] = { { &IID_ILoggingFields_, fields_vt }, { 0 } };
static HRESULT STDMETHODCALLTYPE ff_activate(void *p, void **out)
{
    Obj *o;
    (void)p;
    if (!out) return E_POINTER;
    *out = 0;
    HRESULT hr = obj_new(K_FIELDS, &o);
    return FAILED(hr) ? hr : obj_out(o, out);
}
static const void *const ff_vt[] = { OBJ_FNS, (void *)ff_activate };
static const IfaceDesc ff_ifs[] = { { &IID_IActivationFactory_l, ff_vt }, { 0 } };

/* ---- LoggingActivity: a name and an id on a channel; it is a target of
 * its own (its events go where the channel's do: nowhere) -------------- */
static HRESULT STDMETHODCALLTYPE ac_channel(void *p, void **v)
{
    Obj *ch = SELF(p)->channel;
    if (!v) return E_POINTER;
    *v = 0;
    if (!ch) return S_OK;
    o_addref(&ch->ifs[0]);
    *v = &ch->ifs[0];
    return S_OK;
}
static const void *const act_vt[] = { OBJ_FNS, (void *)get_name, (void *)get_id };
static const void *const act2_vt[] = { OBJ_FNS, (void *)ac_channel, (void *)drop_s, (void *)drop_sp, (void *)drop_spip };
static const IfaceDesc act_ifs[] = { { &IID_ILoggingActivity_, act_vt }, { &IID_ILoggingActivity2_, act2_vt },
                                     { &IID_ILoggingTarget_, target_vt }, { &IID_IClosable_l, closable_vt } };
static HRESULT activity_make(HSTRING name, void *channel, void **out)
{
    if (!out) return E_POINTER;
    *out = 0;
    if (!channel) return E_INVALIDARG;
    /* the caller's ILoggingChannel: ours, so its Iface leads back to it */
    void *ch = 0;
    HRESULT hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, REFIID, void **))(*(void ***)channel)[0])(channel, &IID_ILoggingChannel_, &ch);
    if (FAILED(hr)) return hr;
    if (((Iface *)ch)->vt != channel_vt) { o_release(ch); return E_INVALIDARG; }
    hr = start_activity(ch, name, out);
    o_release(ch);
    return hr;
}
static HRESULT STDMETHODCALLTYPE af_activate(void *p, void **o) { (void)p; if (o) *o = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE af_create(void *p, HSTRING n, void *ch, void **o) { (void)p; return activity_make(n, ch, o); }
static HRESULT STDMETHODCALLTYPE af_create_l(void *p, HSTRING n, void *ch, int l, void **o) { (void)p; (void)l; return activity_make(n, ch, o); }
static const void *const af_vt[] = { OBJ_FNS, (void *)af_activate };
static const void *const af1_vt[] = { OBJ_FNS, (void *)af_create, (void *)af_create_l };
static const IfaceDesc af_ifs[] = { { &IID_IActivationFactory_l, af_vt }, { &IID_ILoggingActivityFactory_, af1_vt }, { 0 } };

static const IfaceDesc *g_descs[K_COUNT] = { channel_ifs, chopt_ifs, opt_ifs, fields_ifs, act_ifs,
                                             chf_ifs, cof_ifs, opf_ifs, ff_ifs, af_ifs };

static Obj g_factories[5];
static void factories_init(void)
{
    static const IfaceDesc *const f[5] = { chf_ifs, cof_ifs, opf_ifs, ff_ifs, af_ifs };
    for (int i = 0; i < 5; i++) {
        Obj *o = &g_factories[i];
        if (o->kind) continue;
        o->refs = -1;
        for (int j = 0; j < MAX_IFACES && f[i][j].iid; j++) { o->ifs[j].vt = f[i][j].vt; o->ifs[j].self = o; }
        o->kind = K_CHANNEL_FACTORY + i;
    }
}

static BOOL is_name(HSTRING s, const WCHAR *want)
{
    UINT32 n, i = 0;
    const WCHAR *c = WindowsGetStringRawBuffer(s, &n);
    for (; i < n && want[i]; i++) if (c[i] != want[i]) return FALSE;
    return i == n && !want[i];
}
/* twinapi.appcore.dll's DllGetActivationFactory, for these five classes */
HRESULT logging_factory(HSTRING cls, void **factory)
{
    factories_init();
    for (int i = 0; i < 5; i++)
        if (is_name(cls, g_names[K_CHANNEL_FACTORY + i])) { *factory = &g_factories[i].ifs[0]; return S_OK; }
    return CLASS_E_CLASSNOTAVAILABLE;
}
