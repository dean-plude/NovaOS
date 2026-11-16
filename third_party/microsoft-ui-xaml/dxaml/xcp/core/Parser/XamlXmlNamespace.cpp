// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

#include "precomp.h"
#include "xamlxmlnamespace.h"
#include "TypeTableStructs.h"
#include "MetadataAPI.h"
#include "XamlTypeNamespace.h"

_Check_return_ HRESULT 
XamlXmlNamespace::AddTypeNamespace(const std::shared_ptr<XamlTypeNamespace> inTypeNamespace)
{
    m_typeNamespaceList.push_back(inTypeNamespace);

    // If we have been tracking any types that we couldn't find previously, 
    // we need to clear that list here because subsequent GetXamlType calls 
    // could conceivably be handled by the new XamlTypeNamespace being added.
    m_mapKnownNotFoundTypes.clear();
    
    return S_OK;
}

bool XamlXmlNamespace::get_IsResolved() const
{
    return true;
}


//------------------------------------------------------------------------
//
//  Method:   GetXamlType
//
//  Synopsis:
//      Xxx
//
//------------------------------------------------------------------------
_Check_return_ HRESULT 
XamlXmlNamespace::GetXamlType(
    _In_ const xstring_ptr& inTypeName, 
    _Out_ std::shared_ptr<XamlType>& outType)
{
    IFC_RETURN(GetXamlTypeImpl(inTypeName, outType));

    if (!outType)
    {
        xstring_ptr spExtensionName;
        IFC_RETURN(GetTypeExtensionName(inTypeName, &spExtensionName));
        IFC_RETURN(GetXamlTypeImpl(spExtensionName, outType));
    }
    
    return S_OK;
}


//------------------------------------------------------------------------
//
//  Method:   GetXamlTypeImpl
//
//  Synopsis:
//      Xxx
//
//------------------------------------------------------------------------
// NovaOS: the framework (WinUI 3's source) is the system Windows.UI.Xaml
// here, its own types named Microsoft.UI.Xaml.* inside it, and WinUI 2,
// which a UWP app carries, has types of its own under the same names
// (Microsoft.UI.Xaml.Controls.FontIconSource, IconSource, ...).  Markup
// names WinUI 2's through a "using:Microsoft.UI.Xaml..." xmlns (the
// framework's own are in the presentation xmlns, which maps to the same
// built-in namespaces), so in a "using:" xmlns the metadata providers'
// type of the name comes first: WinUI 2's NavigationView template sets
// AnimatedIcon.FallbackIconSource to a WinUI 2 FontIconSource, which an
// app's provider casts to WinUI 2's IconSource.  Null when no provider
// has the name (MetadataAPI::TryGetCustomClassInfoByName).
_Check_return_ HRESULT
XamlXmlNamespace::GetCustomXamlType(
    _In_ const std::shared_ptr<XamlTypeNamespace>& typeNamespace,
    _In_ const xstring_ptr& inTypeName,
    _Out_ std::shared_ptr<XamlType>& outType)
{
    outType.reset();
    DECLARE_CONST_STRING_IN_FUNCTION_SCOPE(c_strUsing, L"using:");
    const auto& token = typeNamespace->get_TypeNamespaceToken();
    if (!m_spXmlNamespaceUri.StartsWith(c_strUsing, xstrCompareCaseSensitive) ||
        token.GetProviderKind() != tpkNative || !DirectUI::MetadataAPI::IsKnownIndex(token.GetHandle()))
    {
        return S_OK;
    }
    const CClassInfo* pType = nullptr;
    IFC_RETURN(DirectUI::MetadataAPI::TryGetCustomClassInfoByName(token.GetHandle(), inTypeName, &pType));
    if (pType)
    {
        auto schemaContext = m_spXamlSchemaContext.lock();
        IFCEXPECT_RETURN(schemaContext);
        IFC_RETURN(schemaContext->GetXamlType(XamlTypeToken::FromType(pType), typeNamespace, inTypeName, outType));
    }
    return S_OK;
}

