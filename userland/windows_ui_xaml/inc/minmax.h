/* NovaOS: the Windows SDK's <minmax.h>: min and max as macros, in C++ too
 * (MinGW-w64's defines them for C only), which XAML's connected animations
 * (components/animation/ConnectedAnimation.cpp) use */
#pragma once
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
