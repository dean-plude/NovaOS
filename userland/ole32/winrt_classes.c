/*
 * The few Windows Runtime classes NovaOS answers for.  A class Windows
 * always has must activate even when NovaOS has nothing behind it: a
 * program asks it whether the feature is there, and only then decides not
 * to use it.  Every object here is static and agile (usable from any
 * thread), so reference counts are kept only for show.
 *
 * Windows.Security.Credentials.KeyCredentialManager (Windows Hello):
 * IsSupportedAsync completes at once with false.  KeePassXC asks it from a
 * PPL task when it starts; a failed activation there is an exception no
 * one observes, which ends the program.
 *
 * Windows.Foundation.Metadata.ApiInformation: which API contracts, and
 * which runtime classes, this Windows has.  NovaOS reports Windows 10 22H2
 * (build 19045), whose SDK is 10.0.19041: Windows.Foundation.
 * UniversalApiContract 10.0 and FoundationContract 4.0.  A type is present
 * when it is a class NovaOS can activate (here, or one the registry names);
 * interfaces, structs, enums and members are reported absent, as Windows
 * reports what its metadata (System32\WinMetadata, which NovaOS does not
 * ship) lacks.  Godot 4 asks IsApiContractPresent before it uses the
 * DispatcherQueue's DisplayInformation, outside any try: a failed
 * activation there ends the program.
 *
 * UISettings and UIViewSettings (Windows.UI.ViewManagement): uisettings.c.
 */
#define NOVA_BUILD_OLE32
#include <objbase.h>

typedef struct HSTRING_ *HSTRING;
HRESULT WINAPI WindowsCreateString(const WCHAR *src, UINT32 len, HSTRING *out);     /* winrt.c */
const WCHAR *WINAPI WindowsGetStringRawBuffer(HSTRING h, UINT32 *len);               /* winrt.c */
BOOL ole32_winrt_registered(const WCHAR *name, UINT32 len);                         /* winrt.c */
HRESULT ole32_winrt_factory(const WCHAR *name, UINT32 len, REFIID iid, void **out);

static const GUID IID_IUnknown_w       = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_w   = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_w   = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IActivationFactory_w = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IAsyncInfo_w     = { 0x00000036, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
/* IAsyncOperation<Boolean> */
static const GUID IID_IAsyncOpBool_w   = { 0xCDB5EFB3, 0x5788, 0x509D, { 0x9B, 0xE1, 0x71, 0xCC, 0xB8, 0xA3, 0x36, 0x2A } };
static const GUID IID_IApiInformationStatics_w = { 0x997439FE, 0xF681, 0x4A11, { 0xB4, 0x16, 0xC1, 0x3A, 0x47, 0xE8, 0xBA, 0x36 } };
static const GUID IID_IKeyCredentialManagerStatics_w = { 0x6AAC468B, 0x0EF1, 0x4CE0, { 0x82, 0x90, 0x41, 0x06, 0xDA, 0x6A, 0x63, 0xB5 } };

#define WRT_E_NOTIMPL ((HRESULT)0x80004001L)
enum { AsyncCompleted = 1 };

static HRESULT STDMETHODCALLTYPE no_iids(void *o, ULONG *n, GUID **iids) { (void)o; *n = 0; *iids = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE base_trust(void *o, int *t) { (void)o; *t = 0; return S_OK; }   /* BaseTrust */
static ULONG STDMETHODCALLTYPE static_ref(void *o) { (void)o; return 2; }

/* ---- IAsyncOperation<bool>, already completed with false ---------------- */
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG   (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *, ULONG *, GUID **);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *, HSTRING *);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *, int *);
} InspVtbl;

typedef struct { InspVtbl i; HRESULT (STDMETHODCALLTYPE *put_Completed)(void *, IUnknown *);
                 HRESULT (STDMETHODCALLTYPE *get_Completed)(void *, IUnknown **);
                 HRESULT (STDMETHODCALLTYPE *GetResults)(void *, BOOLEAN *); } OpBoolVtbl;
