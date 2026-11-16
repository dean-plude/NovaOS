// NovaOS: the phone library's XamlRuntimeType, which activates its types
// and gets and sets their members for the framework's
// XamlLibMetadataProvider: Microsoft's generated XamlTypeInfo.g.cpp
// (dxaml/xcp/dxaml/dllsrv/exts/phone, which its XamlTypeInfo.cpp compiles
// over every class's header), as build.py's phone_built() keeps it
// (xaml_phone_runtime_type.g.inc: no activation of the classes NovaOS does
// not build yet).  See docs/design/windows-ui-xaml.md.
#include "precomp.h"
#include <XamlTypeInfo.h>
#include "xaml_phone_runtime_type.g.inc"
