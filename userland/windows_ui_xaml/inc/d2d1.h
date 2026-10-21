/* NovaOS: MinGW-w64's d2d1.h, with ID2D1Factory given its IID for
 * __uuidof as the Windows SDK's header does (as d2d1_1.h here does for
 * Direct2D 1.1's interfaces; XAML's Direct2D factory, CD2DFactory, asks
 * D2D1CreateFactory for it). */
#pragma once
#include_next <d2d1.h>

#ifdef __cplusplus
struct __declspec(uuid("06152247-6f50-465a-9245-118bfd3b6007")) ID2D1Factory;
#endif
