- Release 0.1 (Phase 22.5): pushing a version tag builds, tests and
  publishes a release (`.github/workflows/release.yml`,
  [releasing.md](releasing.md)): every CI suite on the tagged commit, with
  the audio DSP firmware fetched first, then `nova.iso`, its checksums,
  the update channel's files and notes from the history.  `v0.1.0` was
  tagged and published on 2026-10-04 ([release notes](releases/0.1.0.md)),
  and every green CI run on a push to `main` republishes a rolling
  `latest` release.  Still to do: start the release's ISO on the
  reference PC (step 23.1).
