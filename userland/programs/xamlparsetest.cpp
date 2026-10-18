/* xamlparsetest.exe — Microsoft's XAML markup parser built for NovaOS
 * (roadmap step 32.3, part 6; docs/design/windows-ui-xaml.md): XAML text
 * read by XamlLite (NovaOS's xmllite.dll), scanned and parsed into XAML's
 * node stream (namespaces, objects, properties, text, markup extensions
 * such as {StaticResource}) by the parser from dxaml/xcp/core/Parser, on
 * the type metadata of every built-in type, and the resources layer
 * (dxaml/xcp/components/resources: resource keys, the theme walk's
 * resource cache) linked beside it.  The parser's core services are
 * Microsoft's own mock from its isolated parser unit tests
 * (components/parser/unittests), as those tests run it, until the XAML
 * core builds; the cases follow those tests' (XamlTextParserUnitTests).
 *
 *   xamlparsetest       the self-test runs it with no arguments
 */
#include <stdio.h>
#include <xmllite.h>
#include "ParserUnitTestIncludes.h"
#include "MockParserCoreServices.h"
#include <XamlProperty.h>
#include <XamlType.h>
#include <XamlText.h>
#include <XamlNode.h>
#include <ResourceDictionaryKey.h>
#include <ResourceDictionaryMapTypes.h>
#include <ThemeWalkResourceCache.h>
#include <CDependencyObject.h>
#include <CStaticLock.h>
#include <ParserErrorService.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); fflush(stdout); } } while (0)

using Microsoft::UI::Xaml::Tests::Parser::MockParserCoreServices;

static std::string narrow(const xstring_ptr &s)
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

static std::shared_ptr<XamlSchemaContext> schema()
{
    static IParserCoreServices *services = new MockParserCoreServices();
    std::shared_ptr<ParserErrorReporter> reporter = std::make_shared<PrintingErrorReporter>();
    std::shared_ptr<XamlSchemaContext> ctx;
    if (FAILED(XamlSchemaContext::Create(services, reporter, ctx))) return nullptr;
    return ctx;
}

/* the node stream of XAML text @xaml, one word per node: NS, SO(type),
 * EA (end of attributes), SP(property), T(text), V, EP, EO; and the
 * HRESULT the reading stopped on (S_FALSE at the end) */
static std::string parse(const wchar_t *xaml, HRESULT *last = nullptr)
{
    std::string out;
    const XUINT8 *buf = reinterpret_cast<const XUINT8 *>(xaml);
    std::shared_ptr<XamlTextReader> reader;
    std::shared_ptr<XamlSchemaContext> ctx = schema();     /* (the reader holds it weakly) */
    HRESULT hr = XamlTextReader::Create(ctx, XamlTextReaderSettings(false, true, false),
                                        (XUINT32)(wcslen(xaml) * 2), buf, reader);
    if (FAILED(hr)) {
        if (last) *last = hr;
        return "create failed";
    }
    while ((hr = reader->Read()) == S_OK) {
        const XamlNode &n = reader->CurrentNode();
        xstring_ptr name;
        if (!out.empty()) out += ' ';
        switch (n.get_NodeType()) {
        case XamlNodeType::xntNamespace: out += "NS"; break;
        case XamlNodeType::xntStartObject:
            n.get_XamlType()->get_Name(&name);
            out += "SO(" + narrow(name) + ")";
            break;
        case XamlNodeType::xntEndOfAttributes: out += "EA"; break;
        case XamlNodeType::xntStartProperty:
            if (n.get_Property()->IsImplicit()) out += "SP(_)";
            else { n.get_Property()->get_Name(&name); out += "SP(" + narrow(name) + ")"; }
            break;
        case XamlNodeType::xntText:
            n.get_Text()->get_Text(&name);
            out += "T(" + narrow(name) + ")";
            break;
        case XamlNodeType::xntValue: out += "V"; break;
        case XamlNodeType::xntEndProperty: out += "EP"; break;
        case XamlNodeType::xntEndObject: out += "EO"; break;
        default: out += "?"; break;
        }
    }
    if (last) *last = hr;
    return out;
}

