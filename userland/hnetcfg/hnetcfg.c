/*
 * hnetcfg.dll — the Windows Firewall's COM API: the NetFwPolicy2 class
 * (INetFwPolicy2: each profile's state, its default actions, the rules)
 * and the NetFwRule class (INetFwRule3), which installers and programs use
 * to open a port or allow themselves through the firewall.
 *
 * The firewall itself is in NovaOS's kernel (kernel/net/firewall.c) and
 * keeps its profiles and rules where Windows keeps them, under
 * HKLM\SYSTEM\CurrentControlSet\Services\SharedAccess\Parameters\
 * FirewallPolicy; this DLL asks it through NtNovaSockCtl op 23
 * (userland/include/novafw.h).  A rule taken from INetFwRules::Item or the
 * enumerator is live, as on Windows: a property put changes the rule in
 * force.  A new NetFwRule is only a description until INetFwRules::Add.
 * Changes need an administrator (E_ACCESSDENIED).
 *
 * Not here yet: IDispatch (no type library, so scripts that bind late
 * cannot use these objects), INetFwServiceRestriction, the per-profile
 * excluded interfaces, ICMP types and codes, the legacy INetFwMgr API and
 * Windows Firewall with Advanced Security's IPsec objects.
 */
#include <windows.h>
#include <oleauto.h>
#include <netfw.h>
#include <winternl.h>
#include "../include/novafw.h"

#define FWAPI __declspec(dllexport)

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

static volatile LONG g_objects, g_locks;

static void *zalloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void zfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

/* ---------------------------------------------------------------------------
 * The kernel's firewall
 * ------------------------------------------------------------------------- */
/* An HRESULT (the kernel's are 32-bit values, zero-extended) or a count */
static HRESULT fw(UINT32 op, UINT32 profile, void *req, LONG *result)
{
    UINT64 r = (UINT64)NtNovaSockCtl(0, NOVAFW_FW, (ULONG_PTR)(op | (profile << 16)), req);
    if (r >= 0x80000000u && r <= 0xFFFFFFFFu) {
        if (r == NOVAWFP_E_NOT_FOUND) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (r == NOVAWFP_E_INVALID_PARAMETER) return E_INVALIDARG;
        return (HRESULT)(UINT32)r;
    }
    if (result) *result = (LONG)r;
    return S_OK;
}

static BOOL one_profile(NET_FW_PROFILE_TYPE2 p)
{
    return p == NET_FW_PROFILE2_DOMAIN || p == NET_FW_PROFILE2_PRIVATE || p == NET_FW_PROFILE2_PUBLIC;
}

static HRESULT profile_get(NET_FW_PROFILE_TYPE2 p, NovaFwProfile *out)
{
    if (!one_profile(p)) return E_INVALIDARG;
    memset(out, 0, sizeof(*out));
    out->profile = p;
    return fw(NOVAFWC_PROFILE_GET, 0, out, NULL);
}

/* A put names one profile or several (NET_FW_PROFILE2_ALL) */
enum { P_ENABLED, P_BLOCK_ALL, P_NOTIFY_OFF, P_UNICAST_OFF, P_DEFAULT_IN, P_DEFAULT_OUT };
static HRESULT profile_put(NET_FW_PROFILE_TYPE2 mask, int what, int value)
{
    if (!(mask & 7)) return E_INVALIDARG;
    for (unsigned bit = 1; bit <= 4; bit <<= 1) {
        if (!(mask & bit)) continue;
        NovaFwProfile p;
        HRESULT hr = profile_get((NET_FW_PROFILE_TYPE2)bit, &p);
        if (FAILED(hr)) return hr;
        int *f = what == P_ENABLED ? &p.enabled : what == P_BLOCK_ALL ? &p.block_all :
                 what == P_NOTIFY_OFF ? &p.notify_off : what == P_UNICAST_OFF ? &p.unicast_off :
                 what == P_DEFAULT_IN ? &p.default_in : &p.default_out;
        *f = value;
        if (FAILED(hr = fw(NOVAFWC_PROFILE_SET, 0, &p, NULL))) return hr;
    }
    return S_OK;
}

