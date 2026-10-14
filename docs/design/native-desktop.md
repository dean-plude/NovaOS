# NovaOS desktop implementation

The default desktop theme, **Aurora**, adapts a user-supplied HTML
prototype, kept unchanged as a design reference in
[novaos-2030-reference.html](novaos-2030-reference.html). NovaOS does not
run the prototype: the desktop is drawn natively by the kernel's window
manager and software GDI (`kernel/wm/desktop.c`, `kernel/wm/aurora_workspace.h`,
`kernel/wm/wm.c`), and none of the prototype's remote scripts, fonts,
images or API calls are loaded.

## From prototype to native code

| Prototype feature | What NovaOS does |
|---|---|
| City wallpaper | `C:\Pictures\Aurora-Sunrise.png` (from `userland/samples/`), decoded once; a procedural sunrise (`kernel/wm/aurora.h`) if the file is missing |
| Translucent surfaces | Blurred, tinted "glass" panels and window frames with rounded corners and drop shadows |
| Home / Apps / Create / Explore | File Explorer / the Start menu / Notepad / Firefox |
| Pinned apps | Notes (Notepad), Calendar, Photos, Terminal; then Settings, and Install NovaOS on the live media |
| Global search | The Start menu's search over apps, programs, settings pages, folders and files; Ctrl+K (Aurora only) and Win+S open it |
| Clock, date and profile | The local time and date, and the configured user name and initials (opens Settings) |
| Calendar card | The current month with today marked, leap years included; opens the Calendar app |
| "Your workspace" card | A greeting by time of day and shortcuts to File Explorer, Notepad and Firefox |
| Dock hover and running indicators | The dock's hover state, pinned apps and running-window indicators |
| Explorer preview | A preview pane: thumbnail, size and type, PNG dimensions, a folder's item count, **Open** and **Copy path** |
| Window dragging, resizing and controls | The existing window manager |
| AI chat, analysis and automation | Not built: needs an assistant service and account set-up that NovaOS does not have |
| Weather, CPU/GPU gauges, music and update notices | Not shown: the prototype's values are demo data |
| Spaces, cloud drives, mail and canvas | Not built |

No mock conversations, made-up usage figures or invented update notices
are shown anywhere in the OS.

## Layout rules

- The Aurora workspace (sidebar, search bar, calendar and workspace cards)
  appears only with the Aurora theme on a screen of at least 900x640
  logical pixels. Smaller screens, and the other wallpaper themes (Settings
  > Personalization), get the classic grid of desktop icons.
- File Explorer shows its preview pane only with the Aurora theme and a
  client area at least 902 pixels wide and 400 high.

## Screenshot

[`../screenshots/aurora-preview.png`](../screenshots/aurora-preview.png)
was rendered when the theme was added, with the native GDI, font, icon,
window-frame and Explorer painters on a host buffer and the real wallpaper
file. It is a preview of the design, not a capture of a running system
([screenshots/README.md](../screenshots/README.md) lists the screenshots
and how each was taken).
