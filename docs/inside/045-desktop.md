- **Desktop** (`kernel/wm/`, `kernel/apps/`): a window manager and shell
  drawn by the kernel's software GDI ([design/native-desktop.md](design/native-desktop.md)).
  - Windows 11-style frames, a dock with pinned and running apps, a Start
    menu that searches apps, programs, settings pages and files, Alt+Tab,
    snapping, desktop icons and context menus, the Aurora workspace or the
    classic wallpaper themes, and several monitors arranged in Settings.
  - Wallpapers and icon sets are files on drive C:, never part of the
    kernel image: PNG pictures in `C:\Windows\Web\Wallpaper`
    (`kernel/wm/wallpaper.c`: the Aurora theme draws the chosen one,
    filling the screen, decoded and thumbnailed on a thread of its own;
    `HKCU\Control Panel\Desktop\Wallpaper` keeps it) and folders of
    `.ico`/`.png` files in `C:\Windows\Web\Icons` named after the icons
    they replace (`kernel/apps/iconset.c`; the built-in drawings stand in
    for any it lacks).  Settings > Personalization picks them and adds
    more from the user's folders; `HKCU\Software\NovaOS\Personalization`
    keeps the theme and the icon set.
  - Built-in apps: File Explorer, Notepad, Terminal (a command shell over
    drive C:, which also runs Windows console programs), Settings,
    Calendar, Photos, the App Store (downloads open-source Windows
    programs and NovaOS updates) and, on the live media, Install NovaOS.
  - Settings (`kernel/apps/settings.c`): one table of pages (each with its
    name, icon, Windows' `ms-settings:` names, search words and its
    drawing, mouse, keyboard and refresh hooks) behind one frame: the
    sidebar with **Find a setting** (only the pages' icons in a narrow
    window), and a content area that scrolls whenever a page is taller
    than the window.  Every page change goes one way, which closes the
    page's forms and wipes what was typed.  Start's search lists the same
    pages; `start ms-settings:NAME` opens one; `settings check` draws every
    page at three window sizes and checks each (the core self-tests run it).
  - Task Manager (`taskmgr.exe`, a Windows program in System32;
    Ctrl+Shift+Esc, Start's right-click menu or search): Processes with
    End task, Performance (each logical processor, memory, disk and
    Ethernet over 60 seconds), Details and Services, read through the same
    APIs Windows' Task Manager uses.
  - Calculator (`calc.exe`, a Windows program in System32; Start menu
    search or `calc` in the Terminal): Windows Calculator's own engine,
    CalcManager from github.com/microsoft/calculator (MIT, vendored
    unchanged in `third_party/calcmanager`: arbitrary-precision rational
    arithmetic, operator precedence, the expression line, memory), drawn
    with the GDI in the layouts of Windows 11's Standard, Scientific and
    Programmer calculators, with their keyboard shortcuts.  The shipped
    app is a UWP (XAML) package, which NovaOS cannot run yet.
  - Disk Management (`diskmgmt.exe`, a Windows program in System32; Start
    menu search): the volumes with their letters, file systems, capacity
    and free space above, each disk and its partitions drawn below;
    Initialize Disk (GPT or MBR), New Simple Volume, Format (NTFS or
    FAT32, through `fmifs.dll`'s `FormatEx`) and Change Drive Letter,
    done through the same APIs Windows' Disk Management uses
    (`IOCTL_DISK_*_DRIVE_LAYOUT_EX`, `IOCTL_STORAGE_QUERY_PROPERTY`,
    `FindFirstVolume`, `GetVolumeInformation`, `SetVolumeMountPoint`).
