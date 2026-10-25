/* NovaOS: MinGW-w64's powrprof.h, plus the power setting notifications the
 * Windows SDK's declares (powerbase.h, powersetting.h) and MinGW-w64 leaves
 * out: ToolTipService hides open tooltips when the display turns off.
 * Values as Microsoft's Win32 metadata gives them. */
#pragma once
#include_next <powrprof.h>

#ifndef DEVICE_NOTIFY_CALLBACK
#define DEVICE_NOTIFY_CALLBACK 2
typedef ULONG CALLBACK DEVICE_NOTIFY_CALLBACK_ROUTINE(PVOID Context, ULONG Type, PVOID Setting);
typedef DEVICE_NOTIFY_CALLBACK_ROUTINE *PDEVICE_NOTIFY_CALLBACK_ROUTINE;
typedef struct _DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS {
    PDEVICE_NOTIFY_CALLBACK_ROUTINE Callback;
    PVOID Context;
} DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS, *PDEVICE_NOTIFY_SUBSCRIBE_PARAMETERS;
#ifdef __cplusplus
extern "C" {
#endif
DWORD WINAPI PowerSettingRegisterNotification(LPCGUID SettingGuid, DWORD Flags, HANDLE Recipient, PHPOWERNOTIFY RegistrationHandle);
DWORD WINAPI PowerSettingUnregisterNotification(HPOWERNOTIFY RegistrationHandle);
#ifdef __cplusplus
}
#endif
#endif
