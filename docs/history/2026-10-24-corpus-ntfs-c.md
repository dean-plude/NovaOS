## The app corpus runs with an NTFS drive C:

Audacity's nightly test failed from #533 on: drive C: now reports the
file system it is saved to, and the corpus machine's C: was a FAT32 data
disk, so Audacity warned "The temporary files directory is on a FAT
formatted drive" and the test's clicks missed. The corpus machine's C: is
now an NTFS volume, as on an installed system (Setup installs on NTFS only).

- **`tools/novarun.py`**: `make_data(..., fs='ntfs')` copies files onto the
  volume (`put_ntfs`: an ntfs-3g mount, through `sudo -n` when not root).
- **`tools/appcorpus.py`**: the corpus machine's data disk is NTFS.
- **Still open:** with C: on its disk, Audacity sometimes waits after
  Alt+F4 until a key or a mouse event arrives before it exits; under
  investigation.
