- ~~Move files, the registry, process creation and the console off the
  big kernel lock~~ Done (Phase 17.7): file and registry throughput scale
  about 3x from one CPU to four (`smpstress scaling 3`).  The nightly
  run and CI's performance budget run `smpstress scaling 0` and fail
  when files and the registry scale less than half as well as bare
  system calls.
