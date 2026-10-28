/*
 * strvec.c — a read-only Windows Runtime list of strings: IVectorView<String>
 * and IIterable<String> (with its IIterator), as a ResourceContext's
 * Languages are.  The strings are copied when it is made.
 */
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include "strmap.h"

#define E_BOUNDS_ ((HRESULT)0x8000000BL)
static const GUID IID_IUnknown_ = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_ = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_ = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IVectorView_ = { 0x2F13C006, 0xA03A, 0x5F69, { 0xB0, 0x90, 0x75, 0xA4, 0x3E, 0x33, 0x42, 0x3E } };
static const GUID IID_IIterable_ = { 0xE2FCC7C1, 0x3BFC, 0x5A0B, { 0xB2, 0xB0, 0x72, 0xE7, 0x69, 0xD1, 0xCB, 0x7E } };
static const GUID IID_IIterator_ = { 0x8C304EBB, 0x6615, 0x50A4, { 0x88, 0x29, 0x87, 0x9E, 0xCD, 0x44, 0x32, 0x36 } };

typedef struct {
    const void *const *view, *const *iter;   // its two faces
    LONG refs;
    UINT32 count;
    HSTRING items[];
} Vec;
#define OF(p, f) ((Vec *)((char *)(p) - __builtin_offsetof(Vec, f)))

static HRESULT STDMETHODCALLTYPE no_iids(void *p, ULONG *n, GUID **iids) { (void)p; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *p, int *t) { (void)p; *t = 0; return S_OK; }

static HRESULT vec_qi(Vec *v, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IVectorView_)) *out = &v->view;
    else if (IsEqualGUID(iid, &IID_IIterable_)) *out = &v->iter;
    else { *out = 0; return E_NOINTERFACE; }
    InterlockedIncrement(&v->refs);
    return S_OK;
}
static ULONG vec_release(Vec *v)
{
    LONG r = InterlockedDecrement(&v->refs);
    if (!r) {
        for (UINT32 i = 0; i < v->count; i++) WindowsDeleteString(v->items[i]);
        HeapFree(GetProcessHeap(), 0, v);
    }
    return (ULONG)r;
}
static HRESULT vec_name(HSTRING *n)
{
    static const WCHAR s[] = L"Windows.Foundation.Collections.IVectorView`1<String>";
    return WindowsCreateString(s, (UINT32)(sizeof s / sizeof *s - 1), n);
}
#define FACE_FNS(f) \
    static HRESULT STDMETHODCALLTYPE f##_qi(void *p, REFIID iid, void **o) { return vec_qi(OF(p, f), iid, o); } \
    static ULONG STDMETHODCALLTYPE f##_addref(void *p) { return (ULONG)InterlockedIncrement(&OF(p, f)->refs); } \
    static ULONG STDMETHODCALLTYPE f##_release(void *p) { return vec_release(OF(p, f)); } \
    static HRESULT STDMETHODCALLTYPE f##_name(void *p, HSTRING *n) { (void)p; return vec_name(n); }
FACE_FNS(view)
FACE_FNS(iter)
#define FACE_BASE(f) (void *)f##_qi, (void *)f##_addref, (void *)f##_release, (void *)no_iids, (void *)f##_name, (void *)base_trust

/* ---- IVectorView<String> ---- */
static HRESULT get_many(Vec *v, UINT32 start, UINT32 cap, HSTRING *items, UINT32 *n)
{
    if (!n || (cap && !items)) return E_POINTER;
    UINT32 k = 0;
    for (; k < cap && start + k < v->count; k++) WindowsDuplicateString(v->items[start + k], &items[k]);
    *n = k;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE v_at(void *p, UINT32 i, HSTRING *s)
{
    Vec *v = OF(p, view);
    if (!s) return E_POINTER;
    *s = 0;
    return i < v->count ? WindowsDuplicateString(v->items[i], s) : E_BOUNDS_;
}
static HRESULT STDMETHODCALLTYPE v_size(void *p, UINT32 *n) { if (!n) return E_POINTER; *n = OF(p, view)->count; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_index(void *p, HSTRING s, UINT32 *i, BYTE *found)
{
    Vec *v = OF(p, view);
    if (!i || !found) return E_POINTER;
    *i = 0;
    *found = FALSE;
    for (UINT32 k = 0; k < v->count; k++) {
        INT32 c = 1;
        WindowsCompareStringOrdinal(v->items[k], s, &c);
        if (!c) { *i = k; *found = TRUE; break; }
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE v_many(void *p, UINT32 start, UINT32 cap, HSTRING *items, UINT32 *n)
{
    return get_many(OF(p, view), start, cap, items, n);
}
static const void *const view_vt[] = { FACE_BASE(view), (void *)v_at, (void *)v_size, (void *)v_index, (void *)v_many };

/* ---- IIterator<String> ---- */
typedef struct { const void *const *vt; LONG refs; Vec *v; UINT32 at; } Iter;
static HRESULT STDMETHODCALLTYPE it_qi(Iter *t, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_) ||
        IsEqualGUID(iid, &IID_IIterator_)) {
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
    if (!r) { vec_release(t->v); HeapFree(GetProcessHeap(), 0, t); }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE it_name(void *p, HSTRING *n) { (void)p; *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE it_current(Iter *t, HSTRING *s)
{
    if (!s) return E_POINTER;
    *s = 0;
    return t->at < t->v->count ? WindowsDuplicateString(t->v->items[t->at], s) : E_BOUNDS_;
}
static HRESULT STDMETHODCALLTYPE it_has(Iter *t, BYTE *b) { if (!b) return E_POINTER; *b = t->at < t->v->count; return S_OK; }
static HRESULT STDMETHODCALLTYPE it_next(Iter *t, BYTE *b)
{
    if (!b) return E_POINTER;
    if (t->at < t->v->count) t->at++;
    *b = t->at < t->v->count;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE it_many(Iter *t, UINT32 cap, HSTRING *items, UINT32 *n)
{
    HRESULT hr = get_many(t->v, t->at, cap, items, n);
    if (SUCCEEDED(hr)) t->at += *n;
    return hr;
}
static const void *const iterator_vt[] = { (void *)it_qi, (void *)it_addref, (void *)it_release, (void *)no_iids, (void *)it_name,
                                           (void *)base_trust, (void *)it_current, (void *)it_has, (void *)it_next, (void *)it_many };

/* ---- IIterable<String> ---- */
static HRESULT STDMETHODCALLTYPE i_first(void *p, void **out)
{
    Vec *v = OF(p, iter);
    if (!out) return E_POINTER;
    Iter *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *t);
    *out = t;
    if (!t) return E_OUTOFMEMORY;
    t->vt = iterator_vt;
    t->refs = 1;
    t->v = v;
    InterlockedIncrement(&v->refs);
    return S_OK;
}
static const void *const iter_vt[] = { FACE_BASE(iter), (void *)i_first };

HRESULT nova_strvec_new(const WCHAR *const *items, UINT32 count, void **out)
{
    if (!out) return E_POINTER;
    Vec *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *v + count * sizeof(HSTRING));
    *out = v;
    if (!v) return E_OUTOFMEMORY;
    v->view = view_vt;
    v->iter = iter_vt;
    v->refs = 1;
    for (; v->count < count; v->count++) {
        HRESULT hr = WindowsCreateString(items[v->count], (UINT32)lstrlenW(items[v->count]), &v->items[v->count]);
        if (FAILED(hr)) { vec_release(v); *out = 0; return hr; }
    }
    return S_OK;
}
