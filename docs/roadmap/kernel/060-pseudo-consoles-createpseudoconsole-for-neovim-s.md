- Pseudo consoles (`CreatePseudoConsole`, for Neovim's `:terminal` and
  Windows Terminal-style hosts): a child started with
  `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` gets a console of its own whose
  input and output travel over the host's pipes, and
  `ResizePseudoConsole` sets its size and draws the screen again for the
  host (`pipetest`); its host gets line feeds as CR LF, as from conhost,
  and draws the same cells as the console holds (`ptyscreentest`).  Keys
  arrive as a terminal sends them (VT sequences for arrows, Delete and
  F-keys, win32-input-mode records too) and become key records; while a
  program reads lines the console echoes and edits them, so a terminal
  host types into, resizes and exits an interactive `cmd.exe`
  (`conptytest`).  Still to do: run Neovim's `:terminal` on it.
