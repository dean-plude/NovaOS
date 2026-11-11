// NovaOS: the precompiled header of the phone library's sources NovaOS
// builds (third_party/microsoft-ui-xaml/dxaml/phone/lib: the navigation
// transitions, ThemeTransitions.cpp and NavigateTransitionHelper.cpp; and
// SatelliteBase's XamlWrlHelpers.cpp, their dependency-property helper),
// standing in for the library's own precomp.h, which takes every phone
// control (Pivot, the picker flyouts, LoopingSelector ...): the same
// headers, in the same order, less the controls not built yet.  See
// docs/design/windows-ui-xaml.md.
#pragma once

#include <new>

#include <windows.h>
#include <strsafe.h>

// WRL
#include <wrl\async.h>
#include <wrl.h>

// WIL
#include <wil/common.h>

// WinRT
#include <windows.foundation.h>

// STL
#include <list>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <unordered_map>
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

// utility
#include "ValueBoxer.h"

#include "RoVariant.h"

// the navigation transitions
#include "NavigateTransitionHelper.h"
#include "ThemeTransitions.h"
