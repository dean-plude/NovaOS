/* NovaOS: the Windows SDK's intsafe.h is MinGW-w64's, less the fixed-width
 * names the SDK gives its unsigned multiplication and addition (UInt32Mult,
 * as XAML's icons and writeable bitmaps call it, and UInt32Add, as its
 * DirectWrite text analyzer does), which are its UIntMult and UIntAdd, and
 * the conversion Int32ToUInt32 (the imaging component's pixel formats),
 * its IntToUInt. */
#pragma once
#include_next <intsafe.h>

#ifndef UInt32Mult
#define UInt32Mult UIntMult
#endif
#ifndef UInt32Add
#define UInt32Add UIntAdd
#endif
#ifndef Int32ToUInt32
#define Int32ToUInt32 IntToUInt
#endif