#define XMLNS L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
#define XMLNS_X L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'"

static void expect(const char *what, const wchar_t *xaml, const char *nodes)
{
    HRESULT hr = S_OK;
    std::string got = parse(xaml, &hr);
    CHECK(what, got == nodes);
    if (got != nodes) printf("  got:  %s (%08lx)\n  want: %s\n", got.c_str(), (unsigned long)hr, nodes);
}

/* XmlLite itself (xmllite.dll): node types, names, attributes with the
 * namespace declarations among them, empty elements, text, errors */
static void test_xmllite()
{
    const char xml[] = "<a x='1' xmlns:p='urn:p'>\n <p:b/>text &amp; more<!--c--></a>";
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof xml - 1);
    memcpy(GlobalLock(mem), xml, sizeof xml - 1);
    GlobalUnlock(mem);
    IStream *stream = nullptr;
    CHECK("stream", SUCCEEDED(CreateStreamOnHGlobal(mem, TRUE, &stream)));
    IXmlReader *reader = nullptr;
    CHECK("CreateXmlReader", SUCCEEDED(CreateXmlReader(__uuidof(IXmlReader), (void **)&reader, nullptr)) && reader);
    if (!reader || !stream) return;
    CHECK("SetInput", SUCCEEDED(reader->SetInput(stream)));
    XmlNodeType t = XmlNodeType_None;
    const wchar_t *s = nullptr;
    UINT n = 0;
    CHECK("element", reader->Read(&t) == S_OK && t == XmlNodeType_Element);
    CHECK("element name", SUCCEEDED(reader->GetLocalName(&s, &n)) && n == 1 && s[0] == L'a' && !reader->IsEmptyElement());
    UINT count = 0;
    CHECK("two attributes", SUCCEEDED(reader->GetAttributeCount(&count)) && count == 2);
    bool sawx = false, sawns = false;
    for (HRESULT hr = reader->MoveToFirstAttribute(); hr == S_OK; hr = reader->MoveToNextAttribute()) {
        const wchar_t *prefix, *local, *value;
        UINT pn, ln, vn;
        reader->GetPrefix(&prefix, &pn);
        reader->GetLocalName(&local, &ln);
        reader->GetValue(&value, &vn);
        if (!wcscmp(local, L"x") && !wcscmp(value, L"1") && pn == 0) sawx = true;
        if (!wcscmp(prefix, L"xmlns") && !wcscmp(local, L"p") && !wcscmp(value, L"urn:p")) sawns = true;
    }
    CHECK("attribute x='1'", sawx);
    CHECK("xmlns:p as an attribute", sawns);
    CHECK("back to the element", reader->MoveToElement() == S_OK);
    CHECK("whitespace", reader->Read(&t) == S_OK && t == XmlNodeType_Whitespace);
    CHECK("empty element", reader->Read(&t) == S_OK && t == XmlNodeType_Element && reader->IsEmptyElement());
    CHECK("prefix and namespace", SUCCEEDED(reader->GetPrefix(&s, &n)) && !wcscmp(s, L"p") &&
          SUCCEEDED(reader->GetNamespaceUri(&s, &n)) && !wcscmp(s, L"urn:p"));
    CHECK("text", reader->Read(&t) == S_OK && t == XmlNodeType_Text && SUCCEEDED(reader->GetValue(&s, &n)) &&
          !wcscmp(s, L"text & more"));
    CHECK("comment", reader->Read(&t) == S_OK && t == XmlNodeType_Comment);
    CHECK("end element", reader->Read(&t) == S_OK && t == XmlNodeType_EndElement);
    CHECK("end of input", reader->Read(&t) == S_FALSE && reader->IsEOF());
    reader->Release();
    stream->Release();
}

