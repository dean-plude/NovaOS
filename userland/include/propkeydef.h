/* propkeydef.h — PROPERTYKEY and DEFINE_PROPERTYKEY, as the Windows SDK
 * has them (the shell's property keys; the same layout as a DEVPROPKEY) */
#pragma once
#include <guiddef.h>

#ifndef PROPERTYKEY_DEFINED
#define PROPERTYKEY_DEFINED
typedef struct _tagpropertykey { GUID fmtid; DWORD pid; } PROPERTYKEY;
#endif
#define REFPROPERTYKEY const PROPERTYKEY *

#define DEFINE_PROPERTYKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) \
    static const PROPERTYKEY name __attribute__((unused)) = { { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }, pid }
