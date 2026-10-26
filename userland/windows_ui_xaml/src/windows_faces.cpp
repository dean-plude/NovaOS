// NovaOS: the runtime of the XAML framework's Windows.UI.Xaml faces
// (windows_faces.h; the table and thunks are tools/winrt/winfaces.py's
// windows_faces.g.cpp).  The framework's objects answer a Windows.UI.Xaml
// interface ID they do not implement through nova_xaml_face_query (called
// by NovaOS's ComObjectBase.cpp, the framework's QueryInterface, when the
// object itself has no answer), a composed object's overrides through
// nova_xaml_override_face, and the framework's classes are activated by
// their Windows names through nova_xaml_windows_factory
// (windows_activation.cpp).
#include "windows_faces.h"
#include <stdio.h>
#include <string.h>
#include <winstring.h>

static const GUID IID_IUnknown_ = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IInspectable_ = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID IID_IPropertyValue_ = {0x4bd682dd, 0x7554, 0x40e9, {0x9a, 0x9b, 0x82, 0x65, 0x4e, 0xde, 0x7e, 0x62}};
static const GUID IID_IAgileObject_ = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

#define VT(p) (*(void ***)(p))
typedef HRESULT (STDMETHODCALLTYPE *QI_FN)(void *, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *REF_FN)(void *);

extern "C" void nova_addref(void *p)
{
    if (p)
        ((REF_FN)VT(p)[1])(p);
}

extern "C" void nova_release(void *p)
{
    if (p)
        ((REF_FN)VT(p)[2])(p);
}

static HRESULT qi(void *p, const GUID *iid, void **out)
{
    *out = nullptr;
    return ((QI_FN)VT(p)[0])(p, *iid, out);
}

static const NovaFace *find(REFIID iid)
{
    unsigned lo = 0, hi = nova_face_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        int c = memcmp(nova_faces[mid].impl, &iid, sizeof(GUID));
        if (c == 0)
            return &nova_faces[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return nullptr;
}

// a tear-off of face @f over @obj, whose reference it takes
static void *tear(const NovaFace *f, void *obj, BOOL direct)
{
    NovaTear *t = (NovaTear *)CoTaskMemAlloc(sizeof(NovaTear));
    if (!t) {
        nova_release(obj);
        return nullptr;
    }
    t->vtbl = f->vtbl;
    t->face = f;
    t->obj = obj;
    t->ref = 1;
    t->direct = direct;
    return t;
}

extern "C" HRESULT STDMETHODCALLTYPE nova_tear_qi(NovaTear *t, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, *t->face->impl) ||
        ((t->face->flags & NOVA_FACE_DELEGATE) && IsEqualGUID(iid, IID_IUnknown_))) {
        nova_tear_addref(t);
        *out = t;
        return S_OK;
    }
    // (the object's identity, and every other interface, are the object's;
    // one it has only in the other framework's form is a face of that, as
    // the framework asking an app's IXamlType for another of its interfaces)
    HRESULT hr = qi(t->obj, &iid, out);
    const NovaFace *f;
    void *w;
    if (hr == E_NOINTERFACE && (f = find(iid)) && !(f->flags & (NOVA_FACE_DELEGATE | NOVA_FACE_OVERRIDE)) &&
        !(f->flags & NOVA_FACE_REVERSE) == !(t->face->flags & NOVA_FACE_REVERSE) &&
        SUCCEEDED(qi(t->obj, f->target, &w))) {
        *out = tear(f, w, TRUE);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    return hr;
}

extern "C" ULONG STDMETHODCALLTYPE nova_tear_addref(NovaTear *t)
{
    return InterlockedIncrement(&t->ref);
}

extern "C" ULONG STDMETHODCALLTYPE nova_tear_release(NovaTear *t)
{
    ULONG r = InterlockedDecrement(&t->ref);
    if (r == 0) {
        nova_release(t->obj);
        CoTaskMemFree(t);
    }
    return r;
}

extern "C" HRESULT STDMETHODCALLTYPE nova_tear_iids(NovaTear *t, ULONG *count, IID **iids)
{
    IID *a = (IID *)CoTaskMemAlloc(sizeof(IID));
    if (!a)
        return E_OUTOFMEMORY;
    *a = *t->face->impl;
    *count = 1;
    *iids = a;
    return S_OK;
}

