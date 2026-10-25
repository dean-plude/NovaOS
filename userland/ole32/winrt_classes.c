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
 * Windows.UI.Colors: the named colours' statics (WinUI 2's Mica and
 * BackdropMaterial take their fallback colours from it).
 *
 * UISettings, UIViewSettings and AccessibilitySettings
 * (Windows.UI.ViewManagement): uisettings.c.
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

/* ---- Windows.UI.Colors: the named colours (IColorsStatics, AliceBlue to
 * YellowGreen in its order, the ARGB values of the CSS/X11 names) -------- */
static const GUID IID_IColorsStatics_w = { 0xCFF52E04, 0xCCA6, 0x4614, { 0xA1, 0x7E, 0x75, 0x49, 0x10, 0xC8, 0x4A, 0x99 } };
static const UINT32 g_named_colors[141] = {
    0xFFF0F8FF, 0xFFFAEBD7, 0xFF00FFFF, 0xFF7FFFD4, 0xFFF0FFFF, 0xFFF5F5DC,
    0xFFFFE4C4, 0xFF000000, 0xFFFFEBCD, 0xFF0000FF, 0xFF8A2BE2, 0xFFA52A2A,
    0xFFDEB887, 0xFF5F9EA0, 0xFF7FFF00, 0xFFD2691E, 0xFFFF7F50, 0xFF6495ED,
    0xFFFFF8DC, 0xFFDC143C, 0xFF00FFFF, 0xFF00008B, 0xFF008B8B, 0xFFB8860B,
    0xFFA9A9A9, 0xFF006400, 0xFFBDB76B, 0xFF8B008B, 0xFF556B2F, 0xFFFF8C00,
    0xFF9932CC, 0xFF8B0000, 0xFFE9967A, 0xFF8FBC8F, 0xFF483D8B, 0xFF2F4F4F,
    0xFF00CED1, 0xFF9400D3, 0xFFFF1493, 0xFF00BFFF, 0xFF696969, 0xFF1E90FF,
    0xFFB22222, 0xFFFFFAF0, 0xFF228B22, 0xFFFF00FF, 0xFFDCDCDC, 0xFFF8F8FF,
    0xFFFFD700, 0xFFDAA520, 0xFF808080, 0xFF008000, 0xFFADFF2F, 0xFFF0FFF0,
    0xFFFF69B4, 0xFFCD5C5C, 0xFF4B0082, 0xFFFFFFF0, 0xFFF0E68C, 0xFFE6E6FA,
    0xFFFFF0F5, 0xFF7CFC00, 0xFFFFFACD, 0xFFADD8E6, 0xFFF08080, 0xFFE0FFFF,
    0xFFFAFAD2, 0xFF90EE90, 0xFFD3D3D3, 0xFFFFB6C1, 0xFFFFA07A, 0xFF20B2AA,
    0xFF87CEFA, 0xFF778899, 0xFFB0C4DE, 0xFFFFFFE0, 0xFF00FF00, 0xFF32CD32,
    0xFFFAF0E6, 0xFFFF00FF, 0xFF800000, 0xFF66CDAA, 0xFF0000CD, 0xFFBA55D3,
    0xFF9370DB, 0xFF3CB371, 0xFF7B68EE, 0xFF00FA9A, 0xFF48D1CC, 0xFFC71585,
    0xFF191970, 0xFFF5FFFA, 0xFFFFE4E1, 0xFFFFE4B5, 0xFFFFDEAD, 0xFF000080,
    0xFFFDF5E6, 0xFF808000, 0xFF6B8E23, 0xFFFFA500, 0xFFFF4500, 0xFFDA70D6,
    0xFFEEE8AA, 0xFF98FB98, 0xFFAFEEEE, 0xFFDB7093, 0xFFFFEFD5, 0xFFFFDAB9,
    0xFFCD853F, 0xFFFFC0CB, 0xFFDDA0DD, 0xFFB0E0E6, 0xFF800080, 0xFFFF0000,
    0xFFBC8F8F, 0xFF4169E1, 0xFF8B4513, 0xFFFA8072, 0xFFF4A460, 0xFF2E8B57,
    0xFFFFF5EE, 0xFFA0522D, 0xFFC0C0C0, 0xFF87CEEB, 0xFF6A5ACD, 0xFF708090,
    0xFFFFFAFA, 0xFF00FF7F, 0xFF4682B4, 0xFFD2B48C, 0xFF008080, 0xFFD8BFD8,
    0xFFFF6347, 0x00FFFFFF, 0xFF40E0D0, 0xFFEE82EE, 0xFFF5DEB3, 0xFFFFFFFF,
    0xFFF5F5F5, 0xFFFFFF00, 0xFF9ACD32,
};
typedef struct { BYTE A, R, G, B; } WColor;         /* Windows.UI.Color */
#define C(i) static HRESULT STDMETHODCALLTYPE color_##i(void *o, WColor *c) \
    { (void)o; if (!c) return E_POINTER; UINT32 v = g_named_colors[i]; \
      c->A = (BYTE)(v >> 24); c->R = (BYTE)(v >> 16); c->G = (BYTE)(v >> 8); c->B = (BYTE)v; return S_OK; }
    C(0) C(1) C(2) C(3) C(4) C(5) C(6) C(7)
    C(8) C(9) C(10) C(11) C(12) C(13) C(14) C(15)
    C(16) C(17) C(18) C(19) C(20) C(21) C(22) C(23)
    C(24) C(25) C(26) C(27) C(28) C(29) C(30) C(31)
    C(32) C(33) C(34) C(35) C(36) C(37) C(38) C(39)
    C(40) C(41) C(42) C(43) C(44) C(45) C(46) C(47)
    C(48) C(49) C(50) C(51) C(52) C(53) C(54) C(55)
    C(56) C(57) C(58) C(59) C(60) C(61) C(62) C(63)
    C(64) C(65) C(66) C(67) C(68) C(69) C(70) C(71)
    C(72) C(73) C(74) C(75) C(76) C(77) C(78) C(79)
    C(80) C(81) C(82) C(83) C(84) C(85) C(86) C(87)
    C(88) C(89) C(90) C(91) C(92) C(93) C(94) C(95)
    C(96) C(97) C(98) C(99) C(100) C(101) C(102) C(103)
    C(104) C(105) C(106) C(107) C(108) C(109) C(110) C(111)
    C(112) C(113) C(114) C(115) C(116) C(117) C(118) C(119)
    C(120) C(121) C(122) C(123) C(124) C(125) C(126) C(127)
    C(128) C(129) C(130) C(131) C(132) C(133) C(134) C(135)
    C(136) C(137) C(138) C(139) C(140)
