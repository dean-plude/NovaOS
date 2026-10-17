/* NovaOS: MinGW-w64's winstring.h under the Windows SDK's include guard
 * too (__WINSTRING_H_), which WIL's resource.h looks for before it
 * declares unique_hstring */
#pragma once
#include_next <winstring.h>
#ifndef __WINSTRING_H_
#define __WINSTRING_H_
#endif