/* ---------------------------------------------------------------------------
 * Text: the kernel's rules are UTF-8, the API's BSTRs UTF-16
 * ------------------------------------------------------------------------- */
static void to_utf8(const WCHAR *w, char *out, int cap)
{
    int n = 0;
    if (w) for (; *w && n < cap - 1; w++) {
        UINT32 c = *w;
        if (c >= 0xD800 && c < 0xDC00 && w[1] >= 0xDC00 && w[1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (w[1] - 0xDC00); w++; }
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { if (n + 2 >= cap) break; out[n++] = (char)(0xC0 | c >> 6); out[n++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { if (n + 3 >= cap) break; out[n++] = (char)(0xE0 | c >> 12); out[n++] = (char)(0x80 | (c >> 6 & 63)); out[n++] = (char)(0x80 | (c & 63)); }
        else { if (n + 4 >= cap) break; out[n++] = (char)(0xF0 | c >> 18); out[n++] = (char)(0x80 | (c >> 12 & 63)); out[n++] = (char)(0x80 | (c >> 6 & 63)); out[n++] = (char)(0x80 | (c & 63)); }
    }
    out[n] = 0;
}

static HRESULT to_bstr(const char *s, BSTR *out)
{
    if (!out) return E_POINTER;
    WCHAR w[300];
    int n = 0;
    while (*s && n < 298) {
        UINT32 c = (unsigned char)*s++;
        if (c >= 0xF0 && s[0] && s[1] && s[2]) { c = (c & 7) << 18 | (s[0] & 63) << 12 | (s[1] & 63) << 6 | (s[2] & 63); s += 3; }
        else if (c >= 0xE0 && s[0] && s[1]) { c = (c & 15) << 12 | (s[0] & 63) << 6 | (s[1] & 63); s += 2; }
        else if (c >= 0xC0 && s[0]) { c = (c & 31) << 6 | (s[0] & 63); s++; }
        if (c >= 0x10000) { c -= 0x10000; w[n++] = (WCHAR)(0xD800 + (c >> 10)); w[n++] = (WCHAR)(0xDC00 + (c & 0x3FF)); }
        else w[n++] = (WCHAR)c;
    }
    w[n] = 0;
    *out = n ? SysAllocString(w) : NULL;      /* an empty property reads as a null BSTR, as on Windows */
    return n && !*out ? E_OUTOFMEMORY : S_OK;
}

static int eq_nocase(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}

/* ---------------------------------------------------------------------------
 * NetFwRule: INetFwRule3
 * ------------------------------------------------------------------------- */
typedef struct {
    INetFwRule3 iface;
    LONG refs;
    BOOL live;                    /* in the firewall: a put is applied at once */
    NovaFwRule r;
} Rule;

static const INetFwRule3Vtbl g_rule_vtbl;

static Rule *rule_new(const NovaFwRule *from, BOOL live)
{
    Rule *o = zalloc(sizeof(*o));
    if (!o) return NULL;
    o->iface.lpVtbl = (INetFwRule3Vtbl *)&g_rule_vtbl;
    o->refs = 1;
    o->live = live;
    if (from) o->r = *from;
    else { o->r.enabled = 0; o->r.dir = NET_FW_RULE_DIR_IN; o->r.action = NET_FW_ACTION_ALLOW;
           o->r.protocol = NET_FW_IP_PROTOCOL_ANY; o->r.profiles = NOVAFW_ALL; }
    InterlockedIncrement(&g_objects);
    return o;
}

#define RULE(p) ((Rule *)(p))

/* A live rule's change goes to the firewall; a refused one is undone */
static HRESULT rule_changed(Rule *o, const NovaFwRule *before)
{
    if (!o->live) return S_OK;
    NovaFwRule r = o->r;
    HRESULT hr = fw(NOVAFWC_RULE_SET, 0, &r, NULL);
    if (FAILED(hr)) o->r = *before;
    else o->r = r;
    return hr;
}

static HRESULT STDMETHODCALLTYPE rule_qi(INetFwRule3 *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_INetFwRule) ||
        IsEqualIID(riid, &IID_INetFwRule2) || IsEqualIID(riid, &IID_INetFwRule3)) {
        *ppv = t;
        InterlockedIncrement(&RULE(t)->refs);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE rule_addref(INetFwRule3 *t) { return (ULONG)InterlockedIncrement(&RULE(t)->refs); }
static ULONG STDMETHODCALLTYPE rule_release(INetFwRule3 *t)
{
    LONG n = InterlockedDecrement(&RULE(t)->refs);
    if (!n) { zfree(t); InterlockedDecrement(&g_objects); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE disp_count(void *t, UINT *n) { (void)t; if (n) *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE disp_info(void *t, UINT i, LCID l, ITypeInfo **out) { (void)t; (void)i; (void)l; if (out) *out = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE disp_ids(void *t, REFIID r, LPOLESTR *names, UINT n, LCID l, DISPID *ids)
{ (void)t; (void)r; (void)names; (void)n; (void)l; (void)ids; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE disp_invoke(void *t, DISPID id, REFIID r, LCID l, WORD f, DISPPARAMS *p, VARIANT *res, EXCEPINFO *ei, UINT *ae)
{ (void)t; (void)id; (void)r; (void)l; (void)f; (void)p; (void)res; (void)ei; (void)ae; return E_NOTIMPL; }

/* The text properties */
#define TEXT_PROP(get, put, field) \
static HRESULT STDMETHODCALLTYPE get(INetFwRule3 *t, BSTR *out) { return to_bstr(RULE(t)->r.field, out); } \
static HRESULT STDMETHODCALLTYPE put(INetFwRule3 *t, BSTR v) \
{ \
    Rule *o = RULE(t); \
    NovaFwRule before = o->r; \
    to_utf8(v, o->r.field, sizeof(o->r.field)); \
    return rule_changed(o, &before); \
}
TEXT_PROP(rule_get_name, rule_put_name, name)
TEXT_PROP(rule_get_desc, rule_put_desc, desc)
TEXT_PROP(rule_get_app, rule_put_app, app)
TEXT_PROP(rule_get_service, rule_put_service, service)
TEXT_PROP(rule_get_lports, rule_put_lports, lports)
TEXT_PROP(rule_get_rports, rule_put_rports, rports)
TEXT_PROP(rule_get_laddrs, rule_put_laddrs, laddrs)
TEXT_PROP(rule_get_raddrs, rule_put_raddrs, raddrs)
TEXT_PROP(rule_get_group, rule_put_group, group)

/* The number properties */
#define NUM_PROP(get, put, type, field, check) \
static HRESULT STDMETHODCALLTYPE get(INetFwRule3 *t, type *out) { if (!out) return E_POINTER; *out = (type)RULE(t)->r.field; return S_OK; } \
static HRESULT STDMETHODCALLTYPE put(INetFwRule3 *t, type v) \
{ \
    Rule *o = RULE(t); \
    if (!(check)) return E_INVALIDARG; \
    NovaFwRule before = o->r; \
    o->r.field = v; \
    return rule_changed(o, &before); \
}
NUM_PROP(rule_get_protocol, rule_put_protocol, LONG, protocol, v >= 0 && v <= 256)
NUM_PROP(rule_get_dir, rule_put_dir, NET_FW_RULE_DIRECTION, dir, v == NET_FW_RULE_DIR_IN || v == NET_FW_RULE_DIR_OUT)
NUM_PROP(rule_get_profiles, rule_put_profiles, LONG, profiles, (v & 7) != 0)
NUM_PROP(rule_get_action, rule_put_action, NET_FW_ACTION, action, v == NET_FW_ACTION_BLOCK || v == NET_FW_ACTION_ALLOW)

#define BOOL_PROP(get, put, field) \
static HRESULT STDMETHODCALLTYPE get(INetFwRule3 *t, VARIANT_BOOL *out) { if (!out) return E_POINTER; *out = RULE(t)->r.field ? VARIANT_TRUE : VARIANT_FALSE; return S_OK; } \
static HRESULT STDMETHODCALLTYPE put(INetFwRule3 *t, VARIANT_BOOL v) \
{ \
    Rule *o = RULE(t); \
    NovaFwRule before = o->r; \
    o->r.field = v != VARIANT_FALSE; \
    return rule_changed(o, &before); \
}
BOOL_PROP(rule_get_enabled, rule_put_enabled, enabled)
BOOL_PROP(rule_get_edge, rule_put_edge, edge)

static HRESULT STDMETHODCALLTYPE rule_get_icmp(INetFwRule3 *t, BSTR *out) { (void)t; if (!out) return E_POINTER; *out = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE rule_put_icmp(INetFwRule3 *t, BSTR v) { (void)t; return v && *v ? E_NOTIMPL : S_OK; }
static HRESULT STDMETHODCALLTYPE rule_get_ifs(INetFwRule3 *t, VARIANT *out) { (void)t; if (!out) return E_POINTER; VariantInit(out); return S_OK; }
static HRESULT STDMETHODCALLTYPE rule_put_ifs(INetFwRule3 *t, VARIANT v) { (void)t; return v.vt == VT_EMPTY || v.vt == VT_NULL ? S_OK : E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE rule_get_iftypes(INetFwRule3 *t, BSTR *out) { (void)t; if (!out) return E_POINTER; *out = SysAllocString(L"All"); return S_OK; }
static HRESULT STDMETHODCALLTYPE rule_put_iftypes(INetFwRule3 *t, BSTR v)
{
    (void)t;
    char s[16];
    to_utf8(v, s, sizeof(s));
    return !*s || eq_nocase(s, "All") ? S_OK : E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE rule_get_edgeopts(INetFwRule3 *t, LONG *out) { if (!out) return E_POINTER; *out = RULE(t)->r.edge ? 1 : 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE rule_put_edgeopts(INetFwRule3 *t, LONG v) { return rule_put_edge(t, v ? VARIANT_TRUE : VARIANT_FALSE); }
/* INetFwRule3's package, user and IPsec-authorization properties: empty
 * (rules for everyone, no connection security) */
static HRESULT STDMETHODCALLTYPE rule_get_empty(INetFwRule3 *t, BSTR *out) { (void)t; if (!out) return E_POINTER; *out = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE rule_put_empty(INetFwRule3 *t, BSTR v) { (void)t; return v && *v ? E_NOTIMPL : S_OK; }
static HRESULT STDMETHODCALLTYPE rule_get_secure(INetFwRule3 *t, LONG *out) { (void)t; if (!out) return E_POINTER; *out = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE rule_put_secure(INetFwRule3 *t, LONG v) { (void)t; return v ? E_NOTIMPL : S_OK; }

static const INetFwRule3Vtbl g_rule_vtbl = {
    rule_qi, rule_addref, rule_release,
    (void *)disp_count, (void *)disp_info, (void *)disp_ids, (void *)disp_invoke,
    rule_get_name, rule_put_name, rule_get_desc, rule_put_desc, rule_get_app, rule_put_app,
    rule_get_service, rule_put_service, rule_get_protocol, rule_put_protocol,
    rule_get_lports, rule_put_lports, rule_get_rports, rule_put_rports,
    rule_get_laddrs, rule_put_laddrs, rule_get_raddrs, rule_put_raddrs,
    rule_get_icmp, rule_put_icmp, rule_get_dir, rule_put_dir, rule_get_ifs, rule_put_ifs,
    rule_get_iftypes, rule_put_iftypes, rule_get_enabled, rule_put_enabled,
    rule_get_group, rule_put_group, rule_get_profiles, rule_put_profiles,
    rule_get_edge, rule_put_edge, rule_get_action, rule_put_action,
    rule_get_edgeopts, rule_put_edgeopts,
    rule_get_empty, rule_put_empty, rule_get_empty, rule_put_empty, rule_get_empty, rule_put_empty,
    rule_get_empty, rule_put_empty, rule_get_empty, rule_put_empty, rule_get_secure, rule_put_secure,
};

/* ---------------------------------------------------------------------------
 * INetFwRules and its enumerator
 * ------------------------------------------------------------------------- */
typedef struct { INetFwRules iface; LONG refs; } Rules;
typedef struct { IEnumVARIANT iface; LONG refs; LONG next; } RuleEnum;

static const INetFwRulesVtbl g_rules_vtbl;
static const IEnumVARIANTVtbl g_enum_vtbl;

/* The firewall's rule at an index (S_FALSE past the end) */
static HRESULT rule_at(LONG i, NovaFwRule *r)
{
    memset(r, 0, sizeof(*r));
    r->index = i;
    HRESULT hr = fw(NOVAFWC_RULE_AT, 0, r, NULL);
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? S_FALSE : hr;
}

/* The first rule with this name */
static HRESULT rule_named(BSTR name, NovaFwRule *r)
{
    char want[NOVAFW_TEXT];
    to_utf8(name, want, sizeof(want));
    for (LONG i = 0;; i++) {
        HRESULT hr = rule_at(i, r);
        if (hr != S_OK) return hr == S_FALSE ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : hr;
        if (eq_nocase(r->name, want)) return S_OK;
    }
}

static HRESULT STDMETHODCALLTYPE rules_qi(INetFwRules *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_INetFwRules)) {
        *ppv = t;
        InterlockedIncrement(&((Rules *)t)->refs);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE rules_addref(INetFwRules *t) { return (ULONG)InterlockedIncrement(&((Rules *)t)->refs); }
static ULONG STDMETHODCALLTYPE rules_release(INetFwRules *t)
{
    LONG n = InterlockedDecrement(&((Rules *)t)->refs);
    if (!n) { zfree(t); InterlockedDecrement(&g_objects); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE rules_count(INetFwRules *t, LONG *out)
{
    (void)t;
    if (!out) return E_POINTER;
    *out = 0;
    return fw(NOVAFWC_RULE_COUNT, 0, NULL, out);
}
static HRESULT STDMETHODCALLTYPE rules_add(INetFwRules *t, INetFwRule *rule)
{
    (void)t;
    if (!rule) return E_POINTER;
    if (rule->lpVtbl != (void *)&g_rule_vtbl) return E_INVALIDARG;     /* someone else's object */
    Rule *o = RULE(rule);
    if (o->live) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (!o->r.name[0]) return E_INVALIDARG;
    NovaFwRule r = o->r;
    HRESULT hr = fw(NOVAFWC_RULE_ADD, 0, &r, NULL);
    if (FAILED(hr)) return hr;
    o->r = r;
    o->live = TRUE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE rules_remove(INetFwRules *t, BSTR name)
{
    (void)t;
    NovaFwRule r;
    HRESULT hr = rule_named(name, &r);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return S_OK;       /* Windows: nothing to remove is not an error */
    return FAILED(hr) ? hr : fw(NOVAFWC_RULE_DELETE, 0, &r, NULL);
}
static HRESULT STDMETHODCALLTYPE rules_item(INetFwRules *t, BSTR name, INetFwRule **out)
{
    (void)t;
    if (!out) return E_POINTER;
    *out = NULL;
    NovaFwRule r;
    HRESULT hr = rule_named(name, &r);
    if (FAILED(hr)) return hr;
    Rule *o = rule_new(&r, TRUE);
    if (!o) return E_OUTOFMEMORY;
    *out = (INetFwRule *)&o->iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE rules_enum(INetFwRules *t, IUnknown **out)
{
    (void)t;
    if (!out) return E_POINTER;
    RuleEnum *e = zalloc(sizeof(*e));
    if (!e) return E_OUTOFMEMORY;
    e->iface.lpVtbl = (IEnumVARIANTVtbl *)&g_enum_vtbl;
    e->refs = 1;
    InterlockedIncrement(&g_objects);
    *out = (IUnknown *)&e->iface;
    return S_OK;
}

static const INetFwRulesVtbl g_rules_vtbl = {
    rules_qi, rules_addref, rules_release,
    (void *)disp_count, (void *)disp_info, (void *)disp_ids, (void *)disp_invoke,
    rules_count, rules_add, rules_remove, rules_item, rules_enum,
};

static HRESULT STDMETHODCALLTYPE enum_qi(IEnumVARIANT *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumVARIANT)) {
        *ppv = t;
        InterlockedIncrement(&((RuleEnum *)t)->refs);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE enum_addref(IEnumVARIANT *t) { return (ULONG)InterlockedIncrement(&((RuleEnum *)t)->refs); }
static ULONG STDMETHODCALLTYPE enum_release(IEnumVARIANT *t)
{
    LONG n = InterlockedDecrement(&((RuleEnum *)t)->refs);
    if (!n) { zfree(t); InterlockedDecrement(&g_objects); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE enum_next(IEnumVARIANT *t, ULONG n, VARIANT *out, ULONG *got)
{
    RuleEnum *e = (RuleEnum *)t;
    ULONG k = 0;
    if (!out) return E_POINTER;
    while (k < n) {
        NovaFwRule r;
        HRESULT hr = rule_at(e->next, &r);
        if (FAILED(hr)) { while (k) VariantClear(&out[--k]); return hr; }
        if (hr == S_FALSE) break;
        Rule *o = rule_new(&r, TRUE);
        if (!o) { while (k) VariantClear(&out[--k]); return E_OUTOFMEMORY; }
        VariantInit(&out[k]);
        out[k].vt = VT_DISPATCH;
        out[k].pdispVal = (IDispatch *)&o->iface;
        k++;
        e->next++;
    }
    if (got) *got = k;
    return k == n ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE enum_skip(IEnumVARIANT *t, ULONG n) { ((RuleEnum *)t)->next += (LONG)n; return S_OK; }
static HRESULT STDMETHODCALLTYPE enum_reset(IEnumVARIANT *t) { ((RuleEnum *)t)->next = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE enum_clone(IEnumVARIANT *t, IEnumVARIANT **out)
{
    if (!out) return E_POINTER;
    HRESULT hr = rules_enum(NULL, (IUnknown **)out);
    if (SUCCEEDED(hr)) ((RuleEnum *)*out)->next = ((RuleEnum *)t)->next;
    return hr;
}
static const IEnumVARIANTVtbl g_enum_vtbl = { enum_qi, enum_addref, enum_release, enum_next, enum_skip, enum_reset, enum_clone };

/* ---------------------------------------------------------------------------
 * NetFwPolicy2: INetFwPolicy2
 * ------------------------------------------------------------------------- */
typedef struct { INetFwPolicy2 iface; LONG refs; } Policy;

static HRESULT STDMETHODCALLTYPE pol_qi(INetFwPolicy2 *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_INetFwPolicy2)) {
        *ppv = t;
        InterlockedIncrement(&((Policy *)t)->refs);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pol_addref(INetFwPolicy2 *t) { return (ULONG)InterlockedIncrement(&((Policy *)t)->refs); }
static ULONG STDMETHODCALLTYPE pol_release(INetFwPolicy2 *t)
{
    LONG n = InterlockedDecrement(&((Policy *)t)->refs);
    if (!n) { zfree(t); InterlockedDecrement(&g_objects); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE pol_current(INetFwPolicy2 *t, LONG *out)
{
    (void)t;
    if (!out) return E_POINTER;
    *out = 0;
    return fw(NOVAFWC_CURRENT, 0, NULL, out);
}

#define PROFILE_BOOL(get, put, field, what) \
static HRESULT STDMETHODCALLTYPE get(INetFwPolicy2 *t, NET_FW_PROFILE_TYPE2 p, VARIANT_BOOL *out) \
{ \
    (void)t; \
    if (!out) return E_POINTER; \
    NovaFwProfile f; \
    HRESULT hr = profile_get(p, &f); \
    *out = SUCCEEDED(hr) && f.field ? VARIANT_TRUE : VARIANT_FALSE; \
    return hr; \
} \
static HRESULT STDMETHODCALLTYPE put(INetFwPolicy2 *t, NET_FW_PROFILE_TYPE2 p, VARIANT_BOOL v) \
{ (void)t; return profile_put(p, what, v != VARIANT_FALSE); }
PROFILE_BOOL(pol_get_enabled, pol_put_enabled, enabled, P_ENABLED)
PROFILE_BOOL(pol_get_blockall, pol_put_blockall, block_all, P_BLOCK_ALL)
PROFILE_BOOL(pol_get_notify, pol_put_notify, notify_off, P_NOTIFY_OFF)
PROFILE_BOOL(pol_get_unicast, pol_put_unicast, unicast_off, P_UNICAST_OFF)

#define PROFILE_ACTION(get, put, field, what) \
static HRESULT STDMETHODCALLTYPE get(INetFwPolicy2 *t, NET_FW_PROFILE_TYPE2 p, NET_FW_ACTION *out) \
{ \
    (void)t; \
    if (!out) return E_POINTER; \
    NovaFwProfile f; \
    HRESULT hr = profile_get(p, &f); \
    *out = SUCCEEDED(hr) && f.field ? NET_FW_ACTION_ALLOW : NET_FW_ACTION_BLOCK; \
    return hr; \
} \
static HRESULT STDMETHODCALLTYPE put(INetFwPolicy2 *t, NET_FW_PROFILE_TYPE2 p, NET_FW_ACTION v) \
{ \
    (void)t; \
    if (v != NET_FW_ACTION_BLOCK && v != NET_FW_ACTION_ALLOW) return E_INVALIDARG; \
    return profile_put(p, what, v == NET_FW_ACTION_ALLOW); \
}
PROFILE_ACTION(pol_get_defin, pol_put_defin, default_in, P_DEFAULT_IN)
PROFILE_ACTION(pol_get_defout, pol_put_defout, default_out, P_DEFAULT_OUT)

static HRESULT STDMETHODCALLTYPE pol_get_excluded(INetFwPolicy2 *t, NET_FW_PROFILE_TYPE2 p, VARIANT *out)
{
    (void)t;
    if (!out) return E_POINTER;
    VariantInit(out);
    return one_profile(p) ? S_OK : E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE pol_put_excluded(INetFwPolicy2 *t, NET_FW_PROFILE_TYPE2 p, VARIANT v)
{
    (void)t; (void)p;
    return v.vt == VT_EMPTY || v.vt == VT_NULL ? S_OK : E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE pol_rules(INetFwPolicy2 *t, INetFwRules **out)
{
    (void)t;
    if (!out) return E_POINTER;
    Rules *r = zalloc(sizeof(*r));
    if (!r) { *out = NULL; return E_OUTOFMEMORY; }
    r->iface.lpVtbl = (INetFwRulesVtbl *)&g_rules_vtbl;
    r->refs = 1;
    InterlockedIncrement(&g_objects);
    *out = &r->iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pol_restriction(INetFwPolicy2 *t, INetFwServiceRestriction **out)
{
    (void)t;
    if (out) *out = NULL;
    return E_NOTIMPL;
}

/* Every rule of a group (matched by name, as Windows' "@resource" group
 * names too) that applies to one of these profiles */
static HRESULT group_walk(LONG profiles, BSTR group, int enable, VARIANT_BOOL *all_on)
{
    char want[NOVAFW_TEXT];
    to_utf8(group, want, sizeof(want));
    if (!want[0]) return E_INVALIDARG;
    BOOL any = FALSE, on = TRUE;
    for (LONG i = 0;; i++) {
        NovaFwRule r;
        HRESULT hr = rule_at(i, &r);
        if (FAILED(hr)) return hr;
        if (hr == S_FALSE) break;
        if (!eq_nocase(r.group, want) || !(r.profiles & (unsigned)profiles)) continue;
        any = TRUE;
        if (enable >= 0 && r.enabled != enable) {
            r.enabled = enable;
            if (FAILED(hr = fw(NOVAFWC_RULE_SET, 0, &r, NULL))) return hr;
        }
        if (!r.enabled) on = FALSE;
    }
    if (all_on) *all_on = any && on ? VARIANT_TRUE : VARIANT_FALSE;
    return any ? S_OK : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}
static HRESULT STDMETHODCALLTYPE pol_group_enable(INetFwPolicy2 *t, LONG profiles, BSTR group, VARIANT_BOOL enable)
{ (void)t; return group_walk(profiles, group, enable != VARIANT_FALSE, NULL); }
static HRESULT STDMETHODCALLTYPE pol_group_enabled(INetFwPolicy2 *t, LONG profiles, BSTR group, VARIANT_BOOL *out)
{
    (void)t;
    if (!out) return E_POINTER;
    HRESULT hr = group_walk(profiles, group, -1, out);
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? S_OK : hr;
}
static HRESULT STDMETHODCALLTYPE pol_restore(INetFwPolicy2 *t) { (void)t; return fw(NOVAFWC_RESTORE_DEFAULTS, 0, NULL, NULL); }
static HRESULT STDMETHODCALLTYPE pol_group_current(INetFwPolicy2 *t, BSTR group, VARIANT_BOOL *out)
{
    LONG cur = 0;
    HRESULT hr = pol_current(t, &cur);
    return FAILED(hr) ? hr : pol_group_enabled(t, cur, group, out);
}
static HRESULT STDMETHODCALLTYPE pol_modify_state(INetFwPolicy2 *t, NET_FW_MODIFY_STATE *out)
{
    (void)t;
    if (!out) return E_POINTER;
    LONG cur = 0;
    *out = NET_FW_MODIFY_STATE_OK;
    if (FAILED(pol_current(t, &cur))) return S_OK;
    for (unsigned bit = 1; bit <= 4; bit <<= 1) {
        NovaFwProfile f;
        if ((cur & bit) && SUCCEEDED(profile_get((NET_FW_PROFILE_TYPE2)bit, &f)) && f.enabled && f.block_all)
            *out = NET_FW_MODIFY_STATE_INBOUND_BLOCKED;
    }
    return S_OK;
}

static const INetFwPolicy2Vtbl g_policy_vtbl = {
    pol_qi, pol_addref, pol_release,
    (void *)disp_count, (void *)disp_info, (void *)disp_ids, (void *)disp_invoke,
    pol_current, pol_get_enabled, pol_put_enabled, pol_get_excluded, pol_put_excluded,
    pol_get_blockall, pol_put_blockall, pol_get_notify, pol_put_notify,
    pol_get_unicast, pol_put_unicast, pol_rules, pol_restriction,
    pol_group_enable, pol_group_enabled, pol_restore,
    pol_get_defin, pol_put_defin, pol_get_defout, pol_put_defout,
    pol_group_current, pol_modify_state,
};

/* ---------------------------------------------------------------------------
 * The class factories
 * ------------------------------------------------------------------------- */
typedef struct { IClassFactory iface; BOOL policy; } Factory;

static HRESULT STDMETHODCALLTYPE cf_qi(IClassFactory *cf, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory) ? cf : NULL;
    return *ppv ? S_OK : E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cf_addref(IClassFactory *cf)  { (void)cf; return 2; }
static ULONG STDMETHODCALLTYPE cf_release(IClassFactory *cf) { (void)cf; return 1; }
static HRESULT STDMETHODCALLTYPE cf_create(IClassFactory *cf, IUnknown *outer, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    IUnknown *u;
    if (((Factory *)cf)->policy) {
        Policy *p = zalloc(sizeof(*p));
        if (!p) return E_OUTOFMEMORY;
        p->iface.lpVtbl = (INetFwPolicy2Vtbl *)&g_policy_vtbl;
        p->refs = 1;
        InterlockedIncrement(&g_objects);
        u = (IUnknown *)&p->iface;
    } else {
        Rule *r = rule_new(NULL, FALSE);
        if (!r) return E_OUTOFMEMORY;
        u = (IUnknown *)&r->iface;
    }
    HRESULT hr = u->lpVtbl->QueryInterface(u, riid, ppv);
    u->lpVtbl->Release(u);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cf_lock(IClassFactory *cf, BOOL lock)
{
    (void)cf;
    if (lock) InterlockedIncrement(&g_locks); else InterlockedDecrement(&g_locks);
    return S_OK;
}
static const IClassFactoryVtbl g_cf_vtbl = { cf_qi, cf_addref, cf_release, cf_create, cf_lock };
static Factory g_policy_factory = { { (IClassFactoryVtbl *)&g_cf_vtbl }, TRUE };
static Factory g_rule_factory = { { (IClassFactoryVtbl *)&g_cf_vtbl }, FALSE };

FWAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    Factory *f = IsEqualCLSID(clsid, &CLSID_NetFwPolicy2) ? &g_policy_factory :
                 IsEqualCLSID(clsid, &CLSID_NetFwRule) ? &g_rule_factory : NULL;
    if (!f) return CLASS_E_CLASSNOTAVAILABLE;
    return cf_qi(&f->iface, riid, ppv);
}

FWAPI HRESULT WINAPI DllCanUnloadNow(void) { return g_objects || g_locks ? S_FALSE : S_OK; }
