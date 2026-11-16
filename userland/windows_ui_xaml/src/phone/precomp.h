// NovaOS: the precompiled header of the phone library's sources NovaOS
// builds (third_party/microsoft-ui-xaml/dxaml/phone/lib: the navigation
// transitions, ThemeTransitions.cpp and NavigateTransitionHelper.cpp; the
// Pivot, its items, panels, header and automation peers, with the
// library's generated classes, PhoneTypes.g.cpp, and its type metadata,
// dllsrv/exts/phone/XamlTypeInfo.cpp; and SatelliteBase's
// dependency-property helper XamlWrlHelpers.cpp), standing in for the
// library's own precomp.h: the same headers, in the same order, less the
// picker flyouts', LoopingSelector's and the jump list's (they need WRL's
// async operations, which NovaOS's WRL lacks; build.py's phone_built()
// leaves their generated classes out) and the MUI loader's.  See
// docs/design/windows-ui-xaml.md.
#pragma once

#include <new>

// (NovaOS: the classes, composed over the framework's, answer Windows'
// interfaces through the framework's faces: inc/wrl/implements.h)
#define NOVA_WRL_XAML_FACES 1

#include <windows.h>
#include <strsafe.h>

// WRL
#include <wrl\async.h>
#include <wrl.h>

#include <windows.foundation.collections.h>
#include "NamespaceAliases.h"
#include <windows.globalization.h>
#include <Windows.UI.ViewManagement.h>
#include <windowscollections.h>
#include <windows.storage.h>

// WIL
#include <wil/common.h>

// WinRT
#include <windows.graphics.display.h>
#include <robuffer.h>
#include <windows.foundation.h>
#include <windows.foundation.declarations.h>

// STL
#include <list>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <chrono>
#include <type_traits>

// XAML
#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime

#include <microsoft.ui.xaml.h>
#include <microsoft.ui.xaml.controls.controls.h>
#include <microsoft.ui.xaml.private.h>
#include <microsoft.ui.xaml.phone.h>

#include <ReferenceTrackerExtension.h>

#pragma pop_macro("GetCurrentTime")

// common
#include "NamespaceAliases.h"
#include "SatMacros.h"
#include "minerror.h"
#include "macros.h"
#include "RuntimeErrorCodes.h"

// Reference tracking classes
#include "TrackerPtrFamily.h"
#include "ReferenceTrackerRuntimeClass.h"

// utility
#include "DllInitializedHelpers.h"
#include "LocalizationHelpers.h"
#include "DebugWriter.h"
#include "AutomationHelper.h"
#include "ValueBoxer.h"
#include "ValueHelpers.h"
#include "TypeUtils.h"
#include "ApplicationResourceHelpers.h"
#include "BindableToObservableVectorWrapper.h"

// COM aggregation support (NovaOS: SatelliteBase's, by its path: the
// framework's own comTemplateLibrary.h answers this spelling, build.py's
// FOLD_OTHER_CASE)
#include "../../../../third_party/microsoft-ui-xaml/dxaml/xcp/components/SatelliteBase/inc/ComTemplateLibrary.h"

// Localization resources
#include "PhoneResource.h"

// Generated types
#include "PhoneTypes.g.h"

// UIA Headers
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>
// (NovaOS: the scroll pattern's "no scroll" percentage, which MinGW-w64's
// uiautomationcoreapi.h lacks)
#ifndef UIA_ScrollPatternNoScroll
static const double UIA_ScrollPatternNoScroll = -1;
#endif

#include "RoVariant.h"

// controls
#include "Pivot_Partial.h"
#include "PivotPanel_Partial.h"
#include "PivotHeaderPanel_Partial.h"
#include "PivotAutomationPeer_Partial.h"
#include "PivotItem_Partial.h"
#include "PivotItemAutomationPeer_Partial.h"
#include "PivotItemDataAutomationPeer_Partial.h"
#include "PivotHeaderItem.h"
#include "PivotItemEventArgs.h"
#include "NavigateTransitionHelper.h"
#include "ThemeTransitions.h"
#include "PlatformHelpers.h"

