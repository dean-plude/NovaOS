## Direct3D 12 presents in 32-bit programs

The second part of the gaming roadmap's Direct3D 12 item (After 0.1,
step 24.4).  A 32-bit Direct3D 12 program crashed the first time
VKD3D-Proton reused its swap chain's blit command buffer: lavapipe freed
the pointer 0x50 while resetting it.

- **Cause**: 32-bit Mesa (lavapipe in the App Store's Mesa 3D 24.2.4, and
  every Mesa release up to 26.1) sizes each recorded Vulkan command as the
  entry header plus the command's arguments, but the header it measures
  (`vk_cmd_queue_entry_base`, 20 bytes) is 4 bytes shorter than where the
  arguments start in the real entry (24, as the union holds 64-bit
  handles).  Every recorded command is allocated 4 bytes short.  NovaOS's
  heap rounds blocks up to 16 bytes, so most commands had room to spare,
  but `vkCmdSetScissor`'s entry fills its 32-byte class exactly and wrote
  its scissor pointer over the size field of the next block's header.  The
  heap then handed out a live block twice, and a later reset freed garbage.
  64-bit Mesa is not affected (its header is already 8-byte aligned).
- **Fix** (`ntdll`): the 32-bit heap header keeps its 16 bytes, but its 8
  bytes of alignment padding now come first and the size and tag last, so
  a write a few bytes past a block's end lands in padding nobody reads;
  carving leaves the next header's room committed, so a block at the end
  of the heap's committed memory has that slack too.  Mesa and
  VKD3D-Proton are unchanged.
- **Test**: `d3d12test` presents in its 32-bit run too (graphics suite,
  `C:\Tests\d3d12test32.exe 3`): 40 frames on lavapipe in a local boot,
  with the window centre read back red.
