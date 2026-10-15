program: **HandBrake** (App Store)
kind: program
rating: Borked
works: Installs; starts on .NET 8, loads libhb and its video engines; WPF decodes its first icons through NovaOS's Windows Imaging Component, reads the system parameters it needs and creates HandBrake's main window
problems: The main window is never shown: WPF's text formatting asks DirectWrite for a text analyzer (`IDWriteFactory::CreateTextAnalyzer`), which NovaOS's `dwrite.dll` does not have yet
checked: by hand
