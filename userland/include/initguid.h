/* initguid.h — in the Windows SDK, makes DEFINE_GUID define the GUIDs
 * rather than declare them.  NovaOS's DEFINE_GUID always defines them
 * (static const), so this only has to bring it in. */
#pragma once
#include <guiddef.h>
