// NovaOS: the properties' runtime data, what the XAML core's generated
// metadata (core/metadata DynamicMetadata.g.cpp, c_aDependencyPropertyRuntimeData,
// read by ReflectionAPI.cpp's getters) gives each property: the offset of the
// field behind a field-backed property, its storage group, its property
// method and its render-changed handler.  The generated table has a row for
// every property of every element; its rows name the element classes, which
// build as the core does, so NovaOS keeps the rows of the classes that build
// (copied from Microsoft's table, below) and finds a property's by its index.
// A property without a row has none of these, as with Microsoft's isolated
// tests' metadata mocks (components/metadata/mocks), whose getters build.py
// renames out of the way (STUB_RENAMES).
//
// And a type's core constructor, from its row of the type table
// (c_aTypeActivations, ReflectionAPI.cpp), and
// CDependencyProperty::CreateDefaultValueObject, from
// core/core/elements/DependencyProperty.cpp: an object property's default
// value made by its type's core constructor (a dictionary's
// MergedDictionaries collection and ThemeDictionaries dictionary are made
// the first time they are read), when the type table has one.
#include <corep.h>
#include <CDependencyObject.h>
#include <Resources.h>
#include <TypeTableStructsPrivate.h>
#include <MetadataAPI.h>

namespace {

struct PropertyRuntimeData
{
    KnownPropertyIndex index;
    MetaDataDependencyPropertyRuntimeData data;
};

// Microsoft's rows (DynamicMetadata.g.cpp) for the classes that build
const PropertyRuntimeData c_rows[] =
{
    // Microsoft.UI.Xaml.ResourceDictionary.MergedDictionaries
    { KnownPropertyIndex::ResourceDictionary_MergedDictionaries,
      { (METHODPFN)OFFSET(CResourceDictionary, m_pMergedDictionaries), nullptr, nullptr, 0 } },
    // Microsoft.UI.Xaml.ResourceDictionary.Source
    { KnownPropertyIndex::ResourceDictionary_Source,
      { (METHODPFN)OFFSET(CResourceDictionary, m_strSource), nullptr, nullptr, 0 } },
    // Microsoft.UI.Xaml.ResourceDictionary.ThemeDictionaries
    { KnownPropertyIndex::ResourceDictionary_ThemeDictionaries,
      { (METHODPFN)OFFSET(CResourceDictionary, m_pThemeDictionaries), nullptr, nullptr, 0 } },
};

const MetaDataDependencyPropertyRuntimeData *row(KnownPropertyIndex index)
{
    for (const auto &r : c_rows)
    {
        if (r.index == index)
        {
            return &r.data;
        }
    }
    return nullptr;
}

}

const CREATEPFN CClassInfo::GetCoreConstructor() const
{
    if (DirectUI::MetadataAPI::IsKnownIndex(m_nIndex))
    {
        return c_aTypeActivations[static_cast<UINT>(m_nIndex)].m_pfnCreate;
    }
    else
    {
        return nullptr;
    }
}

METHODPFN CDependencyProperty::GetPropertyMethod() const
{
    auto data = row(m_nIndex);
    return data ? data->m_pfn : nullptr;
}

UINT16 CDependencyProperty::GetOffset() const
{
    if (IsPropMethodCall())
    {
        return 0;
    }
    auto data = row(m_nIndex);
    return data ? data->m_nOffset : 0;
}

UINT16 CDependencyProperty::GetGroupOffset() const
{
    auto data = row(m_nIndex);
    return data ? data->m_nGroupOffset : 0;
}

CREATEGROUPPFN CDependencyProperty::GetGroupCreator() const
{
    auto data = row(m_nIndex);
    return data ? data->m_cgpfn : nullptr;
}

RENDERCHANGEDPFN CDependencyProperty::GetRenderChangedHandler() const
{
    auto data = row(m_nIndex);
    return data ? data->m_pfnNWRenderChangedHandler : nullptr;
}

_Check_return_ HRESULT CDependencyProperty::CreateDefaultValueObject(
    _In_ CCoreServices *pCore,
    _Out_ CValue *pDefaultValue) const
{
    CREATEPARAMETERS cp(pCore);
    const CREATEPFN pfnCreate = GetPropertyType()->GetCoreConstructor();
    xref_ptr<CDependencyObject> value;

    // (a program whose type table cannot make the type)
    if (!pfnCreate)
    {
        return E_NOTIMPL;
    }
    IFC_RETURN(pfnCreate(value.ReleaseAndGetAddressOf(), &cp));

    switch (GetIndex())
    {
        case KnownPropertyIndex::ResourceDictionary_ThemeDictionaries:
        {
            CResourceDictionary* pThemeDictionaries = nullptr;
            IFC_RETURN(DoPointerCast(pThemeDictionaries, value.get()));
            pThemeDictionaries->MarkIsThemeDictionaries();

            if (pCore->IsLoadingGlobalThemeResources())
            {
                pThemeDictionaries->MarkIsGlobal();
            }
            break;
        }
    }

    pDefaultValue->Set<valueObject>(value.detach());

    return S_OK;
}
