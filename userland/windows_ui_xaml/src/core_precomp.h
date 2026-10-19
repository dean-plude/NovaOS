// NovaOS: the precompiled header the XAML core's own sources NovaOS builds
// (build.py CORE_SOURCES' core/xcpcore.vcxproj entry) are compiled with:
// the core's (core/precomp.h) and, of the text core's (core/text/precomp.h),
// the headers the text core's CTextCore names (its font and script
// services, the text formatter cache, the Unicode data lookup), as
// core/text/common/TextCore.cpp is compiled with the core's sources: the
// text core's own precompiled header includes the closed Page/Table Layout
// Services' header (Ptls-min.h) that its line services headers are written
// against.
#pragma once
#include "../../../third_party/microsoft-ui-xaml/dxaml/xcp/core/precomp.h"
#include <FontAndScriptServices.h>
#include "../../../third_party/microsoft-ui-xaml/dxaml/xcp/core/text/RichTextServices/inc/Result.h"
#include "../../../third_party/microsoft-ui-xaml/dxaml/xcp/core/text/RichTextServices/inc/TextFormatterCache.h"
#include "../../../third_party/microsoft-ui-xaml/dxaml/xcp/core/text/Classification/UcdData.h"
