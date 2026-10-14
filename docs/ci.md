# CI execution and troubleshooting

CI builds one image per workflow run and distributes that image to independent
jobs. Every consumer checks its commit identity and SHA-256 manifest before
booting. A changed or missing artifact fails before tests start. The release
workflow continues to call CI and consumes `nova-iso` and `nova-boot`; Venus
remains a graphics artifact. Rolling main releases publish only after CI result
passes and the publishing commit is still the current main head.

| Check | Execution |
| --- | --- |
| Checks | Actionlint, host regressions, conflict markers, manifests, all suite inventories and generated-document validation |
| Build | Kernel, bootloader, userland, disk image and ISO; release builds include DSP firmware |
| Compatibility smoke | Seven existing bounded x64/x86 loader, synchronization, Chromium, unwind and WebView2 startup assertions |
| OS tests | Separate core, network and devices jobs, with sibling jobs continuing after a failure |
| Performance budget | Boot time, Firefox start time and `smpstress` scaling against `tools/ci/perf-budget.json` (see below) |
| Graphics tests | Existing full Mesa, DXVK, Venus and browser regression suite |
| CI result | Requires every selected job to succeed; rejects failed, cancelled, missing and unexpected skipped jobs |
| Corpus result | Same completion rule for the nightly build, all app matrix jobs and SMP; validates and combines every selected program's report |

Use **CI result** and **Corpus result** as the required checks when configuring
repository rules. They always exist, including for documentation-only PRs.
This change does not edit repository protection rules. The existing
**Build and boot-test** check is retained as an aggregate of the build, all
three OS suites and the performance budget; **Graphics tests (OpenGL, Direct3D)** keeps its existing name.
Existing protections can therefore continue to require those checks while
CI result covers compatibility and complete workflow execution as well.

`tools/ci/plan.py` diffs the checked-out PR or merge-group tree against the
base SHA declared in the event. Documentation-only changes skip native jobs
explicitly; pushes, manual invocations and unknown code inputs build and test.
Corpus PR execution is selected for corpus fixtures, reference images, host
regressions, CI configuration, download/harness code, the Store catalog and
Firefox bundling. Other kernel/API PRs retain full OS tests and compatibility
smoke. Scheduled and manual corpus runs always select the full corpus.
There are no workflow-level path filters that can leave a required check pending.

The nightly workflow retains three fallback schedules and the 20-hour success
window. Failed or cancelled runs and gate-only success permit another attempt;
active work and successful executed corpus runs suppress duplicates. Runs and
latest-attempt jobs are paginated. API errors fail the gate. PR runs can cancel
superseded PR runs; main, scheduled, manual and release work are not actively
cancelled by new commits. GitHub may replace older queued runs in a concurrency
group; completion checks reject cancelled required execution.

A scheduled run that executed the corpus posts its results (each app's
report, the smpstress log and a link to the run) as a comment on the
tracking issue #210 (`tools/ci/corpus_comment.py`); runs whose gate skipped
an already-tested commit post nothing.

Each selected app runs in its own VM and job, with at most four app jobs running
at once. A failed or timed-out app cannot prevent sibling app jobs or the separate
SMP job from running. Isolation also gives each game's own controller fixture a
fresh VM. All programs in `tests/appcorpus/` are selected exactly once. Unknown,
empty or duplicate `--only` selections fail before download or boot. Application
assertions, screenshot tolerances, strict WebView2 milestones, mutable download
refresh and pinned replay behavior are retained. Reports preserve passed, failed,
skipped and not-run outcomes; an absent or incorrectly attributed report fails
aggregation. Genuine application compatibility failures remain failures.

Runner dependencies live in `tools/ci/install.sh`: build jobs install compilers;
image consumers install runtime tools; graphics adds its driver toolchain. KVM
selection waits for udev and verifies that the runner can create a VM through
KVM, instead of checking only file permissions. Bounded PR OS jobs allow TCG;
corpus and SMP jobs require KVM unless a manual `allow_tcg` invocation requests
software emulation. Accelerator selection is recorded in job summaries. KVM
infrastructure failure remains visible and can be retried by rerunning that job.

Compiler and host-tool caches include platform and builder identity. Application
download caches restore prior inputs but still use the existing URL/hash/refresh
validation. Every boot starts from its own downloaded image, so suites cannot
persist changes into another suite's image. Live raw serial logs are written directly into artifact directories and survive
VM cleanup or workflow interruption. Test jobs keep serial logs,
screenshots, download provenance and available JUnit results even on failure.
The image bundle includes `ci-image.json` with commit and file checksums.

