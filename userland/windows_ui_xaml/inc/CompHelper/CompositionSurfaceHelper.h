/* NovaOS: the Windows App SDK's CompHelper/CompositionSurfaceHelper.h
 * (shipped closed).  XAML's DirectComposition surfaces draw through it:
 * BeginDraw with the gutters (the edge pixels bilinear sampling reads)
 * reported, copying a surface's pixels out, resizing, and swapping in the
 * Windows.UI.Composition surface behind an IDCompositionSurface.  Declared
 * here as XAML calls it (dxaml/xcp/plat/win/desktop/DCompSurface.cpp);
 * NovaOS defines it over its dcomp.dll when the platform layer builds. */
#pragma once
#include <windows.h>
#include <dcomp.h>
#include <dxgi.h>
#include <microsoft.ui.composition.h>

struct COMPOSITION_HELPER_GUTTERS
{
    UINT leftWidth;
    UINT topWidth;
    UINT rightWidth;
    UINT bottomWidth;
};

class CompositionSurfaceHelper
{
public:
    _Check_return_ HRESULT Initialize(_In_opt_ IDCompositionSurface *surface);
    _Check_return_ HRESULT ResetWinRTSurface(_In_opt_ ABI::Microsoft::UI::Composition::ICompositionSurface *winrtSurface,
                                             _In_opt_ IDCompositionSurface *surface);
    void ReleaseSurface();
    bool HasSurface() const { return m_surface != nullptr; }
    _Check_return_ HRESULT HasValidPixels(_Out_ BOOL *valid);
    _Check_return_ HRESULT Resize(UINT width, UINT height);
    _Check_return_ HRESULT CopySurface(_Outptr_ IDXGISurface **copy);
    _Check_return_ HRESULT BeginDrawWithGutters(_In_opt_ const RECT *updateRect, REFIID iid, _Outptr_ void **updateObject,
                                                _Out_ POINT *updateOffset, _Out_ COMPOSITION_HELPER_GUTTERS *gutters);

private:
    IDCompositionSurface *m_surface = nullptr;
};
