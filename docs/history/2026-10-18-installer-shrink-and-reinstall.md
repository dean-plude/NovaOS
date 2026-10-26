## Setup makes room by shrinking Windows, and reinstalls keeping drive C:

Third and last part of roadmap step 33.4 ([design](design/installer.md)),
which completes it.  Most PCs sold with Windows have no free space: Windows'
volume runs up to its recovery partition.  Setup now offers "Beside Windows,
shrinking it from X to Y": NovaOS's NTFS driver checks Windows left the
volume clean (no hibernation or Fast Startup, no chkdsk pending), moves every
cluster past the new end below it (files, index blocks, `$LogFile`,
`$MFTMirr`), rewriting each attribute's mapping pairs, then cuts `$Bitmap`
and `$BadClus` and writes the new size into the boot sector and its backup;
Setup makes the partition smaller and installs NovaOS in the room.  It is
written from the on-disk format (ntfsresize is GPL, and no permissively
licensed NTFS resizer exists), leaves the volume marked for checking, and a
shrink cut off part way leaves a volume that mounts at its old size or its
new one.  On a disk NovaOS is already on, "Reinstall NovaOS, keeping drive
C: and your files" formats only its EFI System Partition again and keeps
`NOVADATA` (`C:\Users`) as it is.  A host test cuts the shrink off at every
write; the devices suite's `shrink` boot shrinks a 2400 MiB volume made by
mkntfs, reinstalls, and checks every Windows file byte for byte from the host.
