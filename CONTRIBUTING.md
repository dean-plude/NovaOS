# Contributing to NovaOS

Many changes are in flight at once, so the repository is laid out to keep
them from colliding: **add a file instead of editing a shared list.** Lists
that every change used to append to (the DLL table, the self-tests, the app
corpus, the README's tables, the history, the roadmap) are directories with
one file per item. Two pull requests that each add an item touch different
files and merge cleanly in either order.

`main` only takes pull requests. Every pull request is built and
boot-tested by CI ([docs/ci.md](docs/ci.md)).

## Where each kind of change goes

| You are adding... | Add this file | Not this |
|---|---|---|
| a system DLL | `userland/NAME/dll.json` (+ `userland/NAME/build.py` if it needs a library or flags) | a list in `tools/build_userland.py` |
| a program, or making one 32-bit or a System32 one | `userland/programs/NAME.json` (keys `x86`, `system`, `gui`, `libs`, `msstl`, `sources`, `selftest`) | a list in `tools/build_userland.py` |
| export ordinals for a DLL | `"ordinals"` in its `dll.json` | |
| a CI self-test | `tests/selftest/core/NNN-name.py` (or under `graphics/`, `network4/`, `network6/`, `network-e1000e/`, or an existing `devices/NAME/` boot); a new device boot also needs its QEMU setup in `BOOTS` in `tools/selftest.py` | a list in `tools/selftest.py` |
| a nightly app-corpus program | `tests/appcorpus/NNN-name.py` | a list in `tools/appcorpus.py` |
| a history section | `docs/history/YYYY-MM-DD-slug.md` (starts with `## Title`) | `docs/HISTORY.md` |
| a roadmap item for "What comes next", or ticking one off | `docs/roadmap/{graphics,apps,kernel,hardware}/NNN-slug.md` (edit the item's own file to tick it) | that section of `docs/ROADMAP.md` |
| a "What is inside" item, or extending one | `docs/inside/NNN-slug.md` (full text); `docs/readme/inside/NNN-slug.md` for the README's short summary | `docs/inside.md`, or the list in `README.md` |
| a bundled licence | `docs/readme/licenses/NNN-slug.md` (one "Name: licence" line) | the licence sentence in `README.md` |
| a row in building.md's self-test program table | `docs/selftests/NNN-name.md` | the table in `docs/building.md` |
| a program in the compatibility list, or a change to its rating | `docs/compatibility/NNN-slug.md` (`field: value` lines) | the rating tables in `docs/compatibility.md` |

Files are taken in name order, so the prefix places the item: pick a number
between its neighbours (`045` between `040` and `050`). Two changes that
pick the same number still merge, since the names differ; the slug orders
them. History sections use the date instead, so they stay in the order they
landed. `tools/docgen.py --list` prints every generated region and the
files it is built from.

The older DLLs keep their fixed load addresses in `"base"` in their
`dll.json`. A new DLL leaves `base` out and gets a free slot, so two
branches cannot give their DLLs the same address.

## Generated text in pull requests

Parts of `README.md`, `docs/ROADMAP.md`, `docs/HISTORY.md`,
`docs/building.md`, `docs/compatibility.md`, `docs/inside.md` and
`docs/testing.md` are regions between `<!-- BEGIN generated:NAME -->` and
`<!-- END generated:NAME -->`. `tools/docgen.py` builds them from the
fragment files above, from the `DOC` strings of the core self-tests and
app-corpus files, and from the programs marked `"selftest": true`.

The rule, enforced by `tools/docgen.py --check-pr BASE` in CI's **Checks**
job: each region in your pull request must be either

- **unchanged** from the base branch, or
- **exactly** what `python3 tools/docgen.py` produces from your tree.

A region that is neither, such as a hand edit, fails the check. So:

- Put the change in a fragment, never in the region itself.
- Then either leave the regions alone, or run `python3 tools/docgen.py`
  and commit its output unchanged. Both pass. Regenerating is the usual
  choice, so the pull request shows the rendered result.
- Text outside the markers is ordinary prose: edit it as usual.
- A pull request that adds or removes a region is not checked for that
  region.

After two pull requests that both regenerate the same region are merged,
`main`'s region can lag behind its fragments. That is harmless: the next
pull request that runs `tools/docgen.py` brings it up to date (a pull
request that only does that, a "Docs sync", is fine too). The **Docs**
workflow (`.github/workflows/docs.yml`) runs `tools/docgen.py --check` after
each push to `main` and only reports whether the regions are behind; it
never commits.

Every change updates the docs. For most changes that means a history
fragment plus the fragments or prose its feature touches.

## Merging main into a branch

- Merge; do not rebase or force-push a branch someone else may have.
- Resolve every conflict keeping **both** sides' additions.
- A conflict inside a generated region: take either side, then run
  `python3 tools/docgen.py` so the region is rebuilt from both sides'
  fragments.
- CI's **Checks** job runs `tools/ci/check-conflict-markers.py`, which fails
  on `<<<<<<<`, `=======` and `>>>>>>>` lines and on a line holding only a
  branch name (what is left of a deleted `>>>>>>> main`). Run it before
  pushing a merge.
- `nova.iso` is not committed; if a merge brings it back, `git rm nova.iso`.

A branch from before the one-file-per-item layout (October 2026) resolves
its conflicts the same way: take `main`'s version of
`tools/build_userland.py`, `tools/selftest.py`, `tools/appcorpus.py` and the
generated files, then move each item the branch added into its own file as
in the table above.

## Before pushing

These run in seconds, need no build, and are what CI's **Checks** job runs
(apart from actionlint):

```bash
python3 tools/ci/check-conflict-markers.py
python3 tools/ci/check-driver-imports.py
python3 tools/build_userland.py --check
python3 tools/selftest.py --list
python3 -m unittest discover -s tests/tools
python3 tools/docgen.py --check-pr origin/main
```

Then build and run the self-tests as [docs/building.md](docs/building.md#tests)
describes. CI runs the full suites on the pull request
([docs/testing.md](docs/testing.md)).

## Choosing what to work on

The one list of planned work is [docs/ROADMAP.md](docs/ROADMAP.md): start
from the first unticked step of the earliest open phase.  For application
work, start from a failing program in the latest nightly corpus run or one
rated below Platinum in [docs/compatibility.md](docs/compatibility.md), and
fix the gap in NovaOS that the failure shows.  NovaOS changes to suit the
Windows program, never the other way round.

- One focused change per pull request, with a test that reproduces the
  problem and shows it fixed (a self-test, a corpus script or a host test;
  see [docs/testing.md](docs/testing.md)).
- Reuse existing MIT, BSD or zlib licensed code before writing new code;
  no GPL code goes into the kernel or the system DLLs.
- Never make a stub report success for something it does not do.
- Keep the CPU rendering path working when no supported GPU is present.
- Run storage tests on disposable disk images only.
- A QEMU result says nothing about real hardware: update
  [docs/hardware.md](docs/hardware.md) and the compatibility ratings only to
  the level a test has shown.
- Update the README and the affected docs in the same pull request.

## Conventions

- System calls keep Windows 10 1903 x64 numbers (`abitest` and
  `syscalltest` check them).
- Shell blocks in the docs carry no `#` comments, trailing or on their own
  line: zsh, the macOS default shell, does not treat `#` as a comment when
  commands are pasted in, so explanations go in the text around the block.
- A change that makes boot or Firefox start slower on purpose raises the
  budget in `tools/ci/perf-budget.json` in the same pull request and says
  why ([docs/ci.md](docs/ci.md#the-performance-budget)).
