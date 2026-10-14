## Wallpapers and icon sets are files, chosen and added in Settings

Wallpapers and icons are no longer built into `kernel.elf`.  They are
files beside it, in `\EFI\NOVA\Files` on the ISO and on an installed
disk's EFI System Partition (`userland/files` in the source tree), which
the bootloader reads and the kernel installs on drive C: at the same path
(boot protocol version 5, `kernel/fs/bootfiles.c`).  The installer copies
them to the disk it installs on.

- **Wallpapers** are the PNG pictures in `C:\Windows\Web\Wallpaper`: the
  eight brand wallpapers and the Aurora city.  The Aurora theme draws the
  chosen one, filling the screen; it is decoded, and Settings' thumbnails
  are made, on a thread of its own (`kernel/wm/wallpaper.c`).
- **Icon sets** are folders of `.ico` (or `.png`) files in
  `C:\Windows\Web\Icons`, named after the icons they replace.  NovaOS
  ships **Beam**, the brand guide's app and file icons rendered at ten
  sizes by `tools/make_icon_set.py`; the built-in drawings stay the default
  and stand in for any icon a set lacks (`kernel/apps/iconset.c`).
- **Settings > Personalization** has two tabs.  Background picks a theme
  or a picture; Icons picks an icon set.  Each lists PNG pictures, or
  folders of icons, found in Downloads, Pictures, Documents and Desktop,
  and **Add** copies one in and uses it.  The choices are kept in the
  registry (`Control Panel\Desktop\Wallpaper`, as on Windows, and
  `Software\NovaOS\Personalization`).
- The sample pictures in `C:\Pictures` (`Sunset.png`, `Nova.ico`) are
  files beside the kernel too.
