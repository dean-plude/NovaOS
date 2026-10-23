## The Visual C++ Redistributable's window shows again

Run by hand (from the App Store's download or `C:\Downloads`), the
Visual C++ Redistributable's installer showed an empty black box where
its licence page should be, and closing it ended the installer with
exit code 1602 (`ERROR_INSTALL_USEREXIT`: the user cancelled).  The
licence window was there all along, under the black box.

The black box was WiX Burn's engine window.  Burn makes it to hear
`WM_QUERYENDSESSION` and keeps it visible, but empty:
`CreateWindowEx(WS_EX_NOACTIVATE, ..., WS_POPUP | WS_VISIBLE,
CW_USEDEFAULT, SW_SHOWNA, 0, 0, ...)`.  `CW_USEDEFAULT` means a default
position and size only for overlapped windows; for a popup Windows sets
the position (and, for a `CW_USEDEFAULT` width, the size) to 0.  NovaOS
gave every top-level window three quarters of the screen when its `x`
was `CW_USEDEFAULT` and its width 0, so Burn's window became a 960 by
544 popup that never paints, above the installer's own window.

- **`CreateWindowEx`** (`user32/win.c`): a popup's `CW_USEDEFAULT`
  position is 0, 0 and its `CW_USEDEFAULT` size 0 by 0, as on Windows;
  overlapped windows keep their default placement.
- An empty top-level window stays empty after it is shown (the desktop
  makes its windows at least one pixel square; `GetWindowRect` still
  answers 0 by 0).
- **Self-test** `guitest auto` checks both popup cases.
