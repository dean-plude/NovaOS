## MS-DOS 4.0 built from Microsoft's source

Roadmap step 31.1, the first of Phase 31 (DOS programs, as NTVDM ran
them).  `tools/msdos/build_msdos.py` builds MS-DOS 4.00 from the source
Microsoft published under the MIT licence (github.com/microsoft/MS-DOS,
`v4.0/src`, pinned commit) with the tree's own MASM, C compiler, LINK and
NMAKE, run in a headless DOSBox, in under two minutes.  The only changes to
the source are DOS line ends (EXE2BIN waited forever for the carriage
return of its answer file `BIOS\LOCSCR`) and one byte for each UTF-8
replacement character the publication left where code page 437 bytes were
(one comment line in `MAPPER\GETMSG.ASM` had grown past MASM's line
limit).  `tools/msdos/boot_test.py` boots the result from a floppy image in
QEMU, and `COMMAND.COM` runs `VER`, `DIR` and `MEM.EXE`.  A new CI job, MS-DOS
4.0, runs both and keeps the 63 built files.  [dos.md](dos.md) has the
details and the next steps: a real-mode CPU and BIOS (31.2), starting DOS
programs from `cmd.exe` and Explorer (31.3).
