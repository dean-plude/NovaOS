// NovaOS: the platform services the core's DLL sets up (gps, defined with
// the core services in core/dll/xcpcore.cpp), for programs that test the
// libraries without the core: null, as in Microsoft's stubs
// (DCompVisual_Stub.cpp).  Alone in its object file, which programs on the
// real core do not link.
#include <EncodedPtr.h>
#include <pal.h>

EncodedPtr<IPlatformServices> gps;
