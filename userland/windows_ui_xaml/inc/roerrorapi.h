/* NovaOS: the Windows SDK's roerrorapi.h (with restrictederrorinfo.h's
 * interfaces), which MinGW-w64 lacks: the Windows Runtime's error objects,
 * IRestrictedErrorInfo, that XAML's error helpers (dxaml/lib ErrorInfo.h)
 * keep and hand on, the functions that set and take a thread's and that
 * report an unhandled one, and the C++ helpers in
 * Windows::Foundation::Diagnostics the error helper (ErrorHelper.cpp) calls.
 * The SDK's WRL (wrl/implements.h) includes it, as NovaOS's does. */
#pragma once
#include <oaidl.h>
#include <winstring.h>

#ifndef __IRestrictedErrorInfo_INTERFACE_DEFINED__
#define __IRestrictedErrorInfo_INTERFACE_DEFINED__
struct __declspec(uuid("82BA7092-4C88-427D-A7BC-16DD93FEB67E")) __declspec(novtable) IRestrictedErrorInfo : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetErrorDetails(BSTR *description, HRESULT *error, BSTR *restrictedDescription,
                                                      BSTR *capabilitySid) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetReference(BSTR *reference) = 0;
};
#endif

#ifndef __ILanguageExceptionStackBackTrace_INTERFACE_DEFINED__
#define __ILanguageExceptionStackBackTrace_INTERFACE_DEFINED__
struct __declspec(uuid("CBE53FB5-F967-4258-8D34-42F5E25833DE")) __declspec(novtable) ILanguageExceptionStackBackTrace : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetStackBackTrace(ULONG maxFramesToCapture, UINT_PTR stackBackTrace[],
                                                        ULONG *framesCaptured) = 0;
};
#endif

extern "C" {
BOOL WINAPI RoOriginateLanguageException(HRESULT error, HSTRING message, IUnknown *languageException);
BOOL WINAPI RoOriginateError(HRESULT error, HSTRING message);
BOOL WINAPI RoOriginateErrorW(HRESULT error, UINT cchMax, PCWSTR message);
BOOL WINAPI RoTransformError(HRESULT oldError, HRESULT newError, HSTRING message);
HRESULT WINAPI GetRestrictedErrorInfo(IRestrictedErrorInfo **ppRestrictedErrorInfo);
HRESULT WINAPI SetRestrictedErrorInfo(IRestrictedErrorInfo *pRestrictedErrorInfo);
HRESULT WINAPI RoReportUnhandledError(IRestrictedErrorInfo *pRestrictedErrorInfo);
HRESULT WINAPI RoGetErrorReportingFlags(UINT32 *pflags);
}

typedef enum RO_ERROR_REPORTING_FLAGS {
    RO_ERROR_REPORTING_NONE = 0, RO_ERROR_REPORTING_SUPPRESSEXCEPTIONS = 1, RO_ERROR_REPORTING_FORCEEXCEPTIONS = 2,
    RO_ERROR_REPORTING_USESETERRORINFO = 4, RO_ERROR_REPORTING_SUPPRESSSETERRORINFO = 8,
} RO_ERROR_REPORTING_FLAGS;

namespace ABI { namespace Windows { namespace Foundation { namespace Diagnostics {
enum RoErrorReportingFlags {
    None = RO_ERROR_REPORTING_NONE, SuppressExceptions = RO_ERROR_REPORTING_SUPPRESSEXCEPTIONS,
    ForceExceptions = RO_ERROR_REPORTING_FORCEEXCEPTIONS, UseSetErrorInfo = RO_ERROR_REPORTING_USESETERRORINFO,
    SuppressSetErrorInfo = RO_ERROR_REPORTING_SUPPRESSSETERRORINFO,
};
inline HRESULT GetErrorReportingFlags(UINT32 *flags) { return RoGetErrorReportingFlags(flags); }
inline BOOL OriginateError(HRESULT error) { return RoOriginateError(error, nullptr); }
inline BOOL OriginateError(HRESULT error, HSTRING message) { return RoOriginateError(error, message); }
inline BOOL OriginateError(HRESULT error, UINT cchMax, PCWSTR message) { return RoOriginateErrorW(error, cchMax, message); }
}}}}
