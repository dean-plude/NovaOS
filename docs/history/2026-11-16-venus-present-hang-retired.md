## The Venus present hang is retired: one cause, fixed on 2026-10-21

Item 6 of 0.2.2 "Pulsar" ([plan](releases/0.2.2.md)) asked for the root
cause of the `d3dtest present x86` hang on Venus, still listed as the
last open flake in the graphics suite.  The cause turned out to be the one
found for the `d3dtest x64` present-fence hang of 2026-10-21
([that entry](#direct3d-on-venus-no-longer-hangs-waiting-for-a-present-fence)), and its fix,
which merged that day, ended both.

- **The sightings before the fix** all had the same shape: Direct3D 11
  through DXVK on Venus, in a test that presents continuously, stopping
  around a present.  `d3dtest present x86` crashed on 2026-10-12 in
  `nova_wait` (`vulkan_virtio.dll`), reading a renderer sync pointer that
  was no longer valid; hung on 2026-10-14 with DXVK's queue thread polling
  a fence that never signaled while the host renderer sat idle; and hung
  again on 2026-10-16.  `d3dtest x64` then hung twice on 2026-10-21 with a
  present fence nothing would ever signal.  (A `d3dtest present x86` stall
  on 2026-10-20 was the boot-time interrupt-level stall fixed separately.)
- **The cause**: Venus presents on its own thread on NovaOS (no sync
  files) and used to unlock the queue while that thread waited, so the
  program's `vkQueueSubmit` and the present's own fence submission ran at
  the same time on one `VkQueue`.  Both build their batches in the queue's
  single scratch buffer (`queue->storage`, grown with `vk_realloc`).  When
  one overwrote the other's entries, a fence was never signaled (the
  hangs); when one grew the buffer under the other, the other read freed
  memory (the crash in `nova_wait`).  32-bit programs hit it first only
  because their presents take longer around the direct-to-GDI switch the
  test makes.
- **The evidence it is gone**: since the fix merged, about 190 graphics
  jobs ran in CI (every pull request and every push to main from
  2026-10-21 to 2026-11-16, including reruns).  None failed or hung in a
  test: the only red graphics jobs failed in `apt` before any test ran
  (2026-10-22).  Before the fix the hang showed up several times in ten
  days.
- **What changed now**: nothing in the code.  The plan's item 6 is done,
  `d3dtest present x86` is no longer a known flake, and the leftover entry
  in 0.2.3's plan is gone.  If a Venus hang ever comes back, the
  2026-10-21 entry says how to read the hang dump, starting with the
  virtio GPU's "requests out".
