/* winui2test.exe — WinUI 2's own controls (Microsoft.UI.Xaml.dll 2.x, built
 * from Microsoft's winui2/main sources by userland/microsoft_ui_xaml) on the
 * system XAML, under its Windows.UI.Xaml names (roadmap step 32.5, second
 * part; docs/design/windows-ui-xaml.md).  The framework starts as
 * wuxamltest starts it, serving the Windows.UI.Xaml classes WinUI 2's
 * controls compose over; in OnLaunched the test is a WinUI 2 app:
 *   - Microsoft.UI.Xaml.dll loaded, its DllGetActivationFactory serving
 *     WinUI 2's XAML type table (XamlControlsXamlMetaDataProvider, Windows'
 *     IXamlMetadataProvider), which the app's Application hands the
 *     framework as a WinUI 2 app's generated one does (the framework's
 *     metadata, XamlLibMetadataProvider, asks it for WinUI 2's types);
 *   - the type Microsoft.UI.Xaml.Controls.InfoBadge, its base type, and an
 *     InfoBadge it activates: its class name, and the framework's Windows
 *     IControl, IFrameworkElement and IDependencyObject behind the same
 *     object (WinUI 2 composes its controls over Windows.UI.Xaml.Controls.
 *     Control);
 *   - InfoBadge.Value (a dependency property WinUI 2 registered on the
 *     framework) read, set and read back through the type's member;
 *   - a DropDownButton (composed over Windows.UI.Xaml.Controls.Button): its
 *     Windows IButton and its content set and read back;
 *   - WinUI 2's XAML resources (step 32.5's third part): the merged
 *     19h1 theme resources and default styles installed where ms-appx:///
 *     Microsoft.UI.Xaml/Themes/ finds them;
 *   - the default styles (step 32.5's eighth part): XamlControlsResources
 *     merged into the app's resources, then InfoBadge, ProgressBar,
 *     ProgressRing and SwipeControl made from markup with their templates
 *     applied, and a page as Calculator's declares them (ProgressRing,
 *     SwipeControl with SwipeItems, InfoBadge,
 *     BackdropMaterial.ApplyToRootOrPageBackground);
 *   - NavigationView and RadioButtons (step 32.5's ninth part): each with
 *     its template, and a page with a NavigationView (NavigationViewItems
 *     in MenuItems) and RadioButtons, their items and SelectedIndex from
 *     markup; the RadioButtons measured and arranged;
 *   - WinUI 2's overrides (step 32.5's tenth part): OnApplyTemplate and
 *     the layout overrides called through Windows' overridable interfaces,
 *     so the RadioButtons' ItemsRepeater makes its three RadioButtons, the
 *     NavigationView applies its template, lays out and, its pane open,
 *     makes its two NavigationViewItems.
 *
 *   winui2test        the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <new>
#include <string>
#include <vector>
#include <corep.h>
#include <CStaticLock.h>
#include <SystemThemingInterop.h>
#include <DXamlInstanceStorage.h>
#include <WindowsXamlManager.g.h>
#include <roapi.h>
#include <restrictederrorinfo.h>
#include <roerrorapi.h>
#include <FrameworkApplication_Partial.h>
#include <DXamlCore.h>
#include <microsoft.ui.dispatching.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.controls.primitives.h>
#include <windows.ui.xaml.markup.h>
#include <windows.ui.xaml.data.h>
#include <windows.ui.xaml.media.h>
#include <windows.ui.core.h>
#include <appmodel.h>

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxm = ABI::Windows::UI::Xaml::Markup;

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define CHECK_HR(what, expr) do { HRESULT h_ = (expr); if (SUCCEEDED(h_)) pass++; else { fail++; \
    printf("FAIL: %s: 0x%08lx (line %d)\n", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } } while (0)
/* what waits on framework parts not built yet: reported, not counted, so
 * the line turns into a pass when the part lands (roadmap 32.5's next
 * parts: the UWP window's present target (32.4's composition)) */
#define NOT_YET(what, cond) do { printf("%s: %s (line %d)\n", (cond) ? "now works (make it a CHECK)" : "not yet", \
    what, __LINE__); fflush(stdout); } while (0)
#define NOT_YET_HR(what, expr) do { HRESULT h_ = (expr); printf("%s: %s: 0x%08lx (line %d)\n", SUCCEEDED(h_) ? \
    "now works (make it a CHECK)" : "not yet", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } while (0)

extern "C" HRESULT nova_xaml_windows_factory(HSTRING cls, IActivationFactory **out);

static bool is(HSTRING h, const wchar_t *s)
{
    return wcscmp(h ? WindowsGetStringRawBuffer(h, nullptr) : L"", s) == 0;
}

template <class T> static HRESULT as(IUnknown *p, T **out)
{
    *out = nullptr;
    return p ? p->QueryInterface(__uuidof(T), reinterpret_cast<void **>(out)) : E_POINTER;
}

static IUnknown *identity(IUnknown *p)
{
    IUnknown *u = nullptr;
    if (p && SUCCEEDED(p->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&u))))
        u->Release();
    return u;
}

static HRESULT box_int(INT32 v, IInspectable **out)
{
    ctl::ComPtr<wf::IPropertyValueStatics> pv;
    IFC_RETURN(wf::GetActivationFactory(wrl_wrappers::HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), pv.ReleaseAndGetAddressOf()));
    return pv->CreateInt32(v, out);
}

static bool unboxed_int(IInspectable *v, INT32 want)
{
    ctl::ComPtr<wf::IPropertyValue> pv;
    INT32 got = 0;
    return v && SUCCEEDED(v->QueryInterface(IID_PPV_ARGS(&pv))) && SUCCEEDED(pv->GetInt32(&got)) && got == want;
}

static bool unboxed_is(IInspectable *v, const wchar_t *s)
{
    ctl::ComPtr<wf::IPropertyValue> pv;
    HSTRING h = nullptr;
    bool r = v && SUCCEEDED(v->QueryInterface(IID_PPV_ARGS(&pv))) && SUCCEEDED(pv->GetString(&h)) && is(h, s);
    WindowsDeleteString(h);
    return r;
}

/* Microsoft.UI.Xaml.dll's factory of class @cls, as C++/WinRT's
 * activation finds a class no registration names (the DLL named after its
 * namespace, then the namespace's parent's) */
typedef HRESULT (WINAPI *GetFactory)(HSTRING, IActivationFactory **);
static GetFactory s_mux;
static HRESULT activate(const wchar_t *cls, IInspectable **out)
{
    *out = nullptr;
    ctl::ComPtr<IActivationFactory> f;
    IFC_RETURN(s_mux ? s_mux(wrl_wrappers::HStringReference(cls).Get(), f.GetAddressOf()) : E_FAIL);
    return f->ActivateInstance(out);
}

static wuxm::IXamlMetadataProvider *winui2_provider();

template <class T> static HRESULT statics(const wchar_t *cls, T **out)
{
    return RoGetActivationFactory(wrl_wrappers::HStringReference(cls).Get(), __uuidof(T), reinterpret_cast<void **>(out));
}

static bool text_is(IInspectable *o, const wchar_t *s)
{
    ctl::ComPtr<wuxc::ITextBlock> tb;
    HSTRING h = nullptr;
    bool r = o && SUCCEEDED(as(o, tb.GetAddressOf())) && SUCCEEDED(tb->get_Text(&h)) && is(h, s);
    WindowsDeleteString(h);
    return r;
}

static HRESULT box_string(const wchar_t *s, IInspectable **out)
{
    ctl::ComPtr<wf::IPropertyValueStatics> pv;
    IFC_RETURN(wf::GetActivationFactory(wrl_wrappers::HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), pv.ReleaseAndGetAddressOf()));
    return pv->CreateString(wrl_wrappers::HStringReference(s).Get(), out);
}

/* the thread's error, as the framework originated it (a parse error's
 * line and message) */
static void why()
{
    IRestrictedErrorInfo *info = nullptr;
    BSTR desc = nullptr, restricted = nullptr, sid = nullptr;
    HRESULT code = S_OK;
    if (SUCCEEDED(GetRestrictedErrorInfo(&info)) && info &&
        SUCCEEDED(info->GetErrorDetails(&desc, &code, &restricted, &sid)))
        printf("  error 0x%08lx: %ls %ls\n", static_cast<unsigned long>(code), desc ? desc : L"", restricted ? restricted : L"");
    SysFreeString(desc);
    SysFreeString(restricted);
    SysFreeString(sid);
    if (info)
        info->Release();
    /* and the core's first error (a parser error's code, line and position) */
    IErrorService *errors = nullptr;
    IError *first = nullptr;
    DirectUI::DXamlCore *core = DirectUI::DXamlCore::GetCurrent();
    if (core && core->GetHandle() && SUCCEEDED(core->GetHandle()->getErrorService(&errors)) && errors &&
        SUCCEEDED(errors->GetLastReportedError(&first)) && first) {
        xstring_ptr message;
        first->GetErrorMessage(&message);
        printf("  the core's last error: code %u, line %u, position %u: %ls\n", first->GetErrorCode(),
               first->GetLineNumber(), first->GetCharPosition(), message.IsNull() ? L"" : message.GetBuffer());
        errors->CleanupErrors();
    }
    fflush(stdout);
}


