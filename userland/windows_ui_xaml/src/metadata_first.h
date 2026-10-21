// NovaOS: what the type table's sources (core/metadata, which include every
// framework class's header) see first, beyond the framework's own first
// headers: IAsyncInfo by its bare name, as the Windows SDK's asyncinfo.h
// gives it (the date and time pickers' and the combo box's headers name it
// so; NovaOS's ABI headers declare it in ABI::Windows::Foundation only)
#pragma once
#include <windows.foundation.h>
using ABI::Windows::Foundation::IAsyncInfo;
