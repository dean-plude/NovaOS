/* wrtapptest.exe — more of the Windows Runtime classes Windows Calculator's
 * start-up asks for: ApplicationData.Current's LocalSettings (its Values a
 * PropertySet taking, finding and removing a boxed value; CreateContainer
 * making then opening a named container, Existing refusing a missing one);
 * ResourceContext.GetForViewIndependentUse's QualifierValues (an
 * IObservableMap<String, String> with Language en-US, iterated) and
 * Languages (an IVectorView<String>); User.GetFromId finding the one user
 * and no other; and NamedPolicy's data for a policy nothing manages
 * (unmanaged, a Boolean, true). */
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
typedef HRESULT (STDMETHODCALLTYPE *Get_t)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *GetS_t)(void *, HSTRING, void *);
#define RELEASE(o) do { if (o) VT(o, 2, Rel_t)(o); (o) = NULL; } while (0)

static const GUID IID_IApplicationDataStatics_T = { 0x5612147B, 0xE843, 0x45E3, { 0x94, 0xD8, 0x06, 0x16, 0x9E, 0x3C, 0x8E, 0x17 } };
static const GUID IID_IMapStringObject_T = { 0x1B0D3570, 0x0877, 0x5EC2, { 0x8A, 0x2C, 0x3B, 0x95, 0x39, 0x50, 0x6A, 0xCA } };
static const GUID IID_IPropertyValueStatics_T = { 0x629BDBC8, 0xD932, 0x4FF4, { 0x96, 0xB9, 0x8D, 0x96, 0xC5, 0xC1, 0xE8, 0x58 } };
static const GUID IID_IPropertyValue_T = { 0x4BD682DD, 0x7554, 0x40E9, { 0x9A, 0x9B, 0x82, 0x65, 0x4E, 0xDE, 0x7E, 0x62 } };
static const GUID IID_IResourceContextStatics2_T = { 0x41F752EF, 0x12AF, 0x41B9, { 0xAB, 0x36, 0xB1, 0xEB, 0x4B, 0x51, 0x24, 0x60 } };
static const GUID IID_IMapStringString_T = { 0xF6D1F700, 0x49C2, 0x52AE, { 0x81, 0x54, 0x82, 0x6F, 0x99, 0x08, 0x77, 0x3C } };
static const GUID IID_IIterableKVString_T = { 0xE9BDAAF0, 0xCBF6, 0x5C72, { 0xBE, 0x90, 0x29, 0xCB, 0xF3, 0xA1, 0x31, 0x9B } };
static const GUID IID_IUserStatics_T = { 0x155EB23B, 0x242A, 0x45E0, { 0xA2, 0xE9, 0x31, 0x71, 0xFC, 0x6A, 0x7F, 0xDD } };
static const GUID IID_INamedPolicyStatics_T = { 0x7F793BE7, 0x76C4, 0x4058, { 0x8C, 0xAD, 0x67, 0x66, 0x2C, 0xD0, 0x5F, 0x0D } };

static HSTRING hs(const WCHAR *s) { HSTRING h = 0; WindowsCreateString(s, (UINT32)wcslen(s), &h); return h; }
static int is(HSTRING h, const WCHAR *s) { return !wcscmp(WindowsGetStringRawBuffer(h, 0), s); }
static HRESULT factory(const WCHAR *cls, const GUID *iid, void **out)
{
    HSTRING s = hs(cls);
    HRESULT hr = RoGetActivationFactory(s, iid, out);
    WindowsDeleteString(s);
    if (hr != S_OK) printf("wrtapptest: RoGetActivationFactory(%ls): 0x%08lX\n", cls, (unsigned long)hr);
    return hr;
}

