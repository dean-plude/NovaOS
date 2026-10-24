# ntfs: what drive C: relies on, on volumes mounted from disk: D: (the
# fresh NTFS volume) and E: (a FAT32 disk, NOVAFAT, beside it).
# volfiletest sets attributes and times (also on a file written in the
# same handle), watches folders for changes, moves files and folders
# between folders and, on NTFS, gives files and folders DACLs and checks
# inheritance and access; NovaOS writes each of these to the volume at
# once.  After a restart, `volfiletest restarted` reads them back from the
# volumes (nothing of the first run is left in memory).
TESTS = [
    Test('volume files ntfs', r'volfiletest D:\VolFiles', [r'volfiletest: \d+ passed, 0 failed'], settle=2),
    Test('volume files fat', r'volfiletest E:\VolFiles', [r'volfiletest: \d+ passed, 0 failed'], settle=2,
         boot_expect=[r'\[DRIVES\] E: is FAT32 volume "NOVAFAT" on \S+ \(read-write\)']),
    Test('volume files restart', 'shutdown /r', [r'\[DRIVES\] E: is FAT32 volume "NOVAFAT" on \S+ \(read-write\)'],
         reboot=True),
    Test('volume files ntfs restarted', r'volfiletest restarted D:\VolFiles',
         [r'volfiletest restarted: \d+ passed, 0 failed'], settle=2),
    Test('volume files fat restarted', r'volfiletest restarted E:\VolFiles',
         [r'volfiletest restarted: \d+ passed, 0 failed'], settle=2),
]
