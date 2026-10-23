/* NovaOS's WRL (see wrl/client.h): <wrl/ftm.h>, FtmBase, which makes an
 * object agile: its IMarshal is COM's free-threaded marshaler's */
#pragma once
#include <objbase.h>
#include <wrl/implements.h>

namespace Microsoft { namespace WRL {

// (IAgileObject, method-less, is answered here with the IMarshal rather
// than derived, so a class may name IAgileObject next to FtmBase, as
// XAML's composition batch handlers do: Implements<..., IAgileObject, FtmBase>)
class FtmBase : public Implements<RuntimeClassFlags<ClassicCom>, CloakedIid<IMarshal>>
{
public:
    bool CanCastTo(REFIID riid, void **ppv) throw()
    {
        if (InlineIsEqualGUID(riid, __uuidof(IAgileObject))) {
            *ppv = static_cast<IMarshal *>(this);
            return true;
        }
        return Implements::CanCastTo(riid, ppv);
    }
    FtmBase() throw()
    {
        ComPtr<IUnknown> unk;
        if (SUCCEEDED(::CoCreateFreeThreadedMarshaler(nullptr, &unk))) unk.As(&marshaller_);
    }
    STDMETHOD(GetUnmarshalClass)(REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags,
                                 CLSID *pCid) override
    {
        return marshaller_ ? marshaller_->GetUnmarshalClass(riid, pv, dwDestContext, pvDestContext, mshlflags, pCid) : E_OUTOFMEMORY;
    }
    STDMETHOD(GetMarshalSizeMax)(REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext, DWORD mshlflags,
                                 DWORD *pSize) override
    {
        return marshaller_ ? marshaller_->GetMarshalSizeMax(riid, pv, dwDestContext, pvDestContext, mshlflags, pSize) : E_OUTOFMEMORY;
    }
    STDMETHOD(MarshalInterface)(IStream *pStm, REFIID riid, void *pv, DWORD dwDestContext, void *pvDestContext,
                                DWORD mshlflags) override
    {
        return marshaller_ ? marshaller_->MarshalInterface(pStm, riid, pv, dwDestContext, pvDestContext, mshlflags) : E_OUTOFMEMORY;
    }
    STDMETHOD(UnmarshalInterface)(IStream *pStm, REFIID riid, void **ppv) override
    {
        return marshaller_ ? marshaller_->UnmarshalInterface(pStm, riid, ppv) : E_OUTOFMEMORY;
    }
    STDMETHOD(ReleaseMarshalData)(IStream *pStm) override
    {
        return marshaller_ ? marshaller_->ReleaseMarshalData(pStm) : E_OUTOFMEMORY;
    }
    STDMETHOD(DisconnectObject)(DWORD dwReserved) override
    {
        return marshaller_ ? marshaller_->DisconnectObject(dwReserved) : E_OUTOFMEMORY;
    }
    static HRESULT CreateGlobalInterfaceTable(IGlobalInterfaceTable **git) throw()
    {
        *git = nullptr;
        return ::CoCreateInstance(CLSID_StdGlobalInterfaceTable, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IGlobalInterfaceTable),
                                  reinterpret_cast<void **>(git));
    }
    ComPtr<IMarshal> marshaller_;
};

}} // namespace Microsoft::WRL