// the object's class name, a framework class's under its Windows name
extern "C" HRESULT STDMETHODCALLTYPE nova_tear_name(NovaTear *t, void **name)
{
    *name = nullptr;
    void *insp;
    HRESULT hr = qi(t->obj, &IID_IInspectable_, &insp);
    if (FAILED(hr))
        return hr;
    HSTRING h = nullptr;
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))VT(insp)[4])(insp, &h);
    nova_release(insp);
    if (FAILED(hr) || !h)
        return hr;
    UINT32 n;
    const wchar_t *s = WindowsGetStringRawBuffer(h, &n);
    static const wchar_t lifted[] = L"Microsoft.UI.Xaml.", windows[] = L"Windows.UI.Xaml.";
    const UINT32 ln = ARRAYSIZE(lifted) - 1, wn = ARRAYSIZE(windows) - 1;
    if (!(t->face->flags & NOVA_FACE_REVERSE) && n > ln && !wcsncmp(s, lifted, ln)) {
        wchar_t buf[256];
        if (n - ln + wn < ARRAYSIZE(buf)) {
            memcpy(buf, windows, wn * sizeof(wchar_t));
            memcpy(buf + wn, s + ln, (n - ln) * sizeof(wchar_t));
            HSTRING w;
            if (SUCCEEDED(WindowsCreateString(buf, n - ln + wn, &w))) {
                WindowsDeleteString(h);
                h = w;
            }
        }
    }
    *name = h;
    return S_OK;
}

extern "C" HRESULT STDMETHODCALLTYPE nova_tear_trust(NovaTear *, INT32 *level)
{
    *level = 0;     // BaseTrust
    return S_OK;
}

extern "C" HRESULT nova_target(NovaTear *t, const GUID *iid, void **p)
{
    if (t->direct && IsEqualGUID(*iid, *t->face->target)) {
        *p = t->obj;
        nova_addref(t->obj);
        return S_OK;
    }
    return qi(t->obj, iid, p);
}

extern "C" IActivationFactory *nova_lifted_factory(const wchar_t *cls);

extern "C" HRESULT nova_target_static(NovaTear *, const wchar_t *cls, const GUID *iid, void **p)
{
    *p = nullptr;
    IActivationFactory *f = nova_lifted_factory(cls);
    if (!f)
        return CLASS_E_CLASSNOTAVAILABLE;
    HRESULT hr = qi(f, iid, p);
    nova_release(f);
    return hr;
}

extern "C" void nova_untarget(NovaTear *, void *p)
{
    nova_release(p);
}

extern "C" HRESULT nova_cross(void *a, const GUID *iid, void **out)
{
    *out = nullptr;
    if (!a)
        return S_OK;
    HRESULT hr = qi(a, iid, out);
    if (SUCCEEDED(hr))
        return hr;
    const NovaFace *f = find(*iid);
    void *q;
    if (f && SUCCEEDED(qi(a, f->target, &q))) {
        *out = tear(f, q, TRUE);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    return hr;
}

static HRESULT rebox(void *a, bool to_windows, void **out);

extern "C" HRESULT nova_out(HRESULT hr, void *o, const GUID *iid, void **a)
{
    if (FAILED(hr) || !a) {
        nova_release(o);
        return hr;
    }
    if (!iid) {
        // (an object the framework gives the app's code: a box of a
        // Windows-named enum or struct as Windows boxes it)
        HRESULT h = o ? rebox(o, true, a) : S_FALSE;
        if (h == S_FALSE) {
            *a = o;
            return hr;
        }
        nova_release(o);
        return FAILED(h) ? h : hr;
    }
    HRESULT h = nova_cross(o, iid, a);
    nova_release(o);
    return FAILED(h) ? h : hr;
}

// a framework box of a Windows.UI.Xaml enum's or struct's value: the
// framework's IReference<T> and, as Windows' boxes have, IPropertyValue
// (OtherType; an enum's value as Int32 and UInt32 too); or (@windows) the
// Windows box of it the app's code is handed, Windows' IReference<T>
struct NovaValueBox
{
    void *const *vtbl;
    void *const *pv_vtbl;
    LONG ref;
    const NovaBox *box;
    bool windows;
    unsigned char value[64];
};

static const GUID &box_iid(NovaValueBox *b)
{
    return b->windows ? *b->box->windows : *b->box->framework;
}

static HRESULT STDMETHODCALLTYPE box_qi(NovaValueBox *b, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, IID_IUnknown_) || IsEqualGUID(iid, IID_IInspectable_) || IsEqualGUID(iid, box_iid(b)) ||
        IsEqualGUID(iid, IID_IAgileObject_))
        *out = b;
    else if (IsEqualGUID(iid, IID_IPropertyValue_))
        *out = &b->pv_vtbl;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&b->ref);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE box_addref(NovaValueBox *b) { return InterlockedIncrement(&b->ref); }