static void test_parser()
{
    CHECK("schema context", schema() != nullptr);

    /* XamlTextParserUnitTests::Create: the root element's type is Grid's */
    {
        std::shared_ptr<XamlTextReader> reader;
        const wchar_t *xaml = L"<Grid " XMLNS XMLNS_X L"/>";
        std::shared_ptr<XamlSchemaContext> ctx = schema();
        CHECK("XamlTextReader::Create", SUCCEEDED(XamlTextReader::Create(ctx, XamlTextReaderSettings(false, true, false),
              (XUINT32)(wcslen(xaml) * 2), reinterpret_cast<const XUINT8 *>(xaml), reader)));
        std::vector<XamlNode> nodes;
        while (reader && reader->Read() == S_OK) nodes.push_back(reader->CurrentNode());
        CHECK("five nodes", nodes.size() == 5);
        if (nodes.size() == 5) {
            CHECK("xmlns", nodes[0].get_NodeType() == XamlNodeType::xntNamespace);
            CHECK("xmlns:x", nodes[1].get_NodeType() == XamlNodeType::xntNamespace);
            CHECK("Grid", nodes[2].get_NodeType() == XamlNodeType::xntStartObject &&
                  nodes[2].get_XamlType()->get_TypeToken() ==
                  XamlTypeToken::FromType(DirectUI::MetadataAPI::GetClassInfoByIndex(KnownTypeIndex::Grid)));
            CHECK("end of attributes", nodes[3].get_NodeType() == XamlNodeType::xntEndOfAttributes);
            CHECK("end of Grid", nodes[4].get_NodeType() == XamlNodeType::xntEndObject);
        }
    }

    /* ParseXStringInXaml: an x:String's text is its initialization */
    expect("x:String", L"<x:String " XMLNS_X L">stringValue</x:String>", "NS SO(String) EA SP(_) T(stringValue) EP EO");

    /* ParseIgnorableElements: mc:Ignorable drops d: elements and attributes */
    expect("mc:Ignorable",
           L"<Grid " XMLNS L"xmlns:mc='http://schemas.openxmlformats.org/markup-compatibility/2006' "
           L"xmlns:d='http://schemas.microsoft.com/expression/blend/2008' mc:Ignorable='d' d:DesignWidth='640' "
           L"Width='100'><d:DesignGridRow><d:DesignGridRow.Content><d:FooBar Width='100'/></d:DesignGridRow.Content>"
           L"</d:DesignGridRow></Grid>",
           "NS NS NS SO(Grid) SP(Width) T(100) EP EA EO");

    /* a page: attributes and child elements as properties, content properties */
    expect("page",
           L"<StackPanel " XMLNS XMLNS_X L" Orientation='Horizontal'>"
           L"<TextBlock x:Name='hello' Text='Hello, NovaOS'/><Button>Click</Button></StackPanel>",
           "NS NS SO(StackPanel) SP(Orientation) T(Horizontal) EP EA SP(Children) SO(UIElementCollection) SP(_) "
           "SO(TextBlock) SP(Name) T(hello) EP SP(Text) T(Hello, NovaOS) EP EA EO "
           "SO(Button) EA SP(Content) T(Click) EP EO EP EO EP EO");

    /* property elements and markup extensions: {StaticResource} is an object
     * whose positional argument sets ResourceKey */
    expect("{StaticResource}",
           L"<Grid " XMLNS L"><Grid.Background><SolidColorBrush Color='{StaticResource Accent}'/></Grid.Background></Grid>",
           "NS SO(Grid) EA SP(Background) EA SO(SolidColorBrush) SP(Color) SO(StaticResource) SP(ResourceKey) T(Accent) "
           "EP EO EP EA EO EP EO");

    /* a resource dictionary: x:Key is a directive */
    expect("ResourceDictionary",
           L"<ResourceDictionary " XMLNS XMLNS_X L"><x:String x:Key='Greeting'>Hi</x:String></ResourceDictionary>",
           "NS NS SO(ResourceDictionary) EA SP(_) SO(String) SP(Key) T(Greeting) EP EA SP(_) T(Hi) EP EO EP EO");

    /* VerifyParseFailures: XML that is not well formed gives no nodes, a
     * broken markup extension stops after the nodes before it */
    HRESULT hr = S_OK;
    std::string got = parse(L"<Grid " XMLNS L"Width='100/>", &hr);
    CHECK("bad XML: no nodes", got.empty());
    CHECK("bad XML: an error", FAILED(hr));
    got = parse(L"<Grid " XMLNS L"Width='{StaticResource {}}'/>", &hr);
    CHECK("bad markup extension: two nodes", got == "NS SO(Grid)");
    CHECK("bad markup extension: an error", FAILED(hr));
    /* an unknown type is an error, not a node */
    got = parse(L"<NoSuchType " XMLNS L"/>", &hr);
    CHECK("unknown type", FAILED(hr) && got.find("NoSuchType") == std::string::npos);
}

