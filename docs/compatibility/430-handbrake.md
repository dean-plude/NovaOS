program: **HandBrake** (App Store)
kind: program
rating: Borked
works: Installs; starts on .NET 8, loads libhb and its video engines
problems: Its WPF main window never shows: WPF decodes its icons with the Windows Imaging Component (WindowsCodecs.dll), which NovaOS does not have yet
checked: by hand
