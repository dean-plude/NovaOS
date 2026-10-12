## Direct presentation under KVM, measured from the host

Direct presentation (roadmap 25.2) takes about as long a frame as the GDI
way on the CI runners, where QEMU uses KVM, so `gltest fps` there still
checks the bytes moved rather than the frame time.  A host-side profile of
each turn (`tools/ci/host_profile.py`: CPU time of QEMU's threads per
turn, and gltest's own figures echoed into the CI log) shows why:

- The runner's CPUs are nearly all busy drawing: llvmpipe's threads take
  about 3 of its 4 CPUs in every virgl turn, QEMU's own threads 0.4 to
  0.6.  Direct presentation saves QEMU about 0.1 CPU, but the frame waits
  on llvmpipe either way.
- The turns vary more than the two ways differ: on one run direct drew
  29.1 and 33.7 frames a second, the GDI 31.7 and 31.0 (31.4 against 31.3
  in all).  A virgl turn run first and not counted drew 31.1, so the host
  compiling the scene's shaders is not what slows a turn.
- `gltest fps` prints every turn's frame rate (`fps: turns:`).
