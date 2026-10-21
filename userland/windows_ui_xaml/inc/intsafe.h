/* NovaOS: the Windows SDK's intsafe.h is MinGW-w64's, less the fixed-width
 * names the SDK gives its unsigned multiplication (UInt32Mult, as XAML's
 * icons and writeable bitmaps call it), which are its UIntMult. */
#pragma once
#include_next <intsafe.h>

#ifndef UInt32Mult
#define UInt32Mult UIntMult
#endif
