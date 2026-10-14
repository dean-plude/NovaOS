- **Desktop** (`kernel/wm/`, `kernel/apps/`): a window manager and shell
  drawn by the kernel's software GDI ([design/native-desktop.md](design/native-desktop.md)).
  - Windows 11-style frames, a dock with pinned and running apps, a Start
    menu that searches apps, programs, settings pages and files, Alt+Tab,
    snapping, desktop icons and context menus, the Aurora workspace or the
    classic wallpaper themes, and several monitors arranged in Settings.
  - Built-in apps: File Explorer, Notepad, Terminal (a command shell over
    drive C:, which also runs Windows console programs), Settings,
    Calendar, Photos, the App Store (downloads open-source Windows
    programs and NovaOS updates) and, on the live media, Install NovaOS.
  - Disk Management (`diskmgmt.exe`, a Windows program in System32; Start
    menu search): the volumes with their letters, file systems, capacity
    and free space above, each disk and its partitions drawn below;
    Initialize Disk (GPT or MBR), New Simple Volume, Format (NTFS or
    FAT32, through `fmifs.dll`'s `FormatEx`) and Change Drive Letter,
    done through the same APIs Windows' Disk Management uses
    (`IOCTL_DISK_*_DRIVE_LAYOUT_EX`, `IOCTL_STORAGE_QUERY_PROPERTY`,
    `FindFirstVolume`, `GetVolumeInformation`, `SetVolumeMountPoint`).
