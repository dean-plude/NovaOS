# bootRogue

`rogue.asm` and `LICENSE` from bootRogue by Oscar Toledo G.
(https://github.com/nanochess/bootRogue, commit 118e1cb, 2020-03-09),
unchanged: a roguelike for the IBM PC in 510 bytes, 8086 code on the 80 x 25
colour text screen.  BSD 2-clause licence (`LICENSE`).

`tools/build_userland.py` assembles it with NASM as the DOS program
(`nasm -f bin -Dcom_file=1`) and ships it as `C:\Tests\Dos\ROGUE.COM`
with its licence; the `dostest` self-test and CI's MS-DOS job play it on
NovaOS's DOS host (docs/dos.md).
