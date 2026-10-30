/* NovaOS: the Windows SDK's MemoryBuffer.h: the byte access a
 * Windows.Foundation.IMemoryBufferReference answers (the imaging
 * component's AsyncCopyToSurfaceTask reads a software bitmap's pixels
 * through it). */
#pragma once
#include <unknwn.h>

namespace Windows {
namespace Foundation {

struct __declspec(uuid("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")) __declspec(novtable) IMemoryBufferByteAccess : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE **value, UINT32 *capacity) = 0;
};

}  // namespace Foundation
}  // namespace Windows
