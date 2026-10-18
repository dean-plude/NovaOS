// NovaOS: the platform layer's sources the parser calls, compiled with the
// parser (CORE_LIBRARIES) until the platform layer builds: pal/common's
// memory streams (XAML binary reading), reference counting and ms-appx: /
// ms-resource: URI helpers, and core/dll/services.cpp (the PAL's service
// getters over the platform services, gps).  Microsoft compiles the
// pal/common files in hostpalplatmisc.vcxproj, whose precompiled header
// gives them the headers included here first.
#include <xstrutil.h>
#include <MemoryStreamBuffer.h>
#include <MemoryStream.h>
#include "../pal/common/MemoryStream.cpp"
#include "../pal/common/MemoryStreamBuffer.cpp"
#include "../pal/common/refcounting.cpp"
#include "../pal/common/MsResourceHelpers.cpp"
#include "../core/dll/services.cpp"
