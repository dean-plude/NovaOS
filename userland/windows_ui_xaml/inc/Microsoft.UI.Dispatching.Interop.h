/* NovaOS: the Windows App SDK's Microsoft.UI.Dispatching.Interop.h (shipped
 * with the SDK, not with XAML): what a Win32 message loop on a
 * DispatcherQueue thread asks before TranslateMessage, whether the content
 * (an input site) takes the message itself.  Application.Start's message
 * loop (dxaml/lib FrameworkApplication_Partial.cpp) calls it. */
#pragma once
#include <windows.h>

extern "C" BOOL WINAPI ContentPreTranslateMessage(const MSG *msg);
