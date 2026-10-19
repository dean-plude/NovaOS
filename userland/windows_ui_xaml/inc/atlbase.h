// NovaOS: the Active Template Library's atlbase.h, which the platform
// layer's precompiled header (plat/win/browserdesktop/precomp.h) includes.
// Of ATL, the headers it includes name only the COM object base its input
// pane handler (FrameworkInputPaneHandler.h) derives from and that class's
// COM map; the sources NovaOS builds of the platform layer make no ATL
// objects, so these are declarations alone (no ATL object is ever made
// through them: the classes have no IUnknown of their own).
#pragma once

namespace ATL {
class CComSingleThreadModel {};
class CComMultiThreadModel {};
template <class ThreadModel> class CComObjectRootEx {};
}

#define DECLARE_NOT_AGGREGATABLE(x)
#define DECLARE_PROTECT_FINAL_CONSTRUCT()
#define BEGIN_COM_MAP(x)
#define COM_INTERFACE_ENTRY(x)
#define END_COM_MAP()
