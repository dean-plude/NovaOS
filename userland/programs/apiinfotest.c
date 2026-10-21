/* apiinfotest.exe — Windows.Foundation.Metadata.ApiInformation (ole32), the
 * class programs ask which Windows Runtime API contracts and types this
 * Windows has before they use them (Godot 4 asks it, outside any try,
 * before it looks for a window's DisplayInformation).  NovaOS reports
 * Windows 10 22H2, so UniversalApiContract up to 10.0 and
 * FoundationContract up to 4.0 are present and later versions are not;
 * a class NovaOS activates (ole32's own, or one the registry names) is a
 * present type, an unknown one is not; members are reported absent; the
 * factory makes no instances.
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <roapi.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define VT(o, i, T) ((T)((void **)*(void **)(o))[i])
typedef ULONG (STDMETHODCALLTYPE *Rel_t)(void *);
typedef HRESULT (STDMETHODCALLTYPE *QI_t)(void *, const GUID *, void **);
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)

static const GUID IID_IApiInformationStatics_ = { 0x997439FE, 0xF681, 0x4A11, { 0xB4, 0x16, 0xC1, 0x3A, 0x47, 0xE8, 0xBA, 0x36 } };
static const GUID IID_IActivationFactory_ = { 0x00000035, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };

/* IApiInformationStatics' slots (IInspectable's six first) */
enum { A_TYPE = 6, A_METHOD, A_METHOD_ARITY, A_EVENT, A_PROPERTY, A_RO_PROPERTY, A_RW_PROPERTY, A_ENUM_VALUE,
       A_CONTRACT, A_CONTRACT_MINOR };
typedef HRESULT (STDMETHODCALLTYPE *Type_t)(void *, HSTRING, BOOLEAN *);
typedef HRESULT (STDMETHODCALLTYPE *Member_t)(void *, HSTRING, HSTRING, BOOLEAN *);
typedef HRESULT (STDMETHODCALLTYPE *Arity_t)(void *, HSTRING, HSTRING, UINT32, BOOLEAN *);
typedef HRESULT (STDMETHODCALLTYPE *Contract_t)(void *, HSTRING, UINT16, BOOLEAN *);
typedef HRESULT (STDMETHODCALLTYPE *ContractMinor_t)(void *, HSTRING, UINT16, UINT16, BOOLEAN *);

static HSTRING hs(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, (UINT32)wcslen(s), &h);
    return h;
}

static void *st;

/* 1 present, 0 absent, -1 the call failed */
static int contract(const WCHAR *name, UINT16 major)
{
    HSTRING h = hs(name);
    BOOLEAN r = 2;
    HRESULT hr = VT(st, A_CONTRACT, Contract_t)(st, h, major, &r);
    WindowsDeleteString(h);
    return FAILED(hr) || r > 1 ? -1 : r;
}
static int contract_minor(const WCHAR *name, UINT16 major, UINT16 minor)
{
    HSTRING h = hs(name);
    BOOLEAN r = 2;
    HRESULT hr = VT(st, A_CONTRACT_MINOR, ContractMinor_t)(st, h, major, minor, &r);
    WindowsDeleteString(h);
    return FAILED(hr) || r > 1 ? -1 : r;
}
static int type(const WCHAR *name)
{
    HSTRING h = hs(name);
    BOOLEAN r = 2;
    HRESULT hr = VT(st, A_TYPE, Type_t)(st, h, &r);
    WindowsDeleteString(h);
    return FAILED(hr) || r > 1 ? -1 : r;
}
static int member(int slot, const WCHAR *t, const WCHAR *m)
{
    HSTRING a = hs(t), b = hs(m);
    BOOLEAN r = 2;
    HRESULT hr = VT(st, slot, Member_t)(st, a, b, &r);
    WindowsDeleteString(a);
    WindowsDeleteString(b);
    return FAILED(hr) || r > 1 ? -1 : r;
}

