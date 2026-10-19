// NovaOS: stand-ins for the members of the core services (CCoreServices,
// core/dll/xcpcore.cpp) the core's resource dictionary and object writer
// call, for programs that test the libraries without the core (xamlbasetest,
// xamlparsetest: "xaml": true or "parser"): the application's and the system
// colours' dictionaries, loading a dictionary's Source, implicit-style
// invalidation, the resource lookup logger, the name scopes' root.  With no
// core they say "nothing here".  Alone in their object file (with
// Microsoft's CoreServices_Stub.cpp and src/error_service.cpp), which
// programs on the real core ("xaml": "core") do not link.
#include <corep.h>
#include <Resources.h>
#include <ResourceLookupLogger.h>
#include <namescope/inc/NameScopeRoot.h>

bool CCoreServices::IsThemeRequestedForSubTree() const
{
    return false;
}

xstringmap<bool> &CCoreServices::GetResourceDictionaryUriCache()
{
    static xstringmap<bool> cache;
    return cache;
}

_Check_return_ HRESULT CCoreServices::GetSystemColorsResources(_Out_ CResourceDictionary **ppSystemColorsResources)
{
    *ppSystemColorsResources = nullptr;
    return S_OK;
}

_Check_return_ HRESULT CCoreServices::InvalidateImplicitStylesOnRoots(_In_opt_ CResourceDictionary *)
{
    return S_OK;
}

_Check_return_ HRESULT CCoreServices::LoadXamlResource(_In_ IPALUri *, _Out_ bool *pfHasBinaryFile, _Outptr_ IPALMemory **ppMemory,
                                                       _Out_opt_ Parser::XamlBuffer *, _Outptr_opt_ IPALUri **ppPhysicalUri)
{
    *pfHasBinaryFile = false;
    *ppMemory = nullptr;
    if (ppPhysicalUri) *ppPhysicalUri = nullptr;
    return E_NOTIMPL;
}

_Check_return_ HRESULT CCoreServices::ParseXamlWithExistingFrameworkRoot(_In_ const Parser::XamlBuffer &, _In_ CDependencyObject *,
                                                                         _In_ const xstring_ptr_view &, _In_ const xstring_ptr_view &,
                                                                         _In_ const bool, _Outptr_ CDependencyObject **ppDependencyObject)
{
    *ppDependencyObject = nullptr;
    return E_NOTIMPL;
}

_Check_return_ ICustomResourceLoader *CCoreServices::GetCustomResourceLoader()
{
    return nullptr;
}

_Check_return_ HRESULT CCoreServices::GetResourceManager(_Outptr_ IPALResourceManager **ppResourceManager)
{
    *ppResourceManager = nullptr;
    return E_NOTIMPL;
}

_Check_return_ HRESULT CCoreServices::ShimGetResourcePropertyBagRaw(_In_ const xstring_ptr &, _In_ const xref_ptr<IPALUri> &,
                                                                    _Out_ std::vector<std::pair<xstring_ptr, xstring_ptr>> &)
{
    return E_NOTIMPL;
}

Diagnostics::ResourceLookupLogger *CCoreServices::GetResourceLookupLogger()
{
    if (!m_resourceLookupLogger)
    {
        m_resourceLookupLogger = std::make_unique<Diagnostics::ResourceLookupLogger>();
    }
    return m_resourceLookupLogger.get();
}

Jupiter::NameScoping::NameScopeRoot &CCoreServices::GetNameScopeRoot()
{
    static Jupiter::NameScoping::NameScopeRoot root;
    return root;
}
