// NovaOS: CCoreServices::getErrorService (core/dll/xcpcore.cpp) for programs
// that test the libraries without the core: no error service, as in
// Microsoft's parser unit tests' stub (components/parser/unittests/Stubs.cpp,
// which xamlparsetest links instead).  Alone in its object file, so a
// program that has its own does not link this one.
#include <corep.h>

_Check_return_ HRESULT CCoreServices::getErrorService(_Out_ IErrorService **ppErrorService)
{
    *ppErrorService = nullptr;
    return S_OK;
}