#define XMLNS L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
#define XMLNS_X L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' "

/* a WinUI 2 control @name (of Microsoft.UI.Xaml.Controls) made from markup,
 * as an app's page makes it (the parser completes its creation, which
 * applies its default style: the style DefaultStyleResourceUri's
 * generic.xaml holds for it; a control made in code gets it entering the
 * tree), and Control.ApplyTemplate: the template's root under it */
static ctl::ComPtr<IInspectable> templated(const wchar_t *name)
{
    ctl::ComPtr<wuxm::IXamlReaderStatics> reader;
    ctl::ComPtr<IInspectable> o;
    ctl::ComPtr<wuxc::IControl> control;
    ctl::ComPtr<wux::IDependencyObject> dobj;
    ctl::ComPtr<wux::Media::IVisualTreeHelperStatics> vth;
    boolean applied = false;
    INT32 parts = 0;
    std::wstring markup = std::wstring(L"<muxc:") + name + L" " XMLNS L"xmlns:muxc='using:Microsoft.UI.Xaml.Controls'/>";
    char what[200];
    HRESULT hr = SUCCEEDED(statics(L"Windows.UI.Xaml.Markup.XamlReader", reader.GetAddressOf())) ?
        reader->Load(wrl_wrappers::HStringReference(markup.c_str()).Get(), o.GetAddressOf()) : E_FAIL;
    snprintf(what, sizeof what, "a %ls loaded from markup", name);
    CHECK_HR(what, hr);
    if (FAILED(hr)) {
        why();
        return o;
    }
    hr = as(o.Get(), control.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = control->ApplyTemplate(&applied);
    snprintf(what, sizeof what, "%ls: ApplyTemplate (WinUI 2's default style)", name);
    CHECK_HR(what, hr);
    if (FAILED(hr))
        why();
    if (SUCCEEDED(as(o.Get(), dobj.GetAddressOf())) &&
        SUCCEEDED(statics(L"Windows.UI.Xaml.Media.VisualTreeHelper", vth.GetAddressOf())))
        vth->GetChildrenCount(dobj.Get(), &parts);
    snprintf(what, sizeof what, "%ls: its template applied, the template's root under it", name);
    CHECK(what, applied && parts == 1);
    return o;
}

/* WinUI 2's controls with their default styles (step 32.5's eighth part):
 * InfoBadge, and the controls Calculator uses (ProgressRing and the
 * ProgressBar under it, SwipeControl with its SwipeItems), each templated
 * alone and loaded together from markup as Calculator's pages declare them,
 * and BackdropMaterial.ApplyToRootOrPageBackground (Calculator's page
 * background) on a control */
static void default_style_checks()
{
    for (const wchar_t *name : {L"InfoBadge", L"ProgressBar", L"ProgressRing", L"SwipeControl"})
        templated(name);

    ctl::ComPtr<wuxm::IXamlReaderStatics> reader;
    ctl::ComPtr<IInspectable> page;
    HRESULT hr = SUCCEEDED(statics(L"Windows.UI.Xaml.Markup.XamlReader", reader.GetAddressOf())) ?
        reader->Load(wrl_wrappers::HStringReference(
            L"<UserControl " XMLNS XMLNS_X L"xmlns:muxc='using:Microsoft.UI.Xaml.Controls' "
            L"muxc:BackdropMaterial.ApplyToRootOrPageBackground='True'><StackPanel>"
            L"<muxc:ProgressRing x:Name='Ring' IsActive='True' Width='20' Height='20'/>"
            L"<muxc:SwipeControl x:Name='Swipe'><muxc:SwipeControl.RightItems><muxc:SwipeItems Mode='Execute'>"
            L"<muxc:SwipeItem x:Name='Delete' Text='Delete'/></muxc:SwipeItems></muxc:SwipeControl.RightItems>"
            L"<TextBlock Text='1 + 1 = 2'/></muxc:SwipeControl>"
            L"<muxc:InfoBadge x:Name='Badge' Value='3'/></StackPanel></UserControl>").Get(),
            page.GetAddressOf()) : E_FAIL;
    CHECK_HR("a page with a ProgressRing, a SwipeControl with SwipeItems and an InfoBadge loaded from markup", hr);
    if (FAILED(hr)) {
        why();
        return;
    }
    ctl::ComPtr<wux::IFrameworkElement> root;
    ctl::ComPtr<IInspectable> swipe, item, badge;
    CHECK("its controls found by name", SUCCEEDED(as(page.Get(), root.GetAddressOf())) &&
          SUCCEEDED(root->FindName(wrl_wrappers::HStringReference(L"Swipe").Get(), swipe.GetAddressOf())) && swipe &&
          SUCCEEDED(root->FindName(wrl_wrappers::HStringReference(L"Delete").Get(), item.GetAddressOf())) && item &&
          SUCCEEDED(root->FindName(wrl_wrappers::HStringReference(L"Badge").Get(), badge.GetAddressOf())) && badge);
    HSTRING name = nullptr;
    CHECK("the SwipeItem, Microsoft.UI.Xaml.Controls.SwipeItem", item && SUCCEEDED(item->GetRuntimeClassName(&name)) &&
          is(name, L"Microsoft.UI.Xaml.Controls.SwipeItem"));
    WindowsDeleteString(name);
    /* BackdropMaterial: the page's Background set to its fallback brush (no
     * Mica on this window: the brush WinUI 2 gives when Mica is unavailable) */
    ctl::ComPtr<wuxc::IControl> control;
    ctl::ComPtr<wux::Media::IBrush> background;
    CHECK("BackdropMaterial.ApplyToRootOrPageBackground: the page's Background set", SUCCEEDED(as(page.Get(), control.GetAddressOf())) &&
          SUCCEEDED(control->get_Background(background.GetAddressOf())) && background);
}

/* the first element under @o (depth first, @o included) whose runtime
 * class is @cls, and how many there are */
static int find_class(IInspectable *o, const wchar_t *cls, std::vector<ctl::ComPtr<IInspectable>> *found)
{
    ctl::ComPtr<wux::Media::IVisualTreeHelperStatics> vth;
    ctl::ComPtr<wux::IDependencyObject> dobj;
    HSTRING name = nullptr;
    int n = 0;
    if (o && SUCCEEDED(o->GetRuntimeClassName(&name)) && is(name, cls)) {
        n++;
        if (found)
            found->push_back(o);
    }
    WindowsDeleteString(name);
    INT32 count = 0;
    if (FAILED(as(o, dobj.GetAddressOf())) || FAILED(statics(L"Windows.UI.Xaml.Media.VisualTreeHelper", vth.GetAddressOf())) ||
        FAILED(vth->GetChildrenCount(dobj.Get(), &count)))
        return n;
    for (INT32 i = 0; i < count; i++) {
        ctl::ComPtr<wux::IDependencyObject> child;
        if (SUCCEEDED(vth->GetChild(dobj.Get(), i, child.GetAddressOf())) && child)
            n += find_class(child.Get(), cls, found);
    }
    return n;
}

/* the visual tree under o, a class per line with its visibility and
 * desired size, for a failed check's log */
static void dump_tree(IInspectable *o, int depth, int *lines)
{
    ctl::ComPtr<wux::Media::IVisualTreeHelperStatics> vth;
    ctl::ComPtr<wux::IDependencyObject> dobj;
    ctl::ComPtr<wux::IUIElement> ui;
    HSTRING name = nullptr;
    if (!o || ++*lines > 300)
        return;
    wux::Visibility vis = wux::Visibility_Visible;
    wf::Size desired{};
    if (SUCCEEDED(as(o, ui.GetAddressOf()))) {
        ui->get_Visibility(&vis);
        ui->get_DesiredSize(&desired);
    }
    o->GetRuntimeClassName(&name);
    printf("  tree %*s%ls%s %gx%g\n", depth * 2, "", WindowsGetStringRawBuffer(name, nullptr),
           vis == wux::Visibility_Collapsed ? " (collapsed)" : "", desired.Width, desired.Height);
    WindowsDeleteString(name);
    INT32 count = 0;
    if (FAILED(as(o, dobj.GetAddressOf())) || FAILED(statics(L"Windows.UI.Xaml.Media.VisualTreeHelper", vth.GetAddressOf())) ||
        FAILED(vth->GetChildrenCount(dobj.Get(), &count)))
        return;
    for (INT32 i = 0; i < count; i++) {
        ctl::ComPtr<wux::IDependencyObject> child;
        if (SUCCEEDED(vth->GetChild(dobj.Get(), i, child.GetAddressOf())) && child)
            dump_tree(child.Get(), depth + 1, lines);
    }
}

/* the rest of Calculator's WinUI 2 controls (step 32.5's ninth part):
 * NavigationView and RadioButtons, each templated alone, and a page as
 * Calculator's declares them (a NavigationView with NavigationViewItems in
 * MenuItems, RadioButtons with RadioButton items); the RadioButtons
 * measured and arranged, with the ItemsRepeater (WinUI 2's Repeater) in
 * its template */
static void navigation_checks()
{
    templated(L"NavigationView");
    templated(L"RadioButtons");

    ctl::ComPtr<wuxm::IXamlReaderStatics> reader;
    ctl::ComPtr<IInspectable> page;
    HRESULT hr = SUCCEEDED(statics(L"Windows.UI.Xaml.Markup.XamlReader", reader.GetAddressOf())) ?
        reader->Load(wrl_wrappers::HStringReference(
            L"<UserControl " XMLNS XMLNS_X L"xmlns:muxc='using:Microsoft.UI.Xaml.Controls'><StackPanel>"
            L"<muxc:NavigationView x:Name='Nav' PaneDisplayMode='LeftMinimal' IsBackButtonVisible='Collapsed' "
            L"IsSettingsVisible='False' Height='300'><muxc:NavigationView.MenuItems>"
            L"<muxc:NavigationViewItemHeader x:Name='Calc' Content='Calculator'/>"
            L"<muxc:NavigationViewItem x:Name='Standard' Content='Standard'/>"
            L"<muxc:NavigationViewItem x:Name='Scientific' Content='Scientific'/>"
            L"<muxc:NavigationViewItemSeparator/>"
            L"</muxc:NavigationView.MenuItems><TextBlock Text='0'/></muxc:NavigationView>"
            L"<muxc:RadioButtons x:Name='Theme' Header='Theme' SelectedIndex='1'>"
            L"<RadioButton Content='Light'/><RadioButton Content='Dark'/><RadioButton Content='Use system setting'/>"
            L"</muxc:RadioButtons></StackPanel></UserControl>").Get(),
            page.GetAddressOf()) : E_FAIL;
    CHECK_HR("a page with a NavigationView (NavigationViewItems in MenuItems) and RadioButtons loaded from markup", hr);
    if (FAILED(hr)) {
        why();
        return;
    }
    ctl::ComPtr<wux::IFrameworkElement> root;
    ctl::ComPtr<IInspectable> navo, item, theme;
    CHECK("its controls found by name", SUCCEEDED(as(page.Get(), root.GetAddressOf())) &&
          SUCCEEDED(root->FindName(wrl_wrappers::HStringReference(L"Nav").Get(), navo.GetAddressOf())) && navo &&
          SUCCEEDED(root->FindName(wrl_wrappers::HStringReference(L"Standard").Get(), item.GetAddressOf())) && item &&
          SUCCEEDED(root->FindName(wrl_wrappers::HStringReference(L"Theme").Get(), theme.GetAddressOf())) && theme);
    HSTRING name = nullptr;
    CHECK("the NavigationViewItem, Microsoft.UI.Xaml.Controls.NavigationViewItem", item &&
          SUCCEEDED(item->GetRuntimeClassName(&name)) && is(name, L"Microsoft.UI.Xaml.Controls.NavigationViewItem"));
    WindowsDeleteString(name);

    /* MenuItems filled from the property element: the parser added the
     * four items to NavigationView's IVector<Object> */
    ctl::ComPtr<wuxm::IXamlType> navType;
    ctl::ComPtr<wuxm::IXamlMember> menuItems;
    ctl::ComPtr<IInspectable> itemsValue;
    ctl::ComPtr<wf::Collections::IVector<IInspectable *>> items;
    UINT32 count = 0;
    CHECK("NavigationView.MenuItems: the four items added from markup",
          SUCCEEDED(winui2_provider()->GetXamlTypeByFullName(
              wrl_wrappers::HStringReference(L"Microsoft.UI.Xaml.Controls.NavigationView").Get(), navType.GetAddressOf())) &&
          navType && SUCCEEDED(navType->GetMember(wrl_wrappers::HStringReference(L"MenuItems").Get(), menuItems.GetAddressOf())) &&
          menuItems && SUCCEEDED(menuItems->GetValue(navo.Get(), itemsValue.GetAddressOf())) && itemsValue &&
          SUCCEEDED(as(itemsValue.Get(), items.GetAddressOf())) && SUCCEEDED(items->get_Size(&count)) && count == 4);

    /* layout: Measure and Arrange the RadioButtons (no window: the layout
     * pass an element's parent runs), which applies its template and runs
     * its ItemsRepeater's layout */
    ctl::ComPtr<wux::IUIElement> ui;
    hr = as(theme.Get(), ui.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = ui->Measure(wf::Size{400, 600});
    if (SUCCEEDED(hr))
        hr = ui->Arrange(wf::Rect{0, 0, 400, 600});
    CHECK_HR("the RadioButtons measured and arranged", hr);
    if (FAILED(hr))
        why();
    /* its ItemsRepeater (given its ItemsSource in OnApplyTemplate, WinUI 2's
     * override of Windows' IFrameworkElementOverrides) laid the three
     * RadioButtons out under it.  (RadioButtons checks the selected one
     * when the repeater is Loaded, in a window's tree: not this page's) */
    CHECK("the RadioButtons' ItemsRepeater made its three RadioButtons",
          find_class(theme.Get(), L"Windows.UI.Xaml.Controls.RadioButton", nullptr) == 3);
    /* and the NavigationView: its layout (its pane toggle button's
     * AnimatedIcon draws with composition shapes) */
    ctl::ComPtr<wux::IUIElement> navUi;
    hr = as(navo.Get(), navUi.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = navUi->Measure(wf::Size{400, 300});
    if (SUCCEEDED(hr))
        hr = navUi->Arrange(wf::Rect{0, 0, 400, 300});
    CHECK_HR("the NavigationView measured and arranged", hr);
    if (FAILED(hr))
        why();
    /* then its pane opened (LeftMinimal, Calculator's mode, keeps it closed
     * until the pane button opens it; a closed pane is not laid out): its
     * menu items' containers, the two NavigationViewItems, under its
     * ItemsRepeater (the header and separator are other classes) */
    ctl::ComPtr<wuxm::IXamlMember> paneOpen;
    ctl::ComPtr<IInspectable> yes;
    ctl::ComPtr<wf::IPropertyValueStatics> pvs;
    hr = wf::GetActivationFactory(wrl_wrappers::HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), pvs.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = pvs->CreateBoolean(TRUE, yes.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = navType ? navType->GetMember(wrl_wrappers::HStringReference(L"IsPaneOpen").Get(), paneOpen.GetAddressOf()) : E_POINTER;
    if (SUCCEEDED(hr))
        hr = paneOpen ? paneOpen->SetValue(navo.Get(), yes.Get()) : E_POINTER;
    if (SUCCEEDED(hr))
        hr = navUi->Measure(wf::Size{400, 300});
    if (SUCCEEDED(hr))
        hr = navUi->Arrange(wf::Rect{0, 0, 400, 300});
    CHECK_HR("its pane opened (IsPaneOpen) and laid out again", hr);
    if (FAILED(hr))
        why();
    /* (the visual tree hands WinUI 2's elements out through the framework's
     * tear, which names them by their Windows.UI.Xaml names, as it does
     * the RadioButtons) */
    int navItems = find_class(navo.Get(), L"Windows.UI.Xaml.Controls.NavigationViewItem", nullptr) +
                   find_class(navo.Get(), L"Microsoft.UI.Xaml.Controls.NavigationViewItem", nullptr);
    CHECK("its pane's ItemsRepeater made the two NavigationViewItems", navItems == 2);
    if (navItems != 2) {
        int lines = 0;
        dump_tree(navo.Get(), 0, &lines);
    }

    /* its items and SelectedIndex, from markup */
    ctl::ComPtr<wuxm::IXamlType> rbType;
    ctl::ComPtr<wuxm::IXamlMember> rbItems, rbIndex;
    ctl::ComPtr<IInspectable> rbValue, indexValue;
    ctl::ComPtr<wf::Collections::IVector<IInspectable *>> rbList;
    ctl::ComPtr<wf::IReference<INT32>> index;
    INT32 selected = -1;
    count = 0;
    CHECK("RadioButtons.Items: the three RadioButtons added from markup",
          SUCCEEDED(winui2_provider()->GetXamlTypeByFullName(
              wrl_wrappers::HStringReference(L"Microsoft.UI.Xaml.Controls.RadioButtons").Get(), rbType.GetAddressOf())) &&
          rbType && SUCCEEDED(rbType->GetMember(wrl_wrappers::HStringReference(L"Items").Get(), rbItems.GetAddressOf())) &&
          rbItems && SUCCEEDED(rbItems->GetValue(theme.Get(), rbValue.GetAddressOf())) && rbValue &&
          SUCCEEDED(as(rbValue.Get(), rbList.GetAddressOf())) && SUCCEEDED(rbList->get_Size(&count)) && count == 3);
    CHECK("RadioButtons.SelectedIndex: 1, as the markup sets it", rbType &&
          SUCCEEDED(rbType->GetMember(wrl_wrappers::HStringReference(L"SelectedIndex").Get(), rbIndex.GetAddressOf())) &&
          rbIndex && SUCCEEDED(rbIndex->GetValue(theme.Get(), indexValue.GetAddressOf())) && indexValue &&
          SUCCEEDED(as(indexValue.Get(), index.GetAddressOf())) && SUCCEEDED(index->get_Value(&selected)) && selected == 1);
}

static void winui2_checks()
{
    if (!s_mux)
        return;

    /* WinUI 2's XAML type table, through which the XAML parser makes its
     * controls (composable classes: their factories make them through
     * their own CreateInstance, IActivationFactory's ActivateInstance is
     * not theirs) */
    wuxm::IXamlMetadataProvider *provider = winui2_provider();
    CHECK("XamlControlsXamlMetaDataProvider activated, its Windows IXamlMetadataProvider", provider != nullptr);
    if (!provider)
        return;
    ctl::ComPtr<wuxm::IXamlType> type, base;
    CHECK_HR("the type Microsoft.UI.Xaml.Controls.InfoBadge", provider->GetXamlTypeByFullName(
        wrl_wrappers::HStringReference(L"Microsoft.UI.Xaml.Controls.InfoBadge").Get(), type.GetAddressOf()));
    CHECK("found", type != nullptr);
    if (!type)
        return;
    HSTRING baseName = nullptr;
    CHECK("its base type, Windows.UI.Xaml.Controls.Control", SUCCEEDED(type->get_BaseType(base.GetAddressOf())) && base &&
          SUCCEEDED(base->get_FullName(&baseName)) && is(baseName, L"Windows.UI.Xaml.Controls.Control"));
    WindowsDeleteString(baseName);

    /* an InfoBadge, composed over the framework's Control */
    ctl::ComPtr<IInspectable> badge;
    CHECK_HR("the type activates an InfoBadge", type->ActivateInstance(badge.GetAddressOf()));
    if (!badge)
        return;
    HSTRING name = nullptr;
    CHECK("its class name, Microsoft.UI.Xaml.Controls.InfoBadge", SUCCEEDED(badge->GetRuntimeClassName(&name)) &&
          is(name, L"Microsoft.UI.Xaml.Controls.InfoBadge"));
    WindowsDeleteString(name);
    ctl::ComPtr<wuxc::IControl> control;
    ctl::ComPtr<wux::IFrameworkElement> fe;
    ctl::ComPtr<wux::IDependencyObject> dobj;
    CHECK("it answers Windows' IControl, IFrameworkElement and IDependencyObject (the framework's Control under it)",
          SUCCEEDED(as(badge.Get(), control.GetAddressOf())) && SUCCEEDED(as(badge.Get(), fe.GetAddressOf())) &&
          SUCCEEDED(as(badge.Get(), dobj.GetAddressOf())));
    CHECK("one object behind them (one IUnknown)", control && fe && identity(control.Get()) == identity(badge.Get()) &&
          identity(fe.Get()) == identity(badge.Get()));
    DOUBLE width = 0;
    CHECK("FrameworkElement.Width set and read back through it", fe && SUCCEEDED(fe->put_Width(16)) &&
          SUCCEEDED(fe->get_Width(&width)) && width == 16);

    /* its Value, a dependency property WinUI 2 registered on the framework */
    ctl::ComPtr<wuxm::IXamlMember> value;
    boolean isDP = false;
    CHECK("the type's Value member, a dependency property", SUCCEEDED(type->GetMember(wrl_wrappers::HStringReference(L"Value").Get(),
          value.GetAddressOf())) && value && SUCCEEDED(value->get_IsDependencyProperty(&isDP)) && isDP);
    if (!value)
        return;
    ctl::ComPtr<IInspectable> v, five;
    CHECK("Value: -1 (InfoBadge's default) read through the member", SUCCEEDED(value->GetValue(badge.Get(), v.GetAddressOf())) &&
          unboxed_int(v.Get(), -1));
    CHECK_HR("Value set to 5", SUCCEEDED(box_int(5, five.GetAddressOf())) ? value->SetValue(badge.Get(), five.Get()) : E_FAIL);
    CHECK("and read back", SUCCEEDED(value->GetValue(badge.Get(), v.ReleaseAndGetAddressOf())) && unboxed_int(v.Get(), 5));

    /* a DropDownButton, composed over the framework's Button */
    ctl::ComPtr<IInspectable> drop, label, got;
    ctl::ComPtr<wuxm::IXamlType> dropType;
    ctl::ComPtr<wuxc::IButton> button;
    ctl::ComPtr<wuxc::IContentControl> content;
    CHECK_HR("Microsoft.UI.Xaml.Controls.DropDownButton activated through its type", SUCCEEDED(provider->GetXamlTypeByFullName(
        wrl_wrappers::HStringReference(L"Microsoft.UI.Xaml.Controls.DropDownButton").Get(), dropType.GetAddressOf())) && dropType ?
        dropType->ActivateInstance(drop.GetAddressOf()) : E_FAIL);
    CHECK("it answers Windows' IButton and IContentControl", SUCCEEDED(as(drop.Get(), button.GetAddressOf())) &&
          SUCCEEDED(as(drop.Get(), content.GetAddressOf())));
    ctl::ComPtr<wf::IPropertyValueStatics> pv;
    CHECK("its content set and read back", content &&
          SUCCEEDED(wf::GetActivationFactory(wrl_wrappers::HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), pv.ReleaseAndGetAddressOf())) &&
          SUCCEEDED(pv->CreateString(wrl_wrappers::HStringReference(L"More").Get(), label.GetAddressOf())) &&
          SUCCEEDED(content->put_Content(label.Get())) && SUCCEEDED(content->get_Content(got.GetAddressOf())) &&
          unboxed_is(got.Get(), L"More"));

    /* an InfoBadge's default style (WinUI 2's generic.xaml, which its
     * DefaultStyleResourceUri names) and its template applied */
    ctl::ComPtr<wuxc::IControl> badgeControl;
    boolean applied = false;
    HRESULT hr = as(badge.Get(), badgeControl.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = badgeControl->ApplyTemplate(&applied);
    CHECK_HR("InfoBadge.ApplyTemplate (WinUI 2's default style)", hr);
    if (FAILED(hr))
        why();

    /* an AcrylicBrush (WinUI 2's, over the framework's
     * XamlCompositionBrushBase) with its base's FallbackColor, as the theme
     * resources set it: the type, its base and the member, then markup */
    ctl::ComPtr<wuxm::IXamlType> acrylic, acrylicBase;
    ctl::ComPtr<wuxm::IXamlMember> fallback;
    HSTRING acrylicBaseName = nullptr;
    hr = provider->GetXamlTypeByFullName(wrl_wrappers::HStringReference(L"Microsoft.UI.Xaml.Media.AcrylicBrush").Get(),
                                         acrylic.GetAddressOf());
    CHECK_HR("the type Microsoft.UI.Xaml.Media.AcrylicBrush", acrylic ? hr : E_FAIL);
    if (acrylic) {
        CHECK("its base type, Windows.UI.Xaml.Media.XamlCompositionBrushBase",
              SUCCEEDED(acrylic->get_BaseType(acrylicBase.GetAddressOf())) && acrylicBase &&
              SUCCEEDED(acrylicBase->get_FullName(&acrylicBaseName)) &&
              is(acrylicBaseName, L"Windows.UI.Xaml.Media.XamlCompositionBrushBase"));
        printf("  acrylic: base %ls, RunInitializer %08lx, GetMember(FallbackColor) %08lx\n",
               acrylicBaseName ? WindowsGetStringRawBuffer(acrylicBaseName, nullptr) : L"(none)",
               (unsigned long)acrylic->RunInitializer(),
               (unsigned long)acrylic->GetMember(wrl_wrappers::HStringReference(L"FallbackColor").Get(), fallback.GetAddressOf()));
        printf("  acrylic: FallbackColor member %s\n", fallback ? "found" : "not found");
        WindowsDeleteString(acrylicBaseName);
    }
    ctl::ComPtr<wuxm::IXamlReaderStatics> acrylicReader;
    ctl::ComPtr<IInspectable> acrylicLoaded;
    hr = SUCCEEDED(statics(L"Windows.UI.Xaml.Markup.XamlReader", acrylicReader.GetAddressOf())) ?
        acrylicReader->Load(wrl_wrappers::HStringReference(
            L"<ResourceDictionary xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
            L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' xmlns:media='using:Microsoft.UI.Xaml.Media'>"
            L"<media:AcrylicBrush x:Key='Acrylic' TintColor='Gray' FallbackColor='Gray'/></ResourceDictionary>").Get(),
            acrylicLoaded.GetAddressOf()) : E_FAIL;
    CHECK_HR("an AcrylicBrush with its FallbackColor (its framework base's) loaded from markup", hr);
    if (FAILED(hr))
        why();

    /* XamlControlsResources: WinUI 2's theme resources, the dictionary an
     * app's App.xaml merges, loaded from its Source (ms-appx:///
     * Microsoft.UI.Xaml/Themes/19h1_themeresources.xaml) */
    ctl::ComPtr<IInspectable> resources, duration;
    ctl::ComPtr<wux::IResourceDictionary> dictionary;
    ctl::ComPtr<wf::Collections::IMap<IInspectable *, IInspectable *>> map;
    ctl::ComPtr<IInspectable> key;
    hr = activate(L"Microsoft.UI.Xaml.Controls.XamlControlsResources", resources.GetAddressOf());
    CHECK_HR("XamlControlsResources made", hr);
    if (FAILED(hr))
        why();
    CHECK("a Windows ResourceDictionary", resources && SUCCEEDED(as(resources.Get(), dictionary.GetAddressOf())) &&
          SUCCEEDED(as(resources.Get(), map.GetAddressOf())));
    CHECK("its theme resources looked up: ControlNormalAnimationDuration, 00:00:00.250", map &&
          SUCCEEDED(box_string(L"ControlNormalAnimationDuration", key.GetAddressOf())) &&
          SUCCEEDED(map->Lookup(key.Get(), duration.GetAddressOf())) && unboxed_is(duration.Get(), L"00:00:00.250"));
    /* its dark theme's acrylic brushes tinted (XamlControlsResources sets
     * their TintLuminosityOpacity through Windows' IAcrylicBrush2, which
     * WinUI 2's AcrylicBrush answers on NovaOS) */
    ctl::ComPtr<wf::Collections::IMap<IInspectable *, IInspectable *>> themeDictionaries, dark;
    ctl::ComPtr<IInspectable> darkKey, darkDictionary, brushKey, brush;
    ctl::ComPtr<wux::Media::IAcrylicBrush2> acrylic2;
    ctl::ComPtr<wf::IReference<DOUBLE>> luminosity;
    DOUBLE tint = 0;
    CHECK("its Default theme's AcrylicBackgroundFillColorDefaultBrush, TintLuminosityOpacity 0.96",
          dictionary && SUCCEEDED(dictionary->get_ThemeDictionaries(themeDictionaries.GetAddressOf())) &&
          SUCCEEDED(box_string(L"Default", darkKey.GetAddressOf())) &&
          SUCCEEDED(themeDictionaries->Lookup(darkKey.Get(), darkDictionary.GetAddressOf())) &&
          SUCCEEDED(as(darkDictionary.Get(), dark.GetAddressOf())) &&
          SUCCEEDED(box_string(L"AcrylicBackgroundFillColorDefaultBrush", brushKey.GetAddressOf())) &&
          SUCCEEDED(dark->Lookup(brushKey.Get(), brush.GetAddressOf())) &&
          SUCCEEDED(as(brush.Get(), acrylic2.GetAddressOf())) &&
          SUCCEEDED(acrylic2->get_TintLuminosityOpacity(luminosity.GetAddressOf())) && luminosity &&
          SUCCEEDED(luminosity->get_Value(&tint)) && tint > 0.95 && tint < 0.97);

    /* merged into the app's resources, as an app's App.xaml merges it
     * (<muxc:XamlControlsResources/> in Application.Resources), where the
     * controls' default styles find their theme resources */
    ctl::ComPtr<wux::IApplicationStatics> appStatics;
    ctl::ComPtr<wux::IApplication> current;
    ctl::ComPtr<wux::IResourceDictionary> appResources;
    ctl::ComPtr<wf::Collections::IVector<wux::ResourceDictionary *>> merged;
    CHECK_HR("XamlControlsResources merged into Application.Current.Resources", dictionary &&
             SUCCEEDED(statics(L"Windows.UI.Xaml.Application", appStatics.GetAddressOf())) &&
             SUCCEEDED(appStatics->get_Current(current.GetAddressOf())) && current &&
             SUCCEEDED(current->get_Resources(appResources.GetAddressOf())) && appResources &&
             SUCCEEDED(appResources->get_MergedDictionaries(merged.GetAddressOf())) && merged ?
             merged->Append(dictionary.Get()) : E_FAIL);
    default_style_checks();
    navigation_checks();

    /* WinUI 2's XAML resources, merged as Microsoft's build merges them,
     * where an unpackaged program's ms-appx:///Microsoft.UI.Xaml/Themes/
     * finds them (the files XamlControlsResources and the controls'
     * DefaultStyleResourceUri name on NovaOS's Windows 10 contracts) */
    static const wchar_t *const themes[] = {L"19h1_themeresources.xaml", L"19h1_compact_themeresources.xaml",
                                            L"19h1_generic.xaml", L"19h1_themeresources_v1.xaml",
                                            L"19h1_compact_themeresources_v1.xaml", L"19h1_generic_v1.xaml"};
    for (const wchar_t *t : themes) {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"C:\\Programs\\Microsoft.UI.Xaml\\Themes\\%ls", t);
        WIN32_FILE_ATTRIBUTE_DATA fa;
        char what[96];
        snprintf(what, sizeof what, "Microsoft.UI.Xaml\\Themes\\%ls installed", t);
        CHECK(what, GetFileAttributesExW(path, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 10000);
    }
}

/* the framework's data binding (step 32.5's fourth part): {Binding} in
 * markup to the element itself, to its DataContext and, in a control
 * template, to the templated parent; a Binding made in code and set with
 * FrameworkElement.SetBinding, following its source's changes; and a
 * control's template applied (ApplyTemplate), its parts in the visual tree */
static void binding_checks()
{
    printf("errors before the binding checks:\n");
    why();
    ctl::ComPtr<wuxm::IXamlReaderStatics> reader;
    ctl::ComPtr<IInspectable> page;
    /* each form on its own first */
    static const wchar_t *const forms[] = {
        L"<TextBlock " XMLNS L"Tag='bound' Text='{Binding Tag, RelativeSource={RelativeSource Self}}'/>",
        L"<TextBlock " XMLNS L"DataContext='from the context' Text='{Binding}'/>",
        L"<ControlTemplate " XMLNS L"TargetType='ContentControl'><Border><TextBlock "
        L"Text='{Binding Tag, RelativeSource={RelativeSource TemplatedParent}}'/></Border></ControlTemplate>"};
    for (const wchar_t *form : forms) {
        ctl::ComPtr<IInspectable> o;
        HRESULT hr = SUCCEEDED(statics(L"Windows.UI.Xaml.Markup.XamlReader", reader.ReleaseAndGetAddressOf())) ?
            reader->Load(wrl_wrappers::HStringReference(form).Get(), o.GetAddressOf()) : E_FAIL;
        char what[300];
        snprintf(what, sizeof what, "XamlReader.Load %ls", form);
        CHECK_HR(what, hr);
        if (FAILED(hr))
            why();
    }
    CHECK_HR("XamlReader.Load of a page with {Binding}s and a control template", SUCCEEDED(statics(L"Windows.UI.Xaml.Markup.XamlReader",
        reader.GetAddressOf())) ? reader->Load(wrl_wrappers::HStringReference(
        L"<Grid " XMLNS XMLNS_X L"><Grid.Resources><Style x:Key='Templated' TargetType='ContentControl'>"
        L"<Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ContentControl'>"
        L"<Border x:Name='Root'><TextBlock Text='{Binding Tag, RelativeSource={RelativeSource TemplatedParent}}'/></Border>"
        L"</ControlTemplate></Setter.Value></Setter></Style></Grid.Resources>"
        L"<TextBlock x:Name='Self' Tag='bound' Text='{Binding Tag, RelativeSource={RelativeSource Self}}'/>"
        L"<TextBlock x:Name='Context' DataContext='from the context' Text='{Binding}'/>"
        L"<TextBlock x:Name='Code'/>"
        L"<ContentControl x:Name='Templated' Style='{StaticResource Templated}' Tag='templated'/></Grid>").Get(),
        page.GetAddressOf()) : E_FAIL);
    if (!page)
        why();
    ctl::ComPtr<wux::IFrameworkElement> root;
    if (!page || FAILED(as(page.Get(), root.GetAddressOf())))
        return;
    auto find = [&](const wchar_t *name) {
        ctl::ComPtr<IInspectable> o;
        root->FindName(wrl_wrappers::HStringReference(name).Get(), o.GetAddressOf());
        return o;
    };
    ctl::ComPtr<IInspectable> self = find(L"Self"), context = find(L"Context"), code = find(L"Code"),
                              templated = find(L"Templated");
    CHECK("{Binding Tag, RelativeSource={RelativeSource Self}}: the element's own Tag", text_is(self.Get(), L"bound"));
    CHECK("{Binding}: the element's DataContext", text_is(context.Get(), L"from the context"));

    /* a Binding made in code, its source the first TextBlock's Tag */
    ctl::ComPtr<wux::Data::IBindingFactory> bf;
    ctl::ComPtr<IInspectable> inner;
    ctl::ComPtr<wux::Data::IBinding> binding;
    CHECK_HR("Windows.UI.Xaml.Data.Binding made", SUCCEEDED(statics(L"Windows.UI.Xaml.Data.Binding", bf.GetAddressOf())) ?
             bf->CreateInstance(nullptr, inner.GetAddressOf(), binding.GetAddressOf()) : E_FAIL);
    ctl::ComPtr<wux::IPropertyPathFactory> ppf;
    ctl::ComPtr<wux::IPropertyPath> path;
    ctl::ComPtr<wuxc::ITextBlockStatics> tbs;
    ctl::ComPtr<wux::IDependencyProperty> textProperty;
    ctl::ComPtr<wux::Data::IBindingBase> bindingBase;
    ctl::ComPtr<wux::IFrameworkElement> codeElement;
    CHECK_HR("its Path (PropertyPath 'Tag') and Source set, FrameworkElement.SetBinding(TextBlock.TextProperty)",
             binding && self && code && SUCCEEDED(statics(L"Windows.UI.Xaml.PropertyPath", ppf.GetAddressOf())) &&
             SUCCEEDED(ppf->CreateInstance(wrl_wrappers::HStringReference(L"Tag").Get(), path.GetAddressOf())) &&
             SUCCEEDED(binding->put_Path(path.Get())) && SUCCEEDED(binding->put_Source(self.Get())) &&
             SUCCEEDED(statics(L"Windows.UI.Xaml.Controls.TextBlock", tbs.GetAddressOf())) &&
             SUCCEEDED(tbs->get_TextProperty(textProperty.GetAddressOf())) &&
             SUCCEEDED(as(binding.Get(), bindingBase.GetAddressOf())) && SUCCEEDED(as(code.Get(), codeElement.GetAddressOf())) ?
             codeElement->SetBinding(textProperty.Get(), bindingBase.Get()) : E_FAIL);
    CHECK("the target reads its source", text_is(code.Get(), L"bound"));
    ctl::ComPtr<wux::IFrameworkElement> selfElement;
    ctl::ComPtr<IInspectable> changed;
    CHECK_HR("the source's Tag changed", self && SUCCEEDED(as(self.Get(), selfElement.GetAddressOf())) &&
             SUCCEEDED(box_string(L"changed", changed.GetAddressOf())) ? selfElement->put_Tag(changed.Get()) : E_FAIL);
    CHECK("both bindings follow it", text_is(code.Get(), L"changed") && text_is(self.Get(), L"changed"));

    /* templates set directly, plain, with a TemplateBinding and with a
     * {Binding} to the templated parent */
#define TEMPLATED(t) L"<ContentControl " XMLNS L"Tag='t'><ContentControl.Template><ControlTemplate " \
    L"TargetType='ContentControl'>" t L"</ControlTemplate></ContentControl.Template></ContentControl>"
    static const wchar_t *const templates[] = {
        TEMPLATED(L"<Border/>"), TEMPLATED(L"<Border Tag='{TemplateBinding Tag}'/>"),
        TEMPLATED(L"<Border Tag='{Binding Tag, RelativeSource={RelativeSource TemplatedParent}}'/>")};
    for (const wchar_t *markup : templates) {
        ctl::ComPtr<IInspectable> o;
        ctl::ComPtr<wuxc::IControl> c;
        boolean ok = false;
        HRESULT hr = SUCCEEDED(reader->Load(wrl_wrappers::HStringReference(markup).Get(), o.GetAddressOf())) &&
            SUCCEEDED(as(o.Get(), c.GetAddressOf())) ? c->ApplyTemplate(&ok) : E_FAIL;
        char what[300];
        snprintf(what, sizeof what, "ApplyTemplate of %ls", markup);
        CHECK_HR(what, hr);
        if (FAILED(hr))
            why();
    }

    /* the ContentControl's template applied: Border > TextBlock, bound to
     * the templated parent's Tag */
    ctl::ComPtr<wuxc::IControl> control;
    boolean applied = false;
    HRESULT hrApply = templated && SUCCEEDED(as(templated.Get(), control.GetAddressOf())) ? control->ApplyTemplate(&applied) : E_FAIL;
    CHECK_HR("Control.ApplyTemplate", hrApply);
    if (FAILED(hrApply))
        why();
    CHECK("the template applied", applied);
    ctl::ComPtr<wux::Media::IVisualTreeHelperStatics> vth;
    ctl::ComPtr<wux::IDependencyObject> templatedDO, border, label;
    INT32 n = 0;
    CHECK("its root, a Border, the control's one visual child", templated &&
          SUCCEEDED(statics(L"Windows.UI.Xaml.Media.VisualTreeHelper", vth.GetAddressOf())) &&
          SUCCEEDED(as(templated.Get(), templatedDO.GetAddressOf())) && SUCCEEDED(vth->GetChildrenCount(templatedDO.Get(), &n)) &&
          n == 1 && SUCCEEDED(vth->GetChild(templatedDO.Get(), 0, border.GetAddressOf())) && border);
    ctl::ComPtr<wuxc::IBorder> b;
    CHECK("(Windows' IBorder)", border && SUCCEEDED(as(border.Get(), b.GetAddressOf())));
    CHECK("its TextBlock bound to the templated parent's Tag", border && SUCCEEDED(vth->GetChildrenCount(border.Get(), &n)) &&
          n == 1 && SUCCEEDED(vth->GetChild(border.Get(), 0, label.GetAddressOf())) && text_is(label.Get(), L"templated"));
}

/* WinUI 2's XAML type table, made once (on the UI thread, when the
 * framework or the test first asks) */
static wuxm::IXamlMetadataProvider *winui2_provider()
{
    static wuxm::IXamlMetadataProvider *provider;
    ctl::ComPtr<IInspectable> made;
    if (!provider && SUCCEEDED(activate(L"Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider", made.GetAddressOf())))
        as(made.Get(), &provider);
    return provider;
}

/* the UWP start-up (step 32.5's fourth part): this program installed as a
 * UWP app (C:\Tests\Appx\XamlUwpTest.msix, tools/appx/mkuwptest.py) and
 * started by its AUMID; inside the package Application.Start takes the UWP
 * path, CoreApplication.Run with the framework's FrameworkViewSource
 * (twinapi.appcore's CoreApplication gives its view a CoreWindow), and
 * OnLaunched finds the CoreWindow's Window as Window.Current */
#define UWP_FAMILY L"NovaOS.XamlUwpTest_rtg2wbn51qgvw"
#define UWP_LOG L"C:\\Temp\\winui2test-uwp.log"
static bool s_uwp;
__declspec(dllimport) extern "C" LONG WINAPI GetCurrentPackageFullName(UINT32 *len, PWSTR name);

static void uwp_checks()
{
    WCHAR full[256];
    UINT32 n = ARRAYSIZE(full);
    CHECK("packaged: its package full name", GetCurrentPackageFullName(&n, full) == ERROR_SUCCESS);
    AppPolicyWindowingModel model = AppPolicyWindowingModel_None;
    CHECK("its windowing model: Universal", AppPolicyGetWindowingModel(GetCurrentThreadEffectiveToken(), &model) ==
          ERROR_SUCCESS && model == AppPolicyWindowingModel_Universal);
    ctl::ComPtr<wux::IWindowStatics> ws;
    ctl::ComPtr<wux::IWindow> window;
    CHECK("Window.Current, the view's window", SUCCEEDED(statics(L"Windows.UI.Xaml.Window", ws.GetAddressOf())) &&
          SUCCEEDED(ws->get_Current(window.GetAddressOf())) && window);
    if (!window)
        return;
    ctl::ComPtr<ABI::Windows::UI::Core::ICoreWindow> core;
    CHECK("its CoreWindow (twinapi.appcore's)", SUCCEEDED(window->get_CoreWindow(core.GetAddressOf())) && core);
    ctl::ComPtr<wuxm::IXamlReaderStatics> reader;
    ctl::ComPtr<IInspectable> page, got;
    ctl::ComPtr<wux::IUIElement> element;
    CHECK_HR("Window.Content set to a page", SUCCEEDED(statics(L"Windows.UI.Xaml.Markup.XamlReader", reader.GetAddressOf())) &&
             SUCCEEDED(reader->Load(wrl_wrappers::HStringReference(L"<TextBlock " XMLNS L"Text='Hello, UWP'/>").Get(),
                                    page.GetAddressOf())) && SUCCEEDED(as(page.Get(), element.GetAddressOf())) ?
             window->put_Content(element.Get()) : E_FAIL);
    ctl::ComPtr<wux::IUIElement> content;
    CHECK("and read back", SUCCEEDED(window->get_Content(content.GetAddressOf())) && content &&
          identity(content.Get()) == identity(element.Get()));
    CHECK_HR("Window.Activate", window->Activate());
}

/* the parent: the package installed, started by its AUMID ("uwp"), its log
 * read, the package removed */
static DWORD appx(const wchar_t *args)
{
    wchar_t cmd[600];
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    DWORD code = 0xFFFF;
    swprintf(cmd, 600, L"appx.exe %ls", args);
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return code;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static void uwp_parent()
{
    static const GUID clsidAam = {0x45BA127D, 0x10A8, 0x46EA, {0x8A, 0xB7, 0x56, 0xEA, 0x90, 0x78, 0x94, 0x3C}};
    static const GUID iidAam = {0x2E941141, 0x7F97, 0x4756, {0xBA, 0x1D, 0x9D, 0xEC, 0xDE, 0x89, 0x4A, 0x3D}};
    CreateDirectoryW(L"C:\\Temp", nullptr);
    appx(L"remove NovaOS.XamlUwpTest");                        /* (a run that stopped half way) */
    DeleteFileW(UWP_LOG);
    CHECK("the UWP package installed (appx install XamlUwpTest.msix)",
          appx(L"install C:\\Tests\\Appx\\XamlUwpTest.msix -AllowUnsigned") == 0);
    IUnknown *am = nullptr;
    DWORD pid = 0, code = 0xFFFF;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT hr = CoCreateInstance(clsidAam, nullptr, CLSCTX_INPROC_SERVER, iidAam, reinterpret_cast<void **>(&am));
    if (SUCCEEDED(hr) && am) {
        typedef HRESULT (STDMETHODCALLTYPE *Activate)(void *, LPCWSTR, LPCWSTR, int, DWORD *);
        hr = reinterpret_cast<Activate>((*reinterpret_cast<void ***>(am))[3])(am, UWP_FAMILY L"!App", L"uwp", 0, &pid);
        am->Release();
    }
    CHECK_HR("started by its AUMID (ApplicationActivationManager)", SUCCEEDED(hr) && pid ? S_OK : FAILED(hr) ? hr : E_FAIL);
    HANDLE p = pid ? OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
    if (p) {
        WaitForSingleObject(p, 120000);
        GetExitCodeProcess(p, &code);
        CloseHandle(p);
    }
    FILE *log = _wfopen(UWP_LOG, L"r");
    char line[512];
    while (log && fgets(line, sizeof line, log))
        printf("  uwp: %s", line);
    if (log)
        fclose(log);
    CHECK("the UWP app passed (Application.Start's UWP path: Window.Current, its CoreWindow, content, Activate)", code == 0);
    CHECK("the UWP package removed", appx(L"remove NovaOS.XamlUwpTest") == 0);
}

/* the type of WinUI 2's IVector<Object> properties (NavigationView's
 * MenuItems, RadioButtons' Items), as the XAML compiler writes it into an
 * app's XamlTypeInfo when the app's markup fills one as a property element:
 * a collection whose items the parser adds to the property's vector
 * (WinUI 2's own provider has no such type; on Windows, too, the app's
 * generated provider is the one that answers for it) */
static const wchar_t s_object_vector[] = L"Windows.Foundation.Collections.IVector`1<Object>";

class ObjectVectorType : public wuxm::IXamlType
{
public:
    LONG m_refs = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(wuxm::IXamlType)) {
            *out = static_cast<wuxm::IXamlType *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_refs);
        if (!r) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *n, IID **iids) override { *n = 0; *iids = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override { *name = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }

    HRESULT STDMETHODCALLTYPE get_BaseType(wuxm::IXamlType **r) override { *r = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ContentProperty(wuxm::IXamlMember **r) override { *r = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FullName(HSTRING *r) override
    {
        return WindowsCreateString(s_object_vector, (UINT32)wcslen(s_object_vector), r);
    }
    HRESULT STDMETHODCALLTYPE get_IsArray(::boolean *r) override { *r = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsCollection(::boolean *r) override { *r = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsConstructible(::boolean *r) override { *r = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsDictionary(::boolean *r) override { *r = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsMarkupExtension(::boolean *r) override { *r = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsBindable(::boolean *r) override { *r = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ItemType(wuxm::IXamlType **r) override { *r = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_KeyType(wuxm::IXamlType **r) override { *r = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_UnderlyingType(wux::Interop::TypeName *r) override
    {
        r->Kind = wux::Interop::TypeKind_Metadata;
        return WindowsCreateString(s_object_vector, (UINT32)wcslen(s_object_vector), &r->Name);
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **r) override { *r = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateFromString(HSTRING, IInspectable **r) override { *r = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMember(HSTRING, wuxm::IXamlMember **r) override { *r = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE AddToVector(IInspectable *instance, IInspectable *value) override
    {
        ctl::ComPtr<wf::Collections::IVector<IInspectable *>> vector;
        HRESULT hr = reinterpret_cast<IUnknown *>(instance)->QueryInterface(__uuidof(wf::Collections::IVector<IInspectable *>),
                                                                             reinterpret_cast<void **>(vector.GetAddressOf()));
        return SUCCEEDED(hr) ? vector->Append(value) : hr;
    }
    HRESULT STDMETHODCALLTYPE AddToMap(IInspectable *, IInspectable *, IInspectable *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RunInitializer() override { return S_OK; }
};

/* the framework started as wuxamltest starts it; the app's Application is
 * its IXamlMetadataProvider, as a WinUI 2 app's (its generated XamlTypeInfo
 * hands the framework WinUI 2's types) */
static bool s_launched, s_exit_called;

class App : public xaml::IApplicationOverrides, public wuxm::IXamlMetadataProvider
{
public:
    IInspectable *m_inner = nullptr;
    LONG m_refs = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(xaml::IApplicationOverrides)) {
            *out = static_cast<xaml::IApplicationOverrides *>(this);
            AddRef();
            return S_OK;
        }
        if (iid == __uuidof(wuxm::IXamlMetadataProvider)) {
            *out = static_cast<wuxm::IXamlMetadataProvider *>(this);
            AddRef();
            return S_OK;
        }
        if (!m_inner) { *out = nullptr; return E_NOINTERFACE; }
        return m_inner->QueryInterface(iid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_refs);
        if (!r) { if (m_inner) m_inner->Release(); delete this; }
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *n, IID **iids) override { *n = 0; *iids = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override
    {
        return WindowsCreateString(L"Winui2Test.App", 14, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *t) override { *t = BaseTrust; return S_OK; }

    HRESULT STDMETHODCALLTYPE GetXamlType(wux::Interop::TypeName type, wuxm::IXamlType **result) override
    {
        *result = nullptr;
        if (type.Name && !wcscmp(WindowsGetStringRawBuffer(type.Name, nullptr), s_object_vector)) {
            *result = new ObjectVectorType;
            return S_OK;
        }
        wuxm::IXamlMetadataProvider *p = winui2_provider();
        return p ? p->GetXamlType(type, result) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetXamlTypeByFullName(HSTRING name, wuxm::IXamlType **result) override
    {
        *result = nullptr;
        if (name && !wcscmp(WindowsGetStringRawBuffer(name, nullptr), s_object_vector)) {
            *result = new ObjectVectorType;
            return S_OK;
        }
        wuxm::IXamlMetadataProvider *p = winui2_provider();
        return p ? p->GetXamlTypeByFullName(name, result) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetXmlnsDefinitions(UINT32 *n, wuxm::XmlnsDefinition **defs) override
    {
        *n = 0;
        *defs = nullptr;
        wuxm::IXamlMetadataProvider *p = winui2_provider();
        return p ? p->GetXmlnsDefinitions(n, defs) : S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnLaunched(xaml::ILaunchActivatedEventArgs *) override
    {
        s_launched = true;
        if (s_uwp) {
            uwp_checks();
            ctl::ComPtr<xaml::IApplication> app;
            CHECK_HR("Application.Exit (CoreApplication.Exit)", SUCCEEDED(QueryInterface(__uuidof(xaml::IApplication),
                     reinterpret_cast<void **>(app.GetAddressOf()))) ? app->Exit() : E_FAIL);
            s_exit_called = true;
            return S_OK;
        }
        binding_checks();
        winui2_checks();
        ctl::ComPtr<msy::IDispatcherQueueStatics> queueStatics;
        ctl::ComPtr<msy::IDispatcherQueue> queue;
        CHECK("the thread's DispatcherQueue", SUCCEEDED(wf::GetActivationFactory(
            wrl_wrappers::HStringReference(RuntimeClass_Microsoft_UI_Dispatching_DispatcherQueue).Get(), queueStatics.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(queueStatics->GetForCurrentThread(&queue)) && queue);
        if (!queue) return S_OK;
        boolean queued = false;
        CHECK_HR("work queued on it", queue->TryEnqueue(wrl::Callback<msy::IDispatcherQueueHandler>([this]() -> HRESULT {
            ctl::ComPtr<xaml::IApplication> app;
            CHECK_HR("Application.Exit", SUCCEEDED(QueryInterface(__uuidof(xaml::IApplication),
                     reinterpret_cast<void **>(app.GetAddressOf()))) ? app->Exit() : E_FAIL);
            s_exit_called = true;
            return S_OK;
        }).Get(), &queued));
        return S_OK;
    }
};
static App *s_app;

static HRESULT on_start(xaml::IApplicationInitializationCallbackParams *)
{
    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationFactory> factory;
    if (appFactory) appFactory.As(&factory);
    s_app = new App();
    ctl::ComPtr<xaml::IApplication> instance;
    CHECK_HR("the app's Application", factory ? factory->CreateInstance(
        static_cast<xaml::IApplicationOverrides *>(s_app), &s_app->m_inner, instance.ReleaseAndGetAddressOf()) : E_FAIL);
    return S_OK;
}

static DWORD WINAPI watchdog(void *)
{
    Sleep(90000);
    printf(s_uwp ? "FAIL: Application.Start did not return\nthe UWP app: %d checks passed, %d did not (%s)\n" :
           "FAIL: Application.Start did not return\nwinui2test: %d passed, %d failed (%s)\n", pass, fail + 1,
           sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(1);
}

/* the classes this program serves, as Windows.UI.Xaml.dll serves them: the
 * framework's WindowsXamlManager (which Application.Start activates by its
 * name) and, by their Windows names (nova_xaml_windows_factory), the
 * Windows.UI.Xaml classes WinUI 2's controls compose over or call */
static const wchar_t *const s_windows_classes[] = {
    L"Windows.UI.Xaml.DependencyObject", L"Windows.UI.Xaml.DependencyProperty", L"Windows.UI.Xaml.PropertyMetadata",
    L"Windows.UI.Xaml.UIElement", L"Windows.UI.Xaml.FrameworkElement", L"Windows.UI.Xaml.Application",
    L"Windows.UI.Xaml.Window", L"Windows.UI.Xaml.VisualStateManager", L"Windows.UI.Xaml.ResourceDictionary",
    L"Windows.UI.Xaml.Style", L"Windows.UI.Xaml.Setter", L"Windows.UI.Xaml.DataTemplate",
    L"Windows.UI.Xaml.Controls.Control", L"Windows.UI.Xaml.Controls.ContentControl", L"Windows.UI.Xaml.Controls.Button",
    L"Windows.UI.Xaml.Controls.Primitives.ButtonBase", L"Windows.UI.Xaml.Controls.Primitives.ToggleButton",
    L"Windows.UI.Xaml.Controls.Primitives.FlyoutBase", L"Windows.UI.Xaml.Controls.IconElement",
    L"Windows.UI.Xaml.Controls.FontIcon", L"Windows.UI.Xaml.Controls.SymbolIcon", L"Windows.UI.Xaml.Controls.BitmapIcon",
    L"Windows.UI.Xaml.Controls.PathIcon", L"Windows.UI.Xaml.Controls.TextBlock", L"Windows.UI.Xaml.Controls.Grid",
    L"Windows.UI.Xaml.Controls.Border", L"Windows.UI.Xaml.Controls.Image", L"Windows.UI.Xaml.Controls.ToolTipService",
    L"Windows.UI.Xaml.Media.VisualTreeHelper", L"Windows.UI.Xaml.Media.SolidColorBrush", L"Windows.UI.Xaml.Media.Brush",
    L"Windows.UI.Xaml.Media.XamlCompositionBrushBase", L"Windows.UI.Xaml.Media.XamlLight",
    L"Windows.UI.Xaml.Media.CompositionTarget", L"Windows.UI.Xaml.Data.Binding", L"Windows.UI.Xaml.Data.RelativeSource",
    L"Windows.UI.Xaml.PropertyPath", L"Windows.UI.Xaml.Controls.ControlTemplate",
    L"Windows.UI.Xaml.Automation.AutomationProperties", L"Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer",
    L"Windows.UI.Xaml.Automation.Peers.ButtonAutomationPeer", L"Windows.UI.Xaml.Hosting.ElementCompositionPreview",
    L"Windows.UI.Xaml.Markup.XamlReader",
    L"Windows.UI.Xaml.Controls.Primitives.RangeBase", L"Windows.UI.Xaml.Controls.UserControl", L"Windows.UI.Xaml.Controls.Panel",
    /* (NavigationView's SelectionModel raises PropertyChanged; WinUI 2's
     * peers ask AutomationPeer's statics; ItemsRepeater's data source
     * raises CollectionChanged when an items vector changes) */
    L"Windows.UI.Xaml.Data.PropertyChangedEventArgs", L"Windows.UI.Xaml.Automation.Peers.AutomationPeer",
    L"Windows.UI.Xaml.Interop.NotifyCollectionChangedEventArgs",
    /* (and what NavigationView, RadioButtons and ItemsRepeater make in
     * code: transforms for their animations, the pane's SplitView, tool
     * tips, RadioButtons' items) */
    L"Windows.UI.Xaml.Media.ScaleTransform", L"Windows.UI.Xaml.Media.TranslateTransform",
    L"Windows.UI.Xaml.Media.CompositeTransform", L"Windows.UI.Xaml.Media.RotateTransform",
    L"Windows.UI.Xaml.Media.TransformGroup", L"Windows.UI.Xaml.Media.Animation.Storyboard",
    L"Windows.UI.Xaml.Controls.SplitView", L"Windows.UI.Xaml.Controls.ToolTip",
    L"Windows.UI.Xaml.Controls.RadioButton", L"Windows.UI.Xaml.Controls.StackPanel",
    L"Windows.UI.Xaml.Controls.Canvas", L"Windows.UI.Xaml.Controls.ContentPresenter",
    L"Windows.UI.Xaml.Shapes.Rectangle",
    /* (and what the overrides WinUI 2's controls now get make: SwipeControl's
     * clip, a RectangleGeometry, NavigationView's pane button's keyboard
     * accelerator and pane's ThemeShadow, the LayoutInformation
     * ItemsRepeater's layouts ask, and the binding, thickness and grid
     * length helpers and settings item's fallback icon NavigationView's
     * template asks) */
    L"Windows.UI.Xaml.Media.RectangleGeometry", L"Windows.UI.Xaml.Input.KeyboardAccelerator",
    L"Windows.UI.Xaml.Controls.Primitives.LayoutInformation", L"Windows.UI.Xaml.Media.ThemeShadow",
    L"Windows.UI.Xaml.Data.BindingOperations", L"Windows.UI.Xaml.ThicknessHelper",
    L"Windows.UI.Xaml.GridLengthHelper", L"Windows.UI.Xaml.CornerRadiusHelper",
    L"Windows.UI.Xaml.Controls.SymbolIconSource", L"Windows.UI.Xaml.Controls.FontIconSource",
    L"Windows.UI.Xaml.Controls.Viewbox", L"Windows.UI.Xaml.Shapes.Path", L"Windows.UI.Xaml.VisualStateGroup",
    /* (and, for the UWP start-up, the view source Application.Start hands
     * CoreApplication.Run and the view it makes) */
    L"Microsoft.UI.Xaml.FrameworkViewSource", L"Microsoft.UI.Xaml.FrameworkView"};

namespace DirectUI
{
    _Check_return_ IActivationFactory *CreateActivationFactory_FrameworkViewSource();
    _Check_return_ IActivationFactory *CreateActivationFactory_FrameworkView();
}

static HRESULT WINAPI make_factory(HSTRING cls, IActivationFactory **out)
{
    *out = nullptr;
    if (is(cls, RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager)) {
        ctl::ComPtr<DirectUI::WindowsXamlManagerFactory> f;
        IFC_RETURN(ctl::ComObject<DirectUI::WindowsXamlManagerFactory>::CreateInstance(f.ReleaseAndGetAddressOf()));
        return f.CopyTo(out);
    }
    if (is(cls, RuntimeClass_Microsoft_UI_Xaml_FrameworkViewSource)) {
        *out = DirectUI::CreateActivationFactory_FrameworkViewSource();
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    if (is(cls, RuntimeClass_Microsoft_UI_Xaml_FrameworkView)) {
        *out = DirectUI::CreateActivationFactory_FrameworkView();
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    return nova_xaml_windows_factory(cls, out);
}

int main()
{
    s_uwp = wcsstr(GetCommandLineW(), L" uwp") != nullptr;
    if (s_uwp)
        _wfreopen(UWP_LOG, L"w", stdout);
    DirectUI::StaticLockGlobalInit();
    DXamlInstanceStorage::Initialize();
    DirectUI::FrameworkApplication::GlobalInit();
    IPlatformServices *pal = nullptr;
    CHECK_HR("ObtainPlatformServices", ObtainPlatformServices(&pal));
    SystemThemingInterop::OverrideSystemTheme(Theming::Theme::Dark);
    const UINT32 nclasses = 1 + ARRAYSIZE(s_windows_classes);
    HSTRING classes[nclasses];
    PFNGETACTIVATIONFACTORY makers[nclasses];
    WindowsCreateString(RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager,
                        (UINT32)wcslen(RuntimeClass_Microsoft_UI_Xaml_Hosting_WindowsXamlManager), &classes[0]);
    for (UINT32 i = 0; i < nclasses; i++) {
        if (i) WindowsCreateString(s_windows_classes[i - 1], (UINT32)wcslen(s_windows_classes[i - 1]), &classes[i]);
        makers[i] = make_factory;
    }
    RO_REGISTRATION_COOKIE cookie = nullptr;
    CHECK_HR("RoRegisterActivationFactories (the framework's classes)", RoRegisterActivationFactories(classes, makers, nclasses, &cookie));
    CloseHandle(CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr));
    if (!s_uwp) {
        HMODULE mux = LoadLibraryW(L"Microsoft.UI.Xaml.dll");
        CHECK("Microsoft.UI.Xaml.dll (WinUI 2) loaded", mux != nullptr);
        s_mux = mux ? reinterpret_cast<GetFactory>(GetProcAddress(mux, "DllGetActivationFactory")) : nullptr;
        CHECK("its DllGetActivationFactory", s_mux != nullptr);
    }

    ctl::ComPtr<DirectUI::FrameworkApplicationFactory> appFactory;
    ctl::ComObject<DirectUI::FrameworkApplicationFactory>::CreateInstance(appFactory.ReleaseAndGetAddressOf());
    ctl::ComPtr<xaml::IApplicationStatics> appStatics;
    CHECK("Application's statics", appFactory && SUCCEEDED(appFactory.As(&appStatics)));
    if (!appStatics) return 1;
    CHECK_HR("Application.Start", appStatics->Start(wrl::Callback<xaml::IApplicationInitializationCallback>(on_start).Get()));
    CHECK("OnLaunched ran", s_launched);
    CHECK("Application.Exit returned from Application.Start", s_exit_called);

    RoRevokeActivationFactories(cookie);
    if (!s_uwp)
        uwp_parent();
    /* (the UWP app's own count says it differently: the self-test reads
     * the first count in the output, and the parent prints the app's log) */
    printf(s_uwp ? "the UWP app: %d checks passed, %d did not (%s)\n" : "winui2test: %d passed, %d failed (%s)\n",
           pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    ExitProcess(fail != 0);
}
