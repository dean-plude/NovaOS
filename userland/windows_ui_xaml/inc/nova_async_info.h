/* NovaOS: IAsyncInfo by its bare name, as the list's incremental loading
 * (incrementalloading.cpp, ListViewBase_Partial_DataVirtualization.cpp)
 * names it: on Windows the SDK's asyncinfo.h makes the name visible there.
 * build.py forces this header first into those two sources only. */
#pragma once
#include <windows.foundation.h>
using ABI::Windows::Foundation::IAsyncInfo;
