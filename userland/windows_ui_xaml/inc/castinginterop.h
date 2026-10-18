// NovaOS: the Windows SDK's castinginterop.h (a XAML media or image
// element as a Windows.Media.Casting source), which MinGW-w64 lacks: the
// interfaces and property names from the public documentation.
// Declarations only (NovaOS has no casting yet).
#pragma once
#include <unknwn.h>
#include <propsys.h>
typedef enum CASTING_CONNECTION_STATE {
    CASTING_CONNECTION_STATE_DISCONNECTED = 0, CASTING_CONNECTION_STATE_CONNECTED = 1,
    CASTING_CONNECTION_STATE_RENDERING = 2, CASTING_CONNECTION_STATE_DISCONNECTING = 3,
    CASTING_CONNECTION_STATE_CONNECTING = 4
} CASTING_CONNECTION_STATE;
typedef enum CASTING_CONNECTION_ERROR_STATUS {
    CASTING_CONNECTION_ERROR_STATUS_SUCCEEDED = 0, CASTING_CONNECTION_ERROR_STATUS_DEVICE_DID_NOT_RESPOND = 1,
    CASTING_CONNECTION_ERROR_STATUS_DEVICE_ERROR = 2, CASTING_CONNECTION_ERROR_STATUS_DEVICE_LOCKED = 3,
    CASTING_CONNECTION_ERROR_STATUS_PROTECTED_PLAYBACK_FAILED = 4,
    CASTING_CONNECTION_ERROR_STATUS_INVALID_CASTING_SOURCE = 5, CASTING_CONNECTION_ERROR_STATUS_UNKNOWN = 6
} CASTING_CONNECTION_ERROR_STATUS;
#define CastingSourceInfo_Property_PreferredSourceUriScheme L"PreferredSourceUriScheme"
#define CastingSourceInfo_Property_CastingTypes L"CastingTypes"
#define CastingSourceInfo_Property_ProtectedMedia L"ProtectedMedia"
MIDL_INTERFACE("c79a6cb7-bebd-47a6-a2ad-4d45ad79c7bc")
ICastingEventHandler : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnStateChanged(CASTING_CONNECTION_STATE newState) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnError(CASTING_CONNECTION_ERROR_STATUS errorStatus, LPCWSTR errorMessage) = 0;
};
MIDL_INTERFACE("f0a56423-a664-4fbd-8b43-409a45e8d9a1")
ICastingController : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Initialize(IUnknown *castingEngine, IUnknown *castingSource) = 0;
    virtual HRESULT STDMETHODCALLTYPE Connect() = 0;
    virtual HRESULT STDMETHODCALLTYPE Disconnect() = 0;
    virtual HRESULT STDMETHODCALLTYPE Advise(ICastingEventHandler *eventHandler, DWORD *cookie) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnAdvise(DWORD cookie) = 0;
};
MIDL_INTERFACE("45101ab7-7c3a-4bce-9500-12c09024b298")
ICastingSourceInfo : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetController(ICastingController **controller) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperties(INamedPropertyStore **props) = 0;
};