#undef C
typedef struct { InspVtbl i; HRESULT (STDMETHODCALLTYPE *get[141])(void *, WColor *); } ColorsVtbl;
typedef struct { const FactoryVtbl *f; const ColorsVtbl *s; } Colors;
static Colors g_colors;
static HRESULT STDMETHODCALLTYPE colors_qi(void *o, REFIID iid, void **out)
{
    (void)o;
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_w) || IsEqualGUID(iid, &IID_IInspectable_w) ||
        IsEqualGUID(iid, &IID_IAgileObject_w) || IsEqualGUID(iid, &IID_IActivationFactory_w)) { *out = &g_colors.f; return S_OK; }
    if (IsEqualGUID(iid, &IID_IColorsStatics_w)) { *out = &g_colors.s; return S_OK; }
    *out = 0;
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE colors_name(void *o, HSTRING *n)
{
    (void)o;
    static const WCHAR name[] = L"Windows.UI.Colors";
    return WindowsCreateString(name, (UINT32)(sizeof(name) / sizeof(WCHAR) - 1), n);
}
static const FactoryVtbl g_colorsf_vtbl = { { colors_qi, static_ref, static_ref, no_iids, colors_name, base_trust }, ai_activate };
#define C(i) color_##i,
static const ColorsVtbl g_colorss_vtbl = { { colors_qi, static_ref, static_ref, no_iids, colors_name, base_trust }, {
    C(0) C(1) C(2) C(3) C(4) C(5) C(6) C(7)
    C(8) C(9) C(10) C(11) C(12) C(13) C(14) C(15)
    C(16) C(17) C(18) C(19) C(20) C(21) C(22) C(23)
    C(24) C(25) C(26) C(27) C(28) C(29) C(30) C(31)
    C(32) C(33) C(34) C(35) C(36) C(37) C(38) C(39)
    C(40) C(41) C(42) C(43) C(44) C(45) C(46) C(47)
    C(48) C(49) C(50) C(51) C(52) C(53) C(54) C(55)
    C(56) C(57) C(58) C(59) C(60) C(61) C(62) C(63)
    C(64) C(65) C(66) C(67) C(68) C(69) C(70) C(71)
    C(72) C(73) C(74) C(75) C(76) C(77) C(78) C(79)
    C(80) C(81) C(82) C(83) C(84) C(85) C(86) C(87)
    C(88) C(89) C(90) C(91) C(92) C(93) C(94) C(95)
    C(96) C(97) C(98) C(99) C(100) C(101) C(102) C(103)
    C(104) C(105) C(106) C(107) C(108) C(109) C(110) C(111)
    C(112) C(113) C(114) C(115) C(116) C(117) C(118) C(119)
    C(120) C(121) C(122) C(123) C(124) C(125) C(126) C(127)
    C(128) C(129) C(130) C(131) C(132) C(133) C(134) C(135)
    C(136) C(137) C(138) C(139) C(140)
} };
#undef C
static Colors g_colors = { &g_colorsf_vtbl, &g_colorss_vtbl };

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
    if (same(name, len, L"Windows.UI.Colors")) return colors_qi(&g_colors, iid, out);
    HRESULT hr = ole32_uisettings_factory(name, len, iid, out);
    if (hr != REGDB_E_CLASSNOTREG) return hr;
    *out = 0;
    return REGDB_E_CLASSNOTREG;                 /* (winrt.c asks the registry next) */
}
