// NovaOS: CErrorService::OriginateInvalidOperationError (core/error/
// ErrorService.cpp) for programs that test the libraries without the core:
// the resource dictionary reports an invalid operation through it, and
// with no error service there is nothing to originate (E_FAIL).  Alone in
// its object file, so a program on the core, which links the error service
// itself, does not link this one.
#include <corep.h>
#include <ErrorService.h>

_Check_return_ HRESULT CErrorService::OriginateInvalidOperationError(_In_ CCoreServices *, _In_ XUINT32, _In_ const xstring_ptr_view &,
                                                                     _In_ const xstring_ptr_view &, _In_ const xstring_ptr_view &)
{
    return E_FAIL;
}
