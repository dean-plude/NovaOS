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
# which must be what kmttest wrote there through Lock2.
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
          r'\[DbgPrint\] wddmtest: device destroyed \(0 left\)',
          r'\[DbgPrint\] wddmtest: process destroyed \(0 left\)',
          r'kmttest wddm: \d+ passed, 0 failed']),
    Test('wddmtest basic render still', 'kmttest', [r'kmttest: \d+ passed, 0 failed']),
]