static void app_data(void)
{
    void *statics = 0, *data = 0, *local = 0, *values = 0, *map = 0, *pv = 0, *box = 0, *found = 0;
    CHECK("ApplicationData's statics", factory(L"Windows.Storage.ApplicationData", &IID_IApplicationDataStatics_T, &statics) == S_OK);
    if (!statics) return;
    VT(statics, 6, Get_t)(statics, &data);                          /* Current */
    CHECK("ApplicationData.Current", data != 0);
    if (data) VT(data, 10, Get_t)(data, &local);                    /* LocalSettings */
    CHECK("LocalSettings", local != 0);
    if (!local) goto out;
    HSTRING name = 0;
    int locality = -1;
    VT(local, 6, Get_t)(local, &name);
    VT(local, 7, Get_t)(local, &locality);
    CHECK("its Name (empty) and Locality (Local)", WindowsGetStringLen(name) == 0 && locality == 0);
    WindowsDeleteString(name);
    VT(local, 8, Get_t)(local, &values);                            /* Values: IPropertySet */
    CHECK("its Values", values != 0);
    if (values) VT(values, 0, QI_t)(values, &IID_IMapStringObject_T, &map);
    CHECK("Values: an IMap<String, Object>", map != 0);
    factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics_T, &pv);
    if (map && pv) {
        VT(pv, 10, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, void **))(pv, 42, &box);   /* CreateInt32 */
        HSTRING k = hs(L"VeryFirstLaunch");
        BYTE had = 1, replaced = 1;
        VT(map, 8, GetS_t)(map, k, &had);                           /* HasKey */
        CHECK("a new key: not there", had == 0);
        CHECK("Insert", VT(map, 10, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, void *, BYTE *))(map, k, box, &replaced) == S_OK && !replaced);
        VT(map, 8, GetS_t)(map, k, &had);
        CHECK("then there", had == 1);
        VT(map, 6, GetS_t)(map, k, &found);                         /* Lookup */
        INT32 v = 0;
        void *fv = 0;
        if (found) VT(found, 0, QI_t)(found, &IID_IPropertyValue_T, &fv);
        if (fv) VT(fv, 11, Get_t)(fv, &v);                          /* GetInt32 */
        CHECK("Lookup: the value put", v == 42);
        RELEASE(fv);
        UINT32 n = 0;
        VT(map, 7, Get_t)(map, &n);
        CHECK("Size 1", n == 1);
        CHECK("Remove", VT(map, 11, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING))(map, k) == S_OK);
        VT(map, 8, GetS_t)(map, k, &had);
        CHECK("then gone", had == 0);
        WindowsDeleteString(k);
    }
    void *c1 = 0, *c2 = 0, *c3 = 0;
    HSTRING cn = hs(L"Sub"), missing = hs(L"Missing");
    typedef HRESULT (STDMETHODCALLTYPE *Create_t)(void *, HSTRING, int, void **);
    CHECK("CreateContainer(Always)", VT(local, 10, Create_t)(local, cn, 0, &c1) == S_OK && c1);
    CHECK("CreateContainer(Existing) opens it", VT(local, 10, Create_t)(local, cn, 1, &c2) == S_OK && c2 == c1);
    CHECK("CreateContainer(Existing) of a missing one: refused", FAILED(VT(local, 10, Create_t)(local, missing, 1, &c3)) && !c3);
    WindowsDeleteString(cn);
    WindowsDeleteString(missing);
    RELEASE(c1);
    RELEASE(c2);
out:
    RELEASE(found);
    RELEASE(box);
    RELEASE(pv);
    RELEASE(map);
    RELEASE(values);
    RELEASE(local);
    RELEASE(data);
    RELEASE(statics);
}

