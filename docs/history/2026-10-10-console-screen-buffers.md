## Console screen buffers

The classic console screen-buffer calls work on NovaOS's consoles.  The
console keeps a copy of each screen buffer's cells (character and
attributes), cursor and current attributes, following what programs write
(text, line ends, tabs, UTF-8 and the common cursor and colour escape
sequences), so `GetConsoleScreenBufferInfo` reports the real cursor and
colours, `ReadConsoleOutput*` read the cells back, and
`FillConsoleOutput*`, `WriteConsoleOutput*` and
`ScrollConsoleScreenBuffer*` change them; only the rows a call changed are
drawn again on the Terminal.  `SetConsoleTextAttribute` sends the colour to
the Terminal instead of being ignored.  `CreateConsoleScreenBuffer` makes
independent buffers (up to seven more per console) and
`SetConsoleActiveScreenBuffer` shows one.  The first buffer follows the
Terminal's size until a program sets it with
`SetConsoleScreenBufferSize`.  `consolecursortest` covers these, 64- and
32-bit.