static void test_resources()
{
    /* resource keys: by name or by type, compared as XAML compares them */
    DECLARE_CONST_XSTRING_PTR_STORAGE(c_accent, L"Accent");
    DECLARE_CONST_XSTRING_PTR_STORAGE(c_accent2, L"Accent");
    DECLARE_CONST_XSTRING_PTR_STORAGE(c_button, L"Button");
    xstring_ptr accent(c_accent), accent2(c_accent2), button(c_button);
    ResourceKey a(accent, false), a2(accent2, false), t(button, true), n(button, false);
    CHECK("key equality", a == a2);
    CHECK("type key differs from name key", !(t == n));
    CHECK("key name", a.GetKey().Compare(accent) == 0 && t.IsKeyType() && !n.IsKeyType());

    /* a dictionary's map: stored keys, looked up by a transient key */
    xref_ptr<CDependencyObject> brush, style;
    brush.attach(new CDependencyObject());
    style.attach(new CDependencyObject());
    ResourceMap map;
    map.emplace(a.ToStorage(), brush.get());
    map.emplace(t.ToStorage(), style.get());
    auto found = map.find(a2);
    CHECK("map lookup by name", found != map.end() && found->second == brush.get());
    found = map.find(t);
    CHECK("map lookup of an implicit style key", found != map.end() && found->second == style.get());
    CHECK("map: name and type keys are distinct", map.find(n) == map.end() && map.size() == 2);

    /* ThemeWalkResourceCacheUnitTests: resources cached per dictionary
     * while a theme walk runs, dropped when it ends */
    ThemeWalkResourceCache cache;
    xref_ptr<CDependencyObject> res1, res2;
    res1.attach(new CDependencyObject());
    res2.attach(new CDependencyObject());
    auto dict1 = reinterpret_cast<CResourceDictionary *>(1234), dict2 = reinterpret_cast<CResourceDictionary *>(5678);
    cache.AddCachedResource(dict1, a, res1.get());
    CHECK("no caching outside a walk", cache.IsEmpty() && !cache.TryGetCachedResource(dict1, a));
    {
        auto walk = cache.BeginCachingThemeResources();
        CHECK("not cached yet", !cache.TryGetCachedResource(dict1, a));
        cache.AddCachedResource(dict1, a, res1.get());
        cache.AddCachedResource(dict2, a, res2.get());
        CHECK("cached", cache.TryGetCachedResource(dict1, a2) == res1.get());
        CHECK("per dictionary", cache.TryGetCachedResource(dict2, a) == res2.get());
        CHECK("by key", !cache.TryGetCachedResource(dict1, n));
    }
    CHECK("dropped after the walk", cache.IsEmpty());
}

int main()
{
    DirectUI::StaticLockGlobalInit();
    test_xmllite();
    test_parser();
    test_resources();
    printf("xamlparsetest: %d passed, %d failed (%d-bit)\n", pass, fail, (int)sizeof(void *) * 8);
    return fail != 0;
}
