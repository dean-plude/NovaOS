// NovaOS: the stable XBF indexes of the properties Windows moved from
// Control (and FrameworkElement) to UIElement in Windows 10 1709: the
// focus properties (IsTabStop, TabIndex, FocusState, UseSystemFocusVisuals,
// XYFocusUp/Down/Left/Right) and AccessKey.  A UWP app's compiled markup
// (Calculator's NavigationView, for one) still names them by their old
// indexes, which Windows' XAML reads as the moved properties; WinUI 3's
// table (StableXbfIndexMapping.g.cpp) maps them to the unknown property,
// so setting one fails the page.  Parser::GetKnownPropertyIndex answers
// the moved property for each, and Microsoft's own
// (XbfMetadataApi.cpp, renamed out by build.py's FILE_RENAMES) the rest.
#include "precomp.h"
#include "XbfMetadataApi.h"

namespace Parser
{
    KnownPropertyIndex GetKnownPropertyIndex_vendored(_In_ StableXbfPropertyIndex index);

    KnownPropertyIndex GetKnownPropertyIndex(_In_ StableXbfPropertyIndex index)
    {
        switch (index)
        {
        case StableXbfPropertyIndex::Control_FocusState_Deleted0: return KnownPropertyIndex::UIElement_FocusState;
        case StableXbfPropertyIndex::Control_IsTabStop_Deleted0: return KnownPropertyIndex::UIElement_IsTabStop;
        case StableXbfPropertyIndex::Control_TabIndex_Deleted0: return KnownPropertyIndex::UIElement_TabIndex;
        case StableXbfPropertyIndex::Control_UseSystemFocusVisuals_Deleted0: return KnownPropertyIndex::UIElement_UseSystemFocusVisuals;
        case StableXbfPropertyIndex::FrameworkElement_AccessKey_Deleted0: return KnownPropertyIndex::UIElement_AccessKey;
        case StableXbfPropertyIndex::Control_XYFocusDown_Deleted0:
        case StableXbfPropertyIndex::Control_XYFocusDown_Deleted1: return KnownPropertyIndex::UIElement_XYFocusDown;
        case StableXbfPropertyIndex::Control_XYFocusLeft_Deleted0:
        case StableXbfPropertyIndex::Control_XYFocusLeft_Deleted1: return KnownPropertyIndex::UIElement_XYFocusLeft;
        case StableXbfPropertyIndex::Control_XYFocusRight_Deleted0:
        case StableXbfPropertyIndex::Control_XYFocusRight_Deleted1: return KnownPropertyIndex::UIElement_XYFocusRight;
        case StableXbfPropertyIndex::Control_XYFocusUp_Deleted0:
        case StableXbfPropertyIndex::Control_XYFocusUp_Deleted1: return KnownPropertyIndex::UIElement_XYFocusUp;
        default: return GetKnownPropertyIndex_vendored(index);
        }
    }
}
