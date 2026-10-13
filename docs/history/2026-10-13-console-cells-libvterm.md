## Console cells on libvterm

Console screen buffers no longer interpret program output with a small
parser of their own, which knew cursor moves, colours and clearing but not
scroll regions, inserted and deleted lines or characters, erasing to the
start of a line, the alternate screen or wide characters.  Each screen
buffer now runs libvterm (the Terminal's VT emulator, MIT), whose state
layer edits the buffer's cells: what `ReadConsoleOutput` returns is what
the Terminal, or a pseudo console's host, draws from the same output.
Cells written with `WriteConsoleOutput`, fills and scrolling are sent on
as VT, now with reverse video, underlining and wide characters, and a
pseudo console's host gets line feeds as CR LF, as from conhost.  libvterm
dropped a character whose first byte ended one write and whose other bytes
began the next (two U+FFFD in its place); the console and the Terminal now
hold such a character back until the rest arrives (`UmVtermWrite`).
`ptyscreentest` draws a full-screen program in a console window and under
`CreatePseudoConsole` at the same size, then writes the pseudo console's
output into a screen buffer of its own: all three give the same cells and
cursor (roadmap step 26.1).
