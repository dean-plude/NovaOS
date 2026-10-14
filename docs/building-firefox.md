# Bundled Firefox

Every NovaOS build includes Firefox 157.0 (Windows x64, en-US), unmodified,
in `C:\Programs\Mozilla Firefox\core`.  This page says how the build gets
it and what that means for building and updating.

## How the build fetches it

`tools/build_userland.py` calls `tools/bundle_firefox.py` on every build.
The script:

1. Downloads `SHA512SUMS` for the pinned release from Mozilla's archive
   (`https://archive.mozilla.org/pub/firefox/releases/157.0/`).  It never
   uses Mozilla's "latest" download links.
2. Takes the SHA-512 of `win64/en-US/Firefox Setup 157.0.exe` from that
   file.  The installer must be listed exactly once.
3. Downloads the installer, unless the cached copy already has that
   SHA-512.  A download is written to a temporary file and only replaces
   the cached copy when it is complete and its SHA-512 matches, so a
   failed download never damages a good cache.
4. Unpacks the installer with `7z` (or `7zz`) and takes its `core`
   directory.  The build fails if `firefox.exe` or `xul.dll` is missing,
   or if the payload contains a symbolic link.
5. Embeds every file of `core` in the image under
   `\Programs\Mozilla Firefox\core`.

## What you need

- `7z` or `7zz`: `sudo apt install p7zip-full` (or the `7zip` package).
- Access to `archive.mozilla.org`.  `SHA512SUMS` is downloaded on every
  build, even when the installer is cached, so an offline build fails
  at this step.
- A cache directory: `~/.cache/novaos/firefox/157.0` by default;
  set `NOVA_FIREFOX_CACHE` to put it elsewhere (the version is still
  appended).  CI keeps this directory in its build cache.

There is no switch to build without Firefox.  `NOVA_NO_NETSURF=1` only
leaves out NetSurf.

## Provenance

The unpacked payload goes to `build/kernel_build/userland/firefox/`, with
a `provenance.json` beside `core` recording the version, the installer's
URL and SHA-512, the `SHA512SUMS` URL and the destination path on drive
C:.  `provenance.json` is a build record; it is not put on drive C:.

## In NovaOS

- The desktop's Firefox entry and the Terminal's `browser` command start
  `C:\Programs\Mozilla Firefox\core\firefox.exe`.  `netsurf` starts the
  NetSurf browser, which is still built (unless `NOVA_NO_NETSURF=1`) and
  has its own regression tests.
- Nothing is changed in Firefox's files: no preferences, policies or
  certificates are added, and Firefox's Windows installer is not run.
- The App Store's Firefox entry and the app corpus's Firefox test use the
  same 157.0 installer.  CI's performance budget measures how long the
  bundled Firefox takes to show its first page ([ci.md](ci.md)).

## Image size

Firefox makes `kernel.elf` much larger, because drive C:'s files live
inside it.  `scripts/create-disk.sh` therefore sizes `nova.img` to hold the
kernel and boot loader twice (the running pair and a staged update, which
must not overwrite the running one before the new kernel has started
successfully), plus 32 MiB for FAT metadata, update markers and settings,
rounded up to a multiple of 32 MiB and never below 128 MiB.  Install
NovaOS sizes the EFI System Partition the same way.  The updater still
checks for free space and refuses an update that does not fit.

## Moving to another Firefox version

Change `VERSION` in `tools/bundle_firefox.py`, the Firefox entry in the
App Store catalog (`userland/store/catalog.json`) and the corpus test
(`tests/appcorpus/890-firefox.py`, with its reference screenshot) together,
then check that the new version runs ([compatibility.md](compatibility.md)).

## Licensing

The payload is Mozilla's, unmodified, and keeps Mozilla's own notices.
Anyone who publishes NovaOS images should follow Mozilla's distribution
policy: <https://www.mozilla.org/en-US/foundation/trademarks/distribution-policy/>.
