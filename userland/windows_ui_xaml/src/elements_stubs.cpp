// NovaOS: stand-ins, in the manner of Microsoft's isolated-test stubs
// (components/unittests/stubs), for what the core's resource dictionary
// (core/core/elements/Resources.cpp) calls in the parts of the core that do
// not build yet: the core services (CCoreServices, core/dll/xcpcore.cpp:
// the application's and the system colours' dictionaries, loading a
// dictionary's Source, implicit-style invalidation, the resource lookup
// logger), the framework's DXamlCore, CApplication's base URIs, the error
// service and eight of CDependencyObject's members from
// core/core/elements/depends.cpp; and for the object writer runtime, the
// framework's callbacks for app-defined (managed) types, the elements
// markup reaches beyond resources (templates, styles, bindings, x:Type and
// Setter targets, visual states, deferred names) and the simple
// properties' value conversions.  With no core they say "nothing here": no
// application or system dictionaries, no theme requested, no custom
// resource loader, no managed peers; Source loading, templates, styles and
// bindings not available.  Compiled with the elements' flags into
// xaml_teststubs.lib, so only programs that test the libraries on their own
// link them; they go as the core builds.
#include <corep.h>
#include <CDependencyObject.h>
#include <Resources.h>
#include <framework.h>
#include <application.h>
#include <ErrorService.h>
#include <DXamlCore.h>
#include <NullKeyedResource.g.h>
#include <XamlTypeTokens.h>
#include <XamlPropertyToken.h>
#include <FxCallbacks.h>
#include <ResourceLookupLogger.h>
#include <TemplateContent.h>
#include <TemplateBindingExtension.h>
#include <Binding.h>
#include <type.h>
#include <DependencyPropertyProxy.h>
#include <Style.h>
#include <DeferredMapping.h>
#include <ResourceGraph.h>
#include <SimplePropertiesCommon.g.h>
#include <Template.h>
#include <vsm/inc/CVisualStateManager2.h>
#include <DeferredNameScopeEntry.h>
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

_Check_return_ HRESULT CFrameworkElement::InvalidateImplicitStyles(_In_ CUIElement *, _In_opt_ CResourceDictionary *)
{
    return S_OK;
}

_Check_return_ HRESULT CApplication::CreateBaseUri(_In_ XUINT32, _In_reads_(cUriString) const WCHAR *, _Outptr_ IPALUri **ppUri)
{
    *ppUri = nullptr;
    return E_NOTIMPL;
}

_Check_return_ HRESULT CErrorService::OriginateInvalidOperationError(_In_ CCoreServices *, _In_ XUINT32, _In_ const xstring_ptr_view &,
                                                                     _In_ const xstring_ptr_view &, _In_ const xstring_ptr_view &)
{
    return E_FAIL;
}

DirectUI::DXamlCore *DirectUI::DXamlCore::GetCurrent()
{
    return nullptr;
}

ctl::ComPtr<DirectUI::NullKeyedResource> DirectUI::DXamlCore::GetCachedNullKeyedResource()
{
    return nullptr;
}

_Check_return_ HRESULT CDependencyObject::SetParticipatesInManagedTreeDefault()
{
    return S_OK;
}

void CDependencyObject::ContextAddRef()
{
}

void CDependencyObject::SetBaseUri(IPALUri *)
{
}

// (no framework peers without the framework: nothing to make or peg)
_Check_return_ HRESULT CDependencyObject::PrivateEnsurePeerAndTryPeg(_In_ bool, _In_ bool, _In_ bool, _Out_opt_ bool *pfPegged)
{
    if (pfPegged) *pfPegged = false;
    return S_OK;
}

_Check_return_ HRESULT CDependencyObject::IsAmbientPropertyValueAvailable(_In_ const CDependencyProperty *, _Out_ bool *pbAvailable)
{
    *pbAvailable = false;
    return S_OK;
}

CDeferredStorage &CDependencyObject::EnsureAndGetScopeDeferredStorage()
{
    return CDeferredMapping::EnsureScopeDeferredStorage(this);
}

_Check_return_ HRESULT CCoreServices::ShimGetResourcePropertyBagRaw(_In_ const xstring_ptr &, _In_ const xref_ptr<IPALUri> &,
                                                                    _Out_ std::vector<std::pair<xstring_ptr, xstring_ptr>> &)
{
    return E_NOTIMPL;
}

