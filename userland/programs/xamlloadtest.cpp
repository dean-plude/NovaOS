/* xamlloadtest.exe — Microsoft's XAML resource dictionaries and object
 * writer built for NovaOS (roadmap step 32.3, parts 7 to 9;
 * docs/design/windows-ui-xaml.md): CResourceDictionary from
 * dxaml/xcp/core/core/elements (Resources.cpp: keyed resources and their
 * lookup) and the parser's object writer runtime from dxaml/xcp/core/Parser
 * (Parser.Runtime: XamlNativeRuntime creating the core's objects and adding
 * them to collections and dictionaries), so XAML text is loaded into a live
 * object tree: a ResourceDictionary of strings, numbers, booleans and
 * colours, read by XmlLite, parsed into the node stream and written into
 * objects; and (part 8) a dictionary's merged dictionaries and theme
 * dictionaries, set through field-backed properties whose offsets come
 * from the core's property runtime data
 * (userland/windows_ui_xaml/src/property_data.cpp), searched by lookup;
 * and (part 9) all of it on the XAML core's own core services
 * (CCoreServices, core/dll/xcpcore.cpp), which load the XAML as
 * XamlReader.Load has them load it (CCoreServices::ParseXaml).
 *
 * Until the core's generated type table builds, the table here activates
 * the handful of types the cases use (below).
 *
 *   xamlloadtest       the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <new>
#include <corep.h>
#include <CDependencyObject.h>
#include <Resources.h>
#include <CString.h>
#include <Double.h>
#include <primitives.h>
#include <CColor.h>
#include <TypeTableStructsPrivate.h>
#include <CStaticLock.h>
#include <ParserAPI.h>
#include <FrameworkTheming.h>
#include <ThemeWalkResourceCache.h>
#include "TestThemingInterop.h"

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define CHECK_HR(what, expr) do { HRESULT h_ = (expr); if (SUCCEEDED(h_)) pass++; else { fail++; \
    printf("FAIL: %s: 0x%08lx (line %d)\n", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } } while (0)

/* the types the object writer may create here, by the core's own
 * constructors (the XAML core's generated table, core/metadata
 * DynamicMetadata.g.cpp, has every type's) */
static HRESULT create_dictionary(CDependencyObject **out, CREATEPARAMETERS *create)
{
    return CResourceDictionary::Create(out, create);
}

#define ACT(type, fn) [static_cast<int>(KnownTypeIndex::type)] = { fn, nullptr }
extern const MetaDataTypeActivation c_aTypeActivations[KnownTypeCount] = {
    ACT(ResourceDictionary, &create_dictionary),
    ACT(ResourceDictionaryCollection, &CResourceDictionaryCollection::Create),
    ACT(String, &CString::Create),
    ACT(Double, &CDouble::Create),
    ACT(Int32, &CInt32::Create),
    ACT(Boolean, &CBoolean::Create),
    ACT(Color, &CColor::Create),
};

static std::string narrow(const xstring_ptr_view &s)
{
    std::string out;
    for (XUINT32 i = 0; i < s.GetCount(); i++) out += (char)s.GetBuffer()[i];
    return out;
}

/* the core: the XAML core's own core services (CCoreServices,
 * core/dll/xcpcore.cpp, built since part 9), made by its own constructor;
 * the objects it makes name it, and the parser reads it through its
 * IParserCoreServices.  CCoreServices::Create sets up the rest of the
 * core's services, the platform layer's queues and work items, the media
 * queue, the text core and render target bitmaps, which do not build yet:
 * what the resource dictionary reads of what Create sets is set here as
 * Create sets it, through the test hooks class the core names as its
 * friend (the core's thread: collections check their items against it;
 * the theme cache and the theming, here over Microsoft's test theming,
 * system theme Dark).  XamlReader.Load loads XAML as load() does, through
 * CCoreServices::ParseXaml. */
namespace DirectUI {
class DxamlCoreTestHooks {
public:
    static CCoreServices *Make()
    {
        CCoreServices *c = new CCoreServices;
        c->m_nThreadID = GetCurrentThreadId();
        c->m_spTheming.reset(new FrameworkTheming(std::make_shared<TestThemingInterop>(), []() { return S_OK; }));
        c->m_themeWalkResourceCache.reset(new ThemeWalkResourceCache());
        return c;
    }
};
}

static CDependencyObject *load(const wchar_t *xaml, HRESULT *hr_out = nullptr);

/* the core's own theme resources, which the framework loads on first
 * lookup (FrameworkCallbacks_LoadThemeResources, from its built-in theme
 * dictionaries) and a dictionary's lookup falls back on; until the
 * framework builds, a small dictionary of this test's, loaded through the
 * core and given it as the framework gives it its own */