static void resource_context(void)
{
    void *statics = 0, *ctx = 0, *q = 0, *qm = 0, *it = 0, *first = 0, *langs = 0;
    CHECK("ResourceContext's statics", factory(L"Windows.ApplicationModel.Resources.Core.ResourceContext",
                                              &IID_IResourceContextStatics2_T, &statics) == S_OK);
    if (!statics) return;
    VT(statics, 10, Get_t)(statics, &ctx);                          /* GetForViewIndependentUse */
    CHECK("GetForViewIndependentUse", ctx != 0);
    if (ctx) VT(ctx, 6, Get_t)(ctx, &q);                            /* QualifierValues */
    CHECK("QualifierValues", q != 0);
    if (q) VT(q, 0, QI_t)(q, &IID_IMapStringString_T, &qm);
    CHECK("an IMap<String, String>", qm != 0);
    if (qm) {
        HSTRING k = hs(L"Language"), v = 0;
        VT(qm, 6, GetS_t)(qm, k, &v);                               /* Lookup */
        CHECK("Language: en-US", v && is(v, L"en-US"));
        WindowsDeleteString(v);
        WindowsDeleteString(k);
        UINT32 n = 0, seen = 0;
        VT(qm, 7, Get_t)(qm, &n);                                   /* Size */
        VT(qm, 0, QI_t)(qm, &IID_IIterableKVString_T, &it);
        if (it) VT(it, 6, Get_t)(it, &first);
        BYTE has = 0;
        if (first) VT(first, 7, Get_t)(first, &has);
        while (first && has && seen < 100) {
            seen++;
            VT(first, 8, Get_t)(first, &has);                       /* MoveNext */
        }
        CHECK("iterated: every qualifier", n > 5 && seen == n);
    }
    if (ctx) VT(ctx, 11, Get_t)(ctx, &langs);                       /* Languages */
    CHECK("Languages", langs != 0);
    if (langs) {
        UINT32 n = 0;
        HSTRING l = 0;
        VT(langs, 7, Get_t)(langs, &n);
        VT(langs, 6, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32, HSTRING *))(langs, 0, &l);
        CHECK("Languages: en-US", n == 1 && l && is(l, L"en-US"));
        WindowsDeleteString(l);
    }
    RELEASE(langs);
    RELEASE(first);
    RELEASE(it);
    RELEASE(qm);
    RELEASE(q);
    RELEASE(ctx);
    RELEASE(statics);
}

static void user_and_policy(void)
{
    void *users = 0, *user = 0, *other = (void *)1, *policies = 0, *data = 0;
    CHECK("User's statics", factory(L"Windows.System.User", &IID_IUserStatics_T, &users) == S_OK);
    if (users) {
        HSTRING id = hs(L"NovaOS.User"), no = hs(L"someone else");
        VT(users, 10, GetS_t)(users, id, &user);                    /* GetFromId */
        CHECK("GetFromId: the one user", user != 0);
        VT(users, 10, GetS_t)(users, no, &other);
        CHECK("GetFromId of another: null", other == 0);
        WindowsDeleteString(id);
        WindowsDeleteString(no);
    }
    CHECK("NamedPolicy's statics", factory(L"Windows.Management.Policies.NamedPolicy", &IID_INamedPolicyStatics_T, &policies) == S_OK);
    if (policies) {
        HSTRING area = hs(L"Education"), name = hs(L"AllowGraphingCalculator"), got = 0;
        VT(policies, 7, HRESULT (STDMETHODCALLTYPE *)(void *, void *, HSTRING, HSTRING, void **))(policies, user, area, name, &data);
        CHECK("GetPolicyFromPathForUser", data != 0);
        if (data) {
            BYTE managed = 1, b = 0;
            int kind = 0;
            VT(data, 7, Get_t)(data, &got);
            VT(data, 8, Get_t)(data, &kind);
            VT(data, 9, Get_t)(data, &managed);
            VT(data, 12, Get_t)(data, &b);
            CHECK("its Name", got && is(got, L"AllowGraphingCalculator"));
            CHECK("unmanaged, a Boolean, allowed", !managed && kind == 2 && b == 1);
            WindowsDeleteString(got);
        }
        WindowsDeleteString(area);
        WindowsDeleteString(name);
    }
    RELEASE(data);
    RELEASE(policies);
    RELEASE(user);
    RELEASE(users);
}

int main(void)
{
    RoInitialize(1);
    app_data();
    resource_context();
    user_and_policy();
    printf("wrtapptest: %d passed, %d failed (%d-bit)\n", pass, fail, (int)sizeof(void *) * 8);
    return fail ? 1 : 0;
}
