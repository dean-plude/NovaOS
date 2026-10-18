# Updating NovaOS

An installed NovaOS updates itself from an **update channel**: a small text
file on a web server that names the newest version and its files. By
default that is the `novaos-update.txt` of the newest GitHub release. The
App Store's **Updates** page (or the Terminal's `update`) checks it,
downloads a newer version and stages it; the next restart starts the new
version. If the new version does not reach the desktop, NovaOS goes back to
the one it had.

NovaOS running live from its installation disc or USB stick has nothing to
update: a newer build there is a newer ISO.

## Using it

Open the App Store and choose **Updates**. The page checks the channel the
first time it is shown, then says either that this NovaOS is up to date or
which version is available, with its size and a line of notes. **Update**
downloads it, checks it and writes it next to the installed system;
**Restart** starts it. The page also shows where updates come from and,
after an update (or one that did not start), what happened.

The Terminal does the same:

```
C:\Documents> update
This is NovaOS 0.1.0. Checking https://github.com/dean-plude/os/releases/latest/download/novaos-update.txt ...
NovaOS 0.1.1 is available (38.9 MB): Fixes and new drivers.
Type 'update install' to install it.
C:\Documents> update install
...
NovaOS 0.1.1 is ready: restart to finish the update (shutdown /r).
```

| Command | What it does |
|---|---|
| `update` or `update check` | check the channel |
| `update install` | download, check and stage the newer version |
| `update channel` | show the channel |
| `update channel <url>` | use another channel (kept in the registry: `HKLM\SOFTWARE\NovaOS\Update`, value `Channel`) |
| `update channel default` | go back to the GitHub releases |
| `store updates` | open the App Store on its Updates page |

## How an update replaces a running system

An installed NovaOS is two files on its EFI System Partition (ESP): the
boot loader, `\EFI\BOOT\BOOTX64.EFI`, and the kernel,
`\EFI\NOVA\kernel.elf`. The kernel carries every system DLL and program;
they are unpacked onto drive C: at each start and never saved to disk, so
replacing the kernel replaces all of them at once. Both files are in use
while NovaOS runs, so the update is a pending rename carried out at the
next start by the boot loader and the new kernel, the same idea as
`MoveFileEx` with `MOVEFILE_DELAY_UNTIL_REBOOT`, one level down.

