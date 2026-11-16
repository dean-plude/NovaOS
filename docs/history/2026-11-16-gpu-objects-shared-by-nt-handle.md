## GPU objects shared by NT handle, and 32-bit programs' D3DKMT calls

Direct3D 12 and Vulkan share GPU memory and fences between processes and
APIs by NT handle: VKD3D-Proton's shared heaps and fences, a browser's GPU
process handing frames to its window's process.  NovaOS's graphics kernel
had no way to do it, and a 32-bit program's `D3DKMT*` calls all failed
with `STATUS_NOT_SUPPORTED`.

As the sixth part of step 3 of the Intel GPU design
([design/intel-gpu.md](design/intel-gpu.md)), `D3DKMTCreateAllocation2`
makes resources (`CreateResource`: allocations together, with the
runtime's and the driver's private data), shareable with `CreateShared`
and `NtSecuritySharing`, and fences take the same sharing flags.
`D3DKMTShareObjects` turns one into an NT handle: a kernel object holding
its pages, and on a driver's adapter the driver's allocation, which stays
alive while any handle to it is open, even after the resource it was made
from is gone.  Another process that has the handle, inherited or
duplicated, opens it on its own device: `D3DKMTQueryResourceInfoFromNtHandle`
says how much data comes with it, `D3DKMTOpenResourceFromNtHandle` opens
the driver's same allocation there (`DxgkDdiOpenAllocation` with the data
it was made with) and gives the data back, and
`D3DKMTOpenSyncObjectFromNtHandle2` maps the fence's value page into the
process for its CPU (read-only) and its GPU.  The second process maps the
allocation into its own GPU address space like one of its own.

For 32-bit programs, gdi32 now rebuilds each `D3DKMT*` structure in its
x64 layout (pointers zero-extended, handles sign-extended, arrays of
allocation descriptions rebuilt) and copies the kernel's answers back, as
WoW64 does on Windows; the few structures laid out alike on both go as
they are.

`kmttest wddm` shares a resource and a monitored fence with a second
process it starts with the handles inherited: that process sees what the
first wrote, has its own GPU write the allocation and signal the fence,
signals it again from its CPU, and the first process sees all of it.  The
same checks pass as a 32-bit program (`C:\Programs\x86\kmttest.exe wddm`),
and the core suite runs `kmttest` as a 32-bit program too, which found
NovaOS's own `MEMORY_BASIC_INFORMATION` 4 bytes too long for 32-bit
programs built with its headers (`PartitionId` is x64 only), now fixed.  Not yet:
named shares (`D3DKMTOpenSyncObjectNtHandleFromName`), keyed mutexes and
the older global share handles.

