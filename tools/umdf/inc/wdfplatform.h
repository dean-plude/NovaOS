/* wdfplatform.h — the UMDF platform interfaces the host hands the framework
 * (NovaOS stand-in for the internal header, README.md; the host's side is
 * tools/umdf/host) */
#pragma once

/* A DDI call's activation frame: each framework entry point a driver calls
 * pushes one on the calling thread's list (IUMDFPlatformModule::
 * GetActivationListHead) for the call's lifetime, so the host can tell
 * framework code from driver code on a thread */
typedef struct _WDF_ACTIVATION_FRAME {
    struct _WDF_ACTIVATION_FRAME *Next;
    PVOID ReturnAddress;
} WDF_ACTIVATION_FRAME, *PWDF_ACTIVATION_FRAME;

DEFINE_GUID(IID_IUMDFPlatform, 0x6d1a2f47, 0x8b2e, 0x4c4e, 0x9f, 0x10, 0x4e, 0x6f, 0x76, 0x61, 0x4f, 0x01);
DEFINE_GUID(IID_IUMDFPlatformModule, 0x6d1a2f47, 0x8b2e, 0x4c4e, 0x9f, 0x10, 0x4e, 0x6f, 0x76, 0x61, 0x4f, 0x02);

#ifdef __cplusplus
struct IUMDFPlatform : public IUnknown {
};

struct IUMDFPlatformModule : public IUnknown {
    virtual PWDF_ACTIVATION_FRAME *STDMETHODCALLTYPE GetActivationListHead(void) = 0;
};

class WDF_ACTIVATION {
    WDF_ACTIVATION_FRAME m_Frame;
    PWDF_ACTIVATION_FRAME *m_List;
public:
    WDF_ACTIVATION(PWDF_ACTIVATION_FRAME *List, PVOID ReturnAddress = _ReturnAddress()) : m_List(List)
    {
        m_Frame.ReturnAddress = ReturnAddress;
        m_Frame.Next = List ? *List : NULL;
        if (List) *List = &m_Frame;
    }
    ~WDF_ACTIVATION()
    {
        if (m_List) *m_List = m_Frame.Next;
    }
};
#endif