typedef struct { InspVtbl i; HRESULT (STDMETHODCALLTYPE *get_Id)(void *, UINT32 *);
                 HRESULT (STDMETHODCALLTYPE *get_Status)(void *, int *);
                 HRESULT (STDMETHODCALLTYPE *get_ErrorCode)(void *, HRESULT *);
                 HRESULT (STDMETHODCALLTYPE *Cancel)(void *);
                 HRESULT (STDMETHODCALLTYPE *Close)(void *); } AsyncInfoVtbl;

typedef struct { const OpBoolVtbl *op; const AsyncInfoVtbl *info; } FalseOp;
static FalseOp g_false_op;

static HRESULT STDMETHODCALLTYPE op_qi(void *o, REFIID iid, void **out)
{
    (void)o;
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_w) || IsEqualGUID(iid, &IID_IInspectable_w) ||
        IsEqualGUID(iid, &IID_IAgileObject_w) || IsEqualGUID(iid, &IID_IAsyncOpBool_w)) { *out = &g_false_op.op; return S_OK; }
    if (IsEqualGUID(iid, &IID_IAsyncInfo_w)) { *out = &g_false_op.info; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE op_name(void *o, HSTRING *n)
{
    (void)o;
    static const WCHAR name[] = L"Windows.Foundation.IAsyncOperation`1<Boolean>";
    return WindowsCreateString(name, (UINT32)(sizeof(name) / sizeof(WCHAR) - 1), n);
}
/* The handler (AsyncOperationCompletedHandler<bool>) is called at once:
 * Invoke(this, AsyncStatus.Completed) is the slot after IUnknown's */
static HRESULT STDMETHODCALLTYPE op_put_completed(void *o, IUnknown *h)
{
    (void)o;
    if (!h) return E_POINTER;
    typedef HRESULT (STDMETHODCALLTYPE *Invoke)(IUnknown *, void *, int);
    Invoke inv = (Invoke)((void **)h->lpVtbl)[3];
    inv(h, &g_false_op.op, AsyncCompleted);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE op_get_completed(void *o, IUnknown **h) { (void)o; if (h) *h = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE op_results(void *o, BOOLEAN *r) { (void)o; if (!r) return E_POINTER; *r = FALSE; return S_OK; }
static HRESULT STDMETHODCALLTYPE info_id(void *o, UINT32 *id) { (void)o; *id = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE info_status(void *o, int *s) { (void)o; *s = AsyncCompleted; return S_OK; }
static HRESULT STDMETHODCALLTYPE info_error(void *o, HRESULT *e) { (void)o; *e = S_OK; return S_OK; }
static HRESULT STDMETHODCALLTYPE info_nop(void *o) { (void)o; return S_OK; }

static const OpBoolVtbl g_op_vtbl = {
    { op_qi, static_ref, static_ref, no_iids, op_name, base_trust },
    op_put_completed, op_get_completed, op_results,
};
static const AsyncInfoVtbl g_info_vtbl = {
    { op_qi, static_ref, static_ref, no_iids, op_name, base_trust },
    info_id, info_status, info_error, info_nop, info_nop,
};
static FalseOp g_false_op = { &g_op_vtbl, &g_info_vtbl };

/* ---- KeyCredentialManager statics -------------------------------------- */
typedef struct { InspVtbl i;
                 HRESULT (STDMETHODCALLTYPE *IsSupportedAsync)(void *, void **);
                 HRESULT (STDMETHODCALLTYPE *RenewAttestationAsync)(void *, void **);
                 HRESULT (STDMETHODCALLTYPE *RequestCreateAsync)(void *, HSTRING, int, void **);
                 HRESULT (STDMETHODCALLTYPE *OpenAsync)(void *, HSTRING, void **);
                 HRESULT (STDMETHODCALLTYPE *DeleteAsync)(void *, HSTRING, void **); } KcmVtbl;
typedef struct { const KcmVtbl *v; } Kcm;
static Kcm g_kcm;

static HRESULT STDMETHODCALLTYPE kcm_qi(void *o, REFIID iid, void **out)
{
    (void)o;
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_w) || IsEqualGUID(iid, &IID_IInspectable_w) ||
        IsEqualGUID(iid, &IID_IAgileObject_w) || IsEqualGUID(iid, &IID_IActivationFactory_w) ||
        IsEqualGUID(iid, &IID_IKeyCredentialManagerStatics_w)) { *out = &g_kcm; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE kcm_name(void *o, HSTRING *n)
{
    (void)o;
    static const WCHAR name[] = L"Windows.Security.Credentials.KeyCredentialManager";
    return WindowsCreateString(name, (UINT32)(sizeof(name) / sizeof(WCHAR) - 1), n);
}
static HRESULT STDMETHODCALLTYPE kcm_supported(void *o, void **op) { (void)o; if (!op) return E_POINTER; *op = &g_false_op.op; return S_OK; }
static HRESULT STDMETHODCALLTYPE kcm_renew(void *o, void **op) { (void)o; if (op) *op = 0; return WRT_E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE kcm_create(void *o, HSTRING n, int opt, void **op) { (void)o; (void)n; (void)opt; if (op) *op = 0; return WRT_E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE kcm_open(void *o, HSTRING n, void **op) { (void)o; (void)n; if (op) *op = 0; return WRT_E_NOTIMPL; }
static const KcmVtbl g_kcm_vtbl = {
    { kcm_qi, static_ref, static_ref, no_iids, kcm_name, base_trust },
    kcm_supported, kcm_renew, kcm_create, kcm_open, kcm_open,
};
static Kcm g_kcm = { &g_kcm_vtbl };

/* ---- ApiInformation statics ------------------------------------------- */
typedef struct { InspVtbl i; HRESULT (STDMETHODCALLTYPE *ActivateInstance)(void *, void **); } FactoryVtbl;
typedef struct { InspVtbl i;
                 HRESULT (STDMETHODCALLTYPE *IsTypePresent)(void *, HSTRING, BOOLEAN *);
                 HRESULT (STDMETHODCALLTYPE *IsMethodPresent)(void *, HSTRING, HSTRING, BOOLEAN *);
                 HRESULT (STDMETHODCALLTYPE *IsMethodPresentWithArity)(void *, HSTRING, HSTRING, UINT32, BOOLEAN *);
                 HRESULT (STDMETHODCALLTYPE *IsEventPresent)(void *, HSTRING, HSTRING, BOOLEAN *);
                 HRESULT (STDMETHODCALLTYPE *IsPropertyPresent)(void *, HSTRING, HSTRING, BOOLEAN *);
                 HRESULT (STDMETHODCALLTYPE *IsReadOnlyPropertyPresent)(void *, HSTRING, HSTRING, BOOLEAN *);
                 HRESULT (STDMETHODCALLTYPE *IsWriteablePropertyPresent)(void *, HSTRING, HSTRING, BOOLEAN *);
                 HRESULT (STDMETHODCALLTYPE *IsEnumNamedValuePresent)(void *, HSTRING, HSTRING, BOOLEAN *);
                 HRESULT (STDMETHODCALLTYPE *IsApiContractPresentByMajor)(void *, HSTRING, UINT16, BOOLEAN *);
                 HRESULT (STDMETHODCALLTYPE *IsApiContractPresentByMajorAndMinor)(void *, HSTRING, UINT16, UINT16, BOOLEAN *); } ApiInfoVtbl;
typedef struct { const FactoryVtbl *f; const ApiInfoVtbl *s; } ApiInfo;
static ApiInfo g_apiinfo;

static BOOL same(const WCHAR *name, UINT32 len, const WCHAR *want)
{
    UINT32 i = 0;
    while (i < len && want[i] && name[i] == want[i]) i++;
    return i == len && !want[i];
}

static HRESULT STDMETHODCALLTYPE ai_qi(void *o, REFIID iid, void **out)
{
    (void)o;
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_w) || IsEqualGUID(iid, &IID_IInspectable_w) ||
        IsEqualGUID(iid, &IID_IAgileObject_w) || IsEqualGUID(iid, &IID_IActivationFactory_w)) { *out = &g_apiinfo.f; return S_OK; }
    if (IsEqualGUID(iid, &IID_IApiInformationStatics_w)) { *out = &g_apiinfo.s; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE ai_name(void *o, HSTRING *n)
{
    (void)o;
    static const WCHAR name[] = L"Windows.Foundation.Metadata.ApiInformation";
    return WindowsCreateString(name, (UINT32)(sizeof(name) / sizeof(WCHAR) - 1), n);
}
/* A static class: no instances of its own */
static HRESULT STDMETHODCALLTYPE ai_activate(void *o, void **out) { (void)o; if (out) *out = 0; return WRT_E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE ai_type(void *o, HSTRING type, BOOLEAN *r)
{
    (void)o;
    if (!r) return E_POINTER;
    UINT32 len;
    const WCHAR *name = WindowsGetStringRawBuffer(type, &len);
    *r = FALSE;
    if (!len) return E_INVALIDARG;
    IUnknown *f = 0;
    if (SUCCEEDED(ole32_winrt_factory(name, len, &IID_IActivationFactory_w, (void **)&f))) {
        f->lpVtbl->Release(f);
        *r = TRUE;
    } else
        *r = (BOOLEAN)ole32_winrt_registered(name, len);
    return S_OK;
}
/* Members are only known from metadata */
static HRESULT STDMETHODCALLTYPE ai_member(void *o, HSTRING type, HSTRING member, BOOLEAN *r)
{
    (void)o; (void)member;
    if (!r) return E_POINTER;
    *r = FALSE;
    return WindowsGetStringRawBuffer(type, 0)[0] ? S_OK : E_INVALIDARG;
}
static HRESULT STDMETHODCALLTYPE ai_arity(void *o, HSTRING type, HSTRING member, UINT32 n, BOOLEAN *r)
{
    (void)n;
    return ai_member(o, type, member, r);
}

/* The contracts of the Windows 10.0.19041 SDK that NovaOS stands for */
static const struct { const WCHAR *name; UINT16 major, minor; } g_contracts[] = {
    { L"Windows.Foundation.UniversalApiContract", 10, 0 },
    { L"Windows.Foundation.FoundationContract", 4, 0 },
};
static HRESULT STDMETHODCALLTYPE ai_contract_minor(void *o, HSTRING contract, UINT16 major, UINT16 minor, BOOLEAN *r)
{
    (void)o;
    if (!r) return E_POINTER;
    UINT32 len;
    const WCHAR *name = WindowsGetStringRawBuffer(contract, &len);
    *r = FALSE;
    if (!len) return E_INVALIDARG;
    for (UINT32 i = 0; i < sizeof g_contracts / sizeof g_contracts[0]; i++)
        if (same(name, len, g_contracts[i].name))
            *r = major < g_contracts[i].major || (major == g_contracts[i].major && minor <= g_contracts[i].minor);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ai_contract(void *o, HSTRING contract, UINT16 major, BOOLEAN *r)
{
    return ai_contract_minor(o, contract, major, 0, r);
}
static const FactoryVtbl g_aif_vtbl = { { ai_qi, static_ref, static_ref, no_iids, ai_name, base_trust }, ai_activate };
static const ApiInfoVtbl g_ais_vtbl = {
    { ai_qi, static_ref, static_ref, no_iids, ai_name, base_trust },
    ai_type, ai_member, ai_arity, ai_member, ai_member, ai_member, ai_member, ai_member,
    ai_contract, ai_contract_minor,
};
static ApiInfo g_apiinfo = { &g_aif_vtbl, &g_ais_vtbl };

HRESULT ole32_uisettings_factory(const WCHAR *name, UINT32 len, REFIID iid, void **out);    /* uisettings.c */

/* The activation factory for runtime class @name (@len characters), or
 * REGDB_E_CLASSNOTREG, which is logged: the class a program needs next */
HRESULT ole32_winrt_factory(const WCHAR *name, UINT32 len, REFIID iid, void **out)
{
    static const WCHAR kcm[] = L"Windows.Security.Credentials.KeyCredentialManager";
    UINT32 n = (UINT32)(sizeof(kcm) / sizeof(WCHAR) - 1), i = 0;
    if (!out) return E_POINTER;
    if (len == n) while (i < n && name[i] == kcm[i]) i++;
    if (len == n && i == n) return kcm_qi(&g_kcm, iid, out);
    if (same(name, len, L"Windows.Foundation.Metadata.ApiInformation")) return ai_qi(&g_apiinfo, iid, out);
    HRESULT hr = ole32_uisettings_factory(name, len, iid, out);
    if (hr != REGDB_E_CLASSNOTREG) return hr;
    *out = 0;
    return REGDB_E_CLASSNOTREG;                 /* (winrt.c asks the registry next) */
}