static const wchar_t c_themeResources[] =
    L"<ResourceDictionary xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
    L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>"
    L"<x:String x:Key='FromTheme'>theme</x:String></ResourceDictionary>";

static CCoreServices *core()
{
    static CCoreServices *c;
    if (!c) {
        c = DirectUI::DxamlCoreTestHooks::Make();
        CDependencyObject *theme = load(c_themeResources);
        c->SetThemeResources(do_pointer_cast<CResourceDictionary>(theme));
        ReleaseInterface(theme);
    }
    return c;
}

/* XAML text @xaml loaded into objects by the core (CCoreServices::ParseXaml:
 * read by XmlLite, parsed into the node stream, written into objects): the
 * root (referenced), or null with the failing HRESULT in *@hr */
static CDependencyObject *load(const wchar_t *xaml, HRESULT *hr_out)
{
    Parser::XamlBuffer buffer;
    buffer.m_count = (uint32_t)(wcslen(xaml) * sizeof(wchar_t));
    buffer.m_buffer = reinterpret_cast<const BYTE *>(xaml);
    CDependencyObject *root = nullptr;
    HRESULT hr = core()->ParseXaml(buffer, /* bForceUtf16 */ true, /* bCreatePermanentNamescope */ false,
                                   /* bRequiresDefaultNamespace */ false, &root);
    if (SUCCEEDED(hr) && !root) hr = E_POINTER;
    if (hr_out) *hr_out = hr;
    return root;
}

static std::string string_of(CDependencyObject *o)
{
    CString *s = do_pointer_cast<CString>(o);
    return s ? narrow(s->m_strString) : "(not a string)";
}

static xstring_ptr str(const wchar_t *s)
{
    xstring_ptr out;
    xstring_ptr::CloneBuffer(s, (XUINT32)wcslen(s), &out);
    return out;
}

static CDependencyObject *lookup(CResourceDictionary *d, const wchar_t *key)
{
    CDependencyObject *o = nullptr;
    if (FAILED(d->GetKeyNoRef(str(key), &o))) return nullptr;
    return o;
}

static CDependencyObject *make_string(const wchar_t *text)
{
    CDependencyObject *o = nullptr;
    CREATEPARAMETERS cp(core(), str(text));
    return SUCCEEDED(CString::Create(&o, &cp)) ? o : nullptr;
}

/* CResourceDictionary on its own: keyed values added, found, refused
 * twice, removed */
static void test_dictionary()
{
    CDependencyObject *o = nullptr;
    CREATEPARAMETERS cp(core());
    CHECK("CResourceDictionary::Create", SUCCEEDED(CResourceDictionary::Create(&o, &cp)) && o);
    CResourceDictionary *d = do_pointer_cast<CResourceDictionary>(o);
    CHECK("is a CResourceDictionary", d != nullptr && o->GetTypeIndex() == KnownTypeIndex::ResourceDictionary);
    if (!d) return;
    CValue v;
    CDependencyObject *a = make_string(L"apple"), *b = make_string(L"banana");
    v.WrapObjectNoRef(a);
    CHECK_HR("Add a", d->Add(str(L"a"), &v, nullptr, false));
    v.WrapObjectNoRef(b);
    CHECK_HR("Add b", d->Add(str(L"b"), &v, nullptr, false));
    CHECK("two items", d->GetCount() == 2);
    CHECK("HasKey a", d->HasKey(str(L"a"), false));
    CHECK("lookup a", string_of(lookup(d, L"a")) == "apple");
    CHECK("lookup b", string_of(lookup(d, L"b")) == "banana");
    CHECK("missing key", lookup(d, L"c") == nullptr);
    CHECK("the core's theme resources", string_of(lookup(d, L"FromTheme")) == "theme");
    v.WrapObjectNoRef(a);
    CHECK("a duplicate key is refused", FAILED(d->Add(str(L"a"), &v, nullptr, false)));
    CHECK("Remove b", SUCCEEDED(d->Remove(str(L"b"))) && d->GetCount() == 1 && lookup(d, L"b") == nullptr);
    ReleaseInterface(a);
    ReleaseInterface(b);
    d->Release();
}

#define XMLNS L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
#define XMLNS_X L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"

