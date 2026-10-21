/* NovaOS: the lifted content API's header (Microsoft.UI.Content, the
 * Windows App SDK's), as tools/winrt/abigen.py writes it from the vendored
 * metadata (third_party/winmd's Microsoft.UI.winmd), and the one type XAML
 * names that is newer than that metadata: a content island's display
 * orientations (ContentDisplayOrientations, a flags enumeration with the
 * values of Windows.Graphics.Display.DisplayOrientations), which XAML's
 * display orientation helper reads for an island
 * (dxaml/lib/DisplayOrientationHelper.h). */
#pragma once
#include_next <microsoft.ui.content.h>

namespace ABI { namespace Microsoft { namespace UI { namespace Content {
enum ContentDisplayOrientations : unsigned int
{
    ContentDisplayOrientations_None = 0,
    ContentDisplayOrientations_Landscape = 0x1,
    ContentDisplayOrientations_Portrait = 0x2,
    ContentDisplayOrientations_LandscapeFlipped = 0x4,
    ContentDisplayOrientations_PortraitFlipped = 0x8,
};
}}}}
