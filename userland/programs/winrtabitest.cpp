/* winrtabitest.exe — the Windows Runtime C++ ABI headers NovaOS generates
 * without MIDL (tools/winrt/abigen.py, from Windows' metadata and the XAML
 * framework's IDL), checked against NovaOS's own Windows Runtime: the
 * Compositor (dcomp.dll) driven only through the generated interfaces
 * (their IIDs by __uuidof, their methods by vtable slot), so every call
 * checks that the header's method order and parameter types match the
 * implementation; plus IIDs Windows publishes, for a generic instance
 * (IIterable<HSTRING>), a lifted Microsoft.UI.Composition interface and the
 * XAML framework's interfaces the generator synthesizes from IDL.
 *
 *   winrtabitest        the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.ui.composition.h>
#include <microsoft.ui.composition.h>
#include <microsoft.ui.xaml.h>
#include <microsoft.ui.xaml.controls.h>
#include <microsoft.ui.xaml.media.dxinterop.h>

namespace wuc = ABI::Windows::UI::Composition;
namespace wfn = ABI::Windows::Foundation::Numerics;
namespace wfc = ABI::Windows::Foundation::Collections;
namespace xaml = ABI::Microsoft::UI::Xaml;

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)

template <class T> static void release(T *&p) { if (p) p->Release(); p = nullptr; }

template <class T> static T *qi(IUnknown *o)
{
    void *p = nullptr;
    return o && o->QueryInterface(__uuidof(T), &p) == S_OK ? (T *)p : nullptr;
}

static bool iid_is(const GUID &g, const char *text)
{
    char buf[40];
    snprintf(buf, sizeof buf, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             (unsigned long)g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
             g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return !strcmp(buf, text);
}

static bool same_object(IUnknown *a, IUnknown *b)
{
    IUnknown *ua = qi<IUnknown>(a), *ub = qi<IUnknown>(b);
    bool same = ua && ua == ub;
    release(ua);
    release(ub);
    return same;
}

static bool class_name_is(IInspectable *o, const wchar_t *want)
{
    HSTRING s = nullptr;
    if (!o || o->GetRuntimeClassName(&s) != S_OK)
        return false;
    UINT32 n = 0;
    const WCHAR *p = WindowsGetStringRawBuffer(s, &n);
    bool ok = n == wcslen(want) && !memcmp(p, want, n * sizeof(WCHAR));
    WindowsDeleteString(s);
    return ok;
}

/* IIDs Windows publishes, against what the generator gave the types */
static void check_iids(void)
{
    CHECK("ICompositor IID (metadata)", iid_is(__uuidof(wuc::ICompositor), "b403ca50-7f8c-4e83-985f-cc45060036d8"));
    CHECK("IIterable<HSTRING> IID (pinterface)", iid_is(__uuidof(wfc::IIterable<HSTRING>), "e2fcc7c1-3bfc-5a0b-b2b0-72e769d1cb7e"));
    CHECK("Microsoft.UI.Composition.ICompositor IID",
          iid_is(__uuidof(ABI::Microsoft::UI::Composition::ICompositor), "95213c13-c4cb-57de-b267-d21ab901ae38"));
    /* from the XAML IDL: synthesized interfaces take the published IIDs */
    CHECK("Microsoft.UI.Xaml.IUIElement IID", iid_is(__uuidof(xaml::IUIElement), "c3c01020-320c-5cf6-9d24-d396bbfa4d8b"));
    CHECK("Microsoft.UI.Xaml.Controls.IButton IID",
          iid_is(__uuidof(xaml::Controls::IButton), "216c183d-d07a-5aa5-b8a4-0300a2683e87"));
    CHECK("Microsoft.UI.Xaml.Controls.IButtonFactory IID",
          iid_is(__uuidof(xaml::Controls::IButtonFactory), "fe393422-d91c-57b1-9a9c-2c7e3f41f77c"));
    /* a classic COM interface from the IDL's com/ files */
    CHECK("ISurfaceImageSourceNative IID", iid_is(__uuidof(ISurfaceImageSourceNative), "e4cecd6c-f14b-4f46-83c3-8bbda27c6504"));
    CHECK("RuntimeClass_ name", !wcscmp(RuntimeClass_Microsoft_UI_Xaml_Controls_Button, L"Microsoft.UI.Xaml.Controls.Button"));
    CHECK("InterfaceName_ name", !wcscmp(InterfaceName_Windows_UI_Composition_IVisual, L"Windows.UI.Composition.IVisual"));
    CHECK("enum value names", xaml::Visibility_Collapsed == 1 && xaml::Visibility::Visibility_Visible == 0);
}

