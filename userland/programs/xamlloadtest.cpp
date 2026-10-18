/* xamlloadtest.exe — Microsoft's XAML resource dictionaries and object
 * writer built for NovaOS (roadmap step 32.3, part 7;
 * docs/design/windows-ui-xaml.md): CResourceDictionary from
 * dxaml/xcp/core/core/elements (Resources.cpp: keyed resources and their
 * lookup) and the parser's object writer runtime from dxaml/xcp/core/Parser
 * (Parser.Runtime: XamlNativeRuntime creating the core's objects and adding
 * them to collections and dictionaries), so XAML text is loaded into a live
 * object tree: a ResourceDictionary of strings, numbers, booleans and
 * colours, read by XmlLite, parsed into the node stream and written into
 * objects.
 *
 * Until the XAML core (CCoreServices) builds, the parser's core services
 * are Microsoft's mock from its parser unit tests, laid where the core's
 * object would hold them, and the type table activates the handful of
 * types the cases use (below).
 *
 *   xamlloadtest       the self-test runs it with no arguments
 */
#include <stdio.h>
#include <string.h>
#include <new>
#include "MockParserCoreServices.h"
#include <corep.h>
#include <CDependencyObject.h>
#include <Resources.h>
#include <CString.h>
#include <Double.h>
#include <primitives.h>
#include <CColor.h>
#include <TypeTableStructsPrivate.h>
#include <XamlSchemaContext.h>
#include <XamlTextReader.h>
#include <XamlTextReaderSettings.h>
#include <ObjectWriter.h>
#include <ObjectWriterSettings.h>
#include <XamlQualifiedObject.h>
#include <ParserErrorService.h>
#include <CStaticLock.h>
#include <FrameworkTheming.h>
#include <ThemeWalkResourceCache.h>
#include "TestThemingInterop.h"

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)
#define CHECK_HR(what, expr) do { HRESULT h_ = (expr); if (SUCCEEDED(h_)) pass++; else { fail++; \
    printf("FAIL: %s: 0x%08lx (line %d)\n", what, static_cast<unsigned long>(h_), __LINE__); fflush(stdout); } } while (0)

using Microsoft::UI::Xaml::Tests::Parser::MockParserCoreServices;

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

/* the parser's errors, printed (the core's own reporter shows them to the app) */
class PrintingErrorReporter final : public ParserErrorReporter
{
protected:
    HRESULT ReportError(XUINT32 code, XUINT32 line, XUINT32 column, XUINT32, XUINT32, const xstring_ptr &message) override
    {
        printf("  parser error %u at %u:%u: %s\n", code, line, column, narrow(message).c_str());
        return S_OK;
    }
};

/* the core: the parser hands its objects the CCoreServices its schema
 * context was made with, cast from IParserCoreServices.  The mock is placed
 * where a CCoreServices holds that interface, in zeroed storage the size of
 * the core, so what the code reads of the core is the empty state.  What the
 * resource dictionary reads beyond that is set as CCoreServices' constructor
 * and initialisation set it, through the test hooks class the core names as
 * its friend: the objects' shared default state (which names the core), the
 * core's thread (collections check their items against it), the theme cache
 * and the theming, here over Microsoft's test theming (system theme Dark) */
namespace DirectUI {
class DxamlCoreTestHooks {
public:
    static void SetUp(CCoreServices *c)
    {
        new (&c->m_flyweightState) CoreState(CDOSharedState(c, CDOSharedState::s_defaultRenderChangedHandler,
                                                            CDOSharedState::s_defaultBaseUri, xref::weakref_ptr<VisualTree>()));
        c->m_nThreadID = GetCurrentThreadId();
        c->m_spTheming.reset(new FrameworkTheming(std::make_shared<TestThemingInterop>(), []() { return S_OK; }));
        c->m_themeWalkResourceCache.reset(new ThemeWalkResourceCache());
    }
};
}

static CCoreServices *core()
{
    static CCoreServices *c;
    if (!c) {
        const uintptr_t probe = 0x10000;
        size_t at = reinterpret_cast<uintptr_t>(static_cast<IParserCoreServices *>(reinterpret_cast<CCoreServices *>(probe))) - probe;
        void *mem = calloc(1, sizeof(CCoreServices) + sizeof(MockParserCoreServices));
        new (static_cast<char *>(mem) + at) MockParserCoreServices();
        c = static_cast<CCoreServices *>(mem);
        DirectUI::DxamlCoreTestHooks::SetUp(c);
    }
    return c;
}

static std::shared_ptr<XamlSchemaContext> schema()
{
    static std::shared_ptr<XamlSchemaContext> ctx;
    if (!ctx) {
        std::shared_ptr<ParserErrorReporter> reporter = std::make_shared<PrintingErrorReporter>();
        if (FAILED(XamlSchemaContext::Create(static_cast<IParserCoreServices *>(core()), reporter, ctx))) return nullptr;
    }
    return ctx;
}

/* XAML text @xaml loaded into objects: the root (referenced), or null
 * with the failing HRESULT in *@hr */
static CDependencyObject *load(const wchar_t *xaml, HRESULT *hr_out = nullptr)
{
    std::shared_ptr<XamlSchemaContext> ctx = schema();
    std::shared_ptr<XamlTextReader> reader;
    std::shared_ptr<ObjectWriter> writer;
    HRESULT hr = ctx ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
        hr = XamlTextReader::Create(ctx, XamlTextReaderSettings(false, true, false), (XUINT32)(wcslen(xaml) * 2),
                                    reinterpret_cast<const XUINT8 *>(xaml), reader);
    ObjectWriterSettings settings;
    if (SUCCEEDED(hr)) hr = ObjectWriter::Create(ctx, settings, writer);
    while (SUCCEEDED(hr) && (hr = reader->Read()) == S_OK)
        hr = writer->WriteNode(reader->CurrentNode());
    CDependencyObject *root = nullptr;
    if (SUCCEEDED(hr)) {
        std::shared_ptr<XamlQualifiedObject> result = writer->get_Result();
        root = result ? result->GetDependencyObject() : nullptr;
        if (root) root->AddRef();
        else hr = E_POINTER;
    }
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

    /* (merged and theme dictionaries are set through field-backed
     * properties, whose offsets come with the core's metadata tables,
     * core/metadata: the next part) */

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
