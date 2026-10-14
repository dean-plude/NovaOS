# Releasing NovaOS

A release is made by pushing a version tag. The tag starts
`.github/workflows/release.yml`, which runs the whole CI workflow on the
tagged commit and, only if every job passes, publishes the GitHub release.
Nothing is uploaded by hand.

NovaOS 0.1.0 (`v0.1.0`) was released this way on 2026-10-04. Separately,
every green push to `main` republishes a rolling pre-release tagged
`latest` ([ci.md](ci.md#the-rolling-latest-release)); that is not a
release in the sense of this page.

## Cutting a release

1. On `main`, set `NOVA_VERSION` in `kernel/ke/version.h` to the version
   to release, and write `docs/releases/VERSION.md` (for example
   `docs/releases/0.1.1.md`): what the release is, how to start it and its
   known limits. Both go in through a pull request like any other change.
2. Tag the merged commit and push the tag:

   ```bash
   git fetch origin main
   git tag -a v0.1.1 origin/main -m "NovaOS 0.1.1"
   git push origin v0.1.1
   ```

   The same can be done on GitHub's web page: **Releases**, **Draft a new
   release**, **Choose a tag**, type the tag, **Create new tag on publish**
   with target `main`, no description, **Set as the latest release**
   unticked, **Publish release**. That publishes an empty release at once;
   the workflow then fills it in, and clears a "pre-release" tick for a
   plain version.
3. Watch the **Release** run on the Actions page. It takes about as long as
   a pull request's CI; the graphics suite is the longest part.

## What the workflow does

1. **Tag matches the kernel's version.** The tag must be `v` followed by
   `NOVA_VERSION` (`v0.1.1` for `0.1.1`), and `docs/releases/VERSION.md`
   must exist. Otherwise the run stops before building anything.
2. **CI** (`ci.yml`, called with `release: true`): the same jobs as a pull
   request: Checks, Build, Compatibility smoke, the core, network and
   devices suites, the Performance budget and the graphics suite. The
   devices suite also starts NovaOS from the ISO, as a USB stick and as a
   DVD. With `release: true` the Build job first runs
   `tools/fetch_firmware.py`, so the image carries the default firmware set
   ([drivers.md](drivers.md#the-default-firmware-set)) and the Sound Open
   Firmware the audio DSP needs for laptop microphones. Pull request builds
   do not include them.
3. **Publish the release.** From the ISO, kernel and boot loader the Build
   job produced and the suites tested, it makes:

   | File | What it is |
   |---|---|
   | `nova.iso` | the bootable ISO: a UEFI CD image and USB stick image, live system and installer |
   | `nova.iso.sha256` | its SHA-256, for `sha256sum -c` |
   | `kernel.elf`, `bootx64.efi`, `novaos-update.txt` | the update channel, made by `tools/mkupdate.py` ([updates.md](updates.md)) |
   | `SHA256SUMS` | the SHA-256 of all of the above |

   It checks that the channel's version is the tag's version, then creates
   the release (or updates it if it already exists), titled
   "NovaOS VERSION". The notes come from `tools/release_notes.py`:
   `docs/releases/VERSION.md`, a table of the files with their sizes and
   SHA-256, and the heading of every `docs/history/` section added since
   the previous version tag (all of them for the first release), each
   linked at the tag.
4. **The "Latest" mark.** A plain version (`v0.1.1`) is marked Latest.
   Installed NovaOS systems read
   `releases/latest/download/novaos-update.txt`, so this is what offers the
   release to them as an update. A version with a suffix (`v0.1.1-rc1`) is
   published as a pre-release, never marked Latest, and offered to nobody.

**Signing.** `novaos-update.txt` is signed with the Actions secret
`NOVAOS_UPDATE_SIGNING_KEY` ([below](#the-update-signing-key)). The publish
step behaves like this:

| Secret | Key in `kernel/fs/update_key.h` | Result |
|---|---|---|
| not set | empty | published unsigned, with a warning |
| not set | set | run stops: installed systems would refuse an unsigned channel |
| set | empty | signed, with a warning to put the public key in `update_key.h` |
| set, same key | set | signed |
| set, different key | set | run stops before publishing |

As of 0.1.0, `UPDATE_SIGNING_KEY` in `kernel/fs/update_key.h` is empty, so
NovaOS builds check no signature. Shipping the key is roadmap step 23.5.

**Failures and re-runs.** Publishing is the last step, so a failed run
publishes nothing: fix the cause on `main` and tag again. Re-running a run
from the Actions page replaces the files and notes of a release already
there. To move a tag that is on the wrong commit, delete it with
`git push origin :refs/tags/v0.1.1` (and delete its release on the release
page, if one was made), then tag again.

## The update signing key

An installed NovaOS with a key built in installs only from a channel file
signed with that key ([updates.md](updates.md#signatures)). The key pair is
made once, by the repository owner, on a computer they trust. Its secret
half never goes into the repository.

1. Make the key pair from a checkout of the repository. This writes the
   secret into the file (which must not exist yet) and prints the public
   key, 64 hex digits:

   ```bash
   python3 tools/mkupdate.py --new-key ~/novaos-update.key
   ```

2. Store the secret as the repository's Actions secret. On GitHub that is
   **Settings**, **Secrets and variables**, **Actions**, **New repository
   secret**, name `NOVAOS_UPDATE_SIGNING_KEY`, with the file's one line as
   the value. Or:

   ```bash
   gh secret set NOVAOS_UPDATE_SIGNING_KEY --repo dean-plude/os < ~/novaos-update.key
   ```

3. Put the public key into `kernel/fs/update_key.h`
   (`#define UPDATE_SIGNING_KEY "..."`) through a pull request. To print it
   again:

   ```bash
   python3 tools/mkupdate.py --public-key ~/novaos-update.key
   ```

4. Keep `~/novaos-update.key` somewhere safe and offline too. Systems built
   with the key take updates only from channels it signs, so a lost key
   means their next version has to be installed from a new ISO.

The next release then publishes a signed channel. Systems built before
step 3 have no key and check nothing, so they keep accepting updates.

## Trying it before tagging

The same steps run locally after a release-style build:

```bash
python3 tools/fetch_firmware.py
cmake -S . -B build -G "Unix Makefiles"
make -C build -j"$(nproc)"
mkdir -p dist
scripts/create-iso.sh dist/nova.iso build/bootx64.efi build/kernel.elf
python3 tools/mkupdate.py dist --notes "NovaOS 0.1.1" --sign ~/novaos-update.key
python3 tools/release_notes.py 0.1.1 > notes.md
```

Leave out `--sign` if you have no key. `tools/release_notes.py` also prints
the files table when given `--dist dist`, once `nova.iso.sha256` and
`SHA256SUMS` are written beside the other files:

```bash
cd dist
sha256sum nova.iso > nova.iso.sha256
sha256sum nova.iso kernel.elf bootx64.efi novaos-update.txt > SHA256SUMS
cd ..
python3 tools/release_notes.py 0.1.1 --dist dist > notes.md
```

## After the release

The release run's devices suite already boots the release's ISO in QEMU.
Checking it on the reference PC, a ThinkPad T14 Gen 4 (started from a USB
stick it must reach the desktop, and the checks in
[install-and-power.md](install-and-power.md#checks-on-the-t14) must pass),
is done by hand. For 0.1.0 that check is still open: it is roadmap step
23.1 ([ROADMAP.md](ROADMAP.md)).