_Check_return_ HRESULT 
XamlXmlNamespace::GetXamlTypeImpl(
    _In_ const xstring_ptr& inTypeName, 
    _Out_ std::shared_ptr<XamlType>& outType)
{
    std::shared_ptr<XamlType> spXamlType;

    //
    // Check to see if the type name is in the cache for 
    // this xml-namespace
    //
    // 
    // if not, look through the list of XamlTypeNamespaces that have been
    // registered with this XamlXmlNamespace. and cache the value if it is
    // found.
    //
    // ... but first lets see if we have already looked for, but didn't find,
    // this type by checking m_mapKnownNotFoundTypes
    //
    auto itXamlType = m_mapNameToXamlType.find(inTypeName);
    if (itXamlType == m_mapNameToXamlType.end())
    {
        if (m_mapKnownNotFoundTypes.find(inTypeName) == m_mapKnownNotFoundTypes.end())
        {
            for (auto typeNamespace : m_typeNamespaceList)
            {
                IFC_RETURN(GetCustomXamlType(typeNamespace, inTypeName, spXamlType));
                if (spXamlType)
                {
                    break;
                }
                IFC_RETURN(typeNamespace->GetXamlType(inTypeName, spXamlType));

                if (spXamlType)
                {
                    break;
                }
            }

            if (spXamlType)
            {
                VERIFY_COND(m_mapNameToXamlType.insert({ inTypeName, spXamlType }), .second);
            }
            else
            {
                //
                // If we didn't find the type, store that information away so that 
                // we know not to look for it again.  See the comment on 
                // the m_mapKnownNotFoundTypes declaration.
                //
                VERIFY_COND(m_mapKnownNotFoundTypes.insert(inTypeName), .second);
            }
        }
    }
    else
    {
        spXamlType = itXamlType->second;
    }

    outType = spXamlType;

    return S_OK;
}


//------------------------------------------------------------------------
//
//  Method:   Xxxx
//
//  Synopsis:
//      Xxx
//
//------------------------------------------------------------------------
//
_Check_return_ HRESULT 
XamlXmlNamespace::GetDirectiveType(
    _In_ const xstring_ptr& inTypeName, 
    _Out_ std::shared_ptr<XamlType>& outType)
{
    outType = std::shared_ptr<XamlType>();
    
    return S_OK;
}



//------------------------------------------------------------------------
//
//  Method:   Xxxx
//
//  Synopsis:
//      Xxx
//
//------------------------------------------------------------------------
//
_Check_return_ HRESULT 
XamlXmlNamespace::GetDirectiveProperty(
    _In_ const xstring_ptr_view& inPropertyName, 
    _Out_ std::shared_ptr<DirectiveProperty>& ppOut)
{
    ppOut = std::shared_ptr<DirectiveProperty>();
    
    return S_OK;
}


//------------------------------------------------------------------------
//
//  Method:   Xxxx
//
//  Synopsis:
//      Xxx
//
//------------------------------------------------------------------------
//
xstring_ptr XamlXmlNamespace::get_TargetNamespaceCore() const
{
    return m_spXmlNamespaceUri;
}


//------------------------------------------------------------------------
//
//  Method:   IsImplicitlyResolved
//
//  Synopsis:
//      This method was created to indicate if the xmlns XmlNamespace is
//      implicitly resolved. The only case we have at the time of writing
//      is a "using" namespace as in:
//          xmlns:<prefix>="using:<namespace>..."
//
//------------------------------------------------------------------------
//
_Check_return_ HRESULT 
XamlXmlNamespace::GetIsImplicitlyResolved(
    _Out_ bool* pbIsImplicitlyResolved)
{
    if (!m_bIsImplicitlyResolvedComputed)
    {
        std::shared_ptr<XamlSchemaContext> spSchemaContext;

        // Determine if this is a using namespace
        spSchemaContext = m_spXamlSchemaContext.lock();
        if (!spSchemaContext)
        {
            IFC_RETURN(E_FAIL);
        }

        xstring_ptr strNamespace;
        bool isValidXmlns = false;
        HRESULT hr = spSchemaContext->CrackXmlns(m_spXmlNamespaceUri, strNamespace, isValidXmlns);
        m_bIsImplicitlyResolved = SUCCEEDED(hr) && isValidXmlns && !strNamespace.IsNullOrEmpty();

        // Check for failures in CrackXmlns and cache result
        m_bIsImplicitlyResolvedComputed = TRUE;
        IFC_RETURN(hr);
    }

    *pbIsImplicitlyResolved = m_bIsImplicitlyResolved;

    return S_OK;
}

std::shared_ptr<XamlNamespace> XamlXmlNamespace::Clone() const
{
    auto clone = std::make_shared<XamlXmlNamespace>();
    clone->m_xamlPredicateAndArgs = m_xamlPredicateAndArgs;
    clone->m_conditionalPredicateString = m_conditionalPredicateString;
    clone->m_uiRuntimeIndex = m_uiRuntimeIndex;
    clone->m_resolvedConditionalPredicate = m_resolvedConditionalPredicate;

    clone->m_spXamlSchemaContext = m_spXamlSchemaContext;
    clone->m_spXmlNamespaceUri = m_spXmlNamespaceUri;
    clone->m_typeNamespaceList = m_typeNamespaceList;
    clone->m_mapNameToXamlType = m_mapNameToXamlType;
    clone->m_mapKnownNotFoundTypes = m_mapKnownNotFoundTypes;

    return clone;
}