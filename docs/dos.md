# DOS programs

Windows ran 16-bit DOS programs through NTVDM, the NT Virtual DOS
Machine: a DOS kernel and a PC BIOS in a process of their own, with the
program's console in a normal window.  NovaOS takes the same route
(roadmap Phase 31), with one difference: NTVDM put the processor into
virtual-8086 mode, which x86-64 long mode does not have, so NovaOS runs the
DOS on a small real-mode CPU instead.

The DOS is **MS-DOS 4.00**, built from the source Microsoft published under
the MIT licence at
[github.com/microsoft/MS-DOS](https://github.com/microsoft/MS-DOS)
(`v4.0/src`; versions 1.25 and 2.0 in the same repository are historical
and not complete enough to build).  NovaOS uses it unchanged.

## Step 31.1: building it from source

`tools/msdos/build_msdos.py OUTDIR` builds the tree the way it was built in
1988.  The tree carries its own build tools as DOS programs (`v4.0/src/TOOLS`:
MASM 5, the Microsoft C 5 compiler, LINK, NMAKE, EXE2BIN and the message
tools), so the script runs the tree's own top-level `NMAKE` in DOS.  The DOS
for the build is DOSBox, Ubuntu's `dosbox` package run headless; it is a
build tool only, like a compiler, and nothing of it ends up in NovaOS.

The script fetches the pinned commit of the repository into
`~/.cache/novaos/msdos` and copies `v4.0/src` into a work directory as a
DOS disk would hold it.  Two things differ from the repository, and nothing
else:

- **Line ends.**  The repository stores text with LF only.  MASM and NMAKE
  read that, but EXE2BIN reads its answer to "Fix-ups needed - base segment
  (hex):" from `BIOS\LOCSCR` and waits for a carriage return forever
  without one, so every text file gets DOS line ends (CR LF).
- **Lost characters.**  The publication turned the code page 437 bytes of
  some files (box-drawing characters, mostly in comments) into the
  three-byte UTF-8 replacement character.  In `MAPPER\GETMSG.ASM` that grows
  a comment line past MASM's 128-character limit ("error A2106: Line too
  long").  Each replacement character becomes one byte, `C4h` (`─`).

The build takes under two minutes and produces the 63 files the tree's
`CPY.BAT` gathers (`IO.SYS`, `MSDOS.SYS`, `COMMAND.COM`, the utilities and
device drivers) plus the boot sector, `MSBOOT.BIN`.  The result is kept in
`~/.cache/novaos/msdos/out-HASH`, with NMAKE's output as `BUILD.LOG`, and
reused while the script is unchanged.

`tools/msdos/boot_test.py OUTDIR` checks that the result is a working DOS:
it formats a 1.44 MB floppy image with mtools using the built boot sector
(setting its BIOS drive number to 0 as `FORMAT` does for a floppy), copies
`IO.SYS` and `MSDOS.SYS` first, as `SYS` does, then `COMMAND.COM`,
`MEM.EXE` and an `AUTOEXEC.BAT`, and boots it in QEMU.  The batch file sends
the output of `VER`, `DIR` and `MEM` to the first serial port; the test
passes when "MS-DOS Version 4.00", the listing and MEM's memory report
arrive.

CI runs both in the **MS-DOS 4.0** job (not a required check) and keeps the
files as the `msdos` artifact ([ci.md](ci.md)).  Locally:

```sh
sudo apt install dosbox mtools qemu-system-x86
python3 tools/msdos/build_msdos.py build/msdos
python3 tools/msdos/boot_test.py build/msdos
```

## What comes next

- **31.2** a real-mode CPU (an 8086/80286 interpreter under the MIT, zlib or
  BSD licence; DOSBox and other GPL emulators are not used in NovaOS) and
  the BIOS services DOS needs (text-mode video, keyboard, disk, clock) on a
  NovaOS console window, booting the built `IO.SYS` and `MSDOS.SYS` to the
  `COMMAND.COM` prompt;
- **31.3** `CreateProcess` on a 16-bit program starts that DOS host, as
  Windows starts `ntvdm.exe`, so `.COM` and `.EXE` files run from `cmd.exe`,
  the Terminal and Explorer, with NovaOS's drives visible to DOS;
- **31.4** a bundled DOS utility and freeware game in CI, their
  compatibility entries and a screenshot.

The steps and what counts as done are in [ROADMAP.md](ROADMAP.md#phase-31-dos-programs).
