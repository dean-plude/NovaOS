/* NovaOS: MinGW-w64's d2d1.h, with ID2D1Factory given its IID for
 * __uuidof as the Windows SDK's header does (as d2d1_1.h here does for
 * Direct2D 1.1's interfaces; XAML's Direct2D factory, CD2DFactory, asks
 * D2D1CreateFactory for it). */
#pragma once
/* (and declared as the Windows SDK declares them: methods returning a
 * structure return it, rather than MinGW-w64's out-parameter forms (which
 * it gives GCC for its own ABI's sake, and clang as GCC), so XAML's own
 * render target, brushes and bitmaps for the windowless Rich Edit
 * (core/native/text/Controls: HWRenderTarget) implement them as they are
 * written; clang's Microsoft ABI returns such a structure through a hidden
 * pointer either way) */
#pragma push_macro("WIDL_EXPLICIT_AGGREGATE_RETURNS")
#undef WIDL_EXPLICIT_AGGREGATE_RETURNS
#include_next <d2d1.h>
#pragma pop_macro("WIDL_EXPLICIT_AGGREGATE_RETURNS")

#ifdef __cplusplus
struct __declspec(uuid("06152247-6f50-465a-9245-118bfd3b6007")) ID2D1Factory;
/* (and the interfaces XAML's render target for the windowless Rich Edit
 * answers and hands out, by __uuidof) */
struct __declspec(uuid("2cd90691-12e2-11dc-9fed-001143a055f9")) ID2D1Resource;
struct __declspec(uuid("2cd90694-12e2-11dc-9fed-001143a055f9")) ID2D1RenderTarget;
struct __declspec(uuid("2cd906a8-12e2-11dc-9fed-001143a055f9")) ID2D1Brush;
struct __declspec(uuid("2cd906a9-12e2-11dc-9fed-001143a055f9")) ID2D1SolidColorBrush;
struct __declspec(uuid("2cd9069d-12e2-11dc-9fed-001143a055f9")) ID2D1StrokeStyle;
struct __declspec(uuid("a2296057-ea42-4099-983b-539fb6505426")) ID2D1Bitmap;
#endif
