/*
 * strmap.c — a Windows Runtime map with string keys (strmap.h): the
 * Windows Runtime's StringMap (string values) and PropertySet (object
 * values).  One object answers IObservableMap<String, V>, IMap<String, V>,
 * IMapView<String, V>, IIterable<IKeyValuePair<String, V>> and, a property
 * set, IPropertySet; its iterators walk a copy of its pairs.  Keys are kept
 * in insertion order (Windows' are hashed: an app must not depend on
 * either).  MapChanged is raised for each insert, change, removal and
 * clear, with the key.
 */
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include "strmap.h"

static const GUID IID_IUnknown_ = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_ = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_ = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IPropertySet_ = { 0x8A43ED9F, 0xF4E6, 0x4421, { 0xAC, 0xF9, 0x1D, 0xAB, 0x29, 0x86, 0x82, 0x0C } };
/* the parameterized interfaces' IDs (tools/winrt/abigen.py's generic_iid),
 * [0] with string values, [1] with objects */
static const GUID IID_IMap_[2] = {
    { 0xF6D1F700, 0x49C2, 0x52AE, { 0x81, 0x54, 0x82, 0x6F, 0x99, 0x08, 0x77, 0x3C } },
    { 0x1B0D3570, 0x0877, 0x5EC2, { 0x8A, 0x2C, 0x3B, 0x95, 0x39, 0x50, 0x6A, 0xCA } } };
static const GUID IID_IMapView_[2] = {
    { 0xAC7F26F2, 0xFEB7, 0x5B2A, { 0x8A, 0xC4, 0x34, 0x5B, 0xC6, 0x2C, 0xAE, 0xDE } },
    { 0xBB78502A, 0xF79D, 0x54FA, { 0x92, 0xC9, 0x90, 0xC5, 0x03, 0x9F, 0xDF, 0x7E } } };
static const GUID IID_IObservableMap_[2] = {
    { 0x1E036276, 0x2F60, 0x55F6, { 0xB7, 0xF3, 0xF8, 0x60, 0x79, 0xE6, 0x90, 0x0B } },
    { 0x236AAC9D, 0xFB12, 0x5C4D, { 0xA4, 0x1C, 0x9E, 0x44, 0x5F, 0xB4, 0xD7, 0xEC } } };
static const GUID IID_IKeyValuePair_[2] = {
    { 0x60310303, 0x49C5, 0x52E6, { 0xAB, 0xC6, 0xA9, 0xB3, 0x6E, 0xCC, 0xC7, 0x16 } },
    { 0x09335560, 0x6C6B, 0x5A26, { 0x93, 0x48, 0x97, 0xB7, 0x81, 0x13, 0x2B, 0x20 } } };
static const GUID IID_IIterable_[2] = {
    { 0xE9BDAAF0, 0xCBF6, 0x5C72, { 0xBE, 0x90, 0x29, 0xCB, 0xF3, 0xA1, 0x31, 0x9B } },
    { 0xFE2F3D47, 0x5D47, 0x5499, { 0x83, 0x74, 0x43, 0x0C, 0x7C, 0xDA, 0x02, 0x04 } } };
static const GUID IID_IIterator_[2] = {
    { 0x05EB86F1, 0x7140, 0x5517, { 0xB8, 0x8D, 0xCB, 0xAE, 0xBE, 0x57, 0xE6, 0xB1 } },
    { 0x5DB5FA32, 0x707C, 0x5849, { 0xA0, 0x6B, 0x91, 0xC8, 0xEB, 0x9D, 0x10, 0xE8 } } };
static const GUID IID_IMapChangedEventArgs_ = { 0x60141EFB, 0xF2F9, 0x5377, { 0x96, 0xFD, 0xF8, 0xC6, 0x0D, 0x95, 0x58, 0xB5 } };

#define E_BOUNDS_ ((HRESULT)0x8000000BL)
#define VT(p) (*(void ***)(p))
typedef BYTE boolean_t;

static void unk_addref(void *p) { if (p) ((ULONG (STDMETHODCALLTYPE *)(void *))VT(p)[1])(p); }
static void unk_release(void *p) { if (p) ((ULONG (STDMETHODCALLTYPE *)(void *))VT(p)[2])(p); }

typedef struct { HSTRING key; void *value; } Pair;
typedef struct { void *handler; INT64 token; } Handler;

