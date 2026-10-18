## A Windows render driver registers

`\Device\DxgKrnl` took only display-only drivers, the
`KMDDOD_INITIALIZATION_DATA` of Windows 8's `DxgkInitializeDisplayOnlyDriver`.
A GPU driver such as Intel's hands `DxgkInitialize` the full
`DRIVER_INITIALIZATION_DATA` instead, and the `D3DKMT*` calls knew only
NovaOS's Basic Render adapter.

Now, as the second part of step 3 of the Intel GPU design
([design/intel-gpu.md](design/intel-gpu.md)), the host takes the full table
(193 entries, read as far as the driver's DDI version has them) from WDDM
2.0 drivers.  A render miniport's adapter starts with its memory segments
(`DXGKQAITYPE_QUERYSEGMENT4`) and is listed by `D3DKMTEnumAdapters2` before
Basic Render, as Windows lists hardware first; `D3DKMTCreateDevice` reaches
`DxgkDdiCreateDevice`, and each allocation is sized by the driver
(`DxgkDdiCreateAllocation`), placed in a segment with room, opened on the
device (`DxgkDdiOpenAllocation`, the driver finding it through
`DxgkCbGetHandleData`) and mapped by `D3DKMTLock2`.  Destroying a device,
or the process going, takes its allocations down in the driver's order.
`DXGK_DRIVERCAPS` is now laid out as Windows lays it out: the display-only
step had `WDDMVersion` at offset 76 instead of 336, where Intel's driver
will write it.

The test driver is `wddmtest.sys` (`tools/drivertest/wddmtest.c`), a render
miniport whose GPU is software, on a root-enumerated device as the Basic
Render Driver is; the devices suite's "wddm" boot makes the device
(`kmttest create`), installs it with `pnputil` and checks its adapter,
devices and allocations with `kmttest wddm`.  Next: GPU virtual addresses
and paging buffers, submission, fences the GPU signals, preemption and TDR.
