## Compatibility ratings, ProtonDB-style

[docs/compatibility.md](compatibility.md) now rates every program and
game that has been tried the way Wine's AppDB and ProtonDB do: Platinum
(works out of the box), Gold (works after a named step, such as installing
"Mesa 3D" and "DXVK" from the App Store), Silver (a minor known problem),
Bronze (starts, but its main use fails or is unchecked), Borked (not
usable) and Untested.  A count per rating opens the page, then one table
each for programs, games and game stores, command-line and developer
tools, and runtimes, drivers and installers, best rating first, with
"What works", "Problems and gaps" and "Checked by" columns; a row checked
by the nightly corpus links its test.

Each row's file in `docs/compatibility/` is now `field: value` lines
(`program`, `kind`, `rating`, `works`, `problems`, `checked`), and
`tools/docgen.py` refuses an unknown rating or kind.  VLC (its exit hang)
and Audacity (its recording dropouts) are Platinum now that both were
fixed, and the App Store's Wine Mono, DirectX End-User Runtimes and OpenAL
Soft gained Untested rows, cnc-ddraw a Platinum one.
