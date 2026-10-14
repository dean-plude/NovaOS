## Graphics tests: a miscounted byte check and a hang that ate the hour

Two reds on the Graphics tests job of unrelated pull requests (#373, #376,
#377, #385):

- **The frame-rate check**: `gltest fps` and `d3dtest fps` compare direct
  presentation with the GDI way, and under KVM they check that direct
  presentation moves 16 times fewer bytes through guest memory.  The
  direct number is an average over every frame, including the few the host
  did not show (each costs a GDI frame, about 2000 KiB), so a loaded
  runner that missed four more frames moved it from 101 to 133 KiB and the
  ratio from 20 to 15.6 although nothing had changed.  The check now takes
  the frames the host did not show out of the average (they are limited by
  their own "frames shown from the host" check) and wants the rest, the
  desktop's traffic and the scene's commands, to be 8 times below the GDI
  way.  A frame shown from the host that is also read back into guest
  memory still fails it.
- **The stalled job**: on #385 `d3dtest present x86` hung right after
  its "alone" phase (the DXVK queue thread spinning in Venus's wait
  inside `vulkan_virtio.dll`, QEMU's vCPU 0 at 100%, the host's Venus ring
  not draining, no serial output).  Ctrl+C did not bring the Terminal
  back, so each of the six tests after it waited out its own 300 to 600
  second limit and the job was cancelled at 60 minutes.  `Nova.run` now
  records that the Terminal stayed blocked, `selftest.py` resets the
  machine as it does after a crash test and runs the rest on the fresh
  boot, and the present and ANGLE tests have a 240 second limit (they take
  4 to 20 seconds).  One hang now fails one test within minutes
  (`tests/tools/test_selftest_hang.py`).  The hang itself is the third
  sighting of Venus stopping `d3dtest present x86` at the direct-to-GDI
  change; its cause is in the host's Venus ring or the guest's wait and
  is still open.
