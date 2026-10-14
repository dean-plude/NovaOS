## A performance budget in CI

Roadmap step 26.4.  Every CI run now times the boot, Firefox's start and
`smpstress` scaling, and fails on a large regression, so the lock and
redraw work of PRs #297–#306 cannot quietly slip back.

- **The job**: CI's "Performance budget" (`tools/ci/perf.py`) boots the
  tested image once on four CPUs and measures the boot (QEMU started to
  the kernel's main loop), the bundled Firefox from `start` to its first
  page on the screen (a local page of one colour, a fresh profile, QEMU
  screenshots twice a second until a fifth of the screen shows it) and
  `smpstress scaling 0`.
- **The budget**: `tools/ci/perf-budget.json` holds 20 s to boot and
  10 s for Firefox (the first KVM runner took 9.8 s and 3.4 s, and
  smpstress scaled 2.4x, files 2.0x and the registry 2.1x); a time over it fails, as do files or the registry scaling
  under half as well as bare system calls (`tools/ci/check-smpstress.py`,
  now also used as a module) and Firefox never drawing its page.  Under
  TCG times are recorded, not judged.  The job is part of the
  "Build and boot-test" check; its numbers are in the run's summary and
  `perf-out/perf.json`.
- **Tests**: `tests/tools/test_perf_budget.py` checks the screen-colour
  measure, the verdicts and the budget file without a VM.
