/* NovaOS: MinGW-w64's uiautomationcore.h, plus what the Windows SDK's has
 * that XAML's UI Automation providers (dxaml/xcp/win/shared: UIAWindow,
 * UIAWrapper) use and MinGW-w64 leaves out.  Interfaces in vtable order,
 * IIDs and values as Microsoft's Win32 metadata gives them. */
#pragma once
#include_next <uiautomationcore.h>

#ifdef __cplusplus
#ifndef __IRawElementProviderAdviseEvents_INTERFACE_DEFINED__
#define __IRawElementProviderAdviseEvents_INTERFACE_DEFINED__
/* a provider told when a client starts or stops listening for an event */
struct __declspec(uuid("a407b27b-0f6d-4427-9292-473c7bf93258")) __declspec(novtable) IRawElementProviderAdviseEvents : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE AdviseEventAdded(EVENTID eventId, SAFEARRAY *propertyIDs) = 0;
    virtual HRESULT STDMETHODCALLTYPE AdviseEventRemoved(EVENTID eventId, SAFEARRAY *propertyIDs) = 0;
};
#endif
#ifndef __IRawElementProviderSimple2_INTERFACE_DEFINED__
#define __IRawElementProviderSimple2_INTERFACE_DEFINED__
struct __declspec(uuid("a0a839a9-8da1-4a82-806a-8e0d44e79f56")) __declspec(novtable) IRawElementProviderSimple2 : public IRawElementProviderSimple
{
    virtual HRESULT STDMETHODCALLTYPE ShowContextMenu() = 0;
};
#endif
#ifndef __IRawElementProviderWindowlessSite_INTERFACE_DEFINED__
#define __IRawElementProviderWindowlessSite_INTERFACE_DEFINED__
/* the site a windowless control's provider hangs in its host's tree from */
struct __declspec(uuid("0a2a93cc-bfad-42ac-9b2e-0991fb0d3ea0")) __declspec(novtable) IRawElementProviderWindowlessSite : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentFragment(enum NavigateDirection direction, IRawElementProviderFragment **parent) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeIdPrefix(SAFEARRAY **runtimeIdPrefix) = 0;
};
#endif
#endif

/* landmark types and the text attributes MinGW-w64 leaves out */
#ifndef UIA_CustomLandmarkTypeId
#define UIA_CustomLandmarkTypeId 80000
#define UIA_FormLandmarkTypeId 80001
#define UIA_MainLandmarkTypeId 80002
#define UIA_NavigationLandmarkTypeId 80003
#define UIA_SearchLandmarkTypeId 80004
#endif
#ifndef UIA_AnnotationObjectsAttributeId
#define UIA_AnnotationObjectsAttributeId 40032
#endif
#ifndef UIA_LinkAttributeId
#define UIA_LinkAttributeId 40035
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* whether any UI Automation client is listening (uiautomationcore.dll) */
BOOL WINAPI UiaClientsAreListening(void);
#ifdef __cplusplus
}
#endif
