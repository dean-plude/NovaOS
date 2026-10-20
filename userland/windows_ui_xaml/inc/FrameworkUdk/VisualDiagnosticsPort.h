/* NovaOS: the Windows App SDK's FrameworkUdk/VisualDiagnosticsPort.h
 * (FrameworkUdk, shipped closed): the system-wide message port a XAML
 * thread opens so diagnostics tools (Visual Studio's live visual tree) can
 * connect to it; the callback receives their connect message. */
#pragma once
#include <windows.h>

typedef HRESULT (CALLBACK *VisualDiagnosticsPort_InitCallback)(void *userContext, const void *payload, int size);
HRESULT VisualDiagnosticsPort_Register(PCWSTR endpointName, VisualDiagnosticsPort_InitCallback callback, void *userContext);
void VisualDiagnosticsPort_Unregister();
