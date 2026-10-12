/*
 * SleepStudyHelper.h — Sleep Study, not available
 *
 * Sleep Study is Modern Standby's report of what kept a laptop awake; the
 * WDK links drivers with a helper library that logs to it.  NovaOS has no
 * Sleep Study: the library never initializes (STATUS_NOT_SUPPORTED) and the
 * framework, as it does on any such failure, logs nothing for the device.
 */
#pragma once

typedef struct _SS_LIBRARY_CONTEXT *SS_LIBRARY;
typedef struct _SS_COMPONENT_CONTEXT *SS_COMPONENT;

typedef enum _SSH_ID_TYPE { SSH_PDO, SSH_FDO } SSH_ID_TYPE;

#define SLEEPSTUDY_POOL_TAG       'SSxF'
#define FRIENDLY_NAME_MAX_LENGTH  256

static __inline NTSTATUS SleepstudyHelper_Initialize(_Out_ SS_LIBRARY *Library, _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    *Library = NULL;
    return STATUS_NOT_SUPPORTED;
}
static __inline VOID SleepstudyHelper_Uninitialize(_In_ SS_LIBRARY Library) { UNREFERENCED_PARAMETER(Library); }
static __inline VOID SleepstudyHelper_GenerateGuid(_In_ SSH_ID_TYPE Type, _In_ ULONG64 Value, _Out_ GUID *Guid)
{
    UNREFERENCED_PARAMETER(Type);
    RtlZeroMemory(Guid, sizeof(*Guid));
    Guid->Data1 = (ULONG)Value;
}
static __inline NTSTATUS SleepstudyHelper_GetPdoFriendlyName(_In_ PDEVICE_OBJECT Pdo, _Inout_ PUNICODE_STRING Name)
{
    UNREFERENCED_PARAMETER(Pdo);
    UNREFERENCED_PARAMETER(Name);
    return STATUS_NOT_SUPPORTED;
}
static __inline NTSTATUS SleepstudyHelper_RegisterComponentEx(_In_ SS_LIBRARY Library, _In_ GUID Parent, _In_ GUID Self,
                                                              _In_ PUNICODE_STRING FriendlyName, _Out_ SS_COMPONENT *Component)
{
    UNREFERENCED_PARAMETER(Library);
    UNREFERENCED_PARAMETER(Parent);
    UNREFERENCED_PARAMETER(Self);
    UNREFERENCED_PARAMETER(FriendlyName);
    *Component = NULL;
    return STATUS_NOT_SUPPORTED;
}
static __inline VOID SleepstudyHelper_UnregisterComponent(_In_ SS_COMPONENT Component) { UNREFERENCED_PARAMETER(Component); }
static __inline VOID SleepstudyHelper_AcquireComponentLock(_In_ SS_COMPONENT Component, _Out_ PKIRQL Irql)
{
    UNREFERENCED_PARAMETER(Component);
    *Irql = PASSIVE_LEVEL;
}
static __inline VOID SleepstudyHelper_ReleaseComponentLock(_In_ SS_COMPONENT Component, _In_ KIRQL Irql)
{
    UNREFERENCED_PARAMETER(Component);
    UNREFERENCED_PARAMETER(Irql);
}
static __inline VOID SleepstudyHelper_ComponentActive(_In_ SS_COMPONENT Component) { UNREFERENCED_PARAMETER(Component); }
static __inline VOID SleepstudyHelper_ComponentActiveLocked(_In_ SS_COMPONENT Component) { UNREFERENCED_PARAMETER(Component); }
static __inline VOID SleepstudyHelper_ComponentInactive(_In_ SS_COMPONENT Component) { UNREFERENCED_PARAMETER(Component); }
static __inline VOID SleepstudyHelper_ResetComponentsStartTime(_In_ SS_COMPONENT Component) { UNREFERENCED_PARAMETER(Component); }
