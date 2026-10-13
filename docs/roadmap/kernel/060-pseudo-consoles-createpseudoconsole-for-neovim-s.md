- Pseudo consoles (`CreatePseudoConsole`, for Neovim's `:terminal` and
  Windows Terminal-style hosts): a child started with
  `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` gets a console of its own whose
  input and output travel over the host's pipes, and
  `ResizePseudoConsole` sets its size (`pipetest`); its host gets line
  feeds as CR LF, as from conhost, and draws the same cells as the
  console holds (`ptyscreentest`).  Still to do: run
  Neovim's `:terminal` on it.
