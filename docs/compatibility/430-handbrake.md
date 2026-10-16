program: **HandBrake** (App Store)
kind: program
rating: Borked
works: Installs; starts on .NET 8, loads libhb and its video engines; WPF decodes its first icons through NovaOS's Windows Imaging Component, reads the system parameters it needs and creates HandBrake's main window
problems: WPF's text formatting now has a basic DirectWrite text analyzer with HarfBuzz-backed glyph shaping, but HandBrake has not been rerun to verify that it gets past text formatting; the current blocker is unknown
checked: by hand
