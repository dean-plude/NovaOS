/* NovaOS: the Windows SDK's roerrorapi.h (with restrictederrorinfo.h's
 * interface), which MinGW-w64 lacks: the Windows Runtime's error objects,
 * IRestrictedErrorInfo, that XAML's error helpers (dxaml/lib ErrorInfo.h)
 * keep and hand on, and the functions that set and take a thread's.  The
 * SDK's WRL (wrl/implements.h) includes it, as NovaOS's does. */
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

extern "C" {
BOOL WINAPI RoOriginateError(HRESULT error, HSTRING message);
BOOL WINAPI RoOriginateErrorW(HRESULT error, UINT cchMax, PCWSTR message);
BOOL WINAPI RoTransformError(HRESULT oldError, HRESULT newError, HSTRING message);
HRESULT WINAPI GetRestrictedErrorInfo(IRestrictedErrorInfo **ppRestrictedErrorInfo);
HRESULT WINAPI SetRestrictedErrorInfo(IRestrictedErrorInfo *pRestrictedErrorInfo);
}
