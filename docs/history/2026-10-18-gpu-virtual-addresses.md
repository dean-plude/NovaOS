## GPU virtual addresses

A render miniport's allocations had memory and a CPU mapping, but no GPU
virtual address: nothing a GPU, which reads memory through its own page
tables, could use, and WDDM 2.0 drivers such as Intel's expect VidMm to
keep those tables.

Now, as the third part of step 3 of the Intel GPU design
([design/intel-gpu.md](design/intel-gpu.md)), a driver that says
`GpuMmuSupported` describes its page tables (levels, entries per level, the
root's size), each process gets the driver's process object
(`DxgkDdiCreateProcess`) and one GPU address space per adapter, and
`D3DKMTReserveGpuVirtualAddress`, `MapGpuVirtualAddress` and
`FreeGpuVirtualAddress` work on it.  VidMm picks or checks the ranges,
makes the page tables in system memory, and has the driver write their
entries from its `DXGK_PTE`s through `DxgkDdiBuildPagingBuffer`
(`UPDATE_PAGE_TABLE`, then `FLUSH_TLB`).  A paging queue's monitored fence
is already signalled when each call returns; `D3DKMTMakeResident` and
`Evict` count residency against the budget.  Destroying a mapped
allocation unmaps it.

The test driver `wddmtest.sys` now has a GPU MMU of two levels in its own
entry format, and reads each new mapping's first word back through its
tables as its GPU would; `kmttest wddm` maps, reserves, remaps, evicts and
frees, and the "wddm" boot checks the words the driver read.  Only drivers
that update their page tables with the CPU are served yet; one whose
paging buffers hold GPU commands waits for submission, the next part.
