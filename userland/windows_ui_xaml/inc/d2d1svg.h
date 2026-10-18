/* NovaOS: the Windows SDK's d2d1svg.h, as far as XAML's headers name it.
 * SVG documents are Direct2D 1.4 (ID2D1DeviceContext5), which NovaOS's
 * d2d1 does not have (d2d1_3.h).  XAML's SvgImageSource holds them in
 * ComPtrs, so the interfaces are complete types with their IIDs, but their
 * methods join, in vtable order, when the imaging code that calls them
 * builds. */
#pragma once
#include <d2d1_3.h>

struct __declspec(uuid("86b88e4d-afa4-4d7b-88e4-68a51c4a0aec")) __declspec(novtable) ID2D1SvgDocument : public ID2D1Resource {};
struct __declspec(uuid("ac7b67a6-183e-49c1-a823-0ebe40b0db29")) __declspec(novtable) ID2D1SvgElement : public ID2D1Resource {};
struct __declspec(uuid("af671749-d241-4db8-8e41-dcc2e5c1a438")) __declspec(novtable) ID2D1SvgGlyphStyle : public ID2D1Resource {};