The NetSurf layout comparison decodes PNG pixels and compares the actual client
geometry reported by the browser, excluding only its 18-pixel load-time status
bar. Desktop clocks cannot change a page verdict. Missing, changed or invalid
client geometry fails the comparison; all page pixels remain compared. The
network scheduling test now correctly recognizes QEMU's little-endian TCG CPUID
signature, retaining its existing 250 ms KVM and 500 ms TCG limits. Thread-pool
callback stress still submits all 5,120 callbacks and checks the exact handle
baseline, joining callback teardown between batches.

For local verification, run the host regressions, actionlint, manifest and
suite-list checks, documentation validation and conflict-marker check. Full
native build/boot validation is supplied by the PR's Actions runs. Rebuilding
or rerunning CI does not establish support for physical hardware or repair
unrelated app/runtime failures.

Corpus visual fixtures use the existing desktop menu to select Sunset, matching
our full-screen reference images. A checked desktop patch fails setup if theme
selection did not take effect. Normal OS boots keep Aurora, and screenshot
thresholds and reference images are unchanged. The selected app declares its
own networking independently of echo/HTTPS fixtures. VLC stages its own
30-second H.264/AAC color-bar clip instead of relying on another app's output.
WebView2 installation accepts the updater's bracketed success log format and
still requires a zero result and every WebView2 host milestone.

## The performance budget

Roadmap step 26.4.  The **Performance budget** job (`tools/ci/perf.py`)
boots the tested image once, on four CPUs with 4 GB, and measures:

- **Boot time**: QEMU started to the kernel's main loop (the desktop is up).
- **Firefox start**: Enter on `start firefox.exe` to its first page on the
  screen.  The bundled Firefox opens a local page whose whole background is
  one colour (`C:\Perf\perf.html`) with a fresh profile whose `user.js`
  turns off the welcome, terms-of-use and default-browser pages; QEMU
  screenshots are taken twice a second until a fifth of the screen shows
  that colour.
- **smpstress scaling**: `smpstress scaling 0`; files and the registry must
  scale at least half as well as bare system calls do in the same run
  (`tools/ci/check-smpstress.py`, the nightly SMP job's rule).

The numbers, the budget and each verdict go to the job summary and to
`perf.json` in the `perf-out` artifact (with the serial log, the smpstress
log and a screenshot of Firefox).  A time over its budget in
`tools/ci/perf-budget.json` (20 s to boot and 10 s for Firefox, against
9.8 s and 3.4 s on the first runner, so only a large regression fails), smpstress failing or scaling too little, or
Firefox never drawing its page fails the job and with it **Build and
boot-test**.  Times are judged only under KVM; when a runner falls back to
TCG they are recorded, not judged.  A change that makes something slower on
purpose raises the budget in the same pull request, saying why.

## What the network and graphics suites run

Moved from the README; the core suite is listed there.

- **Network** (in the boot-test job): two boots with a virtio-net card.
  On QEMU's user network, `ipconfig`, `ping`, Winsock over IPv4 and
  `httptest suite` (winhttp with HTTP/2 by ALPN) against
  `tools/h2server.js`, and `looptest` (Winsock over 127.0.0.1 and ::1);
  on an IPv6-only network that is `tools/v6peer.py`,
  SLAAC and RDNSS, `ping -6`, `curl -6` and Winsock over IPv6.  A third
  boot has an Intel e1000e instead: its PHY and link, the link pulled
  and plugged back, sleep and wake, and the IPv4 tests again.
- **Graphics tests**: `tools/d2dtest` (Direct2D geometry answers, and a
  scene that must match the reference `tools/d2dtest/reference.py` draws
  with Skia), then installs Mesa 3D, DXVK and Venus with the App Store
  (`store install NAME` in the Terminal; `tools/ci/stage-graphics.sh`
  stages the downloads) and runs `tools/gltest` (15 tests, on virgl and
  on llvmpipe) and `tools/d3dtest` (17 tests, on Venus: the first monitor
  is a QEMU 3D virtio-gpu, `virtio-vga-gl,venus=on`, whose Vulkan and
  OpenGL are the runner's lavapipe and llvmpipe); each 64- and 32-bit,
  with a screenshot of each while it draws; `gltest present` (virgl's
  frames shown straight from the host's GPU while the window is alone,
  checked on the screen itself, and through the GDI while a window covers
  it) and `d3dtest present` (the same for Direct3D 9 on Venus, whose
  Vulkan swapchain's frames the host copies into the screen) and
  `dcomptest direct` (a DirectComposition swap chain on Venus composited
  on the GPU and shown the same way, also from the GPU process's window
  embedded in another process's, and the GDI way again, on the screen
  too, once the tree no longer qualifies); then
  `gltest fps` and
  `d3dtest fps`, which draw the same OpenGL scene on virgl (shown
  directly and through the GDI) and on
  llvmpipe, and the same Direct3D 9 scene on Venus (shown directly and
  through the GDI) and on lavapipe,
  inside NovaOS and need the GPU to be faster, and direct presentation
  faster than the GDI way.
