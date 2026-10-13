## ConPTY end to end

A pseudo console's host writes keys as a terminal sends them, but the
console took every byte for a typed character: an arrow arrived as Esc,
`[` and `A`, Backspace as DEL, and nothing was echoed, so a terminal host
running `cmd.exe` showed none of what was typed.  The console now turns
the host's bytes into key records as conhost does: control characters
become Ctrl+key, VT sequences become arrows, Home, End, Insert, Delete,
Page Up and Down and F1-F12 with their Shift, Alt and Ctrl modifiers,
Esc and a character becomes Alt+it, and win32-input-mode records are
taken as they are.  While a program reads lines with echo the console
echoes what is typed, Backspace takes back the last character and Ctrl+C
drops the line, as in conhost.  `ResizePseudoConsole` now draws the
screen again for the host at its new size, and a CR LF a program writes
in two pieces (as `cmd.exe` does) reaches the host as CR LF, not CR CR LF.  `conptytest` is a terminal
host: it starts `cmd.exe` under `CreatePseudoConsole`, types a command
with a mistake taken back, interrupts a line, sends VT keys to a program
reading raw input, resizes the pseudo console while `cmd.exe` runs, draws
the output at the terminal's size and finds the pseudo console's cells,
then `exit` ends `cmd.exe` and the output pipe ends with the pseudo
console (roadmap step 26.2).
