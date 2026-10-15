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

## Step 31.2: DOS in a console window

Type `ntvdm` in the Terminal and MS-DOS boots in the window to `A:\>`;
**F12** ends it and gives the Terminal back (the `COMMAND.COM` DOS boots
to is its primary shell, which has no `EXIT`, as on a PC).  `ntvdm IMAGE`
boots another floppy image instead.

**The disk.**  The build runs `build_msdos.py` (so `dosbox` is one of the
build's packages, [building.md](building.md)) and `tools/msdos/mkdisk.py`
makes a 1.44 MB boot disk of the result: the boot sector, `IO.SYS` and
`MSDOS.SYS` first as `SYS` puts them, then `COMMAND.COM` and the other 59
files, all of MS-DOS 4.00 (1.1 MB), and an `AUTOEXEC.BAT` that sets the
prompt (`PROMPT $P$G`).  NovaOS keeps it as
`C:\Windows\System32\DOS\MSDOS.IMG`, beside `LICENSE.TXT`, Microsoft's
MIT licence.  `ntvdm.exe` reads the image into memory; what DOS writes to
the disk stays there and the file is not changed.

**The CPU.**  `userland/programs/ntvdm/cpu86.c` interprets the 8086's
instruction set with the 80186's additions (`PUSHA`, `PUSH` immediate,
three-operand `IMUL`, shifts by an immediate count, `ENTER`/`LEAVE`,
`BOUND`, `INS`/`OUTS`), in real mode with 1 MB of memory (addresses wrap
at 1 MB, A20 off).  Its flags read as an 8086's, so programs that look for
a 286 find an 8086 and do not try protected mode; there is no 8087 (ESC
instructions do nothing, and the equipment word says so).  It is NovaOS's
own code under the repository's MIT licence: the free interpreters are
GPL (DOSBox, 86Box, emu2), and Microsoft publishes no real-mode CPU under
the MIT licence (OpenVMM's `x86emu`, in Rust, emulates single instructions
for a hypervisor's exits rather than running a CPU).

**The BIOS.**  `pcbios.c` is the PC around the CPU: 640 KB, the interrupt
controller, timer, CMOS clock and keyboard controller ports, and a BIOS
written in C.  As in NTVDM, the ROM's interrupt vectors point at stubs
`BOP nn; IRET` (BOP is `C4 C4 nn`, an `LES` with a register operand that no
program uses), and the CPU calls into C for them:

| Interrupt | What it does |
|-----------|--------------|
| 10h | Colour text mode, 80 x 25, eight pages at B800:0: cursor, scrolling, characters and attributes, teletype output, write string |
| 11h, 12h | One floppy drive, colour 80 x 25, no 8087; 640 KB |
| 13h | Drive A: is the image (160 KB to 2.88 MB, geometry from its size): read, write, verify, drive parameters (and the diskette parameter table, INT 1Eh) |
| 14h | COM1 out: `ntvdm` writes what DOS sends to `AUX` to the debug log |
| 15h | No extended memory; the rest "not supported" |
| 16h | The keyboard buffer at 40:1E: read (waiting), peek, shift state, the 101-key calls |
| 17h | A printer that is always ready |
| 19h | Boot: the image's boot sector to 0:7C00 |
| 1Ah | The tick count, and the date and time from NovaOS's clock |

The timer interrupt (IRQ 0, 18.2 times a second) keeps DOS's clock and
`INT 1Ch` going.  When DOS waits for a key the CPU stops and `ntvdm.exe`
sleeps until a key or the next tick, so an idle DOS prompt costs nothing.

**The console.**  `ntvdm.exe` draws the text screen on the console's
alternate screen with `WriteConsoleOutputW`, code page 437 turned into
Unicode (box drawing, accented letters) and the colour attributes kept as
they are (the CGA's colour bits are the console's).  Keys come from
`ReadConsoleInputW` with the console's processing off, so Ctrl+C reaches
DOS; each becomes the scan code and character a PC keyboard gives (US
layout), and arrows, Home, End and the function keys their extended codes.

**Tests.**  The `dostest` self-test (core boot) starts `ntvdm.exe` in the
Terminal, waits for the prompt, types `VER` and `DIR` and reads the
answers off the console, then presses F12.  CI's MS-DOS job builds the
same CPU and BIOS for Linux with `tools/msdos/vdm_test.c` as the host and
boots the disk in seconds (`tools/msdos/vdm_test.py`): `VER`, `DIR`,
`MEM`, `CHKDSK`, a `COPY` and `TYPE`, and a `DEBUG` session that assembles
and runs a `MUL` and checks its 32-bit product and flags.

## What comes next

- **31.3** `CreateProcess` on a 16-bit program starts that DOS host, as
  Windows starts `ntvdm.exe`, so `.COM` and `.EXE` files run from `cmd.exe`,
  the Terminal and Explorer, with NovaOS's drives visible to DOS;
- **31.4** a bundled DOS utility and freeware game in CI, their
  compatibility entries and a screenshot.

The steps and what counts as done are in [ROADMAP.md](ROADMAP.md#phase-31-dos-programs).
