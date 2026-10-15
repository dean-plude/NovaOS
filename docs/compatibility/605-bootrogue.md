program: **bootRogue** (DOS game, 8086, BSD 2-clause)
kind: game
rating: Platinum
works: Draws its dungeon straight into text-mode video memory, takes the arrow keys, ends on Esc with exit code 0; ships as `C:\Tests\Dos\ROGUE.COM`
problems: —
checked: core self-test `dostest` (screenshot [`dos-bootrogue.png`](screenshots/dos-bootrogue.png)); CI's MS-DOS job ([`vdm_test.py`](../tools/msdos/vdm_test.py))
