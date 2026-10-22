// NovaOS: the platform's resource provider, which the control layer's
// resource manager (control/common/shared/ResourceManager.cpp) resolves
// ms-appx and ms-resource URIs through (the parser asks it whether a XAML
// resource, the framework's theme resources among them, may be cached).
// Microsoft's (plat/win/desktop/PlatformServices.cpp) tries MRT Core's
// ResourceManager first (components/mrt's ModernResourceProvider) and
// falls back to the provider over the app's directory,
// CommonResourceProvider, when MRT cannot be used.  MRT Core's Windows
// Runtime classes are not served on NovaOS yet (MRM.dll's resources.pri
// reader, step 32.6, has no ResourceManager class over it), so this makes
// the fallback, as Microsoft's does when they are missing.  Microsoft's
// CommonResourceProvider.cpp is compiled here, after the headers its own
// project's precompiled header (plat/win/desktop's, not built) gives it.
#include "precomp.h"
#include <CommonResourceProvider.h>
#include <BasePALResource.h>
#include <FilePathResource.h>
#include <MsResourceHelpers.h>
#include <MuiUdk.h>
#include "../../../third_party/microsoft-ui-xaml/dxaml/xcp/plat/win/desktop/CommonResourceProvider.cpp"

_Check_return_ HRESULT CWindowsServices::CreateResourceProvider(
    _In_ CCoreServices *pCore,
    _Outptr_ IPALResourceProvider **ppResourceProvider)
{
    (void)pCore;
    CommonResourceProvider *provider = nullptr;
    IFC_RETURN(CommonResourceProvider::Create(&provider));
    *ppResourceProvider = provider;
    return S_OK;
}
