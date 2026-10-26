// NovaOS: VisualState.Name, which Microsoft's sources declare in the
// generated VisualState.g.h and ship no definition of.  WinUI 2's
// NavigationView reads its visual states' names as it lays out.  A visual
// state's name is its x:Name, DependencyObject.Name, read as
// VisualStateGroup_Partial.cpp reads a group's.
#include "precomp.h"
#include "VisualState.g.h"

using namespace DirectUI;

_Check_return_ HRESULT VisualState::get_NameImpl(_Out_ HSTRING *phName)
{
    ctl::ComPtr<IInspectable> spName;

    IFCPTR_RETURN(phName);
    IFC_RETURN(GetValue(MetadataAPI::GetDependencyPropertyByIndex(KnownPropertyIndex::DependencyObject_Name), &spName));
    IFC_RETURN(ctl::do_get_value(*phName, spName.Get()));
    return S_OK;
}