/* XAML loaded into a ResourceDictionary through the object writer */
static void test_load()
{
    HRESULT hr = S_OK;
    CDependencyObject *root = load(L"<x:String " XMLNS_X L">Hello</x:String>", &hr);
    CHECK_HR("load an x:String", root ? S_OK : hr);
    CHECK("the x:String", string_of(root) == "Hello");
    if (root) root->Release();
    root = load(L"<ResourceDictionary " XMLNS XMLNS_X L"/>", &hr);
    CHECK_HR("load an empty ResourceDictionary", root ? S_OK : hr);
    if (root) root->Release();
    root = load(
        L"<ResourceDictionary " XMLNS XMLNS_X L">"
        L"<x:String x:Key='Greeting'>Hello, Kepler</x:String>"
        L"<x:Double x:Key='Size'>14.5</x:Double>"
        L"<x:Int32 x:Key='Count'>42</x:Int32>"
        L"<x:Boolean x:Key='On'>True</x:Boolean>"
        L"<Color x:Key='Accent'>#FF0078D7</Color>"
        L"</ResourceDictionary>", &hr);
    CHECK("load a ResourceDictionary", root != nullptr);
    if (!root) {
        printf("  hr %08lx\n", (unsigned long)hr);
        return;
    }
    CResourceDictionary *d = do_pointer_cast<CResourceDictionary>(root);
    CHECK("the root is a CResourceDictionary", d != nullptr);
    if (d) {
        CHECK("five resources", d->GetCount() == 5);
        CHECK("x:String", string_of(lookup(d, L"Greeting")) == "Hello, Kepler");
        CDouble *size = do_pointer_cast<CDouble>(lookup(d, L"Size"));
        CHECK("x:Double", size && size->m_eValue == 14.5f);
        CInt32 *count = do_pointer_cast<CInt32>(lookup(d, L"Count"));
        CHECK("x:Int32", count && count->m_iValue == 42);
        /* (a boolean is the core's CEnumerated of the Boolean type) */
        CEnumerated *on = do_pointer_cast<CEnumerated>(lookup(d, L"On"));
        CHECK("x:Boolean", on && on->GetEnumTypeIndex() == KnownTypeIndex::Boolean && on->m_nValue == 1);
        CColor *accent = do_pointer_cast<CColor>(lookup(d, L"Accent"));
        CHECK("Color", accent && accent->m_rgb == 0xFF0078D7);
    }
    root->Release();

    /* merged dictionaries and theme dictionaries */
    root = load(
        L"<ResourceDictionary " XMLNS XMLNS_X L">"
        L"<ResourceDictionary.MergedDictionaries>"
        L"<ResourceDictionary><x:String x:Key='Shared'>first</x:String><x:String x:Key='Deep'>deep</x:String></ResourceDictionary>"
        L"<ResourceDictionary><x:String x:Key='Shared'>second</x:String></ResourceDictionary>"
        L"</ResourceDictionary.MergedDictionaries>"
        L"<ResourceDictionary.ThemeDictionaries>"
        L"<ResourceDictionary x:Key='Light'><x:String x:Key='Themed'>light</x:String></ResourceDictionary>"
        L"<ResourceDictionary x:Key='Dark'><x:String x:Key='Themed'>dark</x:String></ResourceDictionary>"
        L"</ResourceDictionary.ThemeDictionaries>"
        L"<x:String x:Key='Own'>own</x:String>"
        L"</ResourceDictionary>", &hr);
    CHECK("load merged and theme dictionaries", root != nullptr);
    if (!root) {
        printf("  hr %08lx\n", (unsigned long)hr);
        return;
    }
    d = do_pointer_cast<CResourceDictionary>(root);
    if (d) {
        CHECK("own resource", string_of(lookup(d, L"Own")) == "own");
        CHECK("two merged dictionaries", d->GetMergedDictionaries() && d->GetMergedDictionaries()->GetCount() == 2);
        CHECK("the last merged dictionary wins", string_of(lookup(d, L"Shared")) == "second");
        CHECK("found in the first merged dictionary", string_of(lookup(d, L"Deep")) == "deep");
        CResourceDictionary *themes = d->GetThemeDictionaries();
        CHECK("theme dictionaries", themes && themes->IsThemeDictionaries() && themes->GetCount() == 2);
        CResourceDictionary *dark = themes ? do_pointer_cast<CResourceDictionary>(lookup(themes, L"Dark")) : nullptr;
        CHECK("the Dark theme's dictionary", dark && string_of(lookup(dark, L"Themed")) == "dark");
    }
    root->Release();

    /* malformed XAML fails to load, it does not crash */
    root = load(L"<ResourceDictionary " XMLNS XMLNS_X L"><x:String x:Key='a'>unclosed</x:String>", &hr);
    CHECK("unclosed XAML is an error", root == nullptr && FAILED(hr));
    if (root) root->Release();
}

int main()
{
    DirectUI::StaticLockGlobalInit();
    test_dictionary();
    test_load();
    printf("xamlloadtest: %d passed, %d failed (%s)\n", pass, fail, sizeof(void *) == 8 ? "64-bit" : "32-bit");
    fflush(stdout);
    return fail != 0;
}