int main(void)
{
    RoInitialize(RO_INIT_MULTITHREADED);
    HSTRING cls = hs(L"Windows.Foundation.Metadata.ApiInformation");
    HRESULT hr = RoGetActivationFactory(cls, &IID_IApiInformationStatics_, &st);
    CHECK("ApiInformation's statics through RoGetActivationFactory", SUCCEEDED(hr) && st);
    void *f = NULL, *inst = (void *)1;
    hr = RoGetActivationFactory(cls, &IID_IActivationFactory_, &f);
    CHECK("its IActivationFactory", SUCCEEDED(hr) && f);
    if (f) CHECK("a static class makes no instances",
                 FAILED(VT(f, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(f, &inst)) && !inst);
    RELEASE(f);
    WindowsDeleteString(cls);
    if (!st) {
        printf("apiinfotest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
        return 1;
    }

    /* Windows 10 22H2's contracts (what Godot and XAML ask) */
    static const WCHAR uac[] = L"Windows.Foundation.UniversalApiContract";
    static const WCHAR fc[] = L"Windows.Foundation.FoundationContract";
    CHECK("UniversalApiContract 1", contract(uac, 1) == 1);
    CHECK("UniversalApiContract 6", contract(uac, 6) == 1);
    CHECK("UniversalApiContract 7", contract(uac, 7) == 1);
    CHECK("UniversalApiContract 10", contract(uac, 10) == 1);
    CHECK("UniversalApiContract 11 is not (Windows 11)", contract(uac, 11) == 0);
    CHECK("UniversalApiContract 10.0", contract_minor(uac, 10, 0) == 1);
    CHECK("UniversalApiContract 10.1 is not", contract_minor(uac, 10, 1) == 0);
    CHECK("UniversalApiContract 9.5", contract_minor(uac, 9, 5) == 1);
    CHECK("FoundationContract 4", contract(fc, 4) == 1);
    CHECK("FoundationContract 5 is not", contract(fc, 5) == 0);
    CHECK("an unknown contract is not", contract(L"Contoso.ImaginaryContract", 1) == 0);
    CHECK("names are matched whole", contract(L"Windows.Foundation.UniversalApiContractX", 1) == 0);

    /* types: what NovaOS activates */
    CHECK("ApiInformation is a type", type(L"Windows.Foundation.Metadata.ApiInformation") == 1);
    CHECK("PropertyValue (the registry's) is a type", type(L"Windows.Foundation.PropertyValue") == 1);
    CHECK("UISettings (ole32's) is a type", type(L"Windows.UI.ViewManagement.UISettings") == 1);
    CHECK("an unknown class is not", type(L"Contoso.Imaginary.Widget") == 0);
    CHECK("a near name is not", type(L"Windows.Foundation.PropertyValues") == 0);

    /* members: only metadata would know */
    CHECK("IsMethodPresent", member(A_METHOD, L"Windows.UI.ViewManagement.UISettings", L"Imaginary") == 0);
    CHECK("IsEventPresent", member(A_EVENT, L"Windows.UI.ViewManagement.UISettings", L"Imaginary") == 0);
    CHECK("IsPropertyPresent", member(A_PROPERTY, L"Windows.UI.ViewManagement.UISettings", L"Imaginary") == 0);
    CHECK("IsEnumNamedValuePresent", member(A_ENUM_VALUE, L"Contoso.Kind", L"Imaginary") == 0);
    HSTRING a = hs(L"Contoso.Imaginary.Widget"), b = hs(L"Go");
    BOOLEAN r = 2;
    CHECK("IsMethodPresentWithArity", SUCCEEDED(VT(st, A_METHOD_ARITY, Arity_t)(st, a, b, 1, &r)) && r == 0);
    WindowsDeleteString(a);
    WindowsDeleteString(b);

    RELEASE(st);
    printf("apiinfotest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    return fail != 0;
}
