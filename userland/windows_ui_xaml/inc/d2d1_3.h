/* NovaOS: the Windows SDK's d2d1_3.h (Direct2D 1.3), which MinGW-w64 lacks.
 * So far it holds what XAML's core headers need from it: Direct2D 1.1
 * (core/inc/Timeline.h includes d2d1_3.h and uses nothing newer).  The
 * Direct2D 1.2-1.3 interfaces XAML's imaging and text components call
 * (ID2D1Device2, ID2D1DeviceContext4-7 for colour glyphs, the SVG
 * document) join when those components build, declared in vtable order
 * with their IIDs from Microsoft's Win32 metadata. */
#pragma once
#include <d2d1_1.h>
#include <d2d1effects.h>