void Diagnostics::ResourceGraph::CacheMarkupExtensionToTarget(_In_ CMarkupExtensionBase *, _In_ KnownPropertyIndex, _In_ CDependencyObject *)
{
}

CTemplateContent::CTemplateContent(_In_ CCoreServices *pCore) : CShareableDependencyObject(pCore)
{
}

CTemplateContent::~CTemplateContent()
{
}

_Check_return_ HRESULT CTemplateContent::RecordXaml(_In_ const std::shared_ptr<XamlServiceProviderContext> &,
                                                    _In_ const std::shared_ptr<XamlOptimizedNodeList> &,
                                                    _In_ const std::shared_ptr<XamlSavedContext> &,
                                                    _In_ const std::shared_ptr<XamlBinaryFormatSubReader2> &,
                                                    _In_ const std::vector<std::pair<bool, xstring_ptr>> &)
{
    return E_NOTIMPL;
}

// as core/dll/xcpcore.cpp and core/core/elements/depends.cpp have them
// (build.py's STUB_RENAMES)
Diagnostics::ResourceLookupLogger *CCoreServices::GetResourceLookupLogger()
{
    if (!m_resourceLookupLogger)
    {
        m_resourceLookupLogger = std::make_unique<Diagnostics::ResourceLookupLogger>();
    }
    return m_resourceLookupLogger.get();
}

_Check_return_ HRESULT CDependencyObject::AddPeerReferenceToItem(_In_ CDependencyObject *pChild)
{
    IFCEXPECT_RETURN(pChild);
    if (pChild->ParticipatesInManagedTree())
    {
        IFC_RETURN(SetParticipatesInManagedTreeDefault());
        IFC_RETURN(FxCallbacks::DependencyObject_AddPeerReferenceToItem(this, pChild));
    }
    return S_OK;
}

_Check_return_ HRESULT CDependencyObject::RemovePeerReferenceToItem(_In_ CDependencyObject *pChild)
{
    IFCEXPECT_RETURN(pChild);
    if (!HasManagedPeer() && !pChild->HasManagedPeer())
    {
        return S_OK;
    }
    if (pChild->ParticipatesInManagedTree())
    {
        IFC_RETURN(FxCallbacks::DependencyObject_RemovePeerReferenceToItem(this, pChild));
    }
    return S_OK;
}

std::size_t CTemplateContent::GetOrCreateNameIndex(_In_ const xstring_ptr_view &)
{
    return static_cast<std::size_t>(-1);
}

std::size_t CTemplateContent::TryGetNameIndex(_In_ const xstring_ptr_view &) const
{
    return static_cast<std::size_t>(-1);
}

_Check_return_ HRESULT CStyle::SetCustomWriterRuntimeData(_In_ std::shared_ptr<CustomWriterRuntimeData>,
                                                          _In_ std::unique_ptr<CustomWriterRuntimeContext>)
{
    return E_NOTIMPL;
}

_Check_return_ HRESULT CTemplateBindingExtension::SetTemplateBinding(_In_ CDependencyObject *, _In_ XamlPropertyToken)
{
    return E_NOTIMPL;
}

_Check_return_ HRESULT CBinding::SetBinding(_In_ CDependencyObject *, _In_ KnownPropertyIndex)
{
    return E_NOTIMPL;
}

_Check_return_ HRESULT CType::FromXamlType(_In_ const std::shared_ptr<XamlType> &)
{
    return E_NOTIMPL;
}

_Check_return_ HRESULT CDependencyPropertyProxy::FromXamlProperty(_In_ const std::shared_ptr<XamlProperty> &)
{
    return E_NOTIMPL;
}

