## Memory: the compressed system files are given back once unpacked

The kernel image carries the system files, most of them zlib-compressed,
and unpacks them into the RAM disk at start.  The compressed copies (142 MB)
were kept for as long as the system ran.  They now sit in a page-aligned
section of their own (`.userland_z`, written by `build_userland.py`), and
`UmInit` hands its pages back to the physical memory manager once every
file is unpacked ("Gave back the 142 MB ..." in the boot log).  At 2 GB,
memory in use after start fell from 463 MB to 315 MB, and Firefox with
File Explorer open together no longer ran out of memory loading
`xul.dll` ("Couldn't load XPCOM").