static void check_compositor(void)
{
    HSTRING cls = nullptr;
    WindowsCreateString((const WCHAR *)RuntimeClass_Windows_UI_Composition_Compositor,
                        (UINT32)wcslen(RuntimeClass_Windows_UI_Composition_Compositor), &cls);
    IInspectable *insp = nullptr;
    HRESULT hr = RoActivateInstance(cls, (void **)&insp);
    WindowsDeleteString(cls);
    CHECK("RoActivateInstance(Compositor)", hr == S_OK && insp);
    wuc::ICompositor *comp = qi<wuc::ICompositor>(insp);
    CHECK("QueryInterface(ICompositor)", comp != nullptr);
    if (!comp) {
        release(insp);
        return;
    }
    CHECK("Compositor runtime class name", class_name_is(comp, L"Windows.UI.Composition.Compositor"));

    /* a sprite: properties through IVisual */
    wuc::ISpriteVisual *sprite = nullptr;
    CHECK("CreateSpriteVisual", comp->CreateSpriteVisual(&sprite) == S_OK && sprite);
    CHECK("SpriteVisual runtime class name", class_name_is(sprite, L"Windows.UI.Composition.SpriteVisual"));
    wuc::IVisual *vis = qi<wuc::IVisual>(sprite);
    CHECK("QueryInterface(IVisual)", vis != nullptr);
    if (vis) {
        wfn::Vector2 size = { 120.0f, 80.0f }, gs = {};
        CHECK("put_Size", vis->put_Size(size) == S_OK);
        CHECK("get_Size", vis->get_Size(&gs) == S_OK && gs.X == 120.0f && gs.Y == 80.0f);
        wfn::Vector3 off = { 10.0f, 20.0f, 0.0f }, go = {};
        CHECK("put_Offset", vis->put_Offset(off) == S_OK);
        CHECK("get_Offset", vis->get_Offset(&go) == S_OK && go.X == 10.0f && go.Y == 20.0f);
        FLOAT op = 0;
        CHECK("put_Opacity", vis->put_Opacity(0.5f) == S_OK);
        CHECK("get_Opacity", vis->get_Opacity(&op) == S_OK && op == 0.5f);
        ::boolean shown = 2;
        CHECK("put_IsVisible", vis->put_IsVisible(false) == S_OK);
        CHECK("get_IsVisible", vis->get_IsVisible(&shown) == S_OK && shown == 0);
        FLOAT deg = 0;
        CHECK("put_RotationAngleInDegrees", vis->put_RotationAngleInDegrees(30.0f) == S_OK);
        CHECK("get_RotationAngleInDegrees", vis->get_RotationAngleInDegrees(&deg) == S_OK && deg > 29.9f && deg < 30.1f);
        wfn::Vector2 anchor = { 0.5f, 0.25f }, ga = {};
        CHECK("put_AnchorPoint", vis->put_AnchorPoint(anchor) == S_OK);
        CHECK("get_AnchorPoint", vis->get_AnchorPoint(&ga) == S_OK && ga.X == 0.5f && ga.Y == 0.25f);
    }

    /* a colour brush, set as the sprite's brush */
    ABI::Windows::UI::Color red = { 255, 200, 10, 20 }, got = {};
    wuc::ICompositionColorBrush *cb = nullptr;
    CHECK("CreateColorBrushWithColor", comp->CreateColorBrushWithColor(red, &cb) == S_OK && cb);
    if (cb) {
        CHECK("get_Color", cb->get_Color(&got) == S_OK && got.A == 255 && got.R == 200 && got.G == 10 && got.B == 20);
        wuc::ICompositionBrush *brush = qi<wuc::ICompositionBrush>(cb);
        CHECK("QueryInterface(ICompositionBrush)", brush != nullptr);
        wuc::ICompositionBrush *back = nullptr;
        CHECK("put_Brush", sprite && sprite->put_Brush(brush) == S_OK);
        CHECK("get_Brush", sprite && sprite->get_Brush(&back) == S_OK && same_object(back, cb));
        release(back);
        release(brush);
    }

    /* a container and its children */
    wuc::IContainerVisual *box = nullptr;
    CHECK("CreateContainerVisual", comp->CreateContainerVisual(&box) == S_OK && box);
    wuc::IVisualCollection *kids = nullptr;
    CHECK("get_Children", box && box->get_Children(&kids) == S_OK && kids);
    if (kids && vis) {
        INT32 n = -1;
        CHECK("InsertAtTop", kids->InsertAtTop(vis) == S_OK);
        CHECK("get_Count after insert", kids->get_Count(&n) == S_OK && n == 1);
        wuc::IContainerVisual *parent = nullptr;
        CHECK("get_Parent", vis->get_Parent(&parent) == S_OK && same_object(parent, box));
        release(parent);
        CHECK("Remove", kids->Remove(vis) == S_OK);
        CHECK("get_Count after remove", kids->get_Count(&n) == S_OK && n == 0);
    }

    /* the object's compositor (ICompositionObject) */
    wuc::ICompositionObject *obj = qi<wuc::ICompositionObject>(sprite);
    wuc::ICompositor *owner = nullptr;
    CHECK("ICompositionObject::get_Compositor", obj && obj->get_Compositor(&owner) == S_OK && same_object(owner, comp));
    release(owner);
    release(obj);

    release(kids);
    release(box);
    release(cb);
    release(vis);
    release(sprite);
    release(comp);
    release(insp);
}

int main(void)
{
    CHECK("RoInitialize", SUCCEEDED(RoInitialize(RO_INIT_SINGLETHREADED)));
    check_iids();
    check_compositor();
    printf("winrtabitest: %d passed, %d failed (%d-bit)\n", pass, fail, (int)sizeof(void *) * 8);
    return fail ? 1 : 0;
}
