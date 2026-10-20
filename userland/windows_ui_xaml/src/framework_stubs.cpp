// NovaOS: a stand-in, in the manner of Microsoft's isolated-test stubs
// (components/unittests/stubs), for the framework's DXamlCore, which the
// core's resource dictionary asks for (core/core/elements/Resources.cpp),
// for programs without the framework: no framework core on this thread and
// no cached null resource.  Programs on the real core ("xaml": "core") link
// the framework's own, dxaml/lib DXamlCore.cpp, since part 11, and not this
// (build.py CORE_SERVICES_STUBS).
#include <corep.h>
#include <DXamlCore.h>
#include <NullKeyedResource.g.h>

DirectUI::DXamlCore *DirectUI::DXamlCore::GetCurrent()
{
    return nullptr;
}

ctl::ComPtr<DirectUI::NullKeyedResource> DirectUI::DXamlCore::GetCachedNullKeyedResource()
{
    return nullptr;
}
