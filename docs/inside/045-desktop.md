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
  - Task Manager (`taskmgr.exe`, a Windows program in System32;
    Ctrl+Shift+Esc, Start's right-click menu or search): Processes with
    End task, Performance (each logical processor, memory, disk and
    Ethernet over 60 seconds), Details and Services, read through the same
    APIs Windows' Task Manager uses.
