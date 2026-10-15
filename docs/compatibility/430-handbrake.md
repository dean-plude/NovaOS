program: **HandBrake** (App Store)
kind: program
rating: Borked
works: Installs; starts on .NET 8, loads libhb and its video engines; WPF decodes its first icons through NovaOS's Windows Imaging Component
problems: Its WPF main window never shows: after its first icon WPF creates only hidden helper windows and waits in its message loop (cause still open)
checked: by hand
