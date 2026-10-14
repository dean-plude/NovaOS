/*
 * mfplat.dll — the Media Foundation platform: attribute stores, media
 * types, memory buffers and samples, and (camera.c, reader.c) the camera
 * source and the source reader that mf.dll and mfreadwrite.dll hand out
 *
 * MFStartup/MFShutdown only count; MFCreateAttributes, MFCreateMediaType,
 * MFCreateMemoryBuffer and MFCreateSample give the objects Windows'
 * do.  An attribute store copies what it is given (strings and blobs;
 * an IUnknown is referenced) and hands out copies.
 *
 * Not done yet: media sessions and topologies, transforms (MFTs, so no
 * decoders or encoders), the sink writer, byte streams and files
 * (MFCreateSourceReaderFromURL), DXGI and Direct3D buffers, work queues
 * and asynchronous results.
 */
#include "mfpriv.h"

int _fltused = 1;                  /* (GetDouble and SetDouble) */

static LONG g_started;

MFEXPORT HRESULT WINAPI MFStartup(ULONG version, DWORD flags)
{
    (void)flags;
    if ((version >> 16) != MF_SDK_VERSION) return MF_E_NOT_INITIALIZED;   /* (Windows: MF_E_BAD_STARTUP_VERSION) */
    InterlockedIncrement(&g_started);
    return S_OK;
}

MFEXPORT HRESULT WINAPI MFShutdown(void)
{
    if (InterlockedDecrement(&g_started) < 0) InterlockedIncrement(&g_started);
    return S_OK;
}

MFEXPORT LONGLONG WINAPI MFGetSystemTime(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (LONGLONG)((double)c.QuadPart * 10000000.0 / (double)f.QuadPart);
}

/* =======================================================================
 * Attribute stores
 * ======================================================================= */

static AttrObj *obj(IMFAttributes *a) { return (AttrObj *)a; }

void attr_init(AttrObj *a, const void *vtbl)
{
    memset(a, 0, sizeof(*a));
    a->vtbl = vtbl;
    a->ref = 1;
    InitializeCriticalSection(&a->cs);
}

static void prop_clear(PROPVARIANT *v)
{
    switch (v->vt) {
    case VT_LPWSTR:           CoTaskMemFree(v->pwszVal); break;
    case VT_CLSID:            CoTaskMemFree(v->puuid); break;
    case VT_VECTOR | VT_UI1:  CoTaskMemFree(v->caub.pElems); break;
    case VT_UNKNOWN:          if (v->punkVal) v->punkVal->lpVtbl->Release(v->punkVal); break;
    }
    memset(v, 0, sizeof(*v));
}

/* @dst a copy of @src (of a type an attribute may hold) */
static HRESULT prop_copy(PROPVARIANT *dst, const PROPVARIANT *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->vt = src->vt;
    switch (src->vt) {
    case VT_UI4: dst->ulVal = src->ulVal; break;
    case VT_UI8: dst->uhVal = src->uhVal; break;
    case VT_R8:  dst->dblVal = src->dblVal; break;
    case VT_CLSID:
        if (!src->puuid || !(dst->puuid = CoTaskMemAlloc(sizeof(GUID)))) { dst->vt = VT_EMPTY; return E_OUTOFMEMORY; }
        *dst->puuid = *src->puuid;
        break;
    case VT_LPWSTR: {
        int n = src->pwszVal ? lstrlenW(src->pwszVal) + 1 : 1;
        if (!(dst->pwszVal = CoTaskMemAlloc((SIZE_T)n * sizeof(WCHAR)))) { dst->vt = VT_EMPTY; return E_OUTOFMEMORY; }
        if (src->pwszVal) memcpy(dst->pwszVal, src->pwszVal, (size_t)n * sizeof(WCHAR));
        else dst->pwszVal[0] = 0;
        break;
    }
    case VT_VECTOR | VT_UI1:
        dst->caub.cElems = src->caub.cElems;
        if (!(dst->caub.pElems = CoTaskMemAlloc(src->caub.cElems ? src->caub.cElems : 1))) { dst->vt = VT_EMPTY; return E_OUTOFMEMORY; }
        if (src->caub.cElems) memcpy(dst->caub.pElems, src->caub.pElems, src->caub.cElems);
        break;
    case VT_UNKNOWN:
        dst->punkVal = src->punkVal;
        if (dst->punkVal) dst->punkVal->lpVtbl->AddRef(dst->punkVal);
        break;
    default:
        dst->vt = VT_EMPTY;
        return MF_E_INVALIDTYPE;
    }
    return S_OK;
}

static BOOL prop_equal(const PROPVARIANT *a, const PROPVARIANT *b)
{
    if (a->vt != b->vt) return FALSE;
    switch (a->vt) {
    case VT_UI4:    return a->ulVal == b->ulVal;
    case VT_UI8:    return a->uhVal.QuadPart == b->uhVal.QuadPart;
    case VT_R8:     return a->dblVal == b->dblVal;
    case VT_CLSID:  return !memcmp(a->puuid, b->puuid, sizeof(GUID));
    case VT_LPWSTR: return !lstrcmpW(a->pwszVal, b->pwszVal);
    case VT_VECTOR | VT_UI1:
        return a->caub.cElems == b->caub.cElems && !memcmp(a->caub.pElems, b->caub.pElems, a->caub.cElems);
    case VT_UNKNOWN: return a->punkVal == b->punkVal;
    }
    return FALSE;
}

/* The item @key (the store locked); NULL if it has none */
static AttrItem *find(AttrObj *a, const GUID *key)
{
    for (UINT32 i = 0; i < a->count; i++) if (!memcmp(&a->items[i].key, key, sizeof(GUID))) return &a->items[i];
    return NULL;
}

