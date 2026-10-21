// NovaOS stand-in for the Windows SDK's documentsource.h (not in MinGW):
// the two print-preview source interfaces PrintDocument_Partial.h implements.
#pragma once
#include <unknwn.h>
#include <inspectable.h>
#include <documenttarget.h>
struct IPrintPreviewPageCollection;
struct __declspec(uuid("a96bb1db-172e-4667-82b5-ad97a252318f")) __declspec(novtable) IPrintDocumentPageSource : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetPreviewPageCollection(IPrintDocumentPackageTarget *docPackageTarget, IPrintPreviewPageCollection **docPageCollection) = 0;
    virtual HRESULT STDMETHODCALLTYPE MakeDocument(IInspectable *docSettings, IPrintDocumentPackageTarget *docPackageTarget) = 0;
};
struct __declspec(uuid("0b31cc62-d7ec-4747-9d6e-f2537d870f2b")) __declspec(novtable) IPrintPreviewPageCollection : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Paginate(UINT32 currentJobPage, IInspectable *docSettings) = 0;
    virtual HRESULT STDMETHODCALLTYPE MakePage(UINT32 desiredJobPage, FLOAT width, FLOAT height) = 0;
};
