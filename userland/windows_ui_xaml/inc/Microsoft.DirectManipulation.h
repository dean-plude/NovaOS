/* NovaOS: the Windows App SDK's Microsoft.DirectManipulation.h, the
 * header of its lifted DirectManipulation (Microsoft.DirectManipulation.dll,
 * shipped closed).  It is the Windows SDK's directmanipulation.h with the
 * behaviours' class IDs renamed CLSID_Microsoft_* (XAML's
 * plat/win/browserdesktop/DirectManipulationService.cpp creates its
 * manager from that DLL's class factory and its behaviours by those IDs).
 *
 * MinGW-w64's directmanipulation.h has the Windows 8.1 interfaces; what
 * XAML uses beyond them (the drag-drop and auto-scroll behaviours,
 * IDirectManipulationManager3, focus and zoom constants) is declared here
 * from the Windows SDK's documentation.  The interfaces MinGW-w64 lacks and
 * the lifted class IDs are NovaOS's own IDs: NovaOS's
 * Microsoft.DirectManipulation.dll is the one that answers to them. */
#pragma once
#include <directmanipulation.h>

#define DIRECTMANIPULATION_KEYBOARDFOCUS ((DWORD)0xFFFFFFFE)
#define DIRECTMANIPULATION_MOUSEFOCUS ((DWORD)0xFFFFFFFD)
#ifndef DIRECTMANIPULATION_MINIMUM_ZOOM
#define DIRECTMANIPULATION_MINIMUM_ZOOM 0.1f
#endif

typedef enum DIRECTMANIPULATION_DRAG_DROP_STATUS
{
    DIRECTMANIPULATION_DRAG_DROP_READY = 0,
    DIRECTMANIPULATION_DRAG_DROP_PRESELECT = 1,
    DIRECTMANIPULATION_DRAG_DROP_SELECTING = 2,
    DIRECTMANIPULATION_DRAG_DROP_DRAGGING = 3,
    DIRECTMANIPULATION_DRAG_DROP_CANCELLED = 4,
    DIRECTMANIPULATION_DRAG_DROP_COMMITTED = 5,
} DIRECTMANIPULATION_DRAG_DROP_STATUS;

typedef enum DIRECTMANIPULATION_DRAG_DROP_CONFIGURATION
{
    DIRECTMANIPULATION_DRAG_DROP_CONFIGURATION_VERTICAL = 0x1,
    DIRECTMANIPULATION_DRAG_DROP_CONFIGURATION_HORIZONTAL = 0x2,
    DIRECTMANIPULATION_DRAG_DROP_CONFIGURATION_SELECT_ONLY = 0x10,
    DIRECTMANIPULATION_DRAG_DROP_CONFIGURATION_SELECT_DRAG = 0x20,
    DIRECTMANIPULATION_DRAG_DROP_CONFIGURATION_HOLD_DRAG = 0x40,
} DIRECTMANIPULATION_DRAG_DROP_CONFIGURATION;
DEFINE_ENUM_FLAG_OPERATORS(DIRECTMANIPULATION_DRAG_DROP_CONFIGURATION)

typedef enum DIRECTMANIPULATION_AUTOSCROLL_CONFIGURATION
{
    DIRECTMANIPULATION_AUTOSCROLL_CONFIGURATION_STOP = 0,
    DIRECTMANIPULATION_AUTOSCROLL_CONFIGURATION_FORWARD = 1,
    DIRECTMANIPULATION_AUTOSCROLL_CONFIGURATION_REVERSE = -1,
} DIRECTMANIPULATION_AUTOSCROLL_CONFIGURATION;

struct __declspec(uuid("d9e9df75-d810-5ff7-9134-d16b38c0b232")) __declspec(novtable) IDirectManipulationManager3 : public IDirectManipulationManager2
{
    virtual HRESULT STDMETHODCALLTYPE GetService(REFCLSID clsid, REFIID riid, void **object) = 0;
};

struct __declspec(uuid("af27009b-31e7-530c-a9df-73d406a50244")) __declspec(novtable) IDirectManipulationDragDropEventHandler : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnDragDropStatusChange(IDirectManipulationViewport2 *viewport,
                                                             DIRECTMANIPULATION_DRAG_DROP_STATUS current,
                                                             DIRECTMANIPULATION_DRAG_DROP_STATUS previous) = 0;
};

struct __declspec(uuid("2b1d73e1-90fa-5d33-830c-8a3862d57f3c")) __declspec(novtable) IDirectManipulationDragDropBehavior : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetConfiguration(DIRECTMANIPULATION_DRAG_DROP_CONFIGURATION configuration) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStatus(DIRECTMANIPULATION_DRAG_DROP_STATUS *status) = 0;
};

struct __declspec(uuid("76c66316-4b25-546e-8474-08244bdefe20")) __declspec(novtable) IDirectManipulationAutoScrollBehavior : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetConfiguration(DIRECTMANIPULATION_MOTION_TYPES motionTypes,
                                                       DIRECTMANIPULATION_AUTOSCROLL_CONFIGURATION scrollMotion) = 0;
};

/* the lifted behaviours' classes (IDirectManipulationManager2::CreateBehavior,
   IDirectManipulationManager::CreateContent) */
extern "C" const __declspec(selectany) CLSID CLSID_Microsoft_ParametricMotionBehavior =
    {0xc7b4668b, 0xb4ce, 0x5ee3, {0xb7, 0x89, 0x99, 0xe3, 0xb4, 0x4f, 0x9c, 0xc0}};
extern "C" const __declspec(selectany) CLSID CLSID_Microsoft_AutoScrollBehavior =
    {0xb62c2c6d, 0x0b35, 0x57c3, {0x86, 0xc8, 0x89, 0xbb, 0x50, 0x52, 0xbb, 0xfd}};
extern "C" const __declspec(selectany) CLSID CLSID_Microsoft_DragDropConfigurationBehavior =
    {0x1ac7b647, 0xb167, 0x5506, {0xb7, 0x9f, 0x17, 0x83, 0xed, 0x2d, 0xd3, 0x91}};