void attr_free(AttrObj *a)
{
    for (UINT32 i = 0; i < a->count; i++) prop_clear(&a->items[i].value);
    HeapFree(GetProcessHeap(), 0, a->items);
    a->items = NULL;
    a->count = a->cap = 0;
    DeleteCriticalSection(&a->cs);
}

ULONG STDMETHODCALLTYPE attr_AddRef(IMFAttributes *This) { return (ULONG)InterlockedIncrement(&obj(This)->ref); }

HRESULT STDMETHODCALLTYPE attr_GetItem(IMFAttributes *This, REFGUID key, PROPVARIANT *v)
{
    AttrObj *a = obj(This);
    EnterCriticalSection(&a->cs);
    AttrItem *it = find(a, key);
    HRESULT hr = !it ? MF_E_ATTRIBUTENOTFOUND : v ? prop_copy(v, &it->value) : S_OK;
    LeaveCriticalSection(&a->cs);
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_GetItemType(IMFAttributes *This, REFGUID key, MF_ATTRIBUTE_TYPE *t)
{
    AttrObj *a = obj(This);
    EnterCriticalSection(&a->cs);
    AttrItem *it = find(a, key);
    if (it && t) *t = (MF_ATTRIBUTE_TYPE)it->value.vt;
    LeaveCriticalSection(&a->cs);
    return it ? S_OK : MF_E_ATTRIBUTENOTFOUND;
}

HRESULT STDMETHODCALLTYPE attr_CompareItem(IMFAttributes *This, REFGUID key, REFPROPVARIANT v, BOOL *r)
{
    AttrObj *a = obj(This);
    if (!r) return E_POINTER;
    EnterCriticalSection(&a->cs);
    AttrItem *it = find(a, key);
    *r = it && prop_equal(&it->value, v);
    LeaveCriticalSection(&a->cs);
    return S_OK;
}

/* Whether every item of @a is in @b with the same value */
static BOOL subset(IMFAttributes *a, IMFAttributes *b)
{
    UINT32 n = 0;
    a->lpVtbl->GetCount(a, &n);
    for (UINT32 i = 0; i < n; i++) {
        GUID k;
        PROPVARIANT v;
        BOOL same = FALSE;
        if (FAILED(a->lpVtbl->GetItemByIndex(a, i, &k, &v))) return FALSE;
        b->lpVtbl->CompareItem(b, &k, &v, &same);
        prop_clear(&v);
        if (!same) return FALSE;
    }
    return TRUE;
}

HRESULT STDMETHODCALLTYPE attr_Compare(IMFAttributes *This, IMFAttributes *theirs, MF_ATTRIBUTES_MATCH_TYPE t, BOOL *r)
{
    if (!theirs || !r) return E_POINTER;
    UINT32 mine = 0, other = 0;
    This->lpVtbl->GetCount(This, &mine);
    theirs->lpVtbl->GetCount(theirs, &other);
    switch (t) {
    case MF_ATTRIBUTES_MATCH_OUR_ITEMS:   *r = subset(This, theirs); break;
    case MF_ATTRIBUTES_MATCH_THEIR_ITEMS: *r = subset(theirs, This); break;
    case MF_ATTRIBUTES_MATCH_ALL_ITEMS:   *r = mine == other && subset(This, theirs); break;
    case MF_ATTRIBUTES_MATCH_SMALLER:     *r = mine <= other ? subset(This, theirs) : subset(theirs, This); break;
    case MF_ATTRIBUTES_MATCH_INTERSECTION: {
        *r = TRUE;
        for (UINT32 i = 0; i < mine && *r; i++) {
            GUID k;
            PROPVARIANT v, w;
            if (FAILED(This->lpVtbl->GetItemByIndex(This, i, &k, &v))) break;
            if (SUCCEEDED(theirs->lpVtbl->GetItem(theirs, &k, &w))) {
                *r = prop_equal(&v, &w);
                prop_clear(&w);
            }
            prop_clear(&v);
        }
        break;
    }
    default: return E_INVALIDARG;
    }
    return S_OK;
}

/* The item @key, if it has type @vt, copied into @v */
static HRESULT get_typed(IMFAttributes *This, REFGUID key, VARTYPE vt, PROPVARIANT *v)
{
    HRESULT hr = attr_GetItem(This, key, v);
    if (FAILED(hr)) return hr;
    if (v->vt != vt) { prop_clear(v); return MF_E_INVALIDTYPE; }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE attr_GetUINT32(IMFAttributes *This, REFGUID key, UINT32 *out)
{
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_UI4, &v);
    if (SUCCEEDED(hr) && out) *out = v.ulVal;
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_GetUINT64(IMFAttributes *This, REFGUID key, UINT64 *out)
{
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_UI8, &v);
    if (SUCCEEDED(hr) && out) *out = v.uhVal.QuadPart;
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_GetDouble(IMFAttributes *This, REFGUID key, double *out)
{
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_R8, &v);
    if (SUCCEEDED(hr) && out) *out = v.dblVal;
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_GetGUID(IMFAttributes *This, REFGUID key, GUID *out)
{
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_CLSID, &v);
    if (SUCCEEDED(hr)) {
        if (out) *out = *v.puuid;
        prop_clear(&v);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_GetStringLength(IMFAttributes *This, REFGUID key, UINT32 *n)
{
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_LPWSTR, &v);
    if (SUCCEEDED(hr)) {
        if (n) *n = (UINT32)lstrlenW(v.pwszVal);
        prop_clear(&v);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_GetString(IMFAttributes *This, REFGUID key, LPWSTR out, UINT32 size, UINT32 *n)
{
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_LPWSTR, &v);
    if (FAILED(hr)) return hr;
    UINT32 len = (UINT32)lstrlenW(v.pwszVal);
    if (n) *n = len;
    if (!out || size < len + 1) hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    else memcpy(out, v.pwszVal, (len + 1) * sizeof(WCHAR));
    prop_clear(&v);
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_GetAllocatedString(IMFAttributes *This, REFGUID key, LPWSTR *out, UINT32 *n)
{
    if (!out) return E_POINTER;
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_LPWSTR, &v);
    if (FAILED(hr)) { *out = NULL; return hr; }
    *out = v.pwszVal;                                    /* (the copy is the caller's: CoTaskMemFree) */
    if (n) *n = (UINT32)lstrlenW(v.pwszVal);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE attr_GetBlobSize(IMFAttributes *This, REFGUID key, UINT32 *n)
{
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_VECTOR | VT_UI1, &v);
    if (SUCCEEDED(hr)) {
        if (n) *n = v.caub.cElems;
        prop_clear(&v);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_GetBlob(IMFAttributes *This, REFGUID key, UINT8 *buf, UINT32 size, UINT32 *n)
{
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_VECTOR | VT_UI1, &v);
    if (FAILED(hr)) return hr;
    if (n) *n = v.caub.cElems;
    if (!buf || size < v.caub.cElems) hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    else memcpy(buf, v.caub.pElems, v.caub.cElems);
    prop_clear(&v);
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_GetAllocatedBlob(IMFAttributes *This, REFGUID key, UINT8 **buf, UINT32 *n)
{
    if (!buf || !n) return E_POINTER;
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_VECTOR | VT_UI1, &v);
    if (FAILED(hr)) { *buf = NULL; *n = 0; return hr; }
    *buf = v.caub.pElems;
    *n = v.caub.cElems;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE attr_GetUnknown(IMFAttributes *This, REFGUID key, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    PROPVARIANT v;
    HRESULT hr = get_typed(This, key, VT_UNKNOWN, &v);
    if (FAILED(hr)) return hr;
    hr = v.punkVal ? v.punkVal->lpVtbl->QueryInterface(v.punkVal, riid, ppv) : E_NOINTERFACE;
    prop_clear(&v);
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_SetItem(IMFAttributes *This, REFGUID key, REFPROPVARIANT v)
{
    AttrObj *a = obj(This);
    if (!v) return E_POINTER;
    PROPVARIANT copy;
    HRESULT hr = prop_copy(&copy, v);
    if (FAILED(hr)) return hr;
    EnterCriticalSection(&a->cs);
    AttrItem *it = find(a, key);
    if (it) {
        prop_clear(&it->value);
        it->value = copy;
    } else {
        if (a->count == a->cap) {
            UINT32 cap = a->cap ? a->cap * 2 : 8;
            AttrItem *n = a->items ? HeapReAlloc(GetProcessHeap(), 0, a->items, cap * sizeof(AttrItem))
                                   : HeapAlloc(GetProcessHeap(), 0, cap * sizeof(AttrItem));
            if (!n) { LeaveCriticalSection(&a->cs); prop_clear(&copy); return E_OUTOFMEMORY; }
            a->items = n;
            a->cap = cap;
        }
        a->items[a->count].key = *key;
        a->items[a->count++].value = copy;
    }
    LeaveCriticalSection(&a->cs);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE attr_DeleteItem(IMFAttributes *This, REFGUID key)
{
    AttrObj *a = obj(This);
    EnterCriticalSection(&a->cs);
    AttrItem *it = find(a, key);
    if (it) {
        prop_clear(&it->value);
        UINT32 i = (UINT32)(it - a->items);
        memmove(it, it + 1, (a->count - i - 1) * sizeof(AttrItem));
        a->count--;
    }
    LeaveCriticalSection(&a->cs);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE attr_DeleteAllItems(IMFAttributes *This)
{
    AttrObj *a = obj(This);
    EnterCriticalSection(&a->cs);
    for (UINT32 i = 0; i < a->count; i++) prop_clear(&a->items[i].value);
    a->count = 0;
    LeaveCriticalSection(&a->cs);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE attr_SetUINT32(IMFAttributes *This, REFGUID key, UINT32 x)
{
    PROPVARIANT v = { VT_UI4 };
    v.ulVal = x;
    return attr_SetItem(This, key, &v);
}

HRESULT STDMETHODCALLTYPE attr_SetUINT64(IMFAttributes *This, REFGUID key, UINT64 x)
{
    PROPVARIANT v = { VT_UI8 };
    v.uhVal.QuadPart = x;
    return attr_SetItem(This, key, &v);
}

HRESULT STDMETHODCALLTYPE attr_SetDouble(IMFAttributes *This, REFGUID key, double x)
{
    PROPVARIANT v = { VT_R8 };
    v.dblVal = x;
    return attr_SetItem(This, key, &v);
}

HRESULT STDMETHODCALLTYPE attr_SetGUID(IMFAttributes *This, REFGUID key, REFGUID x)
{
    if (!x) return E_POINTER;
    GUID g = *x;
    PROPVARIANT v = { VT_CLSID };
    v.puuid = &g;
    return attr_SetItem(This, key, &v);
}

HRESULT STDMETHODCALLTYPE attr_SetString(IMFAttributes *This, REFGUID key, LPCWSTR x)
{
    if (!x) return E_POINTER;
    PROPVARIANT v = { VT_LPWSTR };
    v.pwszVal = (LPWSTR)x;
    return attr_SetItem(This, key, &v);
}

HRESULT STDMETHODCALLTYPE attr_SetBlob(IMFAttributes *This, REFGUID key, const UINT8 *buf, UINT32 n)
{
    if (!buf && n) return E_POINTER;
    PROPVARIANT v = { VT_VECTOR | VT_UI1 };
    v.caub.cElems = n;
    v.caub.pElems = (UCHAR *)buf;
    return attr_SetItem(This, key, &v);
}

HRESULT STDMETHODCALLTYPE attr_SetUnknown(IMFAttributes *This, REFGUID key, IUnknown *u)
{
    PROPVARIANT v = { VT_UNKNOWN };
    v.punkVal = u;
    return attr_SetItem(This, key, &v);
}

HRESULT STDMETHODCALLTYPE attr_LockStore(IMFAttributes *This)   { EnterCriticalSection(&obj(This)->cs); return S_OK; }
HRESULT STDMETHODCALLTYPE attr_UnlockStore(IMFAttributes *This) { LeaveCriticalSection(&obj(This)->cs); return S_OK; }

HRESULT STDMETHODCALLTYPE attr_GetCount(IMFAttributes *This, UINT32 *n)
{
    if (!n) return E_POINTER;
    AttrObj *a = obj(This);
    EnterCriticalSection(&a->cs);
    *n = a->count;
    LeaveCriticalSection(&a->cs);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE attr_GetItemByIndex(IMFAttributes *This, UINT32 i, GUID *key, PROPVARIANT *v)
{
    AttrObj *a = obj(This);
    HRESULT hr = E_INVALIDARG;
    EnterCriticalSection(&a->cs);
    if (i < a->count) {
        if (key) *key = a->items[i].key;
        hr = v ? prop_copy(v, &a->items[i].value) : S_OK;
    }
    LeaveCriticalSection(&a->cs);
    return hr;
}

HRESULT STDMETHODCALLTYPE attr_CopyAllItems(IMFAttributes *This, IMFAttributes *dst)
{
    if (!dst) return E_POINTER;
    dst->lpVtbl->DeleteAllItems(dst);
    return attr_copy_into(obj(This), dst);
}

/* Every item of @src set on @dst too */
HRESULT attr_copy_into(AttrObj *src, IMFAttributes *dst)
{
    HRESULT hr = S_OK;
    EnterCriticalSection(&src->cs);
    for (UINT32 i = 0; i < src->count && SUCCEEDED(hr); i++)
        hr = dst->lpVtbl->SetItem(dst, &src->items[i].key, &src->items[i].value);
    LeaveCriticalSection(&src->cs);
    return hr;
}

/* ---- a plain store (MFCreateAttributes) ---- */

static HRESULT STDMETHODCALLTYPE plain_qi(IMFAttributes *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFAttributes)) {
        *ppv = This;
        attr_AddRef(This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE plain_release(IMFAttributes *This)
{
    AttrObj *a = obj(This);
    LONG n = InterlockedDecrement(&a->ref);
    if (!n) {
        attr_free(a);
        HeapFree(GetProcessHeap(), 0, a);
    }
    return (ULONG)n;
}

static const IMFAttributesVtbl g_plain_vtbl = { ATTR_VTBL(plain_qi, plain_release) };

HRESULT mf_create_attributes(IMFAttributes **out, UINT32 size)
{
    (void)size;
    if (!out) return E_POINTER;
    AttrObj *a = HeapAlloc(GetProcessHeap(), 0, sizeof(*a));
    if (!a) return E_OUTOFMEMORY;
    attr_init(a, &g_plain_vtbl);
    *out = (IMFAttributes *)a;
    return S_OK;
}

MFEXPORT HRESULT WINAPI MFCreateAttributes(IMFAttributes **out, UINT32 size) { return mf_create_attributes(out, size); }

/* =======================================================================
 * Media types
 * ======================================================================= */

static HRESULT STDMETHODCALLTYPE mt_qi(IMFMediaType *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFAttributes) || IsEqualGUID(riid, &IID_IMFMediaType)) {
        *ppv = This;
        attr_AddRef((IMFAttributes *)This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE mt_GetMajorType(IMFMediaType *This, GUID *major)
{
    if (!major) return E_POINTER;
    HRESULT hr = attr_GetGUID((IMFAttributes *)This, &MF_MT_MAJOR_TYPE, major);
    return hr == MF_E_ATTRIBUTENOTFOUND ? MF_E_INVALIDTYPE : hr;
}

static HRESULT STDMETHODCALLTYPE mt_IsCompressedFormat(IMFMediaType *This, BOOL *compressed)
{
    if (!compressed) return E_POINTER;
    UINT32 independent = 0;
    GUID sub;
    *compressed = FALSE;
    if (SUCCEEDED(attr_GetGUID((IMFAttributes *)This, &MF_MT_SUBTYPE, &sub)))
        *compressed = IsEqualGUID(&sub, &MFVideoFormat_MJPG);
    if (SUCCEEDED(attr_GetUINT32((IMFAttributes *)This, &MF_MT_ALL_SAMPLES_INDEPENDENT, &independent)) && !independent)
        *compressed = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mt_IsEqual(IMFMediaType *This, IMFMediaType *other, DWORD *flags)
{
    if (!other || !flags) return E_POINTER;
    IMFAttributes *a = (IMFAttributes *)This, *b = (IMFAttributes *)other;
    GUID x, y;
    *flags = 0;
    BOOL major = SUCCEEDED(attr_GetGUID(a, &MF_MT_MAJOR_TYPE, &x)) &&
                 SUCCEEDED(b->lpVtbl->GetGUID(b, &MF_MT_MAJOR_TYPE, &y)) && IsEqualGUID(&x, &y);
    if (major) *flags |= MF_MEDIATYPE_EQUAL_MAJOR_TYPES;
    BOOL sub = SUCCEEDED(attr_GetGUID(a, &MF_MT_SUBTYPE, &x)) &&
               SUCCEEDED(b->lpVtbl->GetGUID(b, &MF_MT_SUBTYPE, &y)) && IsEqualGUID(&x, &y);
    if (sub) *flags |= MF_MEDIATYPE_EQUAL_FORMAT_TYPES;
    BOOL same = FALSE;
    attr_Compare(a, b, MF_ATTRIBUTES_MATCH_ALL_ITEMS, &same);
    if (same) *flags |= MF_MEDIATYPE_EQUAL_FORMAT_DATA | MF_MEDIATYPE_EQUAL_FORMAT_USER_DATA;
    return *flags == 0xF ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE mt_GetRepresentation(IMFMediaType *This, GUID guid, void **rep)
{
    (void)This; (void)guid;
    if (rep) *rep = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE mt_FreeRepresentation(IMFMediaType *This, GUID guid, void *rep)
{
    (void)This; (void)guid; (void)rep;
    return E_NOTIMPL;
}

static const IMFMediaTypeVtbl g_mt_vtbl = {
    ATTR_VTBL(mt_qi, plain_release),
    .GetMajorType = mt_GetMajorType, .IsCompressedFormat = mt_IsCompressedFormat, .IsEqual = mt_IsEqual,
    .GetRepresentation = mt_GetRepresentation, .FreeRepresentation = mt_FreeRepresentation,
};

HRESULT mf_create_media_type(IMFMediaType **out)
{
    if (!out) return E_POINTER;
    AttrObj *a = HeapAlloc(GetProcessHeap(), 0, sizeof(*a));
    if (!a) return E_OUTOFMEMORY;
    attr_init(a, &g_mt_vtbl);
    *out = (IMFMediaType *)a;
    return S_OK;
}

MFEXPORT HRESULT WINAPI MFCreateMediaType(IMFMediaType **out) { return mf_create_media_type(out); }

/* A video media type's attributes as a camera's native types have them */
void mf_fill_video_type(IMFMediaType *t, const GUID *subtype, UINT32 width, UINT32 height, UINT32 interval,
                        UINT32 min_interval, BOOL compressed)
{
    IMFAttributes *a = (IMFAttributes *)t;
    attr_SetGUID(a, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    attr_SetGUID(a, &MF_MT_SUBTYPE, subtype);
    attr_SetUINT64(a, &MF_MT_FRAME_SIZE, (UINT64)width << 32 | height);
    /* frame rates as Windows gives them: 30/1 for 333333, else per 1000;
     * the range from the default interval to the shortest */
    UINT32 rates[3] = { interval, interval, min_interval ? min_interval : interval };
    const GUID *keys[3] = { &MF_MT_FRAME_RATE, &MF_MT_FRAME_RATE_RANGE_MIN, &MF_MT_FRAME_RATE_RANGE_MAX };
    for (int i = 0; i < 3; i++) {
        if (!rates[i]) continue;
        UINT32 milli = (UINT32)((10000000ull * 1000 + rates[i] / 2) / rates[i]);
        UINT64 v = milli % 1000 ? (UINT64)milli << 32 | 1000 : (UINT64)(milli / 1000) << 32 | 1;
        attr_SetUINT64(a, keys[i], v);
    }
    attr_SetUINT64(a, &MF_MT_PIXEL_ASPECT_RATIO, (UINT64)1 << 32 | 1);
    attr_SetUINT32(a, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    attr_SetUINT32(a, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
    if (compressed) {
        attr_DeleteItem(a, &MF_MT_FIXED_SIZE_SAMPLES);
        attr_DeleteItem(a, &MF_MT_SAMPLE_SIZE);
        attr_DeleteItem(a, &MF_MT_DEFAULT_STRIDE);
        return;
    }
    UINT32 size = 0;
    LONG stride = 0;
    MFCalculateImageSize(subtype, width, height, &size);
    MFGetStrideForBitmapInfoHeader(subtype->Data1, width, &stride);
    attr_SetUINT32(a, &MF_MT_FIXED_SIZE_SAMPLES, 1);
    attr_SetUINT32(a, &MF_MT_SAMPLE_SIZE, size);
    attr_SetUINT32(a, &MF_MT_DEFAULT_STRIDE, (UINT32)stride);
    attr_SetUINT32(a, &MF_MT_AVG_BITRATE, interval ? (UINT32)((UINT64)size * 8 * 10000000 / interval) : 0);
}

/* =======================================================================
 * Image sizes
 * ======================================================================= */

/* Bits per pixel of an uncompressed video format, its planes counted (0: unknown) */
static UINT32 format_bpp(DWORD fourcc)
{
    switch (fourcc) {
    case 0x16: case 0x15: return 32;                    /* RGB32, ARGB32 */
    case 0x14: return 24;                               /* RGB24 */
    case 0x32595559: case 0x59565955: return 16;        /* YUY2, UYVY */
    case 0x3231564E: case 0x30323449: case 0x32315659: return 12;   /* NV12, I420, YV12 */
    }
    return 0;
}

MFEXPORT HRESULT WINAPI MFCalculateImageSize(REFGUID subtype, UINT32 width, UINT32 height, UINT32 *size)
{
    if (!size) return E_POINTER;
    UINT32 bpp = format_bpp(subtype->Data1);
    if (!bpp) { *size = 0; return E_INVALIDARG; }
    if (bpp == 12) *size = width * height + 2 * ((width + 1) / 2) * ((height + 1) / 2);
    else if (bpp == 16) *size = width * 2 * height;
    else *size = ((width * bpp / 8 + 3) & ~3u) * height;     /* (RGB rows are DWORD-aligned) */
    return S_OK;
}

MFEXPORT HRESULT WINAPI MFGetStrideForBitmapInfoHeader(DWORD fourcc, DWORD width, LONG *stride)
{
    if (!stride) return E_POINTER;
    UINT32 bpp = format_bpp(fourcc);
    if (!bpp) { *stride = 0; return E_INVALIDARG; }
    *stride = bpp == 12 ? (LONG)width : bpp == 16 ? (LONG)width * 2 : (LONG)((width * bpp / 8 + 3) & ~3u);
    return S_OK;
}

/* =======================================================================
 * Memory buffers (with IMF2DBuffer when made for a video frame)
 * ======================================================================= */

typedef struct {
    IMFMediaBuffer iface;
    IMF2DBuffer    iface2d;
    LONG   ref;
    BYTE  *data;
    DWORD  max, cur;
    LONG   stride;                     /* (0: not a 2D buffer) */
    UINT32 height;
} Buffer;

static Buffer *buf_of(IMFMediaBuffer *b) { return (Buffer *)b; }
static Buffer *buf_of2d(IMF2DBuffer *b) { return (Buffer *)((BYTE *)b - __builtin_offsetof(Buffer, iface2d)); }

static HRESULT STDMETHODCALLTYPE buf_qi(IMFMediaBuffer *This, REFIID riid, void **ppv)
{
    Buffer *b = buf_of(This);
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFMediaBuffer)) *ppv = &b->iface;
    else if (IsEqualGUID(riid, &IID_IMF2DBuffer) && b->stride) *ppv = &b->iface2d;
    else { *ppv = NULL; return E_NOINTERFACE; }
    InterlockedIncrement(&b->ref);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE buf_addref(IMFMediaBuffer *This) { return (ULONG)InterlockedIncrement(&buf_of(This)->ref); }

static ULONG STDMETHODCALLTYPE buf_release(IMFMediaBuffer *This)
{
    Buffer *b = buf_of(This);
    LONG n = InterlockedDecrement(&b->ref);
    if (!n) {
        HeapFree(GetProcessHeap(), 0, b->data);
        HeapFree(GetProcessHeap(), 0, b);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE buf_Lock(IMFMediaBuffer *This, BYTE **data, DWORD *max, DWORD *cur)
{
    Buffer *b = buf_of(This);
    if (!data) return E_POINTER;
    *data = b->data;
    if (max) *max = b->max;
    if (cur) *cur = b->cur;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE buf_Unlock(IMFMediaBuffer *This) { (void)This; return S_OK; }

static HRESULT STDMETHODCALLTYPE buf_GetCurrentLength(IMFMediaBuffer *This, DWORD *n)
{
    if (!n) return E_POINTER;
    *n = buf_of(This)->cur;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE buf_SetCurrentLength(IMFMediaBuffer *This, DWORD n)
{
    Buffer *b = buf_of(This);
    if (n > b->max) return E_INVALIDARG;
    b->cur = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE buf_GetMaxLength(IMFMediaBuffer *This, DWORD *n)
{
    if (!n) return E_POINTER;
    *n = buf_of(This)->max;
    return S_OK;
}

static const IMFMediaBufferVtbl g_buf_vtbl = {
    buf_qi, buf_addref, buf_release, buf_Lock, buf_Unlock, buf_GetCurrentLength, buf_SetCurrentLength, buf_GetMaxLength,
};

static HRESULT STDMETHODCALLTYPE b2_qi(IMF2DBuffer *This, REFIID riid, void **ppv) { return buf_qi(&buf_of2d(This)->iface, riid, ppv); }
static ULONG STDMETHODCALLTYPE b2_addref(IMF2DBuffer *This) { return buf_addref(&buf_of2d(This)->iface); }
static ULONG STDMETHODCALLTYPE b2_release(IMF2DBuffer *This) { return buf_release(&buf_of2d(This)->iface); }

static HRESULT STDMETHODCALLTYPE b2_Lock2D(IMF2DBuffer *This, BYTE **scan0, LONG *pitch)
{
    Buffer *b = buf_of2d(This);
    if (!scan0 || !pitch) return E_POINTER;
    *scan0 = b->stride < 0 ? b->data + (size_t)(-b->stride) * (b->height - 1) : b->data;
    *pitch = b->stride;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE b2_Unlock2D(IMF2DBuffer *This) { (void)This; return S_OK; }

static HRESULT STDMETHODCALLTYPE b2_IsContiguousFormat(IMF2DBuffer *This, BOOL *c)
{
    (void)This;
    if (!c) return E_POINTER;
    *c = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE b2_GetContiguousLength(IMF2DBuffer *This, DWORD *n)
{
    if (!n) return E_POINTER;
    *n = buf_of2d(This)->cur;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE b2_ContiguousCopyTo(IMF2DBuffer *This, BYTE *dst, DWORD n)
{
    Buffer *b = buf_of2d(This);
    if (!dst) return E_POINTER;
    if (n < b->cur) return E_INVALIDARG;
    memcpy(dst, b->data, b->cur);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE b2_ContiguousCopyFrom(IMF2DBuffer *This, const BYTE *src, DWORD n)
{
    Buffer *b = buf_of2d(This);
    if (!src) return E_POINTER;
    if (n > b->max) return E_INVALIDARG;
    memcpy(b->data, src, n);
    b->cur = n;
    return S_OK;
}

static const IMF2DBufferVtbl g_b2_vtbl = {
    b2_qi, b2_addref, b2_release, b2_Lock2D, b2_Unlock2D, b2_Lock2D /* GetScanline0AndPitch */, b2_IsContiguousFormat,
    b2_GetContiguousLength, b2_ContiguousCopyTo, b2_ContiguousCopyFrom,
};

HRESULT mf_create_video_buffer(DWORD length, LONG stride, UINT32 height, IMFMediaBuffer **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    Buffer *b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b) return E_OUTOFMEMORY;
    b->data = HeapAlloc(GetProcessHeap(), 0, length ? length : 1);
    if (!b->data) { HeapFree(GetProcessHeap(), 0, b); return E_OUTOFMEMORY; }
    b->iface.lpVtbl = &g_buf_vtbl;
    b->iface2d.lpVtbl = &g_b2_vtbl;
    b->ref = 1;
    b->max = length;
    b->stride = stride;
    b->height = height;
    *out = &b->iface;
    return S_OK;
}

MFEXPORT HRESULT WINAPI MFCreateMemoryBuffer(DWORD max, IMFMediaBuffer **out) { return mf_create_video_buffer(max, 0, 0, out); }

MFEXPORT HRESULT WINAPI MFCreateAlignedMemoryBuffer(DWORD max, DWORD alignment, IMFMediaBuffer **out)
{
    (void)alignment;                   /* (the heap aligns to 16 bytes) */
    return mf_create_video_buffer(max, 0, 0, out);
}

/* =======================================================================
 * Samples
 * ======================================================================= */

#define MAX_BUFFERS 16

typedef struct {
    AttrObj         a;                 /* (first: the IMFAttributes methods) */
    DWORD           flags;
    LONGLONG        time, duration;
    BOOL            has_time, has_duration;
    IMFMediaBuffer *bufs[MAX_BUFFERS];
    DWORD           nbufs;
} Sample;

static Sample *smp(IMFSample *s) { return (Sample *)s; }

static HRESULT STDMETHODCALLTYPE smp_qi(IMFSample *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMFAttributes) || IsEqualGUID(riid, &IID_IMFSample)) {
        *ppv = This;
        attr_AddRef((IMFAttributes *)This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE smp_RemoveAllBuffers(IMFSample *This)
{
    Sample *s = smp(This);
    for (DWORD i = 0; i < s->nbufs; i++) s->bufs[i]->lpVtbl->Release(s->bufs[i]);
    s->nbufs = 0;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE smp_release(IMFSample *This)
{
    Sample *s = smp(This);
    LONG n = InterlockedDecrement(&s->a.ref);
    if (!n) {
        smp_RemoveAllBuffers(This);
        attr_free(&s->a);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE smp_GetSampleFlags(IMFSample *This, DWORD *f)
{
    if (!f) return E_POINTER;
    *f = smp(This)->flags;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE smp_SetSampleFlags(IMFSample *This, DWORD f) { smp(This)->flags = f; return S_OK; }

static HRESULT STDMETHODCALLTYPE smp_GetSampleTime(IMFSample *This, LONGLONG *t)
{
    Sample *s = smp(This);
    if (!t) return E_POINTER;
    if (!s->has_time) return MF_E_NO_SAMPLE_TIMESTAMP;
    *t = s->time;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE smp_SetSampleTime(IMFSample *This, LONGLONG t)
{
    smp(This)->time = t;
    smp(This)->has_time = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE smp_GetSampleDuration(IMFSample *This, LONGLONG *d)
{
    Sample *s = smp(This);
    if (!d) return E_POINTER;
    if (!s->has_duration) return MF_E_NO_SAMPLE_DURATION;
    *d = s->duration;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE smp_SetSampleDuration(IMFSample *This, LONGLONG d)
{
    smp(This)->duration = d;
    smp(This)->has_duration = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE smp_GetBufferCount(IMFSample *This, DWORD *n)
{
    if (!n) return E_POINTER;
    *n = smp(This)->nbufs;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE smp_GetBufferByIndex(IMFSample *This, DWORD i, IMFMediaBuffer **b)
{
    Sample *s = smp(This);
    if (!b) return E_POINTER;
    if (i >= s->nbufs) return E_INVALIDARG;
    *b = s->bufs[i];
    (*b)->lpVtbl->AddRef(*b);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE smp_GetTotalLength(IMFSample *This, DWORD *n)
{
    Sample *s = smp(This);
    if (!n) return E_POINTER;
    *n = 0;
    for (DWORD i = 0; i < s->nbufs; i++) {
        DWORD k = 0;
        s->bufs[i]->lpVtbl->GetCurrentLength(s->bufs[i], &k);
        *n += k;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE smp_CopyToBuffer(IMFSample *This, IMFMediaBuffer *dst)
{
    Sample *s = smp(This);
    if (!dst) return E_POINTER;
    DWORD total = 0, max = 0, at = 0;
    smp_GetTotalLength(This, &total);
    BYTE *d;
    if (FAILED(dst->lpVtbl->Lock(dst, &d, &max, NULL))) return E_FAIL;
    if (max < total) { dst->lpVtbl->Unlock(dst); return MF_E_BUFFERTOOSMALL; }
    for (DWORD i = 0; i < s->nbufs; i++) {
        BYTE *p;
        DWORD n = 0;
        if (FAILED(s->bufs[i]->lpVtbl->Lock(s->bufs[i], &p, NULL, &n))) continue;
        memcpy(d + at, p, n);
        at += n;
        s->bufs[i]->lpVtbl->Unlock(s->bufs[i]);
    }
    dst->lpVtbl->Unlock(dst);
    return dst->lpVtbl->SetCurrentLength(dst, at);
}

static HRESULT STDMETHODCALLTYPE smp_ConvertToContiguousBuffer(IMFSample *This, IMFMediaBuffer **out)
{
    Sample *s = smp(This);
    if (!out) return E_POINTER;
    if (!s->nbufs) return E_UNEXPECTED;
    if (s->nbufs == 1) {
        *out = s->bufs[0];
        (*out)->lpVtbl->AddRef(*out);
        return S_OK;
    }
    DWORD total = 0;
    smp_GetTotalLength(This, &total);
    IMFMediaBuffer *b;
    HRESULT hr = MFCreateMemoryBuffer(total, &b);
    if (FAILED(hr)) return hr;
    hr = smp_CopyToBuffer(This, b);
    if (FAILED(hr)) { b->lpVtbl->Release(b); return hr; }
    smp_RemoveAllBuffers(This);
    s->bufs[s->nbufs++] = b;
    b->lpVtbl->AddRef(b);
    *out = b;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE smp_AddBuffer(IMFSample *This, IMFMediaBuffer *b)
{
    Sample *s = smp(This);
    if (!b) return E_POINTER;
    if (s->nbufs == MAX_BUFFERS) return E_OUTOFMEMORY;
    b->lpVtbl->AddRef(b);
    s->bufs[s->nbufs++] = b;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE smp_RemoveBufferByIndex(IMFSample *This, DWORD i)
{
    Sample *s = smp(This);
    if (i >= s->nbufs) return E_INVALIDARG;
    s->bufs[i]->lpVtbl->Release(s->bufs[i]);
    memmove(&s->bufs[i], &s->bufs[i + 1], (s->nbufs - i - 1) * sizeof(s->bufs[0]));
    s->nbufs--;
    return S_OK;
}

static const IMFSampleVtbl g_smp_vtbl = {
    ATTR_VTBL(smp_qi, smp_release),
    .GetSampleFlags = smp_GetSampleFlags, .SetSampleFlags = smp_SetSampleFlags,
    .GetSampleTime = smp_GetSampleTime, .SetSampleTime = smp_SetSampleTime,
    .GetSampleDuration = smp_GetSampleDuration, .SetSampleDuration = smp_SetSampleDuration,
    .GetBufferCount = smp_GetBufferCount, .GetBufferByIndex = smp_GetBufferByIndex,
    .ConvertToContiguousBuffer = smp_ConvertToContiguousBuffer, .AddBuffer = smp_AddBuffer,
    .RemoveBufferByIndex = smp_RemoveBufferByIndex, .RemoveAllBuffers = smp_RemoveAllBuffers,
    .GetTotalLength = smp_GetTotalLength, .CopyToBuffer = smp_CopyToBuffer,
};

HRESULT mf_create_sample(IMFSample **out)
{
    if (!out) return E_POINTER;
    Sample *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    attr_init(&s->a, &g_smp_vtbl);
    *out = (IMFSample *)s;
    return S_OK;
}

MFEXPORT HRESULT WINAPI MFCreateSample(IMFSample **out) { return mf_create_sample(out); }

/* ---- what mf.dll and mfreadwrite.dll export (NovaOS's own names) ---- */

MFEXPORT HRESULT WINAPI NovaMFEnumDeviceSources(IMFAttributes *attrs, IMFActivate ***sources, UINT32 *count)
{
    return nova_enum_device_sources(attrs, sources, count);
}

MFEXPORT HRESULT WINAPI NovaMFCreateDeviceSource(IMFAttributes *attrs, IMFMediaSource **source)
{
    return nova_create_device_source(attrs, source);
}

MFEXPORT HRESULT WINAPI NovaMFCreateSourceReader(IMFMediaSource *source, IMFAttributes *attrs, IMFSourceReader **reader)
{
    return nova_create_source_reader(source, attrs, reader);
}

/* ---- what the rest of Media Foundation would offer ----
 * Browsers load mfplat.dll and look for these (Chromium delay-loads them,
 * Firefox looks each one up): no transforms (codecs) and no DXGI device
 * manager, so they use their own decoders, as with no mfplat.dll at all */
MFEXPORT HRESULT WINAPI MFTEnumEx(GUID category, UINT32 flags, const void *in_type, const void *out_type,
                                  IMFActivate ***activates, UINT32 *count)
{
    (void)category; (void)flags; (void)in_type; (void)out_type;
    if (!activates || !count) return E_POINTER;
    *activates = NULL;
    *count = 0;
    return S_OK;
}
MFEXPORT HRESULT WINAPI MFTEnum(GUID category, UINT32 flags, const void *in_type, const void *out_type,
                                IMFAttributes *attrs, CLSID **clsids, UINT32 *count)
{
    (void)category; (void)flags; (void)in_type; (void)out_type; (void)attrs;
    if (!clsids || !count) return E_POINTER;
    *clsids = NULL;
    *count = 0;
    return S_OK;
}
MFEXPORT HRESULT WINAPI MFLockPlatform(void) { return S_OK; }
MFEXPORT HRESULT WINAPI MFUnlockPlatform(void) { return S_OK; }
MFEXPORT HRESULT WINAPI MFCreateDXGIDeviceManager(UINT *token, IUnknown **manager)
{
    if (token) *token = 0;
    if (manager) *manager = NULL;
    return E_NOTIMPL;
}
MFEXPORT HRESULT WINAPI MFCreateDXGISurfaceBuffer(REFIID riid, IUnknown *surface, UINT index, BOOL bottom_up,
                                                  IMFMediaBuffer **out)
{
    (void)riid; (void)surface; (void)index; (void)bottom_up;
    if (out) *out = NULL;
    return E_NOTIMPL;
}
MFEXPORT HRESULT WINAPI MFCreate2DMediaBuffer(DWORD width, DWORD height, DWORD fourcc, BOOL bottom_up, IMFMediaBuffer **out)
{
    LONG stride = 0;
    UINT32 size = 0;
    GUID sub = MFVideoFormat_YUY2;
    sub.Data1 = fourcc;
    if (!out) return E_POINTER;
    *out = NULL;
    if (bottom_up || FAILED(MFGetStrideForBitmapInfoHeader(fourcc, width, &stride)) ||
        FAILED(MFCalculateImageSize(&sub, width, height, &size)))
        return E_NOTIMPL;
    return mf_create_video_buffer(size, stride, height, out);
}
