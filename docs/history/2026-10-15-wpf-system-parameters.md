## System parameters for WPF

WPF's `SystemParameters` reads its values with `SystemParametersInfo`
and throws `Win32Exception` when a call fails.  NovaOS's `user32.dll`
failed several of them, `SPI_GETHIGHCONTRAST` among them, so HandBrake
(roadmap step 27.1) died in its start-up when it asked whether high
contrast was on, and then again in its error dialog, which never got
its styles.  `SystemParametersInfo` now answers every value WPF reads,
with Windows' defaults: high contrast (off, available, in the W and A
forms), border width, icon spacing and title wrap, full-window drag,
hover width, height and time, the menu show delay, the foreground flash
count, caret width and focus border sizes.  `apitest` checks each one.

HandBrake now gets as far as creating its main window, but WPF's text
formatting then needs a DirectWrite text analyzer
(`IDWriteFactory::CreateTextAnalyzer`), which NovaOS's `dwrite.dll`
does not have yet, so the window is never shown.  That is HandBrake's
next step; it stays Borked.
