// NovaOS: what the DLL's features need from the parts of WinUI 2 not built
// yet, and the definitions Visual Studio's SDK libraries would give it.
#include "pch.h"
#include "common.h"
#include "TabViewTrace.h"

// TabView's trace switches (TabView.cpp defines them; TestHooks sets them)
bool TabViewTrace::s_IsDebugOutputEnabled{ false };
bool TabViewTrace::s_IsVerboseDebugOutputEnabled{ false };
