program: **MS-DOS 4.00** `COMMAND.COM` and utilities (DOS, 16-bit)
kind: tool
rating: Gold
works: Built from Microsoft's MIT source and run by `ntvdm.exe` in a console window: `DIR`, `COPY`, `TYPE`, `MD`, `RD`, `REN`, `DEL`, `MEM`, `CHKDSK`, `DEBUG` (assembles and runs code) and `EDLIN`, on the boot disk and on drive C:
problems: Text mode only (no CGA/EGA/VGA graphics), no sound, mouse, EMS or XMS, US keyboard layout only; long names show as 8.3 aliases
checked: core self-test `dostest`; CI's MS-DOS job ([`vdm_test.py`](../tools/msdos/vdm_test.py))
