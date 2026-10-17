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
#ifndef RO_E_MUST_BE_AGILE
#define RO_E_MUST_BE_AGILE _HRESULT_TYPEDEF_(0x80000021L)
#endif
#ifndef E_NOT_SET
#define E_NOT_SET HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
#endif
#ifndef E_NOT_VALID_STATE
#define E_NOT_VALID_STATE HRESULT_FROM_WIN32(ERROR_INVALID_STATE)
#endif

/* the Windows SDK's spelling: works on any expression, const or not */
#undef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (P)
