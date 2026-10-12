## Direct presentation measured fairly under KVM

Direct presentation (roadmap 25.2) looked a few per cent slower than the
GDI way on the CI runners, where QEMU uses KVM.  A host-side profile of
each turn of `gltest fps` (`tools/ci/host_profile.py`, CPU time of QEMU's
threads per turn) showed why: the first virgl program after boot waits
while the host compiles the scene's shaders, leaving the host idle for
part of its turn, and later programs find them in the host's cache.
Direct presentation always went first, so it carried that wait; its
second turn used the host's CPU as fully as the GDI turns and drew more
frames.

- **gltest fps** now starts with a short virgl turn that is not counted,
  prints every turn's frame rate, and again requires direct presentation
  to be faster under KVM as well as without it (the check that only
  compared bytes under KVM is gone).
- `tools/ci/host_profile.py` also echoes gltest's own figures into the CI
  log, which otherwise shows a passing test's output only in the
  `graphics-out` artifact.