// the framework's side of the object writer, for app-defined types (and
// of managed peers' references, which nothing here has)
namespace FxCallbacks
{
_Check_return_ HRESULT DependencyObject_AddPeerReferenceToItem(_In_ CDependencyObject *, _In_ CDependencyObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT DependencyObject_RemovePeerReferenceToItem(_In_ CDependencyObject *, _In_ CDependencyObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT FrameworkCallbacks_SupportInitializeEndInit(_In_ CDependencyObject *, _In_ const std::shared_ptr<XamlServiceProviderContext> &)
{
    return S_OK;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_CreateInstance(_In_ XamlTypeToken, _Out_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_CreateFromValue(_In_ void *, _In_ XamlTypeToken, _In_ XamlQualifiedObject *, _In_ XamlPropertyToken,
                                                                   _In_ XamlQualifiedObject *, _Out_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_GetValue(_In_ const XamlQualifiedObject *, _In_ XamlPropertyToken, _Out_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_GetAmbientValue(_In_ const XamlQualifiedObject *, _In_ XamlPropertyToken, _Out_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_SetValue(_In_ XamlQualifiedObject *, _In_ XamlPropertyToken, _In_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_Add(_In_ XamlQualifiedObject *, _In_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_AddToDictionary(_In_ XamlQualifiedObject *, _In_ XamlQualifiedObject *, _In_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_CallProvideValue(_In_ XamlQualifiedObject *, _In_ const std::shared_ptr<XamlServiceProviderContext> &,
                                                                    _Out_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_SetConnectionId(_In_ XamlQualifiedObject *, _In_ XamlQualifiedObject *, _In_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
_Check_return_ HRESULT XamlManagedRuntimeRPInvokes_GetXBindConnector(_In_ XamlQualifiedObject *, _In_ XamlQualifiedObject *, _In_ XamlQualifiedObject *,
                                                                     _In_ XamlQualifiedObject *)
{
    return E_NOTIMPL;
}
}

// the object writer's conversions for simple properties (the core's
// generated core/metadata/SimplePropertiesCommon.g.cpp, which unboxes
// strings through the framework): the values the parser already typed,
// as Microsoft's does; text and the composition types not until the
// framework builds
namespace SimplePropertiesCommon
{
HRESULT ConvertToDouble(const CValue &value, double &convertedValue)
{
    // (CDouble::CreateCValue makes a valueFloat, as Microsoft's comment says)
    if (value.GetType() != valueFloat) return E_NOTIMPL;
    convertedValue = value.As<valueFloat>();
    return S_OK;
}
HRESULT ConvertToInt32(const CValue &value, int32_t &convertedValue)
{
    if (value.GetType() != valueSigned) return E_NOTIMPL;
    convertedValue = value.As<valueSigned>();
    return S_OK;
}
HRESULT ConvertToVector2(const CValue &, ABI::Windows::Foundation::Numerics::Vector2 &) { return E_NOTIMPL; }
HRESULT ConvertToVector3(const CValue &, ABI::Windows::Foundation::Numerics::Vector3 &) { return E_NOTIMPL; }
HRESULT ConvertToMatrix3x2(const CValue &, ABI::Windows::Foundation::Numerics::Matrix3x2 &) { return E_NOTIMPL; }
HRESULT ConvertToMatrix4x4(const CValue &, ABI::Windows::Foundation::Numerics::Matrix4x4 &) { return E_NOTIMPL; }
HRESULT ConvertToTimeSpan(const CValue &, ABI::Windows::Foundation::TimeSpan &) { return E_NOTIMPL; }
HRESULT ConvertToVector3TransitionComponents(const CValue &, ABI::Microsoft::UI::Xaml::Vector3TransitionComponents &) { return E_NOTIMPL; }
}

// the visual state manager's side of a VisualStateGroupCollection (the
// element tree's components/vsm, with the controls)
HRESULT CVisualStateManager2::FaultInChildren(_In_ CVisualStateGroupCollection *)
{
    return S_OK;
}

HRESULT CVisualStateManager2::OnVisualStateGroupCollectionLeave(_In_ CVisualStateGroupCollection *)
{
    return S_OK;
}

HRESULT CVisualStateManager2::OnVisualStateGroupCollectionNotifyThemeChanged(_In_ CVisualStateGroupCollection *)
{
    return S_OK;
}

Jupiter::VisualStateManager::DeferredNameScopeEntry::DeferredNameScopeEntry(xref::weakref_ptr<CVisualStateGroupCollection> collection)
    : m_collection(collection)
{
}

xref_ptr<CDependencyObject> Jupiter::VisualStateManager::DeferredNameScopeEntry::TryGetOrCreateElement(_Out_ bool *shouldRetryLookup)
{
    *shouldRetryLookup = false;
    return nullptr;
}

void CFrameworkTemplate::SetParentXBindConnector(const std::shared_ptr<XamlQualifiedObject> &)
{
}

// the core's names (one root of name scopes for the program)
Jupiter::NameScoping::NameScopeRoot &CCoreServices::GetNameScopeRoot()
{
    static Jupiter::NameScoping::NameScopeRoot root;
    return root;
}
