// NovaOS: CErrorService::OriginateInvalidOperationError, which the core's
// resource dictionary (core/core/elements/Resources.cpp) raises, for
// programs that test the libraries without the core's error service: no
// error to originate, the operation simply fails.  Alone in its object
// file, so a program on the core, which has the core's own
// (core/error/ErrorService.cpp), does not link it (build.py
// CORE_SERVICES_STUBS).
#include <corep.h>
#include <ErrorService.h>

_Check_return_ HRESULT CErrorService::OriginateInvalidOperationError(_In_ CCoreServices *, _In_ XUINT32, _In_ const xstring_ptr_view &,
                                                                     _In_ const xstring_ptr_view &, _In_ const xstring_ptr_view &)
{
    return E_FAIL;
}
