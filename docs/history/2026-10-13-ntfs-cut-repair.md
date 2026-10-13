## NTFS comes back clean after a power cut

Roadmap step 26.3.  A volume NovaOS was cut off writing to (the power
went, the machine was reset) used to stay marked dirty and mount
read-only until Windows ran chkdsk on it; NovaOS now checks and repairs
it itself at the next mount, and the result passes a full chkdsk-style
check with no errors.

- **The repair** (`kernel/fs/ntfs.c`, `repair()`): NovaOS writes each
  change in an order that loses nothing (clusters and records taken
  before they are used and given back after; a record written whole
  before the directories that list it), so a cut leaves only what the MFT
  records can put right.  On a volume marked dirty with an empty
  `$LogFile` (as NovaOS leaves it), `NtfsEnableWrite` reads every record,
  sets `$Bitmap` and the MFT's bitmap to what the records use, rebuilds
  each directory index whose entries differ from its files' names and
  sizes, clears the mark and mounts the volume read-write.  A torn record,
  a cluster two files claim or a name in no directory is not NovaOS's
  doing: that volume stays read-only for chkdsk on Windows, untouched, as
  does a volume Windows marked dirty (its own `$LogFile`).
- **Drive C:** on NTFS is now marked dirty while a save writes to it
  (`kernel/fs/persist.c`), so a cut-off save is repaired the same way.
- **Tests**: `tests/tools/test_ntfs_cut.py` runs the real `ntfs.c` on a
  RAM disk (`tests/host/ntfs_cut.c`) through rewrites, creates, renames,
  hard links and deletes in a directory big enough for index blocks,
  cuts it off after each of its 817 disk writes in turn, and requires
  each disk to mount writable, keep every file whole and pass
  `scripts/ntfs-check.py` with no errors; a torn record must leave the
  volume read-only and unchanged.  The devices suite's `ntfstest cut` now
  finds D: repaired and writable, and the disk after the cut must pass
  `scripts/ntfs-check.py` as it is (its `--after-cut` leniency is gone).
