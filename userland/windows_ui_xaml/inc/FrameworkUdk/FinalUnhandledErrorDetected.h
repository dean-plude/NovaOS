/* NovaOS: the Windows App SDK's FrameworkUdk/FinalUnhandledErrorDetected.h
 * (FrameworkUdk, shipped closed).  The framework's error helper registers
 * here for the last chance to see an error no handler took (the
 * CoreApplication's FinalUnhandledErrorDetected); src/framework_udk.cpp
 * keeps the handler. */
#pragma once
#include <windows.foundation.h>
#include <windows.applicationmodel.core.h>

HRESULT FinalUnhandledErrorDetected_RegisterHandler(
    ABI::Windows::Foundation::IEventHandler<ABI::Windows::ApplicationModel::Core::UnhandledErrorDetectedEventArgs *> *handler,
    EventRegistrationToken *token);
HRESULT FinalUnhandledErrorDetected_UnregisterHandler(EventRegistrationToken *token);
