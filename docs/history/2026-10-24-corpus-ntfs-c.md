## The app corpus runs with an NTFS drive C:

Audacity's nightly test failed from #533 on: drive C: now reports the
file system it is saved to, and the corpus machine's C: was a FAT32 data
disk, so Audacity warned "The temporary files directory is on a FAT
formatted drive" and the test's clicks missed. The corpus machine's C: is
now an NTFS volume, as on an installed system (Setup installs on NTFS only).

- **`tools/novarun.py`**: `make_data(..., fs='ntfs')` copies files onto the
  volume (`put_ntfs`: an ntfs-3g mount, through `sudo -n` when not root).
- **`tools/appcorpus.py`**: the corpus machine's data disk is NTFS.
- **Alt+F4 opened the menu bar** of a program that was slow to close.
  The desktop takes F4's press itself (it closes the window) and passes
  on only its release, and user32 counted only key presses as "another
  key", so Alt's release looked like Alt tapped alone and opened the menu
  bar; Audacity, closing its project on the disk, then sat in the menu
  bar's loop until the next key. Another key's release now counts too
  (`userland/user32/msg.c`); `guitest auto` checks that Alt alone opens
  the menu bar and Alt with F4's release does not.
- **Audacity's test** waits 60 s for its Save As dialog, which lists
  `C:\Documents\Audacity` from the disk now.