typedef struct Map {
    const void *const *map;         /* IMap; also the object's IInspectable */
    const void *const *view;        /* IMapView */
    const void *const *obs;         /* IObservableMap */
    const void *const *iter;        /* IIterable<IKeyValuePair> */
    const void *const *pset;        /* IPropertySet (objects only) */
    LONG refs;
    int objects;
    SRWLOCK lock;
    Pair *pairs;
    UINT32 count, cap;
    Handler *handlers;
    UINT32 nhandlers;
    INT64 next_token;
} Map;
#define OF(p, f) ((Map *)((char *)(p) - __builtin_offsetof(Map, f)))

/* a value's copy (a string duplicated, an object referenced) and its end */
static void *val_copy(Map *m, void *v)
{
    if (m->objects) { unk_addref(v); return v; }
    HSTRING d = 0;
    WindowsDuplicateString((HSTRING)v, &d);
    return d;
}
static void val_free(Map *m, void *v)
{
    if (m->objects) unk_release(v);
    else WindowsDeleteString((HSTRING)v);
}

static int find(Map *m, HSTRING key)
{
    for (UINT32 i = 0; i < m->count; i++) {
        INT32 c = 1;
        WindowsCompareStringOrdinal(m->pairs[i].key, key, &c);
        if (!c) return (int)i;
    }
    return -1;
}

/* ---- the change event's arguments (IMapChangedEventArgs<String>) ---- */
typedef struct { const void *const *vt; LONG refs; int change; HSTRING key; } Change;
static HRESULT STDMETHODCALLTYPE ch_qi(Change *c, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IMapChangedEventArgs_)) {
        InterlockedIncrement(&c->refs);
        *out = c;
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ch_addref(Change *c) { return (ULONG)InterlockedIncrement(&c->refs); }
static ULONG STDMETHODCALLTYPE ch_release(Change *c)
{
    LONG r = InterlockedDecrement(&c->refs);
    if (!r) { WindowsDeleteString(c->key); HeapFree(GetProcessHeap(), 0, c); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE no_iids(void *p, ULONG *n, GUID **iids) { (void)p; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE ch_name(void *p, HSTRING *n) { (void)p; *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *p, int *t) { (void)p; *t = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE ch_change(Change *c, int *v) { if (!v) return E_POINTER; *v = c->change; return S_OK; }
static HRESULT STDMETHODCALLTYPE ch_key(Change *c, HSTRING *v) { if (!v) return E_POINTER; return WindowsDuplicateString(c->key, v); }
static const void *const change_vt[] = { (void *)ch_qi, (void *)ch_addref, (void *)ch_release, (void *)no_iids, (void *)ch_name,
                                         (void *)base_trust, (void *)ch_change, (void *)ch_key };

/* MapChanged to each handler (CollectionChange: Reset 0, ItemInserted 1,
 * ItemRemoved 2, ItemChanged 3), outside the map's lock */
static void raise(Map *m, int change, HSTRING key)
{
    AcquireSRWLockShared(&m->lock);
    UINT32 n = m->nhandlers;
    void **hs = n ? HeapAlloc(GetProcessHeap(), 0, n * sizeof(void *)) : 0;
    for (UINT32 i = 0; hs && i < n; i++) { hs[i] = m->handlers[i].handler; unk_addref(hs[i]); }
    ReleaseSRWLockShared(&m->lock);
    if (!hs) return;
    Change *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *c);
    if (c) {
        c->vt = change_vt;
        c->refs = 1;
        c->change = change;
        if (key) WindowsDuplicateString(key, &c->key);
        for (UINT32 i = 0; i < n; i++)
            ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *))VT(hs[i])[3])(hs[i], &m->obs, c);
        ch_release(c);
    }
    for (UINT32 i = 0; i < n; i++) unk_release(hs[i]);
    HeapFree(GetProcessHeap(), 0, hs);
}

/* ---- the map's identity ---- */
static HRESULT sm_qi(Map *m, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    int o = m->objects;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IMap_[o])) *out = &m->map;
    else if (IsEqualGUID(iid, &IID_IMapView_[o])) *out = &m->view;
    else if (IsEqualGUID(iid, &IID_IObservableMap_[o])) *out = &m->obs;
    else if (IsEqualGUID(iid, &IID_IIterable_[o])) *out = &m->iter;
    else if (o && IsEqualGUID(iid, &IID_IPropertySet_)) *out = &m->pset;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&m->refs);
    return S_OK;
}
static ULONG sm_release(Map *m)
{
    LONG r = InterlockedDecrement(&m->refs);
    if (!r) {
        for (UINT32 i = 0; i < m->count; i++) { WindowsDeleteString(m->pairs[i].key); val_free(m, m->pairs[i].value); }
        for (UINT32 i = 0; i < m->nhandlers; i++) unk_release(m->handlers[i].handler);
        HeapFree(GetProcessHeap(), 0, m->pairs);
        HeapFree(GetProcessHeap(), 0, m->handlers);
        HeapFree(GetProcessHeap(), 0, m);
    }
    return (ULONG)r;
}
static HRESULT sm_name(Map *m, HSTRING *n)
{
    const WCHAR *s = m->objects ? L"Windows.Foundation.Collections.PropertySet" : L"Windows.Foundation.Collections.StringMap";
    return WindowsCreateString(s, (UINT32)lstrlenW(s), n);
}
/* each face's IInspectable, by its field */
#define FACE_FNS(f) \
    static HRESULT STDMETHODCALLTYPE f##_qi(void *p, REFIID iid, void **o) { return sm_qi(OF(p, f), iid, o); } \
    static ULONG STDMETHODCALLTYPE f##_addref(void *p) { return (ULONG)InterlockedIncrement(&OF(p, f)->refs); } \
    static ULONG STDMETHODCALLTYPE f##_release(void *p) { return sm_release(OF(p, f)); } \
    static HRESULT STDMETHODCALLTYPE f##_name(void *p, HSTRING *n) { return sm_name(OF(p, f), n); }
