// NovaOS: the Windows SDK's mfsharingengine.h (Media Foundation's Play To
// sharing engines), which MinGW-w64 lacks: the class factory XAML's image
// casting interface takes, from its public documentation.  Declarations
// only (NovaOS has no casting yet).
#pragma once
#include <unknwn.h>
struct IMFAttributes;
MIDL_INTERFACE("2ba61f92-8305-413b-9733-faf15f259384")
IMFSharingEngineClassFactory : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(DWORD dwFlags, IMFAttributes *pAttr, IUnknown **ppEngine) = 0;
};
#include <mfmediaengine.h>
typedef enum MF_SHARING_ENGINE_EVENT {
    MF_SHARING_ENGINE_EVENT_DISCONNECT = 2000, MF_SHARING_ENGINE_EVENT_LOCALRENDERINGSTARTED = 2001,
    MF_SHARING_ENGINE_EVENT_LOCALRENDERINGENDED = 2002, MF_SHARING_ENGINE_EVENT_STOPPED = 2003,
    MF_SHARING_ENGINE_EVENT_ERROR = 2501
} MF_SHARING_ENGINE_EVENT;
