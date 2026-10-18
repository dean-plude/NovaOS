// NovaOS: stand-ins, in the manner of Microsoft's isolated-test stubs
// (components/unittests/stubs), for the members of the XAML core's element
// classes the component libraries call before the core builds:
// CDependencyObject's property system asks an element for its logical and
// templated parents and its popup, the resource lookup for an element's
// class name and resources and a visual state's group (setters in visual
// states); and the platform services, which the core's DLL sets up and
// which stay null here, as in Microsoft's stubs (DCompVisual_Stub.cpp).
// Linked only by programs that test the
// components on their own (xaml_teststubs.lib); none of them makes an
// element.  Microsoft's UIElement_Stub.cpp has OnAssociationFailure but
// needs the rest of the element tree with it.  Also XAML's zero-filling
// operator new (components/allocation/lib/XcpNewDelete.cpp, which also
// replaces the module's global new and joins with Windows.UI.Xaml.dll).
#include "theming\inc\Theme.h"
#include <Framework.h>
#include <Popup.h>
#include <vsm/inc/CVisualStateManager2.h>
#include <EncodedPtr.h>
#include <string.h>

xstring_ptr CFrameworkElement::GetClassName()
{
    return xstring_ptr::NullString();
}

CDependencyObject *CFrameworkElement::GetLogicalParentNoRef()
{
    return nullptr;
}

CDependencyObject *CFrameworkElement::GetTemplatedParent()
{
    return nullptr;
}

CResourceDictionary *CFrameworkElement::GetResourcesNoCreateNoRef() const
{
    return nullptr;
}

CPopup *CPopupRoot::GetOpenPopupWithChild(_In_ const CUIElement *const, bool) const
{
    return nullptr;
}

xref_ptr<CVisualStateGroupCollection> CVisualStateManager2::GetGroupCollectionFromVisualState(_In_ const CVisualState *)
{
    return nullptr;
}

EncodedPtr<IPlatformServices> gps;

_Check_return_ HRESULT CUIElement::OnAssociationFailure()
{
    return E_NOTIMPL;
}

void *__cdecl operator new(size_t size, ZeroMemAllocationPolicy)
{
    return memset(::operator new(size), 0, size);
}

void *__cdecl operator new[](size_t size, ZeroMemAllocationPolicy)
{
    return memset(::operator new[](size), 0, size);
}
