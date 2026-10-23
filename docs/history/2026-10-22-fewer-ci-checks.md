## Fewer CI checks per pull request

A pull request used to start about 68 check runs; now one that changes
code runs eight jobs and the **Build and boot-test** summary
([ci.md](ci.md#how-many-checks-a-pull-request-runs)).

- **The nightly app corpus** starts on a pull request only through a
  paths filter: the corpus, the App Store, the download harness and the
  bundling scripts (`CORPUS_DIRS`/`CORPUS_FILES` in `tools/ci/plan.py`).
  Changes to `tools/ci/`, `tests/tools/` or other workflows no longer fan
  out to its 53 program jobs. It still runs daily and by hand.
- **Compatibility smoke** and **Performance budget** are steps of
  **OS tests (core)** and **OS tests (network)**; each took about a
  minute in a job of its own.
- **MS-DOS 4.0** moved to `.github/workflows/msdos.yml`: every push to
  `main`, and pull requests that change `tools/msdos/`, ntvdm or bootRogue.
- **CI result** is gone; **Publish nova.iso** needs **Build and
  boot-test** and the graphics tests. The three required check names are
  unchanged.
