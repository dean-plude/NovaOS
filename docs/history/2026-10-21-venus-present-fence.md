## Direct3D on Venus no longer hangs waiting for a present fence

A CI flake in `d3dtest x64` (and before it `d3dtest present x86`): the
Direct3D 11 part stopped mid-loop and the test ran into its 900-second
limit.  DXVK's submit thread sat in `vkWaitForFences` on a swap chain's
present fence (`VK_EXT_swapchain_maintenance1`), which reached the kernel as
`NtNovaGpuCtl` op 10 waiting forever, while the virtio GPU had no request
left unanswered and the host renderer was idle: nothing would ever signal
the fence.

- **Cause**: Mesa's Venus presents on its own thread when the renderer has
  no sync files, which on NovaOS it never has.  That thread waits for each
  of its submissions to finish (`vn_wsi_fence_wait`) and unlocks the queue
  meanwhile, so the program's next `vkQueueSubmit` can run while the
  present goes on with its next submission, the empty one that signals the
  present fence.  Two submissions at once on one `VkQueue` share the
  queue's scratch storage (`queue->storage`), where each builds the list of
  timelines its renderer submission will signal: the present fence's entry
  was overwritten by the program's own fence, so it was never signaled.
  Linux never takes this path (its renderer has sync files).
- **Fix**: on Windows, `novaos.patch` keeps the queue locked while Venus's
  present thread waits (the waits are for work already submitted, so
  nothing else is needed to finish them), and `vn_wsi_sync_wait`, which
  only juggled the same locks around a sync-file wait that returns at once
  on NovaOS, does nothing.  The program's next submission waits for the
  present to finish instead.