1. **Download and check** (`kernel/fs/update.c`). The channel file's
   signature must be good, when this NovaOS has a key to check it with
   ([Signatures](#signatures)). The files are downloaded over HTTPS. Each
   one's size and SHA-256 must match the channel, the kernel must be an ELF
   file whose stamped version ([below](#the-version-stamp)) is the
   channel's version, and the boot loader must be an EFI program.
2. **Stage.** Leftovers of an earlier update (and the previous update's
   `kernel.old`) are deleted to make room. The ESP must then have room for
   the new kernel and boot loader plus 1 MB, or the update stops with a
   message saying how much is free and how much is needed. The files are
   written as `\EFI\NOVA\kernel.new` and `\EFI\NOVA\bootx64.new` and read
   back, `\EFI\NOVA\update.txt` records the versions, and last of all the
   mark `\EFI\NOVA\update.pnd` ("pending") says the update is ready. Until
   that mark is on the disk, nothing the running system or the boot loader
   uses has changed.
3. **First start** (`bootloader/src/main.c`). The boot loader finds the
   pending mark, checks that `kernel.new` begins as an x86-64 ELF kernel
   would, writes the mark `update.try` ("trying"), deletes the pending
   mark, and starts `kernel.new` with `BOOT_FLAG_UPDATE_TRIAL`.
4. **Finish.** Once the new kernel has reached the desktop (its loop has
   answered for three seconds with an account signed in or the sign-in
   screen showing, as Windows counts a boot as good at logon), it renames
   `kernel.elf` to `kernel.old` and `kernel.new` to `kernel.elf`, replaces
   `BOOTX64.EFI` with the new boot loader, and deletes the trying mark. The
   log says
   `[UPDATE] Updated NovaOS from 0.1.0 to 0.1.1 (the previous kernel is kept as \EFI\NOVA\kernel.old)`.

**Going back.** If the new kernel stops before it gets that far, or the PC
is reset or loses power while it starts, the trying mark is still there at
the next start. A kernel that stops (a panic, a failed assertion, a fatal
CPU exception) during that first start does not wait for someone to press
reset: it logs
`[UPDATE] The updated NovaOS 0.1.1 stopped while starting: restarting to go back to the previous version`
and restarts the PC itself after three seconds, as Windows restarts after a
stop error. A kernel that hangs with its interrupts off is reset by the
chipset's watchdog timer (on Intel ICH9-family chipsets such as QEMU's q35),
and one that runs on but makes no progress towards the desktop for 90
seconds restarts the PC itself
([design/recovery.md](design/recovery.md)). The boot loader then deletes it and starts the old
`kernel.elf` with `BOOT_FLAG_UPDATE_FAILED`, and the old NovaOS deletes the
update's files and says so on the Updates page. The steps are ordered so
that a power cut at any point leaves a system that starts: the pending mark
is written after the files, the trying mark before the pending one is
deleted, and if the power goes between the two renames, the boot loader
starts whichever kernel is there (`kernel.new`, else `kernel.old`).

The renames use `FatRename` (`kernel/fs/fat.c`), which writes the new
directory entry before it erases the old one, so a crash in between leaves
both names, never neither.

**ESP size.** The current installer (`kernel/fs/setup.c`) sizes the ESP
for two copies of the kernel and boot loader it installs plus 32 MiB,
rounded up to 32 MiB, and never less than 128 MiB. NovaOS 0.1.0's installer
made a fixed 128 MiB ESP. An update much larger than the installed kernel
may not fit; the update then stops before changing anything. The 0.1.0
kernel is about 50 MB; recent builds of `main` are about 240 MB.

## The version stamp

The version (`NOVA_VERSION` in `kernel/ke/version.h`) is kept in a marked
field of the kernel image (`kernel/ke/version.c`). The boot banner,
Settings > About, `ver` and the updater all read it from that field.
`tools/mkupdate.py --version` writes another version into a copy of a built
kernel, so a test update to a newer version needs no second build. The
updater reads a downloaded kernel's field to check that it is the version
the channel names.

Versions compare number by number (`0.1.10` is newer than `0.1.9`). A
version with a suffix (`0.1.1-test`, `0.1.1-rc1`) is older than the same
version without it. A development build's `+dev.` stamp
(`0.1.0+dev.20261004125600`, [below](#the-rolling-builds-channel)) reads
as a fourth number, so it is newer than `0.1.0` and older than `0.1.1`.

## The channel file

```
NovaOS update 1
version 0.1.1
kernel kernel.elf 40732656 6b1f...e09c
loader bootx64.efi 82821 0d4a...71f2
notes Fixes and new drivers.
signature ed25519 8cafb488...61aded 0263...720d
```

The first line names the format. `kernel` and `loader` give a file's name
(relative to the channel file's address, or a full URL), its size in bytes
and its SHA-256; `loader` may be left out. Lines NovaOS does not know are
skipped, so later versions of the format can add some. `signature`, when
present, comes last: the signing key's public half and the signature of
every byte before that line, both in hex.

`tools/mkupdate.py` writes the three files from a build:

```bash
python3 tools/mkupdate.py update-out --notes "Fixes and new drivers." --sign novaos-update.key
```

This makes `update-out/kernel.elf`, `update-out/bootx64.efi` and
`update-out/novaos-update.txt`. Other options: `--kernel` and `--loader`
pick the input files (default `build/kernel.elf` and `build/bootx64.efi`),
`--no-loader` leaves the boot loader out, `--base URL` names the folder the
files are served from, and `--version` stamps another version.

Attached to a GitHub release, these files are what the default channel,
`https://github.com/dean-plude/os/releases/latest/download/novaos-update.txt`,
finds: GitHub redirects that address to the file of the release marked
"Latest", and the files it names are fetched from the same place. A release
made from a version tag carries them: `.github/workflows/release.yml` runs
`tools/mkupdate.py` on the build its CI tested ([releasing.md](releasing.md)).
Pre-releases (a version such as `0.1.1-rc1`) and the rolling `latest` build
of `main` are never marked "Latest", so systems on the default channel only
see tagged releases.

## The rolling build's channel

Each green CI run on `main` moves the `latest` pre-release to that commit
(`.github/workflows/ci.yml`, job `publish-iso`, see
[ci.md](ci.md#the-rolling-latest-release)) and attaches an update channel
of its own next to `nova.iso`: the kernel and boot loader that run tested,
made with the same `tools/mkupdate.py` as a release and signed with the
same key when the secret is set. Its kernel is stamped with a development
version: the version it was built with, then `+dev.` and the commit's time
(UTC, `YYYYMMDDHHMMSS`):

```
NovaOS update 1
version 0.1.0+dev.20261013235355
kernel kernel.elf 238817896 a340...6b27
loader bootx64.efi 86917 fe2b...ce41
notes NovaOS 0.1.0+dev.20261013235355, a development build of main at 9661113: ...
```

That version is newer than the release it follows (`0.1.0`) and every
earlier development build, and older than the next release (`0.1.1`). Every
NovaOS since 0.1.0 orders it that way. To follow `main`:

```
update channel https://github.com/dean-plude/os/releases/download/latest/novaos-update.txt
```

`update channel default` goes back to releases. A system on the default
channel never sees these builds. A system that goes back to releases from a
development build stays on it until the next release, which is newer.

Two builds of `main` within the same second would carry the same version;
the second reads as up to date. The ISO on `latest` keeps the plain version
(only the channel's copy of the kernel is stamped), so a system installed
from that ISO is offered the same build once more on this channel.

The rolling kernel is now about 240 MB, larger than the whole 128 MiB ESP
that NovaOS 0.1.0's installer made, so a system installed from the 0.1.0
ISO cannot take it: the update stops with the ESP's free space and what it
needs ([ESP size](#how-an-update-replaces-a-running-system)).

## Signatures

The channel file can be signed with an Ed25519 key, so that a changed
release page or a server that is not GitHub's cannot offer an update: the
SHA-256 of each file is in the signed text. The public half of the release
key is built into NovaOS as `UPDATE_SIGNING_KEY` in
`kernel/fs/update_key.h`. Before reading anything else in the channel
file, the updater checks its last line, `signature ed25519 KEY SIGNATURE`,
with Monocypher's `crypto_ed25519_check` (`third_party/monocypher`). The
file is refused, with the reason in the log (`[UPDATE] Failed: ...`) and on
the Updates page, when:

- it has no signature: "The update channel is not signed, and this NovaOS
  installs only signed updates.";
- it is signed with another key: "The update channel is signed with a key
  this NovaOS does not trust (KEY...).";
- it was changed after it was signed: "The update channel's signature is
  not valid: the file was changed after it was signed.".

A good one logs `[UPDATE] The channel's signature is good (key ...)`.

**Current state.** `UPDATE_SIGNING_KEY` is still empty, so NovaOS builds
have nothing to check with: they use the channel unchecked and log
`No update signing key is built in: the channel's signature is not checked`.
NovaOS 0.1.0 predates signature checking altogether and ignores the line.
The 0.1.0 release's channel and the rolling `latest` channel are currently
published unsigned. Shipping the key is roadmap step 23.5.

The secret half is meant to be the GitHub Actions secret
`NOVAOS_UPDATE_SIGNING_KEY`, with which the release and rolling-build jobs
sign. How it is made and stored is in
[releasing.md](releasing.md#the-update-signing-key). By hand:

| Command | What it does |
|---|---|
| `tools/mkupdate.py OUT --sign KEYFILE` | sign the channel with the key in a file |
| `tools/mkupdate.py OUT --sign-env NAME` | sign with the key in an environment variable |
| `tools/mkupdate.py --new-key KEYFILE` | make a key pair; prints the public key |
| `tools/mkupdate.py --public-key KEYFILE` | print a key file's public key |

Signing uses `tools/ed25519.py`, a pure-Python Ed25519 after RFC 8032's
reference code, so it needs nothing installed.

In a QEMU virtual machine, NovaOS also trusts a key the host passes as the
firmware configuration file `opt/novaos/update-key` (64 hex digits;
`-fw_cfg name=opt/novaos/update-key,string=KEY`). That is how the
self-test's key gets in. Only the VM's host can set it, and NovaOS reads it
only when CPUID says it runs under QEMU's TCG or KVM; a PC has no such
device.

## The self-test

The devices suite's `update` boot (`tests/selftest/devices/update/`) starts
`build/nova.img` as an installed NovaOS on QEMU's user network, where
`tools/selftest.py` serves channels made with `tools/mkupdate.py` from the
same build: `v1/`, stamped one version newer than the build (`0.1.1-test`
for 0.1.0), and `v2/`, newer again. They are signed with a key pair kept
only for the tests (`tests/selftest/devices/update/TEST-ONLY-signing-key.txt`,
public, so no real NovaOS trusts it), whose public half QEMU passes as
`opt/novaos/update-key`. In order:

1. `update` refuses three channels for the v1 version: `unsigned/` (no
   signature), `otherkey/` (signed with a key made for the run) and
   `changed/` (notes changed after signing).
2. `same/`, stamped with the build's own version, is up to date, and
   `dev/`, stamped as a development build of it (`0.1.0+dev.20261004125600`),
   is offered.
3. `update` finds v1, `update install` stages it, and a restart starts it
   and finishes the update (`ver` shows the new version).
4. A second restart starts it again, and the channel says it is up to date.
   `dev/` is now older and not offered.
5. v2 is staged, and the machine is reset as soon as its kernel says it is
   starting: the next start is v1 again, which removes the update and offers
   it again.
6. `broken/`, a v3 whose kernel is stamped to stop before it reaches the
   desktop (`tools/mkupdate.py --fail-boot panic`), is staged and started:
   it stops, restarts the machine on its own, and the next start is v1
   again, which removes it. Nothing outside the VM resets it.
7. `hung/`, a v4 stamped to hang there with interrupts off
   (`--fail-boot hang`), is staged and started: the chipset's watchdog
   (QEMU's ICH9 TCO timer) resets the machine a minute later, and the next
   start is v1.
8. `stalled/`, a v5 stamped never to start its desktop
   (`--fail-boot stall`), is staged and started: after 90 seconds without
   progress its `update watch` thread restarts the machine, and the next
   start is v1.
9. `store updates` shows the App Store's Updates page (screenshot
   `store-updates.png`).

To run just this boot after a build:

```bash
python3 tools/selftest.py --suite devices --only 'unsigned channel,unsigned refused,other key channel,other key refused,changed channel,changed refused,same channel,same version up to date,dev channel,dev build newer,update channel,update check,update install,restart into it,updated,restart again,up to date,dev channel again,dev build older,next update,stage it,reset while trying,offered again,broken channel,stage broken update,broken update rolls back,rolled back,hung channel,stage hung update,hung update rolls back,back from the hang,stalled channel,stage stalled update,stalled update rolls back,store updates'
```

## Not done yet

- **The boot menu and recovery.** There is no boot menu, safe mode or
  startup repair yet, and the ACPI WDAT watchdog table is not read (a hang
  with interrupts off is caught only on ICH9-family chipsets): the rest of
  roadmap step 33.3
  ([design/recovery.md](design/recovery.md#to-do-the-next-pieces-of-333)).

- **The release key.** No key is built in yet (roadmap step 23.5), so
  updates are not authenticated.
- **Changing the key.** A NovaOS trusts only the key it was built with, so
  a new key reaches installed systems only through an update signed with
  the old one (a release that builds in the new key), and a lost key only
  through a new ISO.
- **Updates on the reference laptop** have not been checked: an installed
  ThinkPad T14 Gen 4 updating from a release is part of the hand checks in
  [install-and-power.md](install-and-power.md) (roadmap step 23.1).