static ULONG STDMETHODCALLTYPE box_release(NovaValueBox *b)
{
    ULONG r = InterlockedDecrement(&b->ref);
    if (!r)
        CoTaskMemFree(b);
    return r;
}

static HRESULT STDMETHODCALLTYPE box_iids(NovaValueBox *b, ULONG *count, IID **iids)
{
    IID *a = (IID *)CoTaskMemAlloc(2 * sizeof(IID));
    if (!a)
        return E_OUTOFMEMORY;
    a[0] = box_iid(b);
    a[1] = IID_IPropertyValue_;
    *count = 2;
    *iids = a;
    return S_OK;
}

// (the framework reads the type boxed from the name: RoParseTypeName)
static HRESULT STDMETHODCALLTYPE box_name(NovaValueBox *b, HSTRING *name)
{
    if (b->windows) {
        wchar_t s[256];
        int n = swprintf(s, ARRAYSIZE(s), L"Windows.Foundation.IReference`1<%hs>", b->box->name);
        return n > 0 ? WindowsCreateString(s, (UINT32)n, name) : E_FAIL;
    }
    return WindowsCreateString(b->box->framework_name, (UINT32)wcslen(b->box->framework_name), name);
}

static HRESULT STDMETHODCALLTYPE box_trust(NovaValueBox *, INT32 *level)
{
    *level = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE box_value(NovaValueBox *b, void *out)
{
    memcpy(out, b->value, b->box->size);
    return S_OK;
}

#define PV_BOX(p) ((NovaValueBox *)((char *)(p) - offsetof(NovaValueBox, pv_vtbl)))
static HRESULT STDMETHODCALLTYPE pv_qi(void *p, REFIID iid, void **out) { return box_qi(PV_BOX(p), iid, out); }
static ULONG STDMETHODCALLTYPE pv_addref(void *p) { return box_addref(PV_BOX(p)); }
static ULONG STDMETHODCALLTYPE pv_release(void *p) { return box_release(PV_BOX(p)); }
static HRESULT STDMETHODCALLTYPE pv_iids(void *p, ULONG *c, IID **i) { return box_iids(PV_BOX(p), c, i); }
static HRESULT STDMETHODCALLTYPE pv_name(void *p, HSTRING *n) { return box_name(PV_BOX(p), n); }
static HRESULT STDMETHODCALLTYPE pv_trust(void *p, INT32 *l) { return box_trust(PV_BOX(p), l); }
static HRESULT STDMETHODCALLTYPE pv_type(void *, INT32 *type) { *type = 20; return S_OK; }   // PropertyType_OtherType
static HRESULT STDMETHODCALLTYPE pv_numeric(void *, BYTE *v) { *v = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE pv_int32(void *p, INT32 *v)
{
    NovaValueBox *b = PV_BOX(p);
    if (b->box->size != 4)
        return E_NOTIMPL;
    memcpy(v, b->value, 4);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_none() { return E_NOTIMPL; }

// IPropertyValue: IInspectable, get_Type, get_IsNumericScalar, GetUInt8,
// GetInt16, GetUInt16, GetInt32, GetUInt32, then 33 more getters
static void *const s_pv_vtbl[6 + 40] = {
    (void *)&pv_qi, (void *)&pv_addref, (void *)&pv_release, (void *)&pv_iids, (void *)&pv_name, (void *)&pv_trust,
    (void *)&pv_type, (void *)&pv_numeric, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none,
    (void *)&pv_int32, (void *)&pv_int32,
    (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none,
    (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none,
    (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none,
    (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none,
    (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none, (void *)&pv_none,
    (void *)&pv_none, (void *)&pv_none, (void *)&pv_none};
static void *const s_box_vtbl[7] = {(void *)&box_qi, (void *)&box_addref, (void *)&box_release, (void *)&box_iids,
                                    (void *)&box_name, (void *)&box_trust, (void *)&box_value};

// the framework's box of a delegate (the framework's face of the Windows
// delegate an app's code boxed): IReference<D>, its Value the delegate
struct NovaDelegateRef
{
    void *const *vtbl;
    LONG ref;
    const NovaDelegateBox *box;
    void *value;
};

static HRESULT STDMETHODCALLTYPE dref_qi(NovaDelegateRef *b, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, IID_IUnknown_) || IsEqualGUID(iid, IID_IInspectable_) || IsEqualGUID(iid, *b->box->framework) ||
        IsEqualGUID(iid, IID_IAgileObject_)) {
        *out = b;
        InterlockedIncrement(&b->ref);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dref_addref(NovaDelegateRef *b) { return InterlockedIncrement(&b->ref); }

static ULONG STDMETHODCALLTYPE dref_release(NovaDelegateRef *b)
{
    ULONG r = InterlockedDecrement(&b->ref);
    if (!r) {
        nova_release(b->value);
        CoTaskMemFree(b);
    }
    return r;
}

static HRESULT STDMETHODCALLTYPE dref_iids(NovaDelegateRef *b, ULONG *count, IID **iids)
{
    IID *a = (IID *)CoTaskMemAlloc(sizeof(IID));
    if (!a)
        return E_OUTOFMEMORY;
    *a = *b->box->framework;
    *count = 1;
    *iids = a;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dref_name(NovaDelegateRef *b, HSTRING *name)
{
    return WindowsCreateString(b->box->framework_name, (UINT32)wcslen(b->box->framework_name), name);
}

static HRESULT STDMETHODCALLTYPE dref_trust(NovaDelegateRef *, INT32 *level)
{
    *level = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dref_value(NovaDelegateRef *b, void **out)
{
    nova_addref(b->value);
    *out = b->value;
    return S_OK;
}

static void *const s_dref_vtbl[7] = {(void *)&dref_qi, (void *)&dref_addref, (void *)&dref_release, (void *)&dref_iids,
                                     (void *)&dref_name, (void *)&dref_trust, (void *)&dref_value};

// @a, if it is Windows' box of a Windows.UI.Xaml delegate (UIElement.
// AddHandler's handler, as C++/WinRT's box_value boxes it), boxed again as
// the framework's, its delegate the framework's face of the Windows one,
// into @out: S_OK; S_FALSE when it is no such box
static HRESULT rebox_delegate(void *a, void **out)
{
    for (unsigned i = 0; i < nova_delegate_box_count; i++) {
        const NovaDelegateBox *bx = &nova_delegate_boxes[i];
        const NovaFace *f;
        void *r, *d = nullptr;
        if (FAILED(qi(a, bx->windows, &r)))
            continue;
        HRESULT hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))VT(r)[6])(r, &d);
        nova_release(r);
        if (FAILED(hr))
            return hr;
        if (!d || !(f = find(*bx->delegate))) {
            nova_release(d);
            return E_NOINTERFACE;
        }
        NovaDelegateRef *b = (NovaDelegateRef *)CoTaskMemAlloc(sizeof(NovaDelegateRef));
        void *face = tear(f, d, TRUE);
        if (!b || !face) {
            CoTaskMemFree(b);
            nova_release(face);
            return E_OUTOFMEMORY;
        }
        b->vtbl = s_dref_vtbl;
        b->ref = 1;
        b->box = bx;
        b->value = face;
        *out = b;
        return S_OK;
    }
    return S_FALSE;
}

// @a, if it is a tear-off of a forward face (@reverse: a reverse one), as
// the IInspectable of the object it calls, into @out
static bool untear(void *a, BOOL reverse, void **out)
{
    if (VT(a)[0] != (void *)&nova_tear_qi)
        return false;
    NovaTear *t = (NovaTear *)a;
    if (!(t->face->flags & NOVA_FACE_REVERSE) != !reverse || (t->face->flags & NOVA_FACE_DELEGATE))
        return false;
    return SUCCEEDED(qi(t->obj, &IID_IInspectable_, out));
}

// @a, if it is a box of a Windows.UI.Xaml enum or struct on one side
// (@to_windows: the framework's), or Windows' box of a delegate, boxed
// again as the other side's, into @out: S_OK; S_FALSE when it is no such box
static HRESULT rebox(void *a, bool to_windows, void **out)
{
    void *pv;
    if (FAILED(qi(a, &IID_IPropertyValue_, &pv)))
        return S_FALSE;
    INT32 type = 0;
    ((HRESULT (STDMETHODCALLTYPE *)(void *, INT32 *))VT(pv)[6])(pv, &type);
    nova_release(pv);
    if (type != 20)                 // PropertyType_OtherType: perhaps a Windows.UI.Xaml enum, struct or delegate
        return S_FALSE;
    for (unsigned i = 0; i < nova_box_count; i++) {
        const NovaBox *bx = &nova_boxes[i];
        void *r;
        if (bx->size > sizeof(((NovaValueBox *)0)->value) || FAILED(qi(a, to_windows ? bx->framework : bx->windows, &r)))
            continue;
        NovaValueBox *b = (NovaValueBox *)CoTaskMemAlloc(sizeof(NovaValueBox));
        HRESULT hr = b ? ((HRESULT (STDMETHODCALLTYPE *)(void *, void *))VT(r)[6])(r, b->value) : E_OUTOFMEMORY;
        nova_release(r);
        if (FAILED(hr)) {
            CoTaskMemFree(b);
            return hr;
        }
        b->vtbl = s_box_vtbl;
        b->pv_vtbl = s_pv_vtbl;
        b->ref = 1;
        b->box = bx;
        b->windows = to_windows;
        *out = b;
        return S_OK;
    }
    return to_windows ? S_FALSE : rebox_delegate(a, out);
}

extern "C" HRESULT nova_object_in(void *a, void **out)
{
    *out = nullptr;
    if (!a)
        return S_OK;
    HRESULT hr = rebox(a, false, out);
    if (hr != S_FALSE)
        return hr;
    // (a face handed back as an object is the object it is a face of: a
    // framework object the framework knows by its own type)
    if (untear(a, FALSE, out))
        return S_OK;
    nova_addref(a);
    *out = a;
    return S_OK;
}

extern "C" HRESULT nova_object_out(HRESULT hr, void *o, void **a)
{
    if (FAILED(hr) || !a) {
        nova_release(o);
        return hr;
    }
    // (and the framework's face of an app's object is the app's object)
    HRESULT h = S_OK;
    if (!o)
        *a = nullptr;
    else if (!untear(o, TRUE, a)) {
        nova_addref(o);
        *a = o;
    }
    nova_release(o);
    return FAILED(h) ? h : hr;
}

extern "C" HRESULT nova_object_to_app(void *a, void **out)
{
    *out = nullptr;
    if (!a)
        return S_OK;
    HRESULT hr = rebox(a, true, out);
    if (hr != S_FALSE)
        return hr;
    nova_addref(a);
    *out = a;
    return S_OK;
}

struct NovaTypeName
{
    HSTRING Name;
    INT32 Kind;     // TypeKind: Primitive 0, Metadata 1, Custom 2
};

// @name less @from, if what follows is one of the framework's type names
// with a Windows.UI.Xaml one
static HSTRING renamed(HSTRING name, const wchar_t *from, const wchar_t *to)
{
    UINT32 n, fn = (UINT32)wcslen(from), tn = (UINT32)wcslen(to);
    const wchar_t *s = WindowsGetStringRawBuffer(name, &n);
    if (n <= fn || wcsncmp(s, from, fn))
        return nullptr;
    const wchar_t *rest = s + fn;
    unsigned lo = 0, hi = nova_xaml_name_count;
    bool found = false;
    while (lo < hi && !found) {
        unsigned mid = (lo + hi) / 2;
        int c = wcscmp(nova_xaml_names[mid], rest);
        if (c == 0)
            found = true;
        else if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    wchar_t buf[256];
    if (!found || n - fn + tn >= ARRAYSIZE(buf))
        return nullptr;
    memcpy(buf, to, tn * sizeof(wchar_t));
    memcpy(buf + tn, rest, (n - fn) * sizeof(wchar_t));
    HSTRING h = nullptr;
    WindowsCreateString(buf, n - fn + tn, &h);
    return h;
}

extern "C" void nova_typename(void *tn, void **made, int to_framework)
{
    NovaTypeName *t = (NovaTypeName *)tn;
    *made = nullptr;
    // (a Metadata type's name; and a Primitive one's, as a WinUI 2 provider
    // names the system types its types derive from, Windows.UI.Xaml.Media.
    // XamlCompositionBrushBase under AcrylicBrush: the framework looks a
    // primitive up by its full name when it is no primitive it knows.
    // renamed() renames only the framework's own names, so Int32 and
    // String stay as they are)
    if ((t->Kind != 0 && t->Kind != 1) || !t->Name)
        return;
    HSTRING h = to_framework ? renamed(t->Name, L"Windows.UI.Xaml.", L"Microsoft.UI.Xaml.")
                             : renamed(t->Name, L"Microsoft.UI.Xaml.", L"Windows.UI.Xaml.");
    if (h) {
        t->Name = h;
        *made = h;
    }
}

extern "C" void nova_typename_out(void *tn, int to_framework)
{
    NovaTypeName *t = (NovaTypeName *)tn;
    HSTRING old = t->Name;
    void *made;
    nova_typename(t, &made, to_framework);
    if (made)
        WindowsDeleteString(old);
}

static const char *s_reported[512];
static unsigned s_count;
static SRWLOCK s_lock = SRWLOCK_INIT;

extern "C" HRESULT nova_face_missing(const char *name)
{
    AcquireSRWLockExclusive(&s_lock);
    bool seen = false;
    for (unsigned i = 0; i < s_count && !seen; i++)
        seen = s_reported[i] == name;
    if (!seen && s_count < ARRAYSIZE(s_reported))
        s_reported[s_count++] = name;
    ReleaseSRWLockExclusive(&s_lock);
    if (!seen) {
        char line[512];
        snprintf(line, sizeof(line), "Windows.UI.Xaml: %s has no counterpart in the framework yet\n", name);
        OutputDebugStringA(line);
        fputs(line, stdout);
        fflush(stdout);
    }
    return E_NOTIMPL;
}

// The handlers of the events NovaOS never raises (winfaces.py's
// NEVER_RAISED: a UWP app's suspending and resuming), kept, as an event
// source keeps them, until they are removed
static struct { INT64 token; IUnknown *handler; } s_kept[64];
static INT64 s_next_token;

extern "C" HRESULT nova_face_event_kept(IUnknown *handler, INT64 *token)
{
    if (!handler || !token)
        return E_POINTER;
    AcquireSRWLockExclusive(&s_lock);
    unsigned i = 0;
    while (i < ARRAYSIZE(s_kept) && s_kept[i].handler)
        i++;
    if (i < ARRAYSIZE(s_kept)) {
        handler->AddRef();
        s_kept[i].handler = handler;
        s_kept[i].token = *token = ++s_next_token;
    }
    ReleaseSRWLockExclusive(&s_lock);
    return i < ARRAYSIZE(s_kept) ? S_OK : E_OUTOFMEMORY;
}

extern "C" HRESULT nova_face_event_removed(INT64 token)
{
    IUnknown *handler = nullptr;
    AcquireSRWLockExclusive(&s_lock);
    for (auto &k : s_kept)
        if (k.handler && k.token == token) {
            handler = k.handler;
            k.handler = nullptr;
        }
    ReleaseSRWLockExclusive(&s_lock);
    if (handler)
        handler->Release();
    return S_OK;
}

// The hooks NovaOS's ComObjectBase.cpp calls
// a Windows.UI.Xaml interface @iid of a framework object that has none of
// its own: the forward face over the framework interface @impl (the
// object's own query, which the face's target goes through) gives
extern "C" HRESULT nova_xaml_face_query(REFIID iid, void **out, HRESULT (*impl)(void *, REFIID, void **), void *ctx)
{
    const NovaFace *f = find(iid);
    if (!f || (f->flags & NOVA_FACE_REVERSE))
        return E_NOINTERFACE;
    void *p = nullptr;
    if (FAILED(impl(ctx, *f->target, &p)) || !p)
        return E_NOINTERFACE;
    *out = tear(f, p, TRUE);
    return *out ? S_OK : E_OUTOFMEMORY;
}

// Windows.UI.Xaml classes the framework (WinUI 3's) does not have, whose
// WinUI 2 class is the same code (Windows builds WinUI 2's Materials into
// the system as Windows.UI.Xaml.Media's): WinUI 2's theme resources make
// an AcrylicBrush in the presentation namespace, which on NovaOS is
// WinUI 2's own, and XamlControlsResources then asks it for Windows'
// IAcrylicBrush.  The interfaces are the same methods in the same order
// (the enum AcrylicBackgroundSource has the same values), so the outer's
// WinUI 2 interface answers for Windows'.
static const struct { GUID windows, winui2; } s_winui2_aliases[] = {
    // Windows.UI.Xaml.Media.IAcrylicBrush, Microsoft.UI.Xaml.Media.IAcrylicBrush
    {{0x79bbcf4e, 0xcd66, 0x4f1b, {0xa8, 0xb6, 0xcd, 0x6d, 0x29, 0x77, 0xc1, 0x8d}},
     {0x26861c81, 0x72f4, 0x5065, {0xb1, 0x44, 0xe9, 0xec, 0x7f, 0xce, 0x86, 0xa9}}},
    // IAcrylicBrush2 (TintLuminosityOpacity)
    {{0xc9645383, 0xb19e, 0x5ac0, {0x86, 0xff, 0x3d, 0x90, 0x50, 0x6d, 0xbc, 0xda}},
     {0x23fad570, 0x43ed, 0x5a73, {0x9d, 0xe7, 0xa3, 0x03, 0x55, 0x3d, 0x54, 0x14}}},
};

// a framework interface @iid a composed object has no answer of its own
// for, which its outer implements as Windows' (a WinUI 2 app's Application
// is its IXamlMetadataProvider, which XAML's metadata asks the app's
// Application for): the reverse face over the outer's Windows interface
// (or, for an alias above, the outer's WinUI 2 interface)
extern "C" HRESULT nova_xaml_outer_face(REFIID iid, IInspectable *outer, void **out)
{
    if (outer) {
        for (const auto &a : s_winui2_aliases) {
            if (IsEqualGUID(iid, a.windows))
                return qi(outer, &a.winui2, out);
        }
    }
    const NovaFace *f = find(iid);
    if (!f || !(f->flags & NOVA_FACE_REVERSE) || (f->flags & (NOVA_FACE_OVERRIDE | NOVA_FACE_DELEGATE)) || !outer)
        return E_NOINTERFACE;
    void *w;
    if (FAILED(qi(outer, f->target, &w)))
        return E_NOINTERFACE;
    *out = tear(f, w, TRUE);
    return *out ? S_OK : E_OUTOFMEMORY;
}

// a framework overridable interface @iid of an object composed by @outer
// (asked of the object itself, so the outer did not answer it as the
// framework's): the outer's Windows overrides, which call the framework's
// own back when the app does not override them
extern "C" HRESULT nova_xaml_override_face(REFIID iid, IInspectable *outer, void **out)
{
    const NovaFace *f = find(iid);
    if (!f || !(f->flags & NOVA_FACE_OVERRIDE) || !outer)
        return E_NOINTERFACE;
    void *w;
    if (FAILED(qi(outer, f->target, &w)))
        return E_NOINTERFACE;
    nova_release(w);
    outer->AddRef();
    *out = tear(f, outer, FALSE);
    return *out ? S_OK : E_OUTOFMEMORY;
}
