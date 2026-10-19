# wddmtest: a WDDM render miniport on NovaOS's dxgkrnl host (step 3 of
# docs/design/intel-gpu.md, its second part), in the wddm boot after the
# display-only driver's tests (test signing is on).  "kmttest create"
# registers a root-enumerated display device, hardware ID root\WddmTest, as
# Microsoft's Basic Render Driver has ROOT\BasicRender; pnputil installs
# wddmtest.sys (tools/drivertest/wddmtest.c, in C:\Tests\WddmTest) on it.
# The driver hands DxgkInitialize a DRIVER_INITIALIZATION_DATA, starts with
# no outputs, says WDDM 2.0 and describes one 256 MiB segment of system
# memory (DXGKQAITYPE_QUERYSEGMENT4), and dxgkrnl lists it as a render
# adapter.  "kmttest wddm" finds it through D3DKMTEnumAdapters2 beside
# Basic Render and makes a device and allocations on it: the driver sizes
# each (64 KiB GPU pages), VidMm places it in the segment and opens it on
# the device, Lock2 maps it, and destroying the device closes and
# destroys what is left, in the driver.  The driver has a GPU MMU (step
# 3's third part): the device's process gets the driver's process object
# and page tables VidMm makes and the driver writes; after each mapping
# the driver reads the mapping's first word back through its own tables,
# which must be what kmttest wrote there through Lock2.  Step 3's fourth
# part: the driver takes submissions, so dxgkrnl runs a scheduler for it;
# kmttest makes a context (DxgkDdiCreateContext, SetRootPageTable) and
# submits DMA buffers in the driver's command format, which its software
# GPU's thread runs through the page tables, reporting each buffer's fence
# ID through its interrupt routine and DPC: a write, a copy and a monitored
# fence written at its GPU address; a fence the scheduler signals after a
# buffer; a wait from the GPU holding a buffer until the CPU signals; a
# write to a read-only page, which faults: the engine is reset and the
# context lost.  Step 3's fifth part, preemption and TDR: the driver says
# it can be preempted inside a DMA buffer and reset per engine; a buffer
# spinning on its GPU while another context's work waits is preempted
# (DxgkDdiPreemptCommand, DMA_PREEMPTED) and given the GPU again after the
# other's, resuming where it stopped; a spin the GPU cannot stop gets no
# preemption in 2 s, so the engine is reset (DxgkDdiResetEngine): that
# context is lost, its fence raised to the maximum, and the other
# context's buffer is submitted again and runs.
VGA_ID = '"PCI\\VEN_1234&DEV_1111"'
ROOT_ID = r'ROOT\\DISPLAY\\0000'
TESTS = [
    Test('wddmtest create device', 'kmttest create',
         [r'kmttest: registered ' + ROOT_ID, r'kmttest create: \d+ passed, 0 failed']),
    Test('wddmtest install', r'pnputil /add-driver C:\Tests\WddmTest\wddmtest.inf /install',
         [r'Driver package installed on device: ' + ROOT_ID,
          r'\[DXGK\] WddmTest: render miniport registered \(DDI version 0x5023\)',
          r'\[DbgPrint\] wddmtest: started, LUID [0-9a-f]+, a software GPU in \d+ MiB of RAM',
          r'\[DXGK\] WddmTest: started, 0 source\(s\), 0 output\(s\), WDDM 2\.0',
          r'\[DXGK\] WddmTest: render adapter, LUID 0x[0-9a-f]+, 1 segment\(s\), 256 MB \(system memory\)',
          r'\[DXGK\] WddmTest: GPU MMU, 2 page table levels, 30-bit GPU virtual addresses',
          r'\[DXGK\] WddmTest: scheduler running, 1 node\(s\); preemption inside DMA buffers, TDR per engine',
          r'\[PNP\] ' + ROOT_ID + r': started \(driver WddmTest\)'], settle=3),
    Test('wddmtest d3dkmt', 'kmttest wddm',
         [r'kmttest: render adapter LUID 00000000:0000[0-9a-f]+',
          r'\[DbgPrint\] wddmtest: process created \(1\)',
          r'\[DbgPrint\] wddmtest: device created \(1\)',
          r'\[DbgPrint\] wddmtest: allocation of 64 KiB opened \(64 KiB in use\)',
          r'\[DbgPrint\] wddmtest: allocation of 3072 KiB opened \(3136 KiB in use\)',
          r'\[DbgPrint\] wddmtest: mapped 32 page\(s\) at GPU VA 0x[0-9a-f]+ \(allocation offset 0 KiB\)',
          r'\[DbgPrint\] wddmtest: GPU read at 0x[0-9a-f]+: 0x600df00d',
          r'\[DbgPrint\] wddmtest: mapped 16 page\(s\) at GPU VA 0x[0-9a-f]+ \(allocation offset 64 KiB\)',
          r'\[DbgPrint\] wddmtest: GPU read at 0x[0-9a-f]+: 0x5ecd0a6e',
          r'\[DbgPrint\] wddmtest: unmapped 16 page\(s\) at GPU VA 0x[0-9a-f]+',
          r'\[DbgPrint\] wddmtest: unmapped 32 page\(s\) at GPU VA 0x[0-9a-f]+',
          r'\[DbgPrint\] wddmtest: context created on node 0 \(1\)',
          r"\[DbgPrint\] wddmtest: context's root page table set \(512 entries, segment 0\)",
          r'\[DbgPrint\] wddmtest: ran \d+ bytes at GPU VA 0x[0-9a-f]+ \(fence 1\): 3 command\(s\)',
          r'\[DbgPrint\] wddmtest: GPU wrote fence value 5',
          r'\[DbgPrint\] wddmtest: ran 16 bytes at GPU VA 0x[0-9a-f]+ \(fence 2\): 1 command\(s\)',
          r'\[DbgPrint\] wddmtest: ran 16 bytes at GPU VA 0x[0-9a-f]+ \(fence 3\): 1 command\(s\)',
          r'\[DbgPrint\] wddmtest: GPU page fault at 0x[0-9a-f]+ \(fence 4\)',
          r'\[DXGK\] WddmTest: the GPU faulted at fence 4 on node 0 ',
          r'\[DXGK\] WddmTest: node 0 reset \(a GPU fault; DxgkDdiResetEngine 0x00000000',
          r'\[DXGK\] WddmTest: a context on node 0 is lost \(a GPU fault\)',
          r'\[DbgPrint\] wddmtest: GPU spinning at 0x[0-9a-f]+ \(fence \d+, until preempted\)',
          r'\[DbgPrint\] wddmtest: preemption asked for \(fence \d+, the last finished \d+\)',
          r'\[DbgPrint\] wddmtest: GPU stopped at 0x[0-9a-f]+ by a preemption \(fence \d+\)',
          r'\[DXGK\] WddmTest: node 0 preempted \(fence \d+\): [12] buffer\(s\) back in their contexts\' queues',
          r'\[DbgPrint\] wddmtest: resumed at GPU VA 0x[0-9a-f]+ \(fence \d+\)',
          r'\[DbgPrint\] wddmtest: GPU spinning at 0x[0-9a-f]+ \(fence \d+, until reset\)',
          r'\[DXGK\] WddmTest: node 0 timed out: no preemption in 2 s \(fence \d+\)',
          r'\[DbgPrint\] wddmtest: GPU stopped by a reset \(fence \d+\)',
          r'\[DXGK\] WddmTest: node 0 reset \(a timeout; DxgkDdiResetEngine 0x00000000',
          r'\[DXGK\] WddmTest: a context on node 0 is lost \(a timeout\)',
          r'\[DXGK\] WddmTest: node 0: 1 buffer\(s\) of other contexts submitted again',
          r'\[DbgPrint\] wddmtest: context destroyed \(0 left\)',
          r'\[DbgPrint\] wddmtest: device destroyed \(0 left\)',
          r'\[DbgPrint\] wddmtest: process destroyed \(0 left\)',
          r'kmttest wddm: \d+ passed, 0 failed']),
    Test('wddmtest basic render still', 'kmttest', [r'kmttest: \d+ passed, 0 failed']),
]
