/* NovaOS: MinGW-w64's windows.h, plus what the Windows SDK's has that
 * Microsoft's XAML sources (third_party/microsoft-ui-xaml) use and MinGW-w64
 * leaves out */
#pragma once
#include <nova_sal.h>
/* interfaces declared with DECLARE_INTERFACE_IID_ get their IIDs, as with MSVC */
#ifndef DECLSPEC_UUID
#define DECLSPEC_UUID(x) __declspec(uuid(x))
#endif
#include_next <windows.h>

#ifndef InterlockedIncrementNoFence
#define InterlockedIncrementNoFence InterlockedIncrement
#define InterlockedDecrementNoFence InterlockedDecrement
#define InterlockedCompareExchangeNoFence InterlockedCompareExchange
#define InterlockedExchangeNoFence InterlockedExchange
#define InterlockedAddNoFence InterlockedAdd
#define InterlockedIncrementAcquire InterlockedIncrement
#define InterlockedIncrementRelease InterlockedIncrement
#define InterlockedDecrementAcquire InterlockedDecrement
#define InterlockedDecrementRelease InterlockedDecrement
#define InterlockedCompareExchangeAcquire InterlockedCompareExchange
#define InterlockedCompareExchangeRelease InterlockedCompareExchange
#endif

/* winbase.h's GetCurrentTime() macro: clang rejects the macro call that
 * Windows.UI.Xaml's ITimeline-style GetCurrentTime methods make of it (MSVC
 * warns only) */
#undef GetCurrentTime

/* DirectComposition's surface error XAML's offer tracker checks (the
 * Windows SDK's winerror.h) */
#ifndef DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED
#define DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED _HRESULT_TYPEDEF_(0x88980801L)
#endif

/* the Windows Runtime's HRESULTs (the Windows SDK's winerror.h) */
#ifndef E_BOUNDS
#define E_BOUNDS _HRESULT_TYPEDEF_(0x8000000BL)
#endif
#ifndef E_CHANGED_STATE
#define E_CHANGED_STATE _HRESULT_TYPEDEF_(0x8000000CL)
#endif
#ifndef E_ILLEGAL_STATE_CHANGE
#define E_ILLEGAL_STATE_CHANGE _HRESULT_TYPEDEF_(0x8000000DL)
#endif
#ifndef E_ILLEGAL_METHOD_CALL
#define E_ILLEGAL_METHOD_CALL _HRESULT_TYPEDEF_(0x8000000EL)
#endif
#ifndef RO_E_METADATA_NAME_NOT_FOUND
#define RO_E_METADATA_NAME_NOT_FOUND _HRESULT_TYPEDEF_(0x8000000FL)
#endif
#ifndef E_STRING_NOT_NULL_TERMINATED
#define E_STRING_NOT_NULL_TERMINATED _HRESULT_TYPEDEF_(0x80000017L)
#endif
#ifndef E_ILLEGAL_DELEGATE_ASSIGNMENT
#define E_ILLEGAL_DELEGATE_ASSIGNMENT _HRESULT_TYPEDEF_(0x80000018L)
#endif
#ifndef E_ASYNC_OPERATION_NOT_STARTED
#define E_ASYNC_OPERATION_NOT_STARTED _HRESULT_TYPEDEF_(0x80000019L)
#endif
#ifndef RO_E_CLOSED
#define RO_E_CLOSED _HRESULT_TYPEDEF_(0x80000013L)
#endif
#ifndef RO_E_ERROR_STRING_NOT_FOUND
#define RO_E_ERROR_STRING_NOT_FOUND _HRESULT_TYPEDEF_(0x80000016L)
#endif
#ifndef RO_E_CHANGE_NOTIFICATION_IN_PROGRESS
#define RO_E_CHANGE_NOTIFICATION_IN_PROGRESS _HRESULT_TYPEDEF_(0x80000015L)
#endif
#ifndef RO_E_MUST_BE_AGILE
#define RO_E_MUST_BE_AGILE _HRESULT_TYPEDEF_(0x80000021L)
#endif
#ifndef E_NOT_SET
#define E_NOT_SET HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
#endif
#ifndef E_NOT_VALID_STATE
#define E_NOT_VALID_STATE HRESULT_FROM_WIN32(ERROR_INVALID_STATE)
#endif

/* the Windows SDK's flag operators are constexpr (XAML's metadata
 * initializes constexpr defaults with them); MinGW-w64's are not */
#ifdef __cplusplus
#undef DEFINE_ENUM_FLAG_OPERATORS
#define DEFINE_ENUM_FLAG_OPERATORS(E) \
extern "C++" { \
inline constexpr E operator|(E a, E b) throw() { return E((__underlying_type(E))a | (__underlying_type(E))b); } \
inline E &operator|=(E &a, E b) throw() { return a = a | b; } \
inline constexpr E operator&(E a, E b) throw() { return E((__underlying_type(E))a & (__underlying_type(E))b); } \
inline E &operator&=(E &a, E b) throw() { return a = a & b; } \
inline constexpr E operator~(E a) throw() { return E(~(__underlying_type(E))a); } \
inline constexpr E operator^(E a, E b) throw() { return E((__underlying_type(E))a ^ (__underlying_type(E))b); } \
inline E &operator^=(E &a, E b) throw() { return a = a ^ b; } \
}
#endif

/* agile references (the Windows SDK's objidl.h and combaseapi.h;
 * ole32 has RoGetAgileReference) */
#ifdef __cplusplus
#ifndef NOVA_AGILEREFERENCE_DEFINED
#define NOVA_AGILEREFERENCE_DEFINED
enum AgileReferenceOptions { AGILEREFERENCE_DEFAULT = 0, AGILEREFERENCE_DELAYEDMARSHAL = 1 };
extern "C" HRESULT WINAPI RoGetAgileReference(AgileReferenceOptions options, REFIID riid, IUnknown *pUnk,
                                              struct IAgileReference **ppAgileReference);
#endif
#endif

/* the Windows SDK's spelling: works on any expression, const or not */
#undef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (P)

/* GUID_NULL (cguid.h): the Windows SDK's uuid.lib defines it; here every
 * object that uses it carries one copy, folded at link time */
#ifdef __cplusplus
#include <cguid.h>
extern "C" __declspec(selectany) const GUID GUID_NULL = {};
#endif

/* the device family (the Windows SDK's winnt.h; ntdll exports it): XAML's
 * system theming reads the system's light or dark theme from UISettings on
 * Windows Core devices and from the registry elsewhere */
#ifndef DEVICEFAMILYINFOENUM_WINDOWS_CORE
#define DEVICEFAMILYINFOENUM_DESKTOP 0x00000003
#define DEVICEFAMILYINFOENUM_WINDOWS_CORE 0x00000010
#ifdef __cplusplus
extern "C"
#endif
__declspec(dllimport) void __stdcall RtlGetDeviceFamilyInfoEnum(ULONGLONG *uapInfo, DWORD *deviceFamily, DWORD *deviceForm);
#endif

/* the Windows SDK's imm.h's ImmDisableLegacyIME (imm32 exports it), which
 * MinGW-w64's lacks: DXamlCore turns the legacy IMEs off for its thread */
#ifdef __cplusplus
extern "C"
#endif
__declspec(dllimport) BOOL __stdcall ImmDisableLegacyIME(void);
