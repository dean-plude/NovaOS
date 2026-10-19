## Command submission and GPU fences

A render miniport's process had GPU virtual addresses, but nothing ran on
its GPU: no contexts, no way to hand it a command buffer, no interrupt to
say the buffer was done, and fences only the CPU could signal.

Now, as the fourth part of step 3 of the Intel GPU design
([design/intel-gpu.md](design/intel-gpu.md)), `D3DKMTCreateContextVirtual`
makes a context on one of the driver's engines (`DxgkDdiCreateContext`,
then `DxgkDdiSetRootPageTable` with the process's root page table), and
`D3DKMTSubmitCommand` queues a DMA buffer at its GPU address.  A scheduler
thread per adapter gives each context's buffers to the driver in order
(`DxgkDdiSubmitCommandVirtual`, a fence ID per engine) and retires them
when the driver's interrupt routine reports that ID
(`DxgkCbNotifyInterrupt`, then `DxgkCbNotifyDpc` from its DPC).
`D3DKMTSignalSynchronizationObjectFromGpu2` raises fences once the work
before them is done, `D3DKMTWaitForSynchronizationObjectFromGpu` holds a
context's later work until its fences are reached, and a monitored fence
now has a GPU virtual address, its value page mapped for the GPU, which
the GPU writes directly.  A driver's interrupt line is connected when the
PnP manager gives it one; a DMA fault is logged and the work dropped.

The test driver `wddmtest.sys` now has a software GPU: a kernel thread
that runs DMA buffers in a small command format of its own (write, copy,
write a fence) through the context's page tables, from the physical root
alone, and raises its interrupt when each is done.  `kmttest wddm`
submits buffers, signals and waits from the GPU, and makes it fault on a
read-only page; the "wddm" boot checks what the GPU did.  Not yet:
preemption and recovery from a hung GPU (TDR), the next part, and paging
buffers the GPU runs.
