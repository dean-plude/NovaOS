## The 32-bit registry view (WOW6432Node)

32-bit programs, and 64-bit ones asking with `KEY_WOW64_32KEY`, now see
Windows' 32-bit view of `HKLM\SOFTWARE`: their keys live under
`SOFTWARE\WOW6432Node`, apart from the 64-bit ones, and `KEY_WOW64_64KEY`
reaches the 64-bit view from a 32-bit program.  Keys Windows shares between
the views (`Classes`, fonts, OLE, COM3 and the others in `regtest`) stay
shared.  Unlike Windows 7 and later, all of `SOFTWARE\Classes` is shared,
`CLSID` and `Interface` included, because NovaOS registers its COM classes
once for both bitnesses.  The 32-bit view has its own copy of
`Microsoft\Windows NT\CurrentVersion` (with the time zones) and
`Microsoft\Windows\CurrentVersion`, made at start-up from the 64-bit keys,
and the App Store registers 32-bit Vulkan drivers under
`WOW6432Node\Khronos\Vulkan\Drivers`, where 32-bit Vulkan loaders look.
Keys 32-bit installers wrote before this change are in the 64-bit view and
no longer seen by their programs; installing again puts them in place, and
Vulkan drivers from the App Store need reinstalling for 32-bit games.
`regtest` checks the isolation, the explicit view flags and shared keys,
64- and 32-bit.
