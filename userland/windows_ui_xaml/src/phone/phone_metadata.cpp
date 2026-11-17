// NovaOS: the phone library's type metadata, which the framework reads
// through its XamlLibMetadataProvider (components/ExtMetadataProvider) to
// make a Pivot, a PivotItem or a navigation transition named in markup.
// On Windows the library is its own DLL, Microsoft.UI.Xaml.Phone.dll: the
// provider loads it as a data file for its type tables (resources the XAML
// compiler's generator writes, dllsrv/exts/phone/XamlTypeInfo.g.rc) and
// asks the DLL's dependency locator (its GetDependencyLocatorStorage
// export, dllsrv/exts/module.cpp) for the XamlRuntimeType that activates
// the types and gets and sets their members (XamlTypeInfo.g.cpp).  NovaOS
// builds the library into the framework's own module, so:
//   nova_phone_resource: a table's bytes, from the .g.rc as build.py's
//     phone_type_info() lays them out (xaml_phone_type_info.g.inc);
//   the XamlRuntimeType is provided here, to the module's own dependency
//     locator, as module.cpp provides it to the DLL's.
// See docs/design/windows-ui-xaml.md.
#include "precomp.h"
#include <DependencyLocator.h>
#include <XamlTypeInfo.h>

namespace Private
{
    PROVIDE_DEPENDENCY(XamlRuntimeType);
}

namespace {

#include "xaml_phone_type_info.g.inc"

struct Resource
{
    UINT name;
    UINT type;
    DWORD size;
    const char *bytes;
};

const Resource s_resources[] = {NOVA_PHONE_TABLES};

} // namespace

// the table named @name of type @type: S_OK and its bytes, or
// HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND)
extern "C" HRESULT nova_phone_resource(UINT name, UINT type, const void **data, DWORD *size)
{
    for (const Resource &r : s_resources) {
        if (r.name == name && r.type == type) {
            *data = r.bytes;
            *size = r.size;
            return S_OK;
        }
    }
    *data = nullptr;
    *size = 0;
    return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
}