FACE_FNS(map)
FACE_FNS(view)
FACE_FNS(obs)
FACE_FNS(iter)
FACE_FNS(pset)
#define FACE_BASE(f) (void *)f##_qi, (void *)f##_addref, (void *)f##_release, (void *)no_iids, (void *)f##_name, (void *)base_trust

/* ---- IMap / IMapView ---- */
static HRESULT lookup(Map *m, HSTRING key, void **v)
{
    if (!v) return E_POINTER;
    AcquireSRWLockShared(&m->lock);
    int i = find(m, key);
    *v = i < 0 ? 0 : val_copy(m, m->pairs[i].value);
    ReleaseSRWLockShared(&m->lock);
    return i < 0 ? E_BOUNDS_ : S_OK;
}
static HRESULT size(Map *m, UINT32 *v)
{
    if (!v) return E_POINTER;
    AcquireSRWLockShared(&m->lock);
    *v = m->count;
    ReleaseSRWLockShared(&m->lock);
    return S_OK;
}
static HRESULT has_key(Map *m, HSTRING key, boolean_t *v)
{
    if (!v) return E_POINTER;
    AcquireSRWLockShared(&m->lock);
    *v = find(m, key) >= 0;
    ReleaseSRWLockShared(&m->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE m_lookup(void *p, HSTRING k, void **v) { return lookup(OF(p, map), k, v); }
static HRESULT STDMETHODCALLTYPE m_size(void *p, UINT32 *v) { return size(OF(p, map), v); }
static HRESULT STDMETHODCALLTYPE m_has_key(void *p, HSTRING k, boolean_t *v) { return has_key(OF(p, map), k, v); }
static HRESULT STDMETHODCALLTYPE m_view(void *p, void **v)
{
    Map *m = OF(p, map);
    if (!v) return E_POINTER;
    InterlockedIncrement(&m->refs);
    *v = &m->view;
    return S_OK;
}
/* @key set to @value (copied); TRUE in @replaced when it had one */
static HRESULT put(Map *m, HSTRING key, void *value, boolean_t *replaced)
{
    AcquireSRWLockExclusive(&m->lock);
    int i = find(m, key);
    HRESULT hr = S_OK;
    if (i >= 0) {
        val_free(m, m->pairs[i].value);
        m->pairs[i].value = val_copy(m, value);
    } else {
        if (m->count == m->cap) {
            UINT32 cap = m->cap ? m->cap * 2 : 8;
            Pair *n = m->pairs ? HeapReAlloc(GetProcessHeap(), 0, m->pairs, cap * sizeof(Pair))
                               : HeapAlloc(GetProcessHeap(), 0, cap * sizeof(Pair));
            if (!n) hr = E_OUTOFMEMORY;
            else { m->pairs = n; m->cap = cap; }
        }
        if (SUCCEEDED(hr)) {
            WindowsDuplicateString(key, &m->pairs[m->count].key);
            m->pairs[m->count++].value = val_copy(m, value);
        }
    }
    ReleaseSRWLockExclusive(&m->lock);
    if (replaced) *replaced = i >= 0;
    return hr;
}
static HRESULT STDMETHODCALLTYPE m_insert(void *p, HSTRING k, void *v, boolean_t *replaced)
{
    Map *m = OF(p, map);
    boolean_t r = 0;
    HRESULT hr = put(m, k, v, &r);
    if (replaced) *replaced = r;
    if (SUCCEEDED(hr)) raise(m, r ? 3 : 1, k);
    return hr;
}
static HRESULT STDMETHODCALLTYPE m_remove(void *p, HSTRING k)
{
    Map *m = OF(p, map);
    AcquireSRWLockExclusive(&m->lock);
    int i = find(m, k);
    if (i >= 0) {
        WindowsDeleteString(m->pairs[i].key);
        val_free(m, m->pairs[i].value);
        for (UINT32 j = (UINT32)i + 1; j < m->count; j++) m->pairs[j - 1] = m->pairs[j];
        m->count--;
    }
    ReleaseSRWLockExclusive(&m->lock);
    if (i < 0) return E_BOUNDS_;
    raise(m, 2, k);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE m_clear(void *p)
{
    Map *m = OF(p, map);
    AcquireSRWLockExclusive(&m->lock);
    for (UINT32 i = 0; i < m->count; i++) { WindowsDeleteString(m->pairs[i].key); val_free(m, m->pairs[i].value); }
    m->count = 0;
    ReleaseSRWLockExclusive(&m->lock);
    raise(m, 0, 0);
    return S_OK;
}
static const void *const map_vt[] = { FACE_BASE(map), (void *)m_lookup, (void *)m_size, (void *)m_has_key, (void *)m_view,
                                      (void *)m_insert, (void *)m_remove, (void *)m_clear };

static HRESULT STDMETHODCALLTYPE v_lookup(void *p, HSTRING k, void **v) { return lookup(OF(p, view), k, v); }
static HRESULT STDMETHODCALLTYPE v_size(void *p, UINT32 *v) { return size(OF(p, view), v); }
static HRESULT STDMETHODCALLTYPE v_has_key(void *p, HSTRING k, boolean_t *v) { return has_key(OF(p, view), k, v); }
static HRESULT STDMETHODCALLTYPE v_split(void *p, void **a, void **b) { (void)p; if (a) *a = 0; if (b) *b = 0; return S_OK; }
static const void *const view_vt[] = { FACE_BASE(view), (void *)v_lookup, (void *)v_size, (void *)v_has_key, (void *)v_split };

/* ---- IObservableMap ---- */
static HRESULT STDMETHODCALLTYPE o_add(void *p, void *handler, EventRegistrationToken *token)
{
    Map *m = OF(p, obs);
    if (!handler || !token) return E_INVALIDARG;
    AcquireSRWLockExclusive(&m->lock);
    Handler *n = m->handlers ? HeapReAlloc(GetProcessHeap(), 0, m->handlers, (m->nhandlers + 1) * sizeof(Handler))
                             : HeapAlloc(GetProcessHeap(), 0, sizeof(Handler));
    if (n) {
        m->handlers = n;
        unk_addref(handler);
        n[m->nhandlers].handler = handler;
        n[m->nhandlers++].token = token->value = ++m->next_token;
    }
    ReleaseSRWLockExclusive(&m->lock);
    return n ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE o_remove(void *p, EventRegistrationToken token)
{
    Map *m = OF(p, obs);
    void *gone = 0;
    AcquireSRWLockExclusive(&m->lock);
    for (UINT32 i = 0; i < m->nhandlers && !gone; i++)
        if (m->handlers[i].token == token.value) {
            gone = m->handlers[i].handler;
            m->handlers[i] = m->handlers[--m->nhandlers];
        }
    ReleaseSRWLockExclusive(&m->lock);
    unk_release(gone);
    return S_OK;
}
static const void *const obs_vt[] = { FACE_BASE(obs), (void *)o_add, (void *)o_remove };
static const void *const pset_vt[] = { FACE_BASE(pset) };

/* ---- the pairs (IKeyValuePair) and their iterator ---- */
typedef struct { const void *const *vt; LONG refs; Map *m; HSTRING key; void *value; } Kvp;
static HRESULT STDMETHODCALLTYPE kv_qi(Kvp *k, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IKeyValuePair_[k->m->objects])) {
        InterlockedIncrement(&k->refs);
        *out = k;
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE kv_addref(Kvp *k) { return (ULONG)InterlockedIncrement(&k->refs); }
static ULONG STDMETHODCALLTYPE kv_release(Kvp *k)
{
    LONG r = InterlockedDecrement(&k->refs);
    if (!r) {
        WindowsDeleteString(k->key);
        val_free(k->m, k->value);
        sm_release(k->m);
        HeapFree(GetProcessHeap(), 0, k);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE kv_key(Kvp *k, HSTRING *v) { if (!v) return E_POINTER; return WindowsDuplicateString(k->key, v); }
static HRESULT STDMETHODCALLTYPE kv_value(Kvp *k, void **v) { if (!v) return E_POINTER; *v = val_copy(k->m, k->value); return S_OK; }
static const void *const kvp_vt[] = { (void *)kv_qi, (void *)kv_addref, (void *)kv_release, (void *)no_iids, (void *)ch_name,
                                      (void *)base_trust, (void *)kv_key, (void *)kv_value };

typedef struct { const void *const *vt; LONG refs; Map *m; Kvp **items; UINT32 n, at; } Iter;
static HRESULT STDMETHODCALLTYPE it_qi(Iter *t, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IIterator_[t->m->objects])) {
        InterlockedIncrement(&t->refs);
        *out = t;
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE it_addref(Iter *t) { return (ULONG)InterlockedIncrement(&t->refs); }
static ULONG STDMETHODCALLTYPE it_release(Iter *t)
{
    LONG r = InterlockedDecrement(&t->refs);
    if (!r) {
        for (UINT32 i = 0; i < t->n; i++) kv_release(t->items[i]);
        HeapFree(GetProcessHeap(), 0, t->items);
        sm_release(t->m);
        HeapFree(GetProcessHeap(), 0, t);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE it_current(Iter *t, void **v)
{
    if (!v) return E_POINTER;
    if (t->at >= t->n) { *v = 0; return E_BOUNDS_; }
    kv_addref(t->items[t->at]);
    *v = t->items[t->at];
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE it_has(Iter *t, boolean_t *v) { if (!v) return E_POINTER; *v = t->at < t->n; return S_OK; }
static HRESULT STDMETHODCALLTYPE it_next(Iter *t, boolean_t *v)
{
    if (t->at < t->n) t->at++;
    if (v) *v = t->at < t->n;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE it_many(Iter *t, UINT32 cap, void **items, UINT32 *got)
{
    UINT32 n = 0;
    while (n < cap && t->at < t->n) { kv_addref(t->items[t->at]); items[n++] = t->items[t->at++]; }
    if (got) *got = n;
    return S_OK;
}
static const void *const iter_obj_vt[] = { (void *)it_qi, (void *)it_addref, (void *)it_release, (void *)no_iids, (void *)ch_name,
                                           (void *)base_trust, (void *)it_current, (void *)it_has, (void *)it_next,
                                           (void *)it_many };

static HRESULT STDMETHODCALLTYPE i_first(void *p, void **out)
{
    Map *m = OF(p, iter);
    if (!out) return E_POINTER;
    *out = 0;
    Iter *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *t);
    if (!t) return E_OUTOFMEMORY;
    t->vt = iter_obj_vt;
    t->refs = 1;
    t->m = m;
    InterlockedIncrement(&m->refs);
    AcquireSRWLockShared(&m->lock);
    t->items = m->count ? HeapAlloc(GetProcessHeap(), 0, m->count * sizeof(Kvp *)) : 0;
    for (UINT32 i = 0; t->items && i < m->count; i++) {
        Kvp *k = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *k);
        if (!k) break;
        k->vt = kvp_vt;
        k->refs = 1;
        k->m = m;
        InterlockedIncrement(&m->refs);
        WindowsDuplicateString(m->pairs[i].key, &k->key);
        k->value = val_copy(m, m->pairs[i].value);
        t->items[t->n++] = k;
    }
    ReleaseSRWLockShared(&m->lock);
    *out = t;
    return S_OK;
}
static const void *const iter_vt[] = { FACE_BASE(iter), (void *)i_first };

HRESULT nova_strmap_new(BOOL objects, void **out)
{
    if (!out) return E_POINTER;
    Map *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *m);
    *out = m;
    if (!m) return E_OUTOFMEMORY;
    m->map = map_vt;
    m->view = view_vt;
    m->obs = obs_vt;
    m->iter = iter_vt;
    m->pset = pset_vt;
    m->refs = 1;
    m->objects = objects ? 1 : 0;
    InitializeSRWLock(&m->lock);
    return S_OK;
}

HRESULT nova_strmap_put(void *map, const WCHAR *key, const WCHAR *value)
{
    Map *m = OF(map, map);
    HSTRING k = 0, v = 0;
    HRESULT hr = WindowsCreateString(key, (UINT32)lstrlenW(key), &k);
    if (SUCCEEDED(hr)) hr = WindowsCreateString(value, (UINT32)lstrlenW(value), &v);
    if (SUCCEEDED(hr)) hr = put(m, k, v, 0);
    WindowsDeleteString(k);
    WindowsDeleteString(v);
    return hr;
}
